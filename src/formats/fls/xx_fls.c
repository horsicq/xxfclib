/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM SaveRam / SaveRam2 FLS archives.
 *
 *   0x00  u16 LE record count, INCLUDING the leading header record, so the
 *         member count is one less. A count below 2 means no members.
 *
 *   record table, 44 bytes per record, starting at offset 2.
 *
 *   record 0 is the header record, not a member; it is identified by
 *     +0x00  u8  0xFE
 *     +0x01  u8  0x00
 *     +0x02  six bytes 00 00 01 00 00 FF
 *     +0x17  u32 LE size of the name block (reusing the member records'
 *            compressed-size slot)
 *
 *   member record (records 1 .. count-1):
 *     +0x00  u8  0xFE                     record marker
 *     +0x01  u8  compression flag, 0 = stored, 1 = FLS-LZ
 *     +0x02  four bytes 00 00 00 01       member tag, only these four
 *            bytes are invariant
 *     +0x06  u8  0x20 or 0x00
 *     +0x07  u8  free-form ASCII letter, NOT constrained (see below)
 *     +0x08  u32 LE name reference: byte offset, from the start of the name
 *            block, of this member's array of path-component pointers
 *     +0x0c  u16 LE number of path components in that array
 *     +0x0e  u32 LE tail size: padding between this member's payload and the
 *            next member's payload
 *     +0x13  u32 LE data offset, absolute in the file
 *     +0x17  u32 LE compressed size
 *     +0x1b  u16 LE DOS date
 *     +0x1d  u16 LE DOS time
 *     +0x1f  u32 LE uncompressed size
 *
 *   name block, at 2 + count * 44:
 *     +0x00  u16 LE 0xFFFF
 *     +0x02  the seven bytes "SaveRam"
 *     then, at the offsets the records point at, arrays of u32 LE offsets
 *     (again relative to the start of the name block), each pointing at a
 *     length-prefixed path component: one u8 length followed by that many
 *     CP437 bytes. Only the LAST component of a member's array is used as
 *     the member's name; the leading ones are directory components that the
 *     reference reader also discards.
 *
 *   member payloads start at name_block_offset + name_block_size and are laid
 *   out strictly in record order, each at
 *   previous_payload_end + previous_tail_size. A record whose stored data
 *   offset disagrees with that running position is a rejection.
 *
 * A compressed member's payload begins with the FLS-LZ 'S' (0x53) tag byte,
 * and the packed extent handed to the decoder includes that byte.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fls/xx_fls.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/flslz/xx_flslz.h"

#include <stdio.h>

#define XX_FLS_COPY_CHUNK (64 * 1024)

typedef struct xx_fls_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_fls_member;

typedef struct xx_fls_stream_s {
    xx_fls_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_fls_stream;

static void xx_fls_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_fls_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_fls_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_fls_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_fls_stream_free(void *pointer) {
    xx_fls_stream *stream = (xx_fls_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_fls_add(xx_fls_stream *stream,
                          const xx_fls_member *member) {
    xx_fls_member *grown = (xx_fls_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_FLS_RECORD_SIZE 44
#define XX_FLS_NAME_BLOCK_TAG_SIZE 9
#define XX_FLS_MAX_MEMBERS 100000
#define XX_FLS_MAX_PATH_COMPONENTS 64
#define XX_FLS_METHOD_STORE 0U
#define XX_FLS_METHOD_LZ 1U
#define XX_FLS_LZ_TAG 0x53U
#define XX_FLS_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_fls_le16(const uint8_t *data);
static uint32_t xx_fls_le32(const uint8_t *data);
static bool xx_fls_leaf_ok(const uint8_t *bytes, size_t length);
static xx_fls_stream *xx_fls_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_fls_decode(Abstractformat *self, const xx_fls_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static uint16_t xx_fls_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_fls_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Leaf names are CP437, so bytes 0x80..0xFF are genuinely permitted by the
 * format (the reference reader transcodes them). Only C0 controls and 0x7F
 * are rejected, along with the separators that would let a name escape the
 * extraction directory. */
static bool xx_fls_leaf_ok(const uint8_t *bytes, size_t length) {
    size_t index;

    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t character = bytes[index];
        if (character < 0x20U || character == 0x7FU) return false;
        if (character == '/' || character == '\\' || character == ':') {
            return false;
        }
    }
    if (length == 1U && bytes[0] == '.') return false;
    if (length == 2U && bytes[0] == '.' && bytes[1] == '.') return false;
    return true;
}

static xx_fls_stream *xx_fls_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_fls_stream *stream;
    uint8_t header_record[XX_FLS_RECORD_SIZE];
    uint8_t record[XX_FLS_RECORD_SIZE];
    uint8_t tag[XX_FLS_NAME_BLOCK_TAG_SIZE];
    uint8_t count_bytes[2];
    uint8_t marker;
    int64_t total;
    int64_t span;
    int64_t records;
    int64_t name_block_offset;
    int64_t name_block_size;
    int64_t data_start;
    int64_t expected_data_offset;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Two bytes of count, a header record, at least one member record and
     * the nine-byte name-block tag. */
    if (span < 2 + 2 * XX_FLS_RECORD_SIZE + XX_FLS_NAME_BLOCK_TAG_SIZE) {
        return NULL;
    }
    if (!xx_fls_read_at(self, self->base_address, count_bytes,
                        sizeof(count_bytes))) {
        return NULL;
    }
    records = (int64_t)xx_fls_le16(count_bytes);
    /* The count includes the header record, so fewer than two records means
     * there are no members at all. */
    if (records < 2 || records > XX_FLS_MAX_MEMBERS) return NULL;
    if (records > (span - 4) / XX_FLS_RECORD_SIZE) return NULL;

    name_block_offset = 2 + records * XX_FLS_RECORD_SIZE;
    if (!xx_fls_range_within(span, name_block_offset,
                             XX_FLS_NAME_BLOCK_TAG_SIZE)) {
        return NULL;
    }
    if (!xx_fls_read_at(self, self->base_address + name_block_offset, tag,
                        sizeof(tag))) {
        return NULL;
    }
    /* This is the format's signature and its primary false-positive defence:
     * a 16-bit count is no magic at all, so the "SaveRam" string has to land
     * at exactly the offset the count computes. Loosening the placement, or
     * searching for the string, throws that defence away. */
    if (xx_fls_le16(tag) != 0xFFFFU || tag[2] != 'S' || tag[3] != 'a' ||
        tag[4] != 'v' || tag[5] != 'e' || tag[6] != 'R' || tag[7] != 'a' ||
        tag[8] != 'm') {
        return NULL;
    }

    if (!xx_fls_read_at(self, self->base_address + 2, header_record,
                        sizeof(header_record))) {
        return NULL;
    }
    /* Record 0 is a header, not a member, and carries its own discriminator.
     * Its compressed-size slot is reused for the name-block size. */
    if (header_record[0] != 0xFEU || header_record[1] != 0x00U ||
        header_record[2] != 0x00U || header_record[3] != 0x00U ||
        header_record[4] != 0x01U || header_record[5] != 0x00U ||
        header_record[6] != 0x00U || header_record[7] != 0xFFU) {
        return NULL;
    }
    name_block_size = (int64_t)xx_fls_le32(header_record + 23);
    if (name_block_size < XX_FLS_NAME_BLOCK_TAG_SIZE) return NULL;
    if (!xx_fls_range_within(span, name_block_offset, name_block_size)) {
        return NULL;
    }
    data_start = name_block_offset + name_block_size;

    /* Record 1 must be a member record; this is a cheap early reject before
     * any allocation happens. */
    if (!xx_fls_read_at(self, self->base_address + 2 + XX_FLS_RECORD_SIZE,
                        &marker, 1U)) {
        return NULL;
    }
    if (marker != 0xFEU) return NULL;

    stream = (xx_fls_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    expected_data_offset = data_start;
    for (index = 1; index < records; ++index) {
        xx_fls_member member;
        uint8_t reference_bytes[4];
        uint8_t length_byte;
        uint8_t leaf[255];
        char buffer[256];
        char *name;
        int64_t record_offset;
        int64_t name_reference;
        int64_t path_components;
        int64_t tail_size;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t last_reference_offset;
        int64_t name_offset;
        int64_t name_size;
        uint8_t compression_flag;
        uint8_t first_byte;
        size_t copy_index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        record_offset = 2 + index * XX_FLS_RECORD_SIZE;
        if (!xx_fls_range_within(span, record_offset, XX_FLS_RECORD_SIZE)) {
            goto fail;
        }
        if (!xx_fls_read_at(self, self->base_address + record_offset, record,
                            sizeof(record))) {
            goto fail;
        }

        /* Only the first four bytes of the member tag are invariant. Across
         * the reference corpus the fifth is 0x20 or 0x00 and the sixth is a
         * free-form ASCII letter taking at least ten values, so pinning the
         * pair rejects the majority of otherwise identical archives. Do not
         * "tighten" byte +0x07. */
        if (record[0] != 0xFEU || record[2] != 0x00U || record[3] != 0x00U ||
            record[4] != 0x00U || record[5] != 0x01U) {
            goto fail;
        }
        if (record[6] != 0x20U && record[6] != 0x00U) goto fail;

        compression_flag = record[1];
        name_reference = (int64_t)xx_fls_le32(record + 8);
        path_components = (int64_t)xx_fls_le16(record + 12);
        tail_size = (int64_t)xx_fls_le32(record + 14);
        data_offset = (int64_t)xx_fls_le32(record + 19);
        compressed_size = (int64_t)xx_fls_le32(record + 23);
        uncompressed_size = (int64_t)xx_fls_le32(record + 31);

        if (compression_flag > 1U) goto fail;
        if (path_components < 1 ||
            path_components > XX_FLS_MAX_PATH_COMPONENTS) {
            goto fail;
        }
        /* The name block opens with its nine-byte tag, so no pointer array
         * may start before offset 2, and the whole array must fit. */
        if (name_reference < 2 ||
            name_reference > name_block_size - path_components * 4) {
            goto fail;
        }
        /* The payloads are strictly contiguous in record order, separated
         * only by each record's declared tail. A stored data offset that
         * disagrees with the running position means the records do not
         * describe this file's payload area; this is the structural check
         * that makes the format self-consistent rather than merely plausible. */
        if (data_offset != expected_data_offset) goto fail;
        if (compressed_size < 0) goto fail;
        if (!xx_fls_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        if (tail_size < 0 ||
            tail_size > span - data_offset - compressed_size) {
            goto fail;
        }

        /* Only the last path component is the member's own name. */
        last_reference_offset =
            name_block_offset + name_reference + (path_components - 1) * 4;
        if (!xx_fls_read_at(self, self->base_address + last_reference_offset,
                            reference_bytes, sizeof(reference_bytes))) {
            goto fail;
        }
        name_offset =
            name_block_offset + (int64_t)xx_fls_le32(reference_bytes);
        /* A name must live inside the name block, past its tag, and never in
         * the payload area. */
        if (name_offset < name_block_offset + 2 || name_offset >= data_start) {
            goto fail;
        }
        if (!xx_fls_range_within(data_start, name_offset, 1)) goto fail;
        if (!xx_fls_read_at(self, self->base_address + name_offset,
                            &length_byte, 1U)) {
            goto fail;
        }
        name_size = (int64_t)length_byte;
        if (name_size < 1 ||
            !xx_fls_range_within(data_start, name_offset + 1, name_size)) {
            goto fail;
        }
        if (!xx_fls_read_at(self, self->base_address + name_offset + 1, leaf,
                            (size_t)name_size)) {
            goto fail;
        }
        if (!xx_fls_leaf_ok(leaf, (size_t)name_size)) goto fail;

        if (compression_flag == 0U) {
            /* Stored means the two sizes are the same number twice. */
            if (compressed_size != uncompressed_size) goto fail;
        } else {
            /* A compressed payload must actually open with the FLS-LZ tag;
             * this is the only content check available at parse time and the
             * cheapest way to reject a record table that merely looks right. */
            if (compressed_size < 1) goto fail;
            if (!xx_fls_read_at(self, self->base_address + data_offset,
                                &first_byte, 1U)) {
                goto fail;
            }
            if (first_byte != XX_FLS_LZ_TAG) goto fail;
        }

        for (copy_index = 0U; copy_index < (size_t)name_size; ++copy_index) {
            buffer[copy_index] = (char)leaf[copy_index];
        }
        buffer[(size_t)name_size] = '\0';
        name = xx_str_dup(buffer);
        if (!name) goto fail;
        if (!xx_fls_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + record_offset;
        member.header_size = XX_FLS_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container's own flag, unchanged. */
        member.method = (uint32_t)compression_flag;
        /* DOS date/time packed date-high / time-low. The record stores the
         * date word first and the time word second. */
        member.timestamp = ((uint64_t)xx_fls_le16(record + 27) << 16) |
                           (uint64_t)xx_fls_le16(record + 29);
        member.is_folder = false;

        if (!xx_fls_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        expected_data_offset += compressed_size + tail_size;
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    if (stream->count != (size_t)(records - 1)) goto fail;
    /* The payload chain may stop short of EOF (trailing slack exists in the
     * corpus) but must never run past it. */
    if (expected_data_offset > span) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_fls_stream_free(stream);
    return NULL;
}


/* Matches the reference reader's ceiling on the 16-bit-addressed table. */


/* An FLS-LZ stream always opens with this tag, and the decoder is handed the
 * extent WITH the tag, exactly as the container records it. */

/* The container's uncompressed size is attacker-controlled; refuse rather
 * than attempt an allocation this large. */

/* Flag 0 is a byte copy; flag 1 is a complete FLS-LZ stream whose plaintext
 * length the record supplies at +0x1f. */
static bool xx_fls_decode(Abstractformat *self, const xx_fls_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_FLS_MAX_DECODED ||
        member->uncompressed_size > XX_FLS_MAX_DECODED) {
        return false;
    }
    /* The format defines exactly two flags. Anything else must fail here:
     * quietly treating an unknown flag as stored produces garbage that a
     * caller cannot distinguish from data. */
    if (member->method != XX_FLS_METHOD_STORE &&
        member->method != XX_FLS_METHOD_LZ) {
        return false;
    }

    if (member->uncompressed_size == 0 && member->compressed_size == 0) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_fls_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_FLS_METHOD_STORE) {
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(input);
            return false;
        }
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* xx_flslz_decode_memory requires the whole packed extent to be consumed
     * and the end code to be reached; combined with the exact-length check
     * this is what keeps a short decode from being reported as success. */
    if (!xx_flslz_decode_memory(input, (size_t)member->compressed_size, output,
                                (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fls_init(xx_fls *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FLS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ibm-saveram-fls");
    xx_format_set_extension(&archive->format, "fls");
    archive->format.check_is_valid = xx_fls_check_is_valid;
    archive->format.handle_base_info = xx_fls_handle_base_info;
    archive->format.get_format_size = xx_fls_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fls_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fls_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fls_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fls_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fls_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fls_free_archive_records_reading;
    archive->format.destroy = xx_fls_vtable_destroy;
}

xx_fls *xx_fls_create(xx_io_device *device, int64_t base_address) {
    xx_fls *archive = (xx_fls *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_fls_init(archive, device, base_address);
    return archive;
}

void xx_fls_destroy(xx_fls *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fls_free(xx_fls *archive) {
    if (!archive) return;
    xx_fls_destroy(archive);
    xx_mem_free(archive);
}

static void xx_fls_vtable_destroy(Abstractformat *self) {
    xx_fls_destroy((xx_fls *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fls_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fls_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_fls_parse(self, pd);
    if (!stream) return false;
    xx_fls_stream_free(stream);
    return true;
}

bool xx_fls_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_fls *archive = (xx_fls *)self;
    xx_fls_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_fls_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_fls_stream_free(stream);
    return true;
}

int64_t xx_fls_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fls_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fls *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_fls_set_record(xx_archive_record *record,
                                 const xx_fls_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_fls_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_fls_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_fls_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_fls_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fls_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fls_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fls_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_fls_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_fls_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fls_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fls_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_fls_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fls_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fls_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_fls_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_fls_stream *stream;
    const xx_fls_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fls_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_fls_path_safe(member->name)) return false;

    path_option = xx_fls_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_fls_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_fls_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_fls_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

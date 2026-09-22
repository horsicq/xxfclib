/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JBF archives (headerless DOS LZHUF archiver).
 *
 * The container has NO header. The file opens directly with the first
 * member's compressed stream, and everything that describes the archive sits
 * at the end:
 *
 *   0x00                     member payloads, back to back, in an order the
 *                            directory records rather than one this layout
 *                            implies
 *   size - (count*31 + 1)    directory, 31 bytes per entry
 *   size - 1                 u8 member count, the LAST byte of the file
 *
 *   directory entry, 31 bytes:
 *     +0x00  name, 13 bytes, NUL terminated and NUL padded
 *     +0x0d  i32 LE compressed size
 *     +0x11  i32 LE uncompressed size (0 means the payload was stored)
 *     +0x15  i32 LE data offset, from the start of the file
 *     +0x19  u16 LE checksum, algorithm not recoverable from the container
 *     +0x1b  u16 LE DOS time
 *     +0x1d  u16 LE DOS date
 *
 * The only fixed bytes anywhere are the first eight, which are the opening
 * bytes of the first member's LZHUF stream and happen to be identical in
 * every known archive because the codec's adaptive tables always start in
 * the same state. They are a cheap first filter, not a header.
 *
 * What actually proves the format is the trailer arithmetic: taken in offset
 * order the members must tile [0, directory_offset) exactly, with no gap and
 * no overlap.
 *
 * The codec is the same LZHUF sub-variant the "!HZL" compressor uses, so
 * xx_hzl_decode_memory decodes both.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jbf/xx_jbf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/hzl/xx_hzl.h"

#include <stdio.h>

#define XX_JBF_COPY_CHUNK (64 * 1024)

typedef struct xx_jbf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_jbf_member;

typedef struct xx_jbf_stream_s {
    xx_jbf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_jbf_stream;

static void xx_jbf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_jbf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_jbf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_jbf_path_safe(const char *name) {
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

static void xx_jbf_stream_free(void *pointer) {
    xx_jbf_stream *stream = (xx_jbf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_jbf_add(xx_jbf_stream *stream,
                          const xx_jbf_member *member) {
    xx_jbf_member *grown = (xx_jbf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_JBF_LEAD_0 0xE4U
#define XX_JBF_LEAD_1 0x63U
#define XX_JBF_LEAD_2 0x31U
#define XX_JBF_LEAD_3 0x30U
#define XX_JBF_LEAD_4 0xB3U
#define XX_JBF_LEAD_5 0x70U
#define XX_JBF_LEAD_6 0xB4U
#define XX_JBF_LEAD_7 0x5CU
#define XX_JBF_ENTRY_SIZE 31
#define XX_JBF_NAME_SIZE 13
#define XX_JBF_MAX_MEMBERS 255
#define XX_JBF_MAX_UNCOMPRESSED ((int64_t)0x40000000)
#define XX_JBF_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_JBF_METHOD_STORE 0U
#define XX_JBF_METHOD_LZHUF 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_jbf_le16(const uint8_t *data);
static uint32_t xx_jbf_le32(const uint8_t *data);
static bool xx_jbf_name_ok(const uint8_t *field, size_t *out_length);
static xx_jbf_stream *xx_jbf_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_jbf_decode(Abstractformat *self, const xx_jbf_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The eight opening bytes of the first member's LZHUF stream. They are
 * identical in every known archive because the codec's adaptive tables always
 * start in the same state, which makes them a usable first filter - but they
 * are a property of the codec, not a container header, so they cannot be the
 * format's defence on their own. */

static uint16_t xx_jbf_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_jbf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name occupies the full 13-byte field: NUL terminated, NUL padded, and
 * no stale byte behind the terminator. */
static bool xx_jbf_name_ok(const uint8_t *field, size_t *out_length) {
    size_t length = 0U;
    size_t index;

    *out_length = 0U;
    while (length < (size_t)XX_JBF_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    if (length == 0U || length >= (size_t)XX_JBF_NAME_SIZE) return false;
    for (index = length; index < (size_t)XX_JBF_NAME_SIZE; ++index) {
        if (field[index] != 0U) return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t character = field[index];
        /* 8.3 DOS names only; this format grants no high-byte exemption. */
        if (character < 0x20U || character > 0x7EU) return false;
        if (character == '"' || character == '*' || character == '<' ||
            character == '>' || character == '?' || character == '|' ||
            character == ':' || character == '/' || character == '\\') {
            return false;
        }
    }
    *out_length = length;
    return true;
}

static xx_jbf_stream *xx_jbf_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_jbf_stream *stream = NULL;
    uint8_t *directory = NULL;
    uint8_t *placed = NULL;
    uint8_t lead[8];
    uint8_t count_byte;
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t directory_size;
    int64_t directory_offset;
    int64_t expected;
    int64_t index;
    int64_t step;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Eight lead bytes, one directory entry and the count byte. */
    if (span < 8 + XX_JBF_ENTRY_SIZE + 1) return NULL;
    if (!xx_jbf_read_at(self, self->base_address, lead, sizeof(lead))) {
        return NULL;
    }
    if (lead[0] != XX_JBF_LEAD_0 || lead[1] != XX_JBF_LEAD_1 ||
        lead[2] != XX_JBF_LEAD_2 || lead[3] != XX_JBF_LEAD_3 ||
        lead[4] != XX_JBF_LEAD_4 || lead[5] != XX_JBF_LEAD_5 ||
        lead[6] != XX_JBF_LEAD_6 || lead[7] != XX_JBF_LEAD_7) {
        return NULL;
    }

    if (!xx_jbf_read_at(self, self->base_address + span - 1, &count_byte,
                        1U)) {
        return NULL;
    }
    count = (int64_t)count_byte;
    if (count == 0) return NULL;
    directory_size = count * XX_JBF_ENTRY_SIZE;
    directory_offset = span - (directory_size + 1);
    /* The payload area must be non-empty: a directory that starts at or
     * before offset 0 leaves no room for the streams it describes. */
    if (directory_offset <= 0) return NULL;

    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_jbf_read_at(self, self->base_address + directory_offset, directory,
                        (size_t)directory_size)) {
        goto fail;
    }
    placed = (uint8_t *)xx_mem_alloc((size_t)count);
    if (!placed) goto fail;
    xx_mem_zero(placed, (size_t)count);

    for (index = 0; index < count; ++index) {
        const uint8_t *entry = directory + index * XX_JBF_ENTRY_SIZE;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        size_t name_length;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_jbf_name_ok(entry, &name_length)) goto fail;

        /* Signed on purpose: a negative field is corruption, not a huge
         * member. */
        compressed_size = (int64_t)(int32_t)xx_jbf_le32(entry + 0x0d);
        uncompressed_size = (int64_t)(int32_t)xx_jbf_le32(entry + 0x11);
        data_offset = (int64_t)(int32_t)xx_jbf_le32(entry + 0x15);
        if (compressed_size < 0 || uncompressed_size < 0 || data_offset < 0) {
            goto fail;
        }
        if (uncompressed_size > XX_JBF_MAX_UNCOMPRESSED) goto fail;
        /* Every member lives strictly inside the payload area, which ends
         * where the directory begins. */
        if (data_offset > directory_offset ||
            compressed_size > directory_offset - data_offset) {
            goto fail;
        }
        /* A member that claims plaintext but carries no stream is corrupt. */
        if (uncompressed_size > 0 && compressed_size == 0) goto fail;
    }

    /* THE false-positive defence. There is no header, so the only evidence
     * that this file is a JBF archive is that the directory closes: taken in
     * offset order the members must tile [0, directory_offset) with no gap
     * and no overlap. A random file whose last byte happens to be a small
     * count does not survive this, and loosening it to "every member fits
     * inside the payload area" throws the format's whole identity away.
     *
     * Walked rather than sorted: each step looks for the not-yet-placed
     * entry that starts exactly where the previous one ended. Count is at
     * most 255, so the quadratic walk is cheap, and requiring each entry to
     * be placed exactly once rejects duplicate offsets as well as gaps. */
    expected = 0;
    for (step = 0; step < count; ++step) {
        bool found = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        for (index = 0; index < count; ++index) {
            const uint8_t *entry = directory + index * XX_JBF_ENTRY_SIZE;
            if (placed[index]) continue;
            if ((int64_t)(int32_t)xx_jbf_le32(entry + 0x15) != expected) {
                continue;
            }
            placed[index] = 1U;
            expected += (int64_t)(int32_t)xx_jbf_le32(entry + 0x0d);
            found = true;
            break;
        }
        if (!found) goto fail;
    }
    if (expected != directory_offset) goto fail;

    stream = (xx_jbf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < count; ++index) {
        const uint8_t *entry = directory + index * XX_JBF_ENTRY_SIZE;
        xx_jbf_member member;
        char buffer[XX_JBF_NAME_SIZE + 1];
        char *name;
        size_t name_length;
        size_t copy_index;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        uint32_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_jbf_name_ok(entry, &name_length)) goto fail;
        for (copy_index = 0U; copy_index < name_length; ++copy_index) {
            buffer[copy_index] = (char)entry[copy_index];
        }
        buffer[name_length] = '\0';

        compressed_size = (int64_t)(int32_t)xx_jbf_le32(entry + 0x0d);
        uncompressed_size = (int64_t)(int32_t)xx_jbf_le32(entry + 0x11);
        data_offset = (int64_t)(int32_t)xx_jbf_le32(entry + 0x15);
        if (uncompressed_size == 0) {
            /* Zero declared plaintext means the payload was stored, so the
             * stream bytes are the member; publishing the two sizes as equal
             * keeps the listing coherent. */
            method = XX_JBF_METHOD_STORE;
            uncompressed_size = compressed_size;
        } else {
            method = XX_JBF_METHOD_LZHUF;
        }
        if (!xx_jbf_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        name = xx_str_dup(buffer);
        if (!name) goto fail;
        if (!xx_jbf_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset =
            self->base_address + directory_offset + index * XX_JBF_ENTRY_SIZE;
        member.header_size = XX_JBF_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = method;
        /* DOS date/time packed date-high / time-low. The entry stores the
         * time word FIRST and the date word second. */
        member.timestamp = ((uint64_t)xx_jbf_le16(entry + 0x1d) << 16) |
                           (uint64_t)xx_jbf_le16(entry + 0x1b);
        /* The format has no directory entries and no attribute field. */
        member.is_folder = false;
        /* The u16 at +0x19 is a checksum whose algorithm is not recoverable
         * from the container, so it is not published as a verifiable CRC. */

        if (!xx_jbf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }

    xx_mem_free(placed);
    xx_mem_free(directory);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(placed);
    xx_mem_free(directory);
    xx_jbf_stream_free(stream);
    return NULL;
}


/* The count is a single byte, so this is the format's own hard limit, not a
 * runaway guard. */


/* The directory has NO method field. These two numbers are derived from the
 * uncompressed-size field exactly as the reference reader derives its handle
 * method: a declared plaintext length of zero means the payload is stored. */

static bool xx_jbf_decode(Abstractformat *self, const xx_jbf_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_JBF_METHOD_STORE &&
        member->method != XX_JBF_METHOD_LZHUF) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_JBF_MAX_DECODED ||
        member->uncompressed_size > XX_JBF_MAX_DECODED) {
        return false;
    }

    if (member->compressed_size == 0) {
        /* A zero-length stored member is a real, empty file. */
        if (member->uncompressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_jbf_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_JBF_METHOD_STORE) {
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
    /* LZHUF carries no terminator, so the directory's plaintext length is
     * the only stop condition; demanding exactly that many bytes is what
     * stops a truncated stream from being reported as a short success. */
    if (!xx_hzl_decode_memory(input, (size_t)member->compressed_size, output,
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

void xx_jbf_init(xx_jbf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_JBF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-jbf");
    xx_format_set_extension(&archive->format, "jbf");
    archive->format.check_is_valid = xx_jbf_check_is_valid;
    archive->format.handle_base_info = xx_jbf_handle_base_info;
    archive->format.get_format_size = xx_jbf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jbf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jbf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jbf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jbf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jbf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jbf_free_archive_records_reading;
    archive->format.destroy = xx_jbf_vtable_destroy;
}

xx_jbf *xx_jbf_create(xx_io_device *device, int64_t base_address) {
    xx_jbf *archive = (xx_jbf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_jbf_init(archive, device, base_address);
    return archive;
}

void xx_jbf_destroy(xx_jbf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jbf_free(xx_jbf *archive) {
    if (!archive) return;
    xx_jbf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jbf_vtable_destroy(Abstractformat *self) {
    xx_jbf_destroy((xx_jbf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jbf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jbf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_jbf_parse(self, pd);
    if (!stream) return false;
    xx_jbf_stream_free(stream);
    return true;
}

bool xx_jbf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jbf *archive = (xx_jbf *)self;
    xx_jbf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_jbf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_jbf_stream_free(stream);
    return true;
}

int64_t xx_jbf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jbf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_jbf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jbf_set_record(xx_archive_record *record,
                                 const xx_jbf_member *member) {
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

static bool xx_jbf_copy_options(xx_list_s *target,
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

static const xx_var *xx_jbf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_jbf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_jbf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_jbf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_jbf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_jbf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_jbf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_jbf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jbf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jbf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_jbf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jbf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_jbf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jbf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_jbf_stream *stream;
    const xx_jbf_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jbf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_jbf_path_safe(member->name)) return false;

    path_option = xx_jbf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_jbf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_jbf_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_jbf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

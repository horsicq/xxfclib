/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "RSVKDATA" / "DLIBDATA" containers.
 *
 *   file header, 16 bytes at offset 0:
 *     0x00  4 bytes  "RSVK" or "DLIB"
 *     0x04  4 bytes  "DATA" - the first member block starts at 0x04, so the
 *                    container tag and the first block's tag are adjacent and
 *                    the eight bytes together are the detection signature
 *     0x0c  u32 LE   that block's packed size, required to be non-negative
 *
 *   trailer, the LAST 12 bytes of the file:
 *     0x00  4 bytes  "ECDR" or "DEND"
 *     0x08  u32 LE   offset of the directory
 *
 *   the directory runs from that offset up to the trailer; each entry is
 *     0x00  4 bytes  "CFHS" or "FILE"
 *     0x04  u32 LE   a packed-size HINT - it does NOT match the real chain
 *     0x08  i32 LE   uncompressed size of the member
 *     0x0c  i32 LE   number of blocks in the member's chain
 *     0x10  i32 LE   offset of the member's first block
 *     0x14  u16 LE   DOS time
 *     0x16  u16 LE   DOS date
 *     0x18  u32 LE   attributes
 *     0x1c  ...      NUL-terminated DOS path ("d:\aida32.da0")
 *
 *   a member is a chain of blocks, each a 20-byte header ("DATA", CRC-32,
 *   packed size at +0x08, BWT indices) followed by its arithmetic-coded
 *   payload.  THE ENTRY'S PACKED-SIZE FIELD IS A HINT AND IS NOT USABLE, so
 *   the parse walks the declared number of blocks to obtain the member's real
 *   compressed extent - that walk doubles as the check that the chain really
 *   lands inside the data area.
 *
 * The member codec (block-sorting: arithmetic decode -> MTF -> inverse BWT,
 * with a CRC-32 per block) lives in xx_rsvk_decode_memory() and is NOT
 * duplicated here; this file is the container only.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rsvk/xx_rsvk.h"

#include "xxfclib/algo/rsvk/xx_rsvk.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* REGISTRATION PENDING.  xxfc_defs.h carries no XX_FILE_TYPE_RSVK yet and this
 * port must not edit that shared header.  Delete this block when the enum is
 * added - until then the reader reports itself as plain binary. */

#define XX_RSVK_HEADER_SIZE 16
#define XX_RSVK_TRAILER_SIZE 12
#define XX_RSVK_DIR_ENTRY_SIZE 28
#define XX_RSVK_BLOCK_HEADER_SIZE 20
#define XX_RSVK_FIRST_BLOCK_OFFSET 4
#define XX_RSVK_MAX_NAME_SIZE 1024
#define XX_RSVK_MAX_MEMBERS 100000
#define XX_RSVK_MAX_BLOCKS 100000
#define XX_RSVK_MAX_UNCOMPRESSED ((int64_t)0x40000000)
/* The directory is read whole, so its declared extent bounds an allocation
 * driven by an attacker-controlled trailer field.  Nothing in the reference
 * corpus comes near this. */
#define XX_RSVK_MAX_DIRECTORY_SIZE ((int64_t)16 * 1024 * 1024)
#define XX_RSVK_METHOD_STORE 0U
#define XX_RSVK_METHOD_BWT 1U

typedef struct xx_rsvk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t attributes;
    uint64_t timestamp;
    bool is_folder;
} xx_rsvk_member;

typedef struct xx_rsvk_stream_s {
    xx_rsvk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t directory_offset;
    int64_t directory_size;
} xx_rsvk_stream;

static void xx_rsvk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_rsvk_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_rsvk_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_rsvk_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_rsvk_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool xx_rsvk_is_tag(const uint8_t *data, const char *tag) {
    return data[0] == (uint8_t)tag[0] && data[1] == (uint8_t)tag[1] &&
           data[2] == (uint8_t)tag[2] && data[3] == (uint8_t)tag[3];
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rsvk_path_safe(const char *name) {
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

/* The stored name is printable ASCII; a control byte is the cheapest sign
 * that the "directory" is really some other file's bytes. High-bit characters
 * are left alone, exactly as the reference does - DOS code pages used them. */
static bool xx_rsvk_printable_name(const uint8_t *raw, size_t length) {
    size_t index;

    if (length == 0U || length > (size_t)XX_RSVK_MAX_NAME_SIZE) return false;
    for (index = 0U; index < length; ++index) {
        if (raw[index] < 0x20U || raw[index] == 0x7fU) return false;
    }
    return true;
}

/* Remove every "../" sequence, repeatedly, so that no rewriting of the rest of
 * the name can reintroduce one. */
static void xx_rsvk_strip_parent(char *text) {
    bool changed = true;

    while (changed) {
        size_t index = 0U;

        changed = false;
        while (text[index]) {
            if (text[index] == '.' && text[index + 1U] == '.' &&
                text[index + 2U] == '/') {
                size_t copy = index;
                while (text[copy + 3U]) {
                    text[copy] = text[copy + 3U];
                    ++copy;
                }
                text[copy] = '\0';
                changed = true;
            } else {
                ++index;
            }
        }
    }
}

/* Turn the stored DOS path into a relative POSIX one: separators flipped, a
 * drive prefix dropped, leading separators dropped, parent references removed.
 * Returns NULL when nothing usable is left. */
static char *xx_rsvk_clean_name(const uint8_t *raw, size_t length) {
    char *name;
    size_t index;
    size_t start = 0U;
    size_t remaining;

    if (!xx_rsvk_printable_name(raw, length)) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        name[index] = (raw[index] == (uint8_t)'\\') ? '/' : (char)raw[index];
    }
    name[length] = '\0';

    if (length >= 2U && name[1] == ':') start = 2U;
    while (name[start] == '/') ++start;
    if (start != 0U) {
        remaining = xx_str_len(name + start);
        xx_rt_memmove(name, name + start, remaining + 1U);
    }
    xx_rsvk_strip_parent(name);
    if (!name[0]) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

static void xx_rsvk_stream_free(void *pointer) {
    xx_rsvk_stream *stream = (xx_rsvk_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p member->name. */
static bool xx_rsvk_add(xx_rsvk_stream *stream, const xx_rsvk_member *member) {
    xx_rsvk_member *grown = (xx_rsvk_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

/* Walk a member's declared block chain and return its real compressed extent.
 * Every block header is validated against the directory offset, which is the
 * hard end of the data area, so a forged block count or packed size cannot
 * push the walk past it. */
static bool xx_rsvk_measure_chain(Abstractformat *self, int64_t base,
                                  int64_t data_offset, int64_t block_count,
                                  int64_t directory_offset, int64_t *size,
                                  xx_pd_struct *pd) {
    uint8_t header[XX_RSVK_BLOCK_HEADER_SIZE];
    int64_t cursor = data_offset;
    int64_t index;

    for (index = 0; index < block_count; ++index) {
        int64_t packed;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (cursor > directory_offset - (int64_t)XX_RSVK_BLOCK_HEADER_SIZE) {
            return false;
        }
        if (!xx_rsvk_read_at(self, base + cursor, header, sizeof(header))) {
            return false;
        }
        if (!xx_rsvk_is_tag(header, "DATA")) return false;
        packed = (int64_t)xx_rsvk_le32(header + 8U);
        if (packed <= 0 ||
            packed > directory_offset - cursor -
                         (int64_t)XX_RSVK_BLOCK_HEADER_SIZE) {
            return false;
        }
        cursor += (int64_t)XX_RSVK_BLOCK_HEADER_SIZE + packed;
    }
    *size = cursor - data_offset;
    return true;
}

static xx_rsvk_stream *xx_rsvk_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_rsvk_stream *stream = NULL;
    xx_rsvk_member member;
    uint8_t header[XX_RSVK_HEADER_SIZE];
    uint8_t trailer[XX_RSVK_TRAILER_SIZE];
    uint8_t *directory = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t cursor = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_RSVK_FIRST_BLOCK_OFFSET +
                         XX_RSVK_BLOCK_HEADER_SIZE + XX_RSVK_TRAILER_SIZE)) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    if (!xx_rsvk_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* Eight bytes of signature: the container tag AND the first block's
     * "DATA". Either half alone would match far too much. */
    if (!xx_rsvk_is_tag(header, "RSVK") && !xx_rsvk_is_tag(header, "DLIB")) {
        return NULL;
    }
    if (!xx_rsvk_is_tag(header + 4U, "DATA")) return NULL;
    /* The first block's packed size is written as u32 but read signed, as the
     * reference detector does; a negative one is a rejection. */
    if ((int32_t)xx_rsvk_le32(header + 12U) < 0) return NULL;

    if (!xx_rsvk_read_at(self, self->base_address + span -
                                   (int64_t)XX_RSVK_TRAILER_SIZE,
                         trailer, sizeof(trailer))) {
        return NULL;
    }
    if (!xx_rsvk_is_tag(trailer, "ECDR") && !xx_rsvk_is_tag(trailer, "DEND")) {
        return NULL;
    }
    directory_offset = (int64_t)xx_rsvk_le32(trailer + 8U);
    if (directory_offset < (int64_t)XX_RSVK_FIRST_BLOCK_OFFSET ||
        directory_offset > span - (int64_t)XX_RSVK_TRAILER_SIZE -
                               (int64_t)XX_RSVK_DIR_ENTRY_SIZE) {
        return NULL;
    }
    directory_size =
        (span - (int64_t)XX_RSVK_TRAILER_SIZE) - directory_offset;
    if (directory_size > XX_RSVK_MAX_DIRECTORY_SIZE) return NULL;

    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_rsvk_read_at(self, self->base_address + directory_offset,
                         directory, (size_t)directory_size)) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_rsvk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    while (cursor <= directory_size - (int64_t)XX_RSVK_DIR_ENTRY_SIZE) {
        const uint8_t *entry = directory + cursor;
        int64_t packed_hint;
        int64_t uncompressed_size;
        int64_t block_count;
        int64_t data_offset;
        int64_t compressed_size = 0;
        int64_t name_start;
        int64_t name_end;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_RSVK_MAX_MEMBERS) goto fail;
        if (!xx_rsvk_is_tag(entry, "CFHS") && !xx_rsvk_is_tag(entry, "FILE")) {
            goto fail;
        }

        /* Every size in the entry is written as u32 and read signed. */
        packed_hint = (int64_t)(int32_t)xx_rsvk_le32(entry + 4U);
        uncompressed_size = (int64_t)(int32_t)xx_rsvk_le32(entry + 8U);
        block_count = (int64_t)(int32_t)xx_rsvk_le32(entry + 12U);
        data_offset = (int64_t)(int32_t)xx_rsvk_le32(entry + 16U);
        if (packed_hint < 0 || uncompressed_size < 0 ||
            uncompressed_size > XX_RSVK_MAX_UNCOMPRESSED || block_count <= 0 ||
            block_count > (int64_t)XX_RSVK_MAX_BLOCKS ||
            data_offset < (int64_t)XX_RSVK_FIRST_BLOCK_OFFSET ||
            data_offset >= directory_offset) {
            goto fail;
        }

        /* The name is NUL-terminated inside the directory; an unterminated one
         * means the directory is not one. */
        name_start = cursor + (int64_t)XX_RSVK_DIR_ENTRY_SIZE;
        name_end = name_start;
        while (name_end < directory_size && directory[name_end] != 0U) {
            ++name_end;
        }
        if (name_end >= directory_size) goto fail;
        name = xx_rsvk_clean_name(directory + name_start,
                                  (size_t)(name_end - name_start));
        if (!name) goto fail;

        /* The chain walk is what yields the member's real packed length: the
         * entry's own hint does not match it. */
        if (!xx_rsvk_measure_chain(self, self->base_address, data_offset,
                                   block_count, directory_offset,
                                   &compressed_size, pd)) {
            goto fail;
        }
        /* The walk already bounds the chain by the directory offset, which is
         * itself inside the file; this is the belt to that braces, stated in
         * terms of the device rather than of the directory. */
        if (!xx_rsvk_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + directory_offset + cursor;
        member.header_size =
            (int64_t)XX_RSVK_DIR_ENTRY_SIZE + (name_end - name_start) + 1;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* An empty member carries no blocks worth decoding. */
        member.method = (uncompressed_size == 0) ? XX_RSVK_METHOD_STORE
                                                 : XX_RSVK_METHOD_BWT;
        member.attributes = xx_rsvk_le32(entry + 24U);
        member.timestamp = ((uint64_t)xx_rsvk_le16(entry + 22U) << 16) |
                           (uint64_t)xx_rsvk_le16(entry + 20U);
        member.is_folder = false;

        if (!xx_rsvk_add(stream, &member)) goto fail;
        name = NULL;

        cursor = name_end + 1;
    }

    if (stream->count == 0U) goto fail;

    xx_mem_free(directory);
    /* The trailer is the last thing in the container, so the archive ends with
     * it. */
    stream->archive_size = span;
    stream->directory_offset = self->base_address + directory_offset;
    stream->directory_size = directory_size;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_mem_free(directory);
    xx_rsvk_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* Decode one member with the container's block-sorting codec.  The member's
 * whole block chain is handed over in one piece: the codec walks the "DATA"
 * headers itself and verifies each block's CRC-32. */
static bool xx_rsvk_decode(Abstractformat *self, const xx_rsvk_member *member,
                           uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_RSVK_MAX_UNCOMPRESSED) return false;
    if (member->uncompressed_size > XX_RSVK_MAX_UNCOMPRESSED) return false;

    /* A zero-length member is legal, but the codec refuses a zero output
     * capacity, so it is answered here with a one-byte allocation and a
     * reported length of zero. */
    if (member->uncompressed_size == 0) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_rsvk_read_at(self, member->data_offset, input,
                         (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    decoded = xx_rsvk_decode_memory(input, (size_t)member->compressed_size,
                                    output, (size_t)member->uncompressed_size,
                                    &written);
    /* The codec already demands an exact fill; the second half of this test is
     * what makes that a property of this reader rather than of the codec. */
    if (!decoded || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);

    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_rsvk_init(xx_rsvk *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RSVK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rsvk");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_rsvk_check_is_valid;
    archive->format.handle_base_info = xx_rsvk_handle_base_info;
    archive->format.get_format_size = xx_rsvk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rsvk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rsvk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rsvk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rsvk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rsvk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rsvk_free_archive_records_reading;
    archive->format.destroy = xx_rsvk_vtable_destroy;
    archive->directory_offset = -1;
}

xx_rsvk *xx_rsvk_create(xx_io_device *device, int64_t base_address) {
    xx_rsvk *archive = (xx_rsvk *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rsvk_init(archive, device, base_address);
    return archive;
}

void xx_rsvk_destroy(xx_rsvk *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rsvk_free(xx_rsvk *archive) {
    if (!archive) return;
    xx_rsvk_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rsvk_vtable_destroy(Abstractformat *self) {
    xx_rsvk_destroy((xx_rsvk *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rsvk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rsvk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rsvk_parse(self, pd);
    if (!stream) return false;
    xx_rsvk_stream_free(stream);
    return true;
}

bool xx_rsvk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rsvk *archive = (xx_rsvk *)self;
    xx_rsvk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rsvk_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->directory_offset = stream->directory_offset;
    archive->directory_size = stream->directory_size;
    xx_rsvk_stream_free(stream);
    return true;
}

int64_t xx_rsvk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rsvk_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rsvk *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rsvk_set_record(xx_archive_record *record,
                               const xx_rsvk_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_rsvk_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_rsvk_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rsvk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rsvk_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rsvk_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rsvk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rsvk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rsvk_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rsvk_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rsvk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rsvk_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_rsvk_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rsvk_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_rsvk_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rsvk_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rsvk_stream *stream;
    const xx_rsvk_member *member;
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
    stream = (xx_rsvk_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rsvk_path_safe(member->name)) return false;

    path_option = xx_rsvk_get_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rsvk_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rsvk_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_rsvk_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

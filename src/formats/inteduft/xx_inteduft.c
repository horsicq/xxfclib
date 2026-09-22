/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * INTEDUFT resource packs.
 *
 *   header, 6 bytes at 0x00:
 *     0x00  u32 LE magic 0x04072E7C  (bytes 7C 2E 07 04)
 *     0x04  u16 LE member count, at least 1
 *
 *   index, immediately at 0x06, one VARIABLE-length entry per member:
 *     +0x00  i32 LE absolute file offset of the member header, must be > 0
 *     +0x04  u8  name length, 1..12
 *     +0x05  name bytes, exactly that many, no terminator
 *
 *   The entries are packed with no padding, so the index cannot be indexed
 *   arithmetically: the only way to find entry N is to walk entries 0..N-1.
 *   The index therefore also has no stored size - it ends where the last
 *   entry ends, and that end is the lower bound every member offset is
 *   checked against.
 *
 *   member header, 16 bytes at the entry's offset:
 *     0x00  u16 reserved / flags, not interpreted
 *     0x02  u16 LE method: 0 = stored, 8 = raw Deflate. Nothing else.
 *     0x04  u32 LE CRC-32 of the plain bytes (EDB88320, init/xor FFFFFFFF)
 *     0x08  i32 LE packed size
 *     0x0c  i32 LE uncompressed size
 *   the payload follows the header directly.
 *
 *   Method 8 is RAW Deflate (RFC 1951) - there is no zlib header and no
 *   Adler-32 trailer, which matches the reference reader dispatching it
 *   through HANDLE_METHOD_DEFLATE rather than a zlib wrapper.
 *
 * Members are addressed by absolute offset and may be laid out in any order,
 * so the archive ends at the furthest member end, not at the last entry.
 *
 * A four-byte magic is cheap to hit by accident. What actually keeps a stray
 * 7C 2E 07 04 from parsing is the index walk: every one of `count` entries
 * must carry a 1..12 byte name that fits inside the index bound, and each of
 * the resulting offsets must land at or after the index end and address a
 * full 16-byte header whose method is one of exactly two values and whose
 * payload fits in the file.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/inteduft/xx_inteduft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <stdio.h>

#define XX_INTEDUFT_COPY_CHUNK (64 * 1024)

typedef struct xx_inteduft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_inteduft_member;

typedef struct xx_inteduft_stream_s {
    xx_inteduft_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_inteduft_stream;

static void xx_inteduft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_inteduft_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_inteduft_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_inteduft_path_safe(const char *name) {
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

static void xx_inteduft_stream_free(void *pointer) {
    xx_inteduft_stream *stream = (xx_inteduft_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_inteduft_add(xx_inteduft_stream *stream,
                          const xx_inteduft_member *member) {
    xx_inteduft_member *grown = (xx_inteduft_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_INTEDUFT_HEADER_SIZE 6
#define XX_INTEDUFT_MEMBER_HEADER_SIZE 16
#define XX_INTEDUFT_MAGIC 0x04072E7CU
#define XX_INTEDUFT_MAX_NAME 12
#define XX_INTEDUFT_MIN_ENTRY_SIZE 6 /* 5 fixed bytes plus a 1-byte name */
#define XX_INTEDUFT_MAX_ENTRY_SIZE (5 + XX_INTEDUFT_MAX_NAME)
#define XX_INTEDUFT_METHOD_STORE 0U
#define XX_INTEDUFT_METHOD_DEFLATE 8U
#define XX_INTEDUFT_MAX_MEMBERS 65535
#define XX_INTEDUFT_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_inteduft_le32(const uint8_t *data);
static uint16_t xx_inteduft_le16(const uint8_t *data);
static size_t xx_inteduft_name_length(const uint8_t *raw, size_t size);
static xx_inteduft_stream *xx_inteduft_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_inteduft_decode(Abstractformat *self, const xx_inteduft_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The count is a u16, so the container itself cannot describe more. */

static uint32_t xx_inteduft_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_inteduft_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

/* The raw name field is not terminated but may be NUL-padded, and trailing
 * spaces are padding too. The reference reader percent-escapes any other
 * byte outside the printable range; this reader rejects instead, because the
 * name here becomes a real path on disk and an escaped name is a name the
 * archive never contained. */
static size_t xx_inteduft_name_length(const uint8_t *raw, size_t size) {
    size_t length = 0U;

    while (length < size && raw[length] != 0U) ++length;
    while (length > 0U && raw[length - 1U] == 0x20U) --length;
    return length;
}

static xx_inteduft_stream *xx_inteduft_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_inteduft_stream *stream = NULL;
    uint8_t header[XX_INTEDUFT_HEADER_SIZE];
    uint8_t member_header[XX_INTEDUFT_MEMBER_HEADER_SIZE];
    uint8_t *index = NULL;
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index_bound;
    int64_t index_position;
    int64_t index_size;
    int64_t archive_end;
    int64_t walk;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Smallest possible archive: header, one 6-byte index entry, one member
     * header, zero payload bytes. */
    if (span < XX_INTEDUFT_HEADER_SIZE + XX_INTEDUFT_MIN_ENTRY_SIZE +
                   XX_INTEDUFT_MEMBER_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_inteduft_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }
    if (xx_inteduft_le32(header) != XX_INTEDUFT_MAGIC) return NULL;

    count = (int64_t)xx_inteduft_le16(header + 4);
    /* A zero-member pack is not a pack; the format has no empty form. */
    if (count < 1 || count > XX_INTEDUFT_MAX_MEMBERS) return NULL;

    /* Entries are variable length, so the index has no computable size - only
     * a bound: it can never exceed count * (5 + 12) bytes, nor the rest of the
     * file. The second half of this test is the cheap structural gate that a
     * random magic hit fails: even the shortest legal index, six bytes per
     * entry, must already fit in what is left of the file. */
    index_bound = span - XX_INTEDUFT_HEADER_SIZE;
    if (index_bound > count * XX_INTEDUFT_MAX_ENTRY_SIZE) {
        index_bound = count * XX_INTEDUFT_MAX_ENTRY_SIZE;
    }
    if (index_bound < count * XX_INTEDUFT_MIN_ENTRY_SIZE) return NULL;

    index = (uint8_t *)xx_mem_alloc((size_t)index_bound);
    if (!index) return NULL;
    if (!xx_inteduft_read_at(self, self->base_address + XX_INTEDUFT_HEADER_SIZE,
                             index, (size_t)index_bound)) {
        xx_mem_free(index);
        return NULL;
    }

    /* First pass: walk the index only to learn where it ends. Nothing can be
     * published yet, because the index end is the floor every member offset
     * is validated against and it is not known until the walk finishes. */
    index_position = 0;
    for (walk = 0; walk < count; ++walk) {
        int64_t entry_offset;
        int64_t name_length;

        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(index);
            return NULL;
        }
        if (index_position + 5 > index_bound) {
            xx_mem_free(index);
            return NULL;
        }
        entry_offset = (int64_t)(int32_t)xx_inteduft_le32(index +
                                                          index_position);
        name_length = (int64_t)index[index_position + 4];
        /* Offset 0 would point into the header, so it is reserved as "no
         * member" and rejected rather than clamped. */
        if (entry_offset <= 0 || name_length < 1 ||
            name_length > XX_INTEDUFT_MAX_NAME) {
            xx_mem_free(index);
            return NULL;
        }
        if (index_position + 5 + name_length > index_bound) {
            xx_mem_free(index);
            return NULL;
        }
        /* A control byte where a name should start is the strongest single
         * signal that this is not an index at all. */
        if (index[index_position + 5] < 0x20U) {
            xx_mem_free(index);
            return NULL;
        }
        index_position += 5 + name_length;
    }
    index_size = XX_INTEDUFT_HEADER_SIZE + index_position;

    stream = (xx_inteduft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(index);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    archive_end = index_size;

    /* Second pass over the same buffer: the entries were already proved to
     * be well formed, so this walk only reads member headers. */
    index_position = 0;
    for (walk = 0; walk < count; ++walk) {
        xx_inteduft_member member;
        const uint8_t *raw;
        char *name;
        size_t name_size;
        size_t raw_size;
        size_t copy;
        int64_t entry_offset;
        int64_t data_offset;
        int64_t packed_size;
        int64_t plain_size;
        int64_t end;
        uint32_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = (int64_t)(int32_t)xx_inteduft_le32(index +
                                                          index_position);
        raw_size = (size_t)index[index_position + 4];
        raw = index + index_position + 5;
        index_position += 5 + (int64_t)raw_size;

        /* A member header overlapping the index it was reached from means the
         * two structures disagree about the file; refuse rather than pick. */
        if (entry_offset < index_size) goto fail;
        if (!xx_inteduft_range_within(span, entry_offset,
                                      XX_INTEDUFT_MEMBER_HEADER_SIZE)) {
            goto fail;
        }
        if (!xx_inteduft_read_at(self, self->base_address + entry_offset,
                                 member_header, sizeof(member_header))) {
            goto fail;
        }

        method = (uint32_t)xx_inteduft_le16(member_header + 2);
        packed_size = (int64_t)(int32_t)xx_inteduft_le32(member_header + 8);
        plain_size = (int64_t)(int32_t)xx_inteduft_le32(member_header + 12);
        if (packed_size < 0 || plain_size < 0) goto fail;
        /* The format defines exactly two methods. Accepting an unknown third
         * one here would hand the decoder bytes it cannot name, so the whole
         * archive is rejected instead. */
        if (method != XX_INTEDUFT_METHOD_STORE &&
            method != XX_INTEDUFT_METHOD_DEFLATE) {
            goto fail;
        }
        /* A stored member that claims to change size is self-contradictory,
         * and this is the one consistency check inside the member header. */
        if (method == XX_INTEDUFT_METHOD_STORE && packed_size != plain_size) {
            goto fail;
        }

        data_offset = entry_offset + XX_INTEDUFT_MEMBER_HEADER_SIZE;
        if (!xx_inteduft_range_within(span, data_offset, packed_size)) {
            goto fail;
        }

        name_size = xx_inteduft_name_length(raw, raw_size);
        for (copy = 0U; copy < name_size; ++copy) {
            if (raw[copy] < 0x20U || raw[copy] > 0x7EU) goto fail;
        }
        if (name_size != 0U) {
            name = (char *)xx_mem_alloc(name_size + 1U);
            if (!name) goto fail;
            for (copy = 0U; copy < name_size; ++copy) {
                name[copy] = (char)raw[copy];
            }
            name[name_size] = '\0';
        } else {
            /* An all-space name is padding, not a name. The reference reader
             * synthesises a positional one rather than dropping the member,
             * so the member count stays the index's count. */
            name = (char *)xx_mem_alloc(32U);
            if (!name) goto fail;
            xx_rt_snprintf(name, 32U, "record%d", (int)walk);
        }
        if (!xx_inteduft_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_INTEDUFT_MEMBER_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed_size;
        member.uncompressed_size = plain_size;
        member.method = method;
        if (!xx_inteduft_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        /* Offsets are absolute and unordered, so the archive ends at the
         * furthest payload end rather than after the final entry. */
        end = data_offset + packed_size;
        if (end > archive_end) archive_end = end;
    }
    if (stream->count == 0U) goto fail;

    xx_mem_free(index);
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_mem_free(index);
    xx_inteduft_stream_free(stream);
    return NULL;
}


/* 256 MiB. The uncompressed size is an attacker-controlled i32, so the
 * allocation is capped rather than attempted. */

static bool xx_inteduft_decode(Abstractformat *self,
                               const xx_inteduft_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_INTEDUFT_MAX_DECODED ||
        member->compressed_size > XX_INTEDUFT_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0 &&
        !xx_inteduft_read_at(self, member->data_offset, packed,
                             (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    /* The read above may be large; give a cancel a chance before decoding. */
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == 0U) {
        /* Parse already refused a stored member whose two sizes disagree, so
         * the packed buffer is the plain buffer. */
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = (size_t)member->uncompressed_size;
        return true;
    }

    if (member->method != 8U) {
        /* Parse only publishes 0 and 8; anything else reaching here would be
         * a method this reader cannot honour, and treating it as stored would
         * write garbage that looks like a successful extraction. */
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* Method 8 is RAW Deflate: no zlib header, no Adler-32 trailer. */
    if (!xx_deflate_decompress_memory(packed, (size_t)member->compressed_size,
                                      plain,
                                      (size_t)member->uncompressed_size,
                                      &written, false)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* A short decode reported as success is the one failure the caller cannot
     * detect, so a length disagreeing with the container is a hard failure. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_inteduft_init(xx_inteduft *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_INTEDUFT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-inteduft");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_inteduft_check_is_valid;
    archive->format.handle_base_info = xx_inteduft_handle_base_info;
    archive->format.get_format_size = xx_inteduft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_inteduft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_inteduft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_inteduft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_inteduft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_inteduft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_inteduft_free_archive_records_reading;
    archive->format.destroy = xx_inteduft_vtable_destroy;
}

xx_inteduft *xx_inteduft_create(xx_io_device *device, int64_t base_address) {
    xx_inteduft *archive = (xx_inteduft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_inteduft_init(archive, device, base_address);
    return archive;
}

void xx_inteduft_destroy(xx_inteduft *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_inteduft_free(xx_inteduft *archive) {
    if (!archive) return;
    xx_inteduft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_inteduft_vtable_destroy(Abstractformat *self) {
    xx_inteduft_destroy((xx_inteduft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_inteduft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_inteduft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_inteduft_parse(self, pd);
    if (!stream) return false;
    xx_inteduft_stream_free(stream);
    return true;
}

bool xx_inteduft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_inteduft *archive = (xx_inteduft *)self;
    xx_inteduft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_inteduft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_inteduft_stream_free(stream);
    return true;
}

int64_t xx_inteduft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_inteduft_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_inteduft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_inteduft_set_record(xx_archive_record *record,
                                 const xx_inteduft_member *member) {
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

static bool xx_inteduft_copy_options(xx_list_s *target,
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

static const xx_var *xx_inteduft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_inteduft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_inteduft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_inteduft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_inteduft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_inteduft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_inteduft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_inteduft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_inteduft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_inteduft_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_inteduft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_inteduft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_inteduft_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_inteduft_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_inteduft_stream *stream;
    const xx_inteduft_member *member;
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
    stream = (xx_inteduft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_inteduft_path_safe(member->name)) return false;

    path_option = xx_inteduft_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_inteduft_decode(self, member, &plain, &plain_size, pd);
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
        !xx_inteduft_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_inteduft_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

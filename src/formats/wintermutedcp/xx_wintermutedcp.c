/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Wintermute Engine DCP packages (".dcp"), versions 0x100 and 0x200.
 * Ported from XArchive's games/xwintermutedcp.cpp and cross-checked against
 * U3's DCP handler (class zha, VMT 004c2458).
 *
 *   header, 128 bytes at offset 0 (132 for version 2):
 *     0x00   8  bytes    magic de ad c0 de "JUNK"
 *     0x08   4  u32 LE   version, 0x100 or 0x200
 *     ...
 *     0x7c   4  u32 LE   directory count
 *     0x80   4  u32 LE   directory offset (version 2 only; version 1 puts the
 *                        directory at 128)
 *
 *   the directory runs from that offset to end of file and is a byte stream,
 *   not a table.  It holds `directory count` groups, each:
 *     len byte + name bytes    directory name
 *     1 byte                   media / CD number, ignored
 *     4 bytes  u32 LE          file count in this directory
 *   then, per file:
 *     len byte + name bytes    file name; in version 2 every byte is XOR 0x44
 *     4 bytes  u32 LE          absolute payload offset
 *     4 bytes  u32 LE          uncompressed size
 *     4 bytes  u32 LE          compressed size, 0 when the member is stored
 *     4 (v1) / 12 (v2) bytes   timestamps and flags, ignored
 *
 * A non-zero compressed size means the payload is an RFC 1950 zlib stream;
 * zero means it is stored and the uncompressed size is also the stored size.
 *
 * Recognition is the 8-byte magic plus a version gate, and then the structural
 * requirement that the payload extents and the directory together account for
 * the file exactly -- the furthest payload end and the directory end must meet
 * end-of-file.  Every declared extent is bounded against the real file size
 * before it is recorded, and the directory blob itself is capped at 256 MB so
 * a bogus directory offset cannot ask for an unbounded read.
 *
 * All 7 corpus samples in F:\ARC\ARC\WINTERMUTE DCP parse (213 MB largest).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wintermutedcp/xx_wintermutedcp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#ifdef WINTERMUTEDCP
#define XX_WINTERMUTEDCP_FILE_TYPE XX_FILE_TYPE_WINTERMUTEDCP
#else
#define XX_WINTERMUTEDCP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_WINTERMUTEDCP_METHOD_STORE 0U
#define XX_WINTERMUTEDCP_METHOD_ZLIB 8U
#define XX_WINTERMUTEDCP_HEADER_SIZE 128
#define XX_WINTERMUTEDCP_VERSION_1 0x100U
#define XX_WINTERMUTEDCP_VERSION_2 0x200U
#define XX_WINTERMUTEDCP_NAME_XOR 0x44U
#define XX_WINTERMUTEDCP_MAX_RECORDS 1000000U
/* The reference implementation's ceiling on the trailing directory blob. */
#define XX_WINTERMUTEDCP_MAX_DIRECTORY (256 * 1024 * 1024)
/* A compressed member's declared plain size is not bounded by the file, so it
 * gets its own ceiling rather than none at all. */
#define XX_WINTERMUTEDCP_MAX_PLAIN (1024ULL * 1024ULL * 1024ULL)

typedef struct xx_wintermutedcp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_wintermutedcp_member;

typedef struct xx_wintermutedcp_stream_s {
    xx_wintermutedcp_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_wintermutedcp_stream;

static void xx_wintermutedcp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_wintermutedcp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_wintermutedcp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_wintermutedcp_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_wintermutedcp_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_wintermutedcp_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_wintermutedcp_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_wintermutedcp_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_wintermutedcp_stream_free(void *pointer) {
    xx_wintermutedcp_stream *stream = (xx_wintermutedcp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_wintermutedcp_add(xx_wintermutedcp_stream *stream,
                          const xx_wintermutedcp_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_wintermutedcp_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_wintermutedcp_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* One length-prefixed byte string out of the directory blob.  Returns the
 * span; every read is bounded against the blob it came from. */
static bool xx_wintermutedcp_take_string(const uint8_t *blob, size_t blob_size,
                                         size_t *cursor, const uint8_t **out,
                                         size_t *out_size) {
    size_t size;

    if (!blob || !cursor || *cursor >= blob_size) return false;
    size = blob[*cursor];
    ++*cursor;
    if (size > blob_size - *cursor) return false;
    *out = blob + *cursor;
    *cursor += size;
    /* Names are stored NUL padded; the padding is not part of the name. */
    while (size != 0U && (*out)[size - 1U] == 0x00U) --size;
    *out_size = size;
    return size != 0U;
}

/* A version 2 package obfuscates every file-name byte with 0x44.  The name is
 * decoded into a scratch buffer and then normalised like any other. */
static char *xx_wintermutedcp_decode_name(const uint8_t *raw, size_t size,
                                          bool obfuscated) {
    uint8_t *plain;
    char *name;
    size_t index;

    if (!obfuscated) return xx_wintermutedcp_make_name(raw, size, true);
    plain = (uint8_t *)xx_mem_alloc(size != 0U ? size : 1U);
    if (!plain) return NULL;
    for (index = 0U; index < size; ++index) {
        plain[index] = (uint8_t)(raw[index] ^ XX_WINTERMUTEDCP_NAME_XOR);
    }
    while (size != 0U && plain[size - 1U] == 0x00U) --size;
    name = size != 0U ? xx_wintermutedcp_make_name(plain, size, true) : NULL;
    xx_mem_free(plain);
    return name;
}

/* Join a directory name and a file name into one archive path. */
static char *xx_wintermutedcp_join(const char *directory, const char *file) {
    if (!file) return NULL;
    if (!directory || !directory[0] || (directory[0] == '_' && !directory[1])) {
        return xx_str_dup(file);
    }
    return xx_str_concat3(directory, "/", file);
}


/* --------------------------------------------------------------- parse -- */

static xx_wintermutedcp_stream *xx_wintermutedcp_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_wintermutedcp_stream *stream = NULL;
    uint8_t head[XX_WINTERMUTEDCP_HEADER_SIZE + 4];
    uint8_t *blob = NULL;
    char *directory_name = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t archive_end;
    uint32_t version;
    uint32_t directory_count;
    uint32_t directory_index;
    uint64_t seen = 0U;
    size_t cursor = 0U;
    size_t record_size;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)sizeof(head)) return NULL;
    if (!xx_wintermutedcp_read_at(self, self->base_address, head,
                                  sizeof(head))) {
        return NULL;
    }
    if (xx_rt_memcmp(head, "\xde\xad\xc0\xde" "JUNK", 8U) != 0) return NULL;

    version = xx_wintermutedcp_le32(head + 8);
    directory_count = xx_wintermutedcp_le32(head + 124);
    if ((version != XX_WINTERMUTEDCP_VERSION_1 &&
         version != XX_WINTERMUTEDCP_VERSION_2) ||
        directory_count == 0U ||
        directory_count > XX_WINTERMUTEDCP_MAX_RECORDS) {
        return NULL;
    }
    record_size = version == XX_WINTERMUTEDCP_VERSION_2 ? 24U : 16U;

    directory_offset = XX_WINTERMUTEDCP_HEADER_SIZE;
    if (version == XX_WINTERMUTEDCP_VERSION_2) {
        directory_offset = (int64_t)xx_wintermutedcp_le32(head + 128);
    }
    if (directory_offset < XX_WINTERMUTEDCP_HEADER_SIZE ||
        directory_offset >= span) {
        return NULL;
    }
    /* The directory blob is read whole, so it is capped before allocation. */
    directory_size = span - directory_offset;
    if (directory_size > XX_WINTERMUTEDCP_MAX_DIRECTORY) return NULL;
    blob = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!blob) return NULL;
    if (!xx_wintermutedcp_read_at(self, self->base_address + directory_offset,
                                  blob, (size_t)directory_size)) {
        goto fail;
    }

    stream = (xx_wintermutedcp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    archive_end = directory_offset;

    for (directory_index = 0U; directory_index < directory_count;
         ++directory_index) {
        const uint8_t *raw;
        size_t raw_size;
        uint32_t file_count;
        uint32_t file_index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_wintermutedcp_take_string(blob, (size_t)directory_size,
                                          &cursor, &raw, &raw_size)) {
            goto fail;
        }
        /* One media byte plus the little-endian file count. */
        if ((size_t)directory_size - cursor < 5U) goto fail;
        file_count = xx_wintermutedcp_le32(blob + cursor + 1U);
        cursor += 5U;
        if (file_count > XX_WINTERMUTEDCP_MAX_RECORDS ||
            seen + file_count > XX_WINTERMUTEDCP_MAX_RECORDS) {
            goto fail;
        }
        seen += file_count;

        xx_str_free(directory_name);
        directory_name = xx_wintermutedcp_make_name(raw, raw_size, true);
        if (!directory_name) goto fail;

        for (file_index = 0U; file_index < file_count; ++file_index) {
            const uint8_t *record;
            char *file_name;
            xx_wintermutedcp_member member;
            int64_t data_offset;
            uint64_t plain_size;
            uint64_t packed_size;
            uint64_t stored_size;

            if (pd && xx_pd_is_stopped(pd)) goto fail;
            if (!xx_wintermutedcp_take_string(blob, (size_t)directory_size,
                                              &cursor, &raw, &raw_size)) {
                goto fail;
            }
            if ((size_t)directory_size - cursor < record_size) goto fail;
            record = blob + cursor;
            data_offset = (int64_t)xx_wintermutedcp_le32(record);
            plain_size = (uint64_t)xx_wintermutedcp_le32(record + 4);
            packed_size = (uint64_t)xx_wintermutedcp_le32(record + 8);
            stored_size = packed_size != 0U ? packed_size : plain_size;

            xx_mem_zero(&member, sizeof(member));
            member.header_offset =
                self->base_address + directory_offset + (int64_t)cursor;
            member.header_size = (int64_t)record_size;
            cursor += record_size;

            /* Bound the payload against the real file before recording it,
             * and give the compressed plain size its own ceiling because the
             * file size does not bound it. */
            if (stored_size > (uint64_t)INT64_MAX ||
                data_offset < 0 || data_offset > span ||
                (int64_t)stored_size > span - data_offset ||
                plain_size > XX_WINTERMUTEDCP_MAX_PLAIN) {
                goto fail;
            }

            file_name = xx_wintermutedcp_decode_name(
                raw, raw_size, version == XX_WINTERMUTEDCP_VERSION_2);
            if (!file_name) goto fail;
            member.name = xx_wintermutedcp_join(directory_name, file_name);
            xx_str_free(file_name);
            if (!member.name) goto fail;

            member.data_offset = self->base_address + data_offset;
            member.packed_size = (int64_t)stored_size;
            member.unpacked_size = plain_size;
            member.method = packed_size != 0U ? XX_WINTERMUTEDCP_METHOD_ZLIB
                                              : XX_WINTERMUTEDCP_METHOD_STORE;
            if (!xx_wintermutedcp_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            if (data_offset + (int64_t)stored_size > archive_end) {
                archive_end = data_offset + (int64_t)stored_size;
            }
        }
    }

    if (directory_offset + (int64_t)cursor > archive_end) {
        archive_end = directory_offset + (int64_t)cursor;
    }
    /* The payloads and the directory together must account for the file
     * exactly; that exactness is what rules out a spliced or truncated
     * package that still carries the right magic. */
    if (stream->count == 0U || archive_end != span) goto fail;

    xx_str_free(directory_name);
    xx_mem_free(blob);
    stream->archive_size = span;
    return stream;

fail:
    xx_str_free(directory_name);
    if (blob) xx_mem_free(blob);
    xx_wintermutedcp_stream_free(stream);
    return NULL;
}

/* Stored members are a bounded read; compressed ones are a zlib stream whose
 * plain length the directory records, so a short or long decode is an error
 * rather than something to guess around. */
static bool xx_wintermutedcp_decode(Abstractformat *self,
                                    const xx_wintermutedcp_member *member,
                                    uint8_t **out, size_t *out_size,
                                    xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->unpacked_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (member->method == XX_WINTERMUTEDCP_METHOD_STORE) {
        if ((uint64_t)member->packed_size != member->unpacked_size) {
            return false;
        }
        if (member->packed_size == 0) return true;
        plain = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
        if (!plain) return false;
        if (!xx_wintermutedcp_read_at(self, member->data_offset, plain,
                                      (size_t)member->packed_size)) {
            xx_mem_free(plain);
            return false;
        }
        *out = plain;
        *out_size = (size_t)member->packed_size;
        return true;
    }
    if (member->packed_size == 0) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    plain = (uint8_t *)xx_mem_alloc(member->unpacked_size != 0U
                                        ? (size_t)member->unpacked_size
                                        : 1U);
    if (!packed || !plain ||
        !xx_wintermutedcp_read_at(self, member->data_offset, packed,
                                  (size_t)member->packed_size) ||
        !xx_zlib_stream_decode_memory(packed, (size_t)member->packed_size,
                                      plain, (size_t)member->unpacked_size,
                                      &written) ||
        written != member->unpacked_size) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = written;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_wintermutedcp_init(xx_wintermutedcp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WINTERMUTEDCP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-wintermute-dcp");
    xx_format_set_extension(&archive->format, "dcp");
    archive->format.check_is_valid = xx_wintermutedcp_check_is_valid;
    archive->format.handle_base_info = xx_wintermutedcp_handle_base_info;
    archive->format.get_format_size = xx_wintermutedcp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wintermutedcp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wintermutedcp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wintermutedcp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wintermutedcp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wintermutedcp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wintermutedcp_free_archive_records_reading;
    archive->format.destroy = xx_wintermutedcp_vtable_destroy;
}

xx_wintermutedcp *xx_wintermutedcp_create(xx_io_device *device, int64_t base_address) {
    xx_wintermutedcp *archive = (xx_wintermutedcp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_wintermutedcp_init(archive, device, base_address);
    return archive;
}

void xx_wintermutedcp_destroy(xx_wintermutedcp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_wintermutedcp_free(xx_wintermutedcp *archive) {
    if (!archive) return;
    xx_wintermutedcp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_wintermutedcp_vtable_destroy(Abstractformat *self) {
    xx_wintermutedcp_destroy((xx_wintermutedcp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_wintermutedcp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wintermutedcp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_wintermutedcp_parse(self, pd);
    if (!stream) return false;
    xx_wintermutedcp_stream_free(stream);
    return true;
}

bool xx_wintermutedcp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wintermutedcp *archive = (xx_wintermutedcp *)self;
    xx_wintermutedcp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_wintermutedcp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_wintermutedcp_stream_free(stream);
    return true;
}

int64_t xx_wintermutedcp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_wintermutedcp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_wintermutedcp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_wintermutedcp_set_record(xx_archive_record *record,
                                 const xx_wintermutedcp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_wintermutedcp_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_wintermutedcp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_wintermutedcp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_wintermutedcp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_wintermutedcp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_wintermutedcp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_wintermutedcp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_wintermutedcp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_wintermutedcp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_wintermutedcp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wintermutedcp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_wintermutedcp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_wintermutedcp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_wintermutedcp_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_wintermutedcp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_wintermutedcp_stream *stream;
    const xx_wintermutedcp_member *member;
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
    stream = (xx_wintermutedcp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_wintermutedcp_path_safe(member->name)) return false;

    path_option =
        xx_wintermutedcp_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_wintermutedcp_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
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
        !xx_wintermutedcp_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_wintermutedcp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

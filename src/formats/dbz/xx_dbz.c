/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compressed UNIX man-page databases (".dbz", also shipped as "MAN.1").
 * XArchive has no module for this one.  The layout was recovered from U3's
 * DBZ handler -- class umb, VMT 006d9458, recognition predicate
 * decompiled/functions/006d/006d97e0.c -> 006d94d0, open/list
 * 006d9800 -> 006d9510 -- and then confirmed against the corpus and against
 * U3's own listing, member for member.
 *
 *   banner, 27 bytes at offset 0:
 *     "!<man database compressed>\n"
 *
 *   index: fixed-width ASCII records of 32 bytes, starting at 27:
 *     +0x00 14  char[14] page name, blank padded
 *     +0x0e  9  char[9]  absolute payload offset, right aligned decimal
 *     +0x17  9  char[9]  payload size, right aligned decimal
 *
 *   The index has no count and no terminator: its length is implied by the
 *   FIRST record's offset, which is where the payload area begins.  So
 *   (offset0 - 27) must be a positive multiple of 32, and that quotient is
 *   the record count.  Trailing records that are all NUL are free slots and
 *   are skipped, which is exactly what U3 does.
 *
 *   Each payload is a self-contained compressed stream: gzip (1f 8b) in the
 *   newer databases, Unix compress (1f 9d) in the older ones.  Two records
 *   may point at the same payload -- that is how aliases such as egrep/fgrep
 *   are stored -- so payloads are NOT required to be contiguous or ordered.
 *
 * Recognition is the 27-byte banner plus the index arithmetic above plus the
 * requirement that the first payload actually opens on one of the two stream
 * signatures.  Every record's extent is bounded against the real file size
 * before it is recorded.
 *
 * TRUNCATION.  Two of the six corpus samples (the two MAN.1 files) are
 * truncated copies: their index is intact and describes 186 pages, but the
 * file stops after page 122.  Rather than reject the whole database or read
 * past the end, this reader lists the records that are wholly inside the
 * file and drops the rest.  U3 lists all 186 and then fails on the missing
 * payloads; listing only what is actually present is the safer answer.
 *
 * A gzip member's plain size is taken from its own ISIZE trailer at parse
 * time, and both ISIZE and the CRC32 trailer are verified on decode.  Unix
 * compress records no plain size anywhere, so that is reported as unknown and
 * the decode is sized by retry; the .Z decoder validates its own code groups,
 * so a successful decode is still a decode of the whole stream.
 *
 * All 6 corpus samples in F:\ARC\ARC\DBZ parse: 286, 251, 271 and 253
 * members for the four complete databases, matching U3 member for member,
 * and 122 each for the two truncated ones (of the 186 their index names).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dbz/xx_dbz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#ifdef DBZ
#define XX_DBZ_FILE_TYPE XX_FILE_TYPE_DBZ
#else
#define XX_DBZ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_DBZ_METHOD_GZIP 8U
#define XX_DBZ_METHOD_COMPRESS 9U
#define XX_DBZ_BANNER_SIZE 27
#define XX_DBZ_ENTRY_SIZE 32
#define XX_DBZ_NAME_SIZE 14
#define XX_DBZ_NUMBER_SIZE 9
#define XX_DBZ_MAX_MEMBERS 1000000U
/* Unix compress stores no plain size; the decode buffer starts here and
 * doubles until the stream fits or the ceiling is reached. */
#define XX_DBZ_Z_FIRST_GUESS 65536U
#define XX_DBZ_Z_MAX_PLAIN (64U * 1024U * 1024U)

typedef struct xx_dbz_member_s {
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
} xx_dbz_member;

typedef struct xx_dbz_stream_s {
    xx_dbz_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_dbz_stream;

static void xx_dbz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_dbz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_dbz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_dbz_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_dbz_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_dbz_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_dbz_path_safe(const char *path) {
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
static char *xx_dbz_make_name(const uint8_t *raw, size_t size,
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

static void xx_dbz_stream_free(void *pointer) {
    xx_dbz_stream *stream = (xx_dbz_stream *)pointer;
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
static bool xx_dbz_add(xx_dbz_stream *stream,
                          const xx_dbz_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_dbz_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_dbz_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* One right-aligned fixed-width decimal field out of the index.  Leading
 * blanks are skipped; at least one digit must follow and nothing else may. */
static bool xx_dbz_field_number(const uint8_t *field, size_t size,
                                int64_t *value) {
    size_t index = 0U;
    int64_t result = 0;
    bool seen = false;

    while (index < size && field[index] == ' ') ++index;
    for (; index < size; ++index) {
        if (field[index] < '0' || field[index] > '9') return false;
        if (result > (INT64_MAX - 9) / 10) return false;
        result = result * 10 + (field[index] - '0');
        seen = true;
    }
    if (!seen) return false;
    *value = result;
    return true;
}

/* A record of nothing but NUL bytes is a free slot, not a member. */
static bool xx_dbz_entry_is_free(const uint8_t *entry) {
    size_t index;

    for (index = 0U; index < XX_DBZ_ENTRY_SIZE; ++index) {
        if (entry[index] != 0x00U) return false;
    }
    return true;
}

/* Trim the blank padding off a fixed-width name field. */
static size_t xx_dbz_name_length(const uint8_t *field) {
    size_t length = XX_DBZ_NAME_SIZE;

    while (length != 0U && (field[length - 1U] == ' ' ||
                            field[length - 1U] == 0x00U)) {
        --length;
    }
    return length;
}

/* Which of the two stream signatures a payload carries, or 0 for neither. */
static uint32_t xx_dbz_sniff(Abstractformat *self, int64_t offset,
                             int64_t size) {
    uint8_t head[3];

    if (size < 3 || !xx_dbz_read_at(self, offset, head, sizeof(head))) {
        return 0U;
    }
    if (head[0] == 0x1fU && head[1] == 0x8bU && head[2] == 0x08U) {
        return XX_DBZ_METHOD_GZIP;
    }
    if (xx_compress_has_header(head, sizeof(head))) {
        return XX_DBZ_METHOD_COMPRESS;
    }
    return 0U;
}

/* Where the deflate stream inside a gzip member begins, i.e. past the
 * variable-length header the flag byte describes. */
static bool xx_dbz_gzip_payload(const uint8_t *data, size_t size,
                                size_t *start) {
    size_t at = 10U;
    uint8_t flags;

    if (size < 18U || data[0] != 0x1fU || data[1] != 0x8bU ||
        data[2] != 0x08U) {
        return false;
    }
    flags = data[3];
    /* The reserved flag bits must be clear or this is not a gzip member. */
    if ((flags & 0xe0U) != 0U) return false;
    if ((flags & 0x04U) != 0U) { /* FEXTRA */
        size_t extra;
        if (size - at < 2U) return false;
        extra = (size_t)data[at] | ((size_t)data[at + 1U] << 8);
        at += 2U;
        if (extra > size - at) return false;
        at += extra;
    }
    if ((flags & 0x08U) != 0U) { /* FNAME */
        while (at < size && data[at] != 0x00U) ++at;
        if (at >= size) return false;
        ++at;
    }
    if ((flags & 0x10U) != 0U) { /* FCOMMENT */
        while (at < size && data[at] != 0x00U) ++at;
        if (at >= size) return false;
        ++at;
    }
    if ((flags & 0x02U) != 0U) { /* FHCRC */
        if (size - at < 2U) return false;
        at += 2U;
    }
    /* The 8-byte CRC32 + ISIZE trailer must still fit behind the stream. */
    if (size - at < 8U) return false;
    *start = at;
    return true;
}


/* --------------------------------------------------------------- parse -- */

static xx_dbz_stream *xx_dbz_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_dbz_stream *stream = NULL;
    uint8_t banner[XX_DBZ_BANNER_SIZE + XX_DBZ_ENTRY_SIZE];
    uint8_t *table = NULL;
    int64_t total;
    int64_t span;
    int64_t first_offset;
    int64_t index_size;
    uint32_t count;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)sizeof(banner)) return NULL;
    if (!xx_dbz_read_at(self, self->base_address, banner, sizeof(banner))) {
        return NULL;
    }
    if (xx_rt_memcmp(banner, "!<man database compressed>\n",
                     XX_DBZ_BANNER_SIZE) != 0) {
        return NULL;
    }

    /* The index carries no count: its length is implied by the first
     * record's offset, which is where the payload area starts. */
    if (!xx_dbz_field_number(banner + XX_DBZ_BANNER_SIZE + XX_DBZ_NAME_SIZE,
                             XX_DBZ_NUMBER_SIZE, &first_offset)) {
        return NULL;
    }
    if (first_offset <= XX_DBZ_BANNER_SIZE || first_offset > span) return NULL;
    index_size = first_offset - XX_DBZ_BANNER_SIZE;
    if (index_size % XX_DBZ_ENTRY_SIZE != 0) return NULL;
    count = (uint32_t)(index_size / XX_DBZ_ENTRY_SIZE);
    if (count == 0U || count > XX_DBZ_MAX_MEMBERS) return NULL;
    /* The first payload must open on one of the two stream signatures; the
     * banner alone would otherwise accept a file with a plausible index. */
    if (xx_dbz_sniff(self, self->base_address + first_offset,
                     span - first_offset) == 0U) {
        return NULL;
    }

    table = (uint8_t *)xx_mem_alloc((size_t)index_size);
    if (!table) return NULL;
    if (!xx_dbz_read_at(self, self->base_address + XX_DBZ_BANNER_SIZE, table,
                        (size_t)index_size)) {
        goto fail;
    }

    stream = (xx_dbz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = table + (size_t)index * XX_DBZ_ENTRY_SIZE;
        xx_dbz_member member;
        size_t name_length;
        int64_t offset;
        int64_t size;
        uint32_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Trailing all-NUL records are free slots, exactly as U3 treats
         * them. */
        if (xx_dbz_entry_is_free(entry)) continue;
        name_length = xx_dbz_name_length(entry);
        if (name_length == 0U) goto fail;
        if (!xx_dbz_field_number(entry + XX_DBZ_NAME_SIZE, XX_DBZ_NUMBER_SIZE,
                                 &offset) ||
            !xx_dbz_field_number(entry + XX_DBZ_NAME_SIZE + XX_DBZ_NUMBER_SIZE,
                                 XX_DBZ_NUMBER_SIZE, &size)) {
            goto fail;
        }
        if (offset < first_offset || size <= 0) goto fail;
        /* A truncated database keeps its full index; list only the payloads
         * that are really there rather than reading past the end. */
        if (offset > span || size > span - offset) continue;
        method = xx_dbz_sniff(self, self->base_address + offset, size);
        if (method == 0U) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_dbz_make_name(entry, name_length, false);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + XX_DBZ_BANNER_SIZE +
                               (int64_t)index * XX_DBZ_ENTRY_SIZE;
        member.header_size = XX_DBZ_ENTRY_SIZE;
        member.data_offset = self->base_address + offset;
        member.packed_size = size;
        member.method = method;
        /* gzip republishes its plain size in the ISIZE trailer; Unix
         * compress records one nowhere, so it stays unknown. */
        if (method == XX_DBZ_METHOD_GZIP && size >= 8) {
            uint8_t trailer[4];
            if (xx_dbz_read_at(self, member.data_offset + size - 4, trailer,
                               sizeof(trailer))) {
                member.unpacked_size = (uint64_t)xx_dbz_le32(trailer);
            }
        }
        if (!xx_dbz_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0U) goto fail;

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_dbz_stream_free(stream);
    return NULL;
}

/* A gzip member is checked against both of its own trailers; a Unix compress
 * member has no recorded length, so the output buffer is grown by retry until
 * the stream fits.  The .Z decoder validates its own code groups and consumes
 * the range exactly, so a successful decode is a decode of the whole stream
 * rather than a plausible prefix. */
static bool xx_dbz_decode_gzip(const uint8_t *packed, size_t packed_size,
                               uint8_t **out, size_t *out_size) {
    uint8_t *plain;
    size_t start = 0U;
    size_t written = 0U;
    size_t declared;
    uint32_t crc;

    if (!xx_dbz_gzip_payload(packed, packed_size, &start)) return false;
    declared = (size_t)xx_dbz_le32(packed + packed_size - 4U);
    crc = xx_dbz_le32(packed + packed_size - 8U);
    plain = (uint8_t *)xx_mem_alloc(declared != 0U ? declared : 1U);
    if (!plain) return false;
    if (!xx_deflate_decompress_memory(packed + start,
                                      packed_size - start - 8U, plain,
                                      declared, &written, false) ||
        written != declared || xx_crc32_calc(0U, plain, written) != crc) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

static bool xx_dbz_decode_z(Abstractformat *self,
                            const xx_dbz_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    size_t capacity = XX_DBZ_Z_FIRST_GUESS;

    while (capacity <= XX_DBZ_Z_MAX_PLAIN) {
        uint8_t *plain = (uint8_t *)xx_mem_alloc(capacity);
        xx_io_device *sink;
        int64_t produced = 0;
        bool ok;

        if (!plain) return false;
        sink = xx_io_mem_open(plain, capacity);
        if (!sink) {
            xx_mem_free(plain);
            return false;
        }
        ok = xx_compress_decode_device(self->device, member->data_offset,
                                       member->packed_size, sink, &produced,
                                       pd);
        xx_io_close(sink);
        if (ok && produced >= 0 && (uint64_t)produced <= capacity) {
            *out = plain;
            *out_size = (size_t)produced;
            return true;
        }
        xx_mem_free(plain);
        if (pd && xx_pd_is_stopped(pd)) return false;
        capacity *= 2U;
    }
    return false;
}

static bool xx_dbz_decode(Abstractformat *self, const xx_dbz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    bool result;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size <= 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method == XX_DBZ_METHOD_COMPRESS) {
        return xx_dbz_decode_z(self, member, out, out_size, pd);
    }
    if (member->method != XX_DBZ_METHOD_GZIP) return false;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return false;
    if (!xx_dbz_read_at(self, member->data_offset, packed,
                        (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    result = xx_dbz_decode_gzip(packed, (size_t)member->packed_size, out,
                                out_size);
    xx_mem_free(packed);
    return result;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_dbz_init(xx_dbz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DBZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-man-database");
    xx_format_set_extension(&archive->format, "dbz");
    archive->format.check_is_valid = xx_dbz_check_is_valid;
    archive->format.handle_base_info = xx_dbz_handle_base_info;
    archive->format.get_format_size = xx_dbz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dbz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dbz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dbz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dbz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dbz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dbz_free_archive_records_reading;
    archive->format.destroy = xx_dbz_vtable_destroy;
}

xx_dbz *xx_dbz_create(xx_io_device *device, int64_t base_address) {
    xx_dbz *archive = (xx_dbz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_dbz_init(archive, device, base_address);
    return archive;
}

void xx_dbz_destroy(xx_dbz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dbz_free(xx_dbz *archive) {
    if (!archive) return;
    xx_dbz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dbz_vtable_destroy(Abstractformat *self) {
    xx_dbz_destroy((xx_dbz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_dbz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dbz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_dbz_parse(self, pd);
    if (!stream) return false;
    xx_dbz_stream_free(stream);
    return true;
}

bool xx_dbz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dbz *archive = (xx_dbz *)self;
    xx_dbz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_dbz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_dbz_stream_free(stream);
    return true;
}

int64_t xx_dbz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_dbz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_dbz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dbz_set_record(xx_archive_record *record,
                                 const xx_dbz_member *member) {
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

static bool xx_dbz_copy_options(xx_list_s *target,
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

static const xx_var *xx_dbz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_dbz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_dbz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_dbz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_dbz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_dbz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_dbz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_dbz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dbz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dbz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_dbz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dbz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_dbz_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dbz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_dbz_stream *stream;
    const xx_dbz_member *member;
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
    stream = (xx_dbz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_dbz_path_safe(member->name)) return false;

    path_option =
        xx_dbz_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_dbz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_dbz_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_dbz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

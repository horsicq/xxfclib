/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compact Pro (.cpt) archives. All integers are BIG-endian.
 *
 *   fixed header, 8 bytes at offset 0:
 *     0x00  u8       1, the format's only version byte
 *     0x01  3 bytes  unconstrained
 *     0x04  u32 BE   catalogue offset
 *
 *   catalogue, at that offset:
 *     +0x00  u32 BE  CRC32 of everything from +0x04 to the catalogue's end
 *     +0x04  u16 BE  number of records directly in the root
 *     +0x06  u8      archive comment length, then that many comment bytes
 *     then the records.
 *
 *   record: a length byte whose low seven bits are the name length and whose
 *   top bit distinguishes a directory from a file, then the name.
 *
 *     directory: u16 BE child count, then that many records nested inside.
 *       The directory itself counts as one record of its parent's total.
 *     file: 45 further bytes -
 *       +0x01  u32 BE  offset of the member's data
 *       +0x17  u32 BE  stored CRC32, complemented
 *       +0x1b  u16 BE  flags: bit 0 encrypted, bit 1 resource fork is LZH,
 *                      bit 2 data fork is LZH
 *       +0x1d  u32 BE  resource fork plaintext size
 *       +0x21  u32 BE  data fork plaintext size
 *       +0x25  u32 BE  resource fork packed size
 *       +0x29  u32 BE  data fork packed size
 *
 *   The resource fork's bytes sit at the record's offset and the data fork's
 *   immediately after them. A fork is listed only when it has content, except
 *   that a file with no resource fork always lists its data fork even when
 *   that fork is empty - so every file yields at least one member. Resource
 *   forks are listed under the file's path with ".rsrc" appended.
 *
 * Because the catalogue sits at an offset the header points at, and the
 * header itself is a single byte 1, the format's real recognition is the
 * catalogue CRC: the whole record walk must land exactly on the byte range
 * whose CRC32 matches the stored word.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/compactpro/xx_compactpro.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/compactpro/xx_compactpro.h"

#include <stdio.h>

#define XX_COMPACTPRO_COPY_CHUNK (64 * 1024)

typedef struct xx_compactpro_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_compactpro_member;

typedef struct xx_compactpro_stream_s {
    xx_compactpro_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_compactpro_stream;

static void xx_compactpro_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_compactpro_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_compactpro_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_compactpro_path_safe(const char *name) {
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

static void xx_compactpro_stream_free(void *pointer) {
    xx_compactpro_stream *stream = (xx_compactpro_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_compactpro_add(xx_compactpro_stream *stream,
                          const xx_compactpro_member *member) {
    xx_compactpro_member *grown = (xx_compactpro_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_COMPACTPRO_HEADER_SIZE 8
#define XX_COMPACTPRO_VERSION 1U
#define XX_COMPACTPRO_MIN_SIZE 15
#define XX_COMPACTPRO_FILE_RECORD_SIZE 45
#define XX_COMPACTPRO_MAX_DEPTH 128
#define XX_COMPACTPRO_MAX_NAME 0x7f
#define XX_COMPACTPRO_MAX_CATALOG ((int64_t)0x4000000)
#define XX_COMPACTPRO_MAX_PATH 0x1000
#define XX_COMPACTPRO_FLAG_ENCRYPTED 0x0001U
#define XX_COMPACTPRO_FLAG_RSRC_LZH 0x0002U
#define XX_COMPACTPRO_FLAG_DATA_LZH 0x0004U
#define XX_COMPACTPRO_METHOD_STORED 0U
#define XX_COMPACTPRO_METHOD_RLE 1U
#define XX_COMPACTPRO_METHOD_LZH 2U
#define XX_COMPACTPRO_MAX_MEMBERS 100000
#define XX_COMPACTPRO_MAX_DECODED ((int64_t)0x10000000)

typedef struct xx_compactpro_ctx_s {
    Abstractformat *self;
    xx_pd_struct *pd;
    const uint8_t *catalog;   /* the catalogue, read whole */
    int64_t catalog_size;     /* its length */
    int64_t catalog_origin;   /* its offset within the span */
    int64_t span;             /* total - base_address */
    int64_t position;         /* cursor inside the catalogue */
    int32_t parsed;           /* records consumed, for the root-count check */
    xx_compactpro_stream *stream;
} xx_compactpro_ctx;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_compactpro_be16(const uint8_t *data);
static uint32_t xx_compactpro_be32(const uint8_t *data);
static uint32_t xx_compactpro_crc32(const uint8_t *data, int64_t size);
static char *xx_compactpro_join(const char *parent, const uint8_t *component, uint32_t length);
static bool xx_compactpro_append_fork(xx_compactpro_ctx *ctx, const char *path, int64_t header_offset, int64_t header_size, bool resource, int64_t offset, int64_t packed, int64_t raw, bool lzh);
static bool xx_compactpro_walk(xx_compactpro_ctx *ctx, const char *parent, int32_t records, int32_t depth);
static xx_compactpro_stream *xx_compactpro_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_compactpro_decode(Abstractformat *self, const xx_compactpro_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The catalogue lives at the tail of the file and is read whole so the CRC
 * can be computed over it. A ceiling keeps a bogus offset from asking for a
 * half-gigabyte buffer. */
/* Flag bits in the file record's 16-bit flags word. */



static uint16_t xx_compactpro_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t xx_compactpro_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* The plain reflected CRC32 the catalogue is protected by, computed bit by
 * bit so no table has to be carried. */
static uint32_t xx_compactpro_crc32(const uint8_t *data, int64_t size) {
    uint32_t crc = 0xffffffffU;
    int64_t index;
    int32_t bit;

    for (index = 0; index < size; ++index) {
        crc ^= (uint32_t)data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
        }
    }
    return crc;
}

/* Join a parent path and one stored name component. Returns NULL when the
 * component is not usable as a path element. */
static char *xx_compactpro_join(const char *parent, const uint8_t *component,
                                uint32_t length) {
    char *result;
    uint32_t start = 0U;
    uint32_t stop = length;
    uint32_t index;
    size_t parent_length = 0U;
    size_t total;

    for (index = 0U; index < length; ++index) {
        /* Mac OS Roman names use the whole high half of the byte range, so
         * bytes above 0x7e are ordinary here; control bytes are not, and a
         * record carrying one is malformed rather than oddly named. */
        if (component[index] < 0x20U) return NULL;
    }
    while (start < stop && component[start] == (uint8_t)' ') ++start;
    while (stop > start && component[stop - 1U] == (uint8_t)' ') --stop;
    if (stop <= start) return NULL;
    length = stop - start;
    /* "." and ".." would make the assembled path escape its directory. */
    if (length == 1U && component[start] == (uint8_t)'.') return NULL;
    if (length == 2U && component[start] == (uint8_t)'.' &&
        component[start + 1U] == (uint8_t)'.') {
        return NULL;
    }

    if (parent) parent_length = xx_rt_strlen(parent);
    total = parent_length + (parent_length ? 1U : 0U) + (size_t)length;
    if (total == 0U || total > (size_t)XX_COMPACTPRO_MAX_PATH) return NULL;

    result = (char *)xx_mem_alloc(total + 1U);
    if (!result) return NULL;
    if (parent_length) {
        xx_rt_memcpy(result, parent, parent_length);
        result[parent_length] = '/';
        ++parent_length;
    }
    xx_rt_memcpy(result + parent_length, component + start, (size_t)length);
    /* A stored name may itself contain a separator or a Mac colon; both are
     * folded so one component can never become two path elements. */
    for (index = 0U; index < length; ++index) {
        char value = result[parent_length + index];
        if (value == '/' || value == ':' || value == '\\') {
            result[parent_length + index] = '_';
        }
    }
    result[total] = '\0';
    return result;
}

static bool xx_compactpro_append_fork(xx_compactpro_ctx *ctx,
                                      const char *path, int64_t header_offset,
                                      int64_t header_size, bool resource,
                                      int64_t offset, int64_t packed,
                                      int64_t raw, bool lzh) {
    xx_compactpro_member member;
    char *name;
    size_t path_length;
    static const char suffix[6] = {'.', 'r', 's', 'r', 'c', '\0'};

    if (ctx->stream->count >= (size_t)XX_COMPACTPRO_MAX_MEMBERS) return false;

    path_length = xx_rt_strlen(path);
    name = (char *)xx_mem_alloc(path_length + (resource ? 5U : 0U) + 1U);
    if (!name) return false;
    xx_rt_memcpy(name, path, path_length);
    if (resource) {
        xx_rt_memcpy(name + path_length, suffix, 5U);
        path_length += 5U;
    }
    name[path_length] = '\0';

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = ctx->catalog_origin + header_offset;
    member.header_size = header_size;
    member.data_offset = ctx->self->base_address + offset;
    member.compressed_size = packed;
    member.uncompressed_size = raw;
    /* An empty fork carries no codec at all; the flag bit is meaningless
     * there, so it is reported as stored rather than as a zero-byte RLE
     * stream. */
    member.method = (raw == 0)
                        ? XX_COMPACTPRO_METHOD_STORED
                        : (lzh ? XX_COMPACTPRO_METHOD_LZH
                               : XX_COMPACTPRO_METHOD_RLE);
    /* The record's date fields are not laid out reliably enough across the
     * corpus to publish, so no timestamp is reported. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_compactpro_add(ctx->stream, &member)) {
        xx_str_free(name);
        return false;
    }
    return true;
}

static bool xx_compactpro_walk(xx_compactpro_ctx *ctx, const char *parent,
                               int32_t records, int32_t depth) {
    int32_t remaining = records;
    int32_t children;
    int64_t record_offset;
    int64_t header_size;
    int64_t file_offset;
    int64_t resource_raw;
    int64_t data_raw;
    int64_t resource_packed;
    int64_t data_packed;
    uint32_t name_field;
    uint32_t name_size;
    uint32_t flags;
    const uint8_t *meta;
    char *path;
    bool ok;

    if (records < 0 || depth > XX_COMPACTPRO_MAX_DEPTH) return false;
    if (ctx->parsed > XX_COMPACTPRO_MAX_MEMBERS - records) return false;

    while (remaining > 0) {
        if (ctx->pd && xx_pd_is_stopped(ctx->pd)) return false;
        if (!xx_compactpro_range_within(ctx->catalog_size, ctx->position, 1)) {
            return false;
        }
        record_offset = ctx->position;
        name_field = (uint32_t)ctx->catalog[ctx->position];
        ++ctx->position;
        name_size = name_field & (uint32_t)XX_COMPACTPRO_MAX_NAME;
        /* A record with no name is not a record. */
        if (name_size == 0U) return false;
        if (!xx_compactpro_range_within(ctx->catalog_size, ctx->position,
                                        (int64_t)name_size)) {
            return false;
        }
        path = xx_compactpro_join(parent, ctx->catalog + ctx->position,
                                  name_size);
        ctx->position += (int64_t)name_size;
        if (!path) return false;

        if (name_field & 0x80U) {
            if (!xx_compactpro_range_within(ctx->catalog_size, ctx->position,
                                            2)) {
                xx_str_free(path);
                return false;
            }
            children = (int32_t)xx_compactpro_be16(ctx->catalog +
                                                   ctx->position);
            ctx->position += 2;
            /* A directory's children are drawn from its parent's own record
             * budget, so a child count that does not fit inside what is left
             * means the catalogue's nesting is inconsistent - the single
             * cheapest way to catch a walk that has lost sync. */
            if (children <= 0 || children >= remaining) {
                xx_str_free(path);
                return false;
            }
            ++ctx->parsed;
            ok = xx_compactpro_walk(ctx, path, children, depth + 1);
            xx_str_free(path);
            if (!ok) return false;
            /* The directory record itself is the +1. */
            remaining -= children + 1;
            continue;
        }

        if (!xx_compactpro_range_within(
                ctx->catalog_size, ctx->position,
                (int64_t)XX_COMPACTPRO_FILE_RECORD_SIZE)) {
            xx_str_free(path);
            return false;
        }
        meta = ctx->catalog + ctx->position;
        file_offset = (int64_t)xx_compactpro_be32(meta + 1);
        flags = (uint32_t)xx_compactpro_be16(meta + 27);
        resource_raw = (int64_t)xx_compactpro_be32(meta + 29);
        data_raw = (int64_t)xx_compactpro_be32(meta + 33);
        resource_packed = (int64_t)xx_compactpro_be32(meta + 37);
        data_packed = (int64_t)xx_compactpro_be32(meta + 41);
        ctx->position += (int64_t)XX_COMPACTPRO_FILE_RECORD_SIZE;
        header_size = ctx->position - record_offset;
        ++ctx->parsed;
        --remaining;

        /* An encrypted member cannot be produced, and the reader has no way
         * to ask for a password, so the archive is refused outright rather
         * than listed with members that can never be extracted. */
        if (flags & XX_COMPACTPRO_FLAG_ENCRYPTED) {
            xx_str_free(path);
            return false;
        }
        /* Both forks must lie inside the file, and the data fork begins
         * exactly where the resource fork ends - there is no gap and no
         * second offset to fall back on. */
        if (!xx_compactpro_range_within(ctx->span, file_offset,
                                        resource_packed) ||
            !xx_compactpro_range_within(ctx->span,
                                        file_offset + resource_packed,
                                        data_packed)) {
            xx_str_free(path);
            return false;
        }

        if (resource_raw != 0) {
            if (!xx_compactpro_append_fork(ctx, path, record_offset,
                                           header_size, true, file_offset,
                                           resource_packed, resource_raw,
                                           (flags &
                                            XX_COMPACTPRO_FLAG_RSRC_LZH) !=
                                               0U)) {
                xx_str_free(path);
                return false;
            }
        }
        /* A file with no resource fork always yields its data fork, even an
         * empty one, so that every file in the catalogue appears somewhere in
         * the listing. */
        if (data_raw != 0 || resource_raw == 0) {
            if (!xx_compactpro_append_fork(
                    ctx, path, record_offset, header_size, false,
                    file_offset + resource_packed, data_packed, data_raw,
                    (flags & XX_COMPACTPRO_FLAG_DATA_LZH) != 0U)) {
                xx_str_free(path);
                return false;
            }
        }
        xx_str_free(path);
    }
    /* A directory whose children overshot its budget leaves this negative. */
    return remaining == 0;
}

static xx_compactpro_stream *xx_compactpro_parse(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    xx_compactpro_ctx ctx;
    xx_compactpro_stream *stream = NULL;
    uint8_t header[XX_COMPACTPRO_HEADER_SIZE];
    uint8_t *catalog = NULL;
    int64_t total;
    int64_t span;
    int64_t catalog_origin;
    int64_t catalog_size;
    uint32_t stored_crc;
    int32_t root_records;
    int64_t comment_size;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_COMPACTPRO_MIN_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_compactpro_read_at(self, self->base_address, header,
                               sizeof(header))) {
        return NULL;
    }
    /* The whole fixed header is one byte with the value 1. That is far too
     * weak to recognise a format on, which is why nothing is decided here:
     * the catalogue CRC at the bottom of this function is the real test. */
    if (header[0] != (uint8_t)XX_COMPACTPRO_VERSION) return NULL;

    catalog_origin = (int64_t)xx_compactpro_be32(header + 4);
    /* Four CRC bytes, the root count and the comment length must all fit. */
    if (!xx_compactpro_range_within(span, catalog_origin, 7)) return NULL;
    catalog_size = span - catalog_origin;
    if (catalog_size > XX_COMPACTPRO_MAX_CATALOG) return NULL;

    catalog = (uint8_t *)xx_mem_alloc((size_t)catalog_size);
    if (!catalog) return NULL;
    if (!xx_compactpro_read_at(self, self->base_address + catalog_origin,
                               catalog, (size_t)catalog_size)) {
        goto fail;
    }

    stored_crc = xx_compactpro_be32(catalog);
    root_records = (int32_t)xx_compactpro_be16(catalog + 4);
    comment_size = (int64_t)catalog[6];
    if (root_records <= 0 || root_records > XX_COMPACTPRO_MAX_MEMBERS) {
        goto fail;
    }
    if (!xx_compactpro_range_within(catalog_size, 7, comment_size)) goto fail;

    stream = (xx_compactpro_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    ctx.self = self;
    ctx.pd = pd;
    ctx.catalog = catalog;
    ctx.catalog_size = catalog_size;
    ctx.catalog_origin = self->base_address + catalog_origin;
    ctx.span = span;
    ctx.position = 7 + comment_size;
    ctx.parsed = 0;
    ctx.stream = stream;

    if (!xx_compactpro_walk(&ctx, NULL, root_records, 0)) goto fail;
    /* Every record the root declared must have been consumed by the walk,
     * directories included. */
    if (ctx.parsed != root_records) goto fail;
    if (stream->count == 0U) goto fail;
    if (ctx.position <= 4) goto fail;

    /* THE defence against a false positive. Byte 0 being 1 and a plausible
     * catalogue offset will happen by chance often; a CRC32 over exactly the
     * bytes the record walk consumed matching the stored word will not. The
     * range is not a stored length - it is wherever the walk stopped - so
     * this check also proves the walk stayed in step with the writer. Never
     * loosen it to "the walk did not fail". */
    if (xx_compactpro_crc32(catalog + 4, ctx.position - 4) != stored_crc) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    stream->archive_size = span;
    xx_mem_free(catalog);
    return stream;

fail:
    if (catalog) xx_mem_free(catalog);
    xx_compactpro_stream_free(stream);
    return NULL;
}


/* Per-fork method numbers. The container encodes this as a flag bit rather
 * than as a number, so these three values are the smallest faithful rendering
 * of what the archive actually says about a fork: nothing stored, the RLE
 * scheme applied to the stored bytes, or the same RLE fed from an LZH
 * stream. */


/* Plaintext sizes come from the catalogue and are attacker-controlled. */

static bool xx_compactpro_decode(Abstractformat *self,
                                 const xx_compactpro_member *member,
                                 uint8_t **out, size_t *out_size,
                                 xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Only these three exist. Anything else must fail rather than fall
     * through to a stored copy, which would emit compressed bytes as if they
     * were plaintext. */
    if (member->method != XX_COMPACTPRO_METHOD_STORED &&
        member->method != XX_COMPACTPRO_METHOD_RLE &&
        member->method != XX_COMPACTPRO_METHOD_LZH) {
        return false;
    }
    if (member->uncompressed_size < 0 || member->compressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_COMPACTPRO_MAX_DECODED ||
        member->uncompressed_size > XX_COMPACTPRO_MAX_DECODED) {
        return false;
    }

    if (member->method == XX_COMPACTPRO_METHOD_STORED) {
        /* An empty fork. It is a real member of the archive - a file may have
         * a zero-length data fork - so this is a success with no bytes, not a
         * failure. One byte is allocated because a zero-sized allocation has
         * no defined result to hand back. */
        if (member->uncompressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_compactpro_read_at(self, member->data_offset, input,
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

    if (member->method == XX_COMPACTPRO_METHOD_LZH) {
        ok = xx_compactpro_lzh_decode_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written);
    } else {
        ok = xx_compactpro_rle_decode_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written);
    }

    /* Both entry points return true only on an exact-length decode, but the
     * length is re-tested here so that a decoder relaxed in future cannot
     * turn a partial fork into a silent success. */
    if (!ok || written != (size_t)member->uncompressed_size) {
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

void xx_compactpro_init(xx_compactpro *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_COMPACTPRO;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compactpro");
    xx_format_set_extension(&archive->format, "cpt");
    archive->format.check_is_valid = xx_compactpro_check_is_valid;
    archive->format.handle_base_info = xx_compactpro_handle_base_info;
    archive->format.get_format_size = xx_compactpro_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_compactpro_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_compactpro_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_compactpro_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_compactpro_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_compactpro_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_compactpro_free_archive_records_reading;
    archive->format.destroy = xx_compactpro_vtable_destroy;
}

xx_compactpro *xx_compactpro_create(xx_io_device *device, int64_t base_address) {
    xx_compactpro *archive = (xx_compactpro *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_compactpro_init(archive, device, base_address);
    return archive;
}

void xx_compactpro_destroy(xx_compactpro *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_compactpro_free(xx_compactpro *archive) {
    if (!archive) return;
    xx_compactpro_destroy(archive);
    xx_mem_free(archive);
}

static void xx_compactpro_vtable_destroy(Abstractformat *self) {
    xx_compactpro_destroy((xx_compactpro *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_compactpro_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_compactpro_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_compactpro_parse(self, pd);
    if (!stream) return false;
    xx_compactpro_stream_free(stream);
    return true;
}

bool xx_compactpro_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_compactpro *archive = (xx_compactpro *)self;
    xx_compactpro_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_compactpro_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_compactpro_stream_free(stream);
    return true;
}

int64_t xx_compactpro_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_compactpro_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_compactpro *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_compactpro_set_record(xx_archive_record *record,
                                 const xx_compactpro_member *member) {
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

static bool xx_compactpro_copy_options(xx_list_s *target,
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

static const xx_var *xx_compactpro_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_compactpro_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_compactpro_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_compactpro_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_compactpro_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_compactpro_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_compactpro_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_compactpro_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_compactpro_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_compactpro_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_compactpro_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_compactpro_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_compactpro_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_compactpro_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_compactpro_stream *stream;
    const xx_compactpro_member *member;
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
    stream = (xx_compactpro_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_compactpro_path_safe(member->name)) return false;

    path_option = xx_compactpro_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_compactpro_decode(self, member, &plain, &plain_size, pd);
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
        !xx_compactpro_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_compactpro_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

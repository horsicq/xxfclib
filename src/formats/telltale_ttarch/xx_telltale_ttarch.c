/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Telltale Tool TTARCH2 archives.  xx_telltale_ttarch.h carries the field
 * tables.  Written from the structure of the format; the layout was
 * confirmed against ttarchext 0.3.1a (listing and extracting generated
 * archives), whose code was not used.
 *
 * A ZCTT archive is read through a one-chunk cache: the inner stream is
 * addressed by inner offset and each access inflates at most the chunks it
 * touches.  The directory (entries and name table) is read into memory once
 * per listing, so a listing inflates each directory chunk only once.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/telltale_ttarch/xx_telltale_ttarch.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef TELLTALE_TTARCH
#define XX_TELLTALE_TTARCH_FILE_TYPE XX_FILE_TYPE_TELLTALE_TTARCH
#else
#define XX_TELLTALE_TTARCH_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TT_ENTRY_SIZE 28U
#define TT_NAME_PAGE 0x10000U
#define TT_MAX_NAME 1024U
/* Real archives hold tens of thousands of files; these caps only refuse
 * fields that are plainly garbage and bound the directory held in memory. */
#define TT_MAX_FILES 0x100000U
#define TT_MAX_NAMES_SIZE 0x4000000U
#define TT_MIN_CHUNK 0x400U
#define TT_MAX_CHUNK 0x400000U
#define TT_MAX_CHUNKS 0x100000U
#define TT_TABLE_BLOCK 512U
#define TT_COPY_BLOCK 0x10000U
#define TT_MAX_SUFFIX 100000U

typedef struct tt_ctx {
    xx_io_device *device;
    int64_t base;
    int64_t avail;
    uint32_t wrapper;
    int64_t archive_size;
    /* bare and NCTT: the inner stream lies in the file */
    int64_t inner_abs;
    /* ZCTT / ECTT */
    uint32_t chunk_size;
    uint32_t chunk_count;
    int64_t table_abs;
    int64_t data_abs;
    uint64_t off0;
    uint8_t *cbuf;
    size_t cbuf_size;
    uint8_t *obuf;
    int64_t cached;
    size_t cached_len;
    bool decodable;
    /* inner directory */
    int64_t inner_size;
    uint32_t version;
    uint32_t names_size;
    uint32_t count;
    int64_t entries_at;
    int64_t names_at;
    int64_t data_at;
} tt_ctx;

typedef struct tt_member {
    char *name;
    int64_t offset; /* inner offset of the data */
    int64_t size;
    bool bad;       /* out of bounds */
} tt_member;

typedef struct tt_stream {
    tt_ctx ctx;
    tt_member *items;
    uint32_t count;
    uint32_t index;
} tt_stream;

static uint32_t tt_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t tt_le64(const uint8_t *p) {
    return (uint64_t)tt_le32(p) | ((uint64_t)tt_le32(p + 4) << 32);
}

static uint16_t tt_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static bool tt_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (io_capacity && request > io_capacity) request = io_capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static void tt_ctx_release(tt_ctx *ctx) {
    if (!ctx) return;
    if (ctx->cbuf) xx_mem_free(ctx->cbuf);
    if (ctx->obuf) xx_mem_free(ctx->obuf);
    ctx->cbuf = NULL;
    ctx->obuf = NULL;
    ctx->cached = -1;
}

static bool tt_chunked(const tt_ctx *ctx) {
    return ctx->wrapper == XX_TELLTALE_TTARCH_WRAP_ZCTT ||
           ctx->wrapper == XX_TELLTALE_TTARCH_WRAP_ECTT;
}

/* Largest compressed chunk accepted: Deflate adds 5 bytes per stored 64K
 * block, so this leaves generous room for any real encoder. */
static uint64_t tt_max_packed(uint32_t chunk_size) {
    return (uint64_t)chunk_size + (chunk_size >> 3) + 1024U;
}

static bool tt_chunk_span(tt_ctx *ctx, uint32_t index, int64_t *pos,
                          size_t *size) {
    uint8_t pair[16];
    uint64_t a, b;
    if (index >= ctx->chunk_count ||
        !tt_read_at(ctx->device, ctx->table_abs + (int64_t)index * 8, pair,
                    sizeof(pair)))
        return false;
    a = tt_le64(pair);
    b = tt_le64(pair + 8);
    if (a < ctx->off0 || b <= a || b - a > tt_max_packed(ctx->chunk_size) ||
        a - ctx->off0 > (uint64_t)(ctx->avail))
        return false;
    *pos = ctx->data_abs + (int64_t)(a - ctx->off0);
    *size = (size_t)(b - a);
    return *pos + (int64_t)*size <= ctx->base + ctx->archive_size;
}

/* Inflate chunk @p index into obuf.  Raw Deflate first, then a zlib-wrapped
 * stream; a non-final chunk must fill the whole chunk size. */
static bool tt_load_chunk(tt_ctx *ctx, uint32_t index) {
    int64_t pos;
    size_t size, written = 0U;
    bool ok;
    bool last = index + 1U == ctx->chunk_count;
    if (ctx->cached == (int64_t)index) return true;
    ctx->cached = -1;
    if (ctx->wrapper != XX_TELLTALE_TTARCH_WRAP_ZCTT) return false;
    if (!ctx->cbuf) {
        ctx->cbuf_size = (size_t)tt_max_packed(ctx->chunk_size);
        ctx->cbuf = (uint8_t *)xx_mem_alloc(ctx->cbuf_size);
        ctx->obuf = (uint8_t *)xx_mem_alloc(ctx->chunk_size);
        if (!ctx->cbuf || !ctx->obuf) {
            tt_ctx_release(ctx);
            return false;
        }
    }
    if (!tt_chunk_span(ctx, index, &pos, &size) || size > ctx->cbuf_size ||
        !tt_read_at(ctx->device, pos, ctx->cbuf, size))
        return false;
    ok = xx_deflate_decompress_memory(ctx->cbuf, size, ctx->obuf,
                                      ctx->chunk_size, &written, false);
    if (ok) ok = last ? (written > 0U && written <= ctx->chunk_size)
                      : written == ctx->chunk_size;
    if (!ok && xx_zlib_stream_header_is_valid(ctx->cbuf, size)) {
        written = 0U;
        ok = xx_zlib_stream_decode_memory(ctx->cbuf, size, ctx->obuf,
                                          ctx->chunk_size, &written);
        if (ok) ok = last ? (written > 0U && written <= ctx->chunk_size)
                          : written == ctx->chunk_size;
    }
    if (!ok) return false;
    ctx->cached = (int64_t)index;
    ctx->cached_len = written;
    return true;
}

static bool tt_inner_read(tt_ctx *ctx, int64_t offset, void *buffer,
                          size_t size) {
    uint8_t *out = (uint8_t *)buffer;
    if (offset < 0 || ctx->inner_size < 0 || offset > ctx->inner_size ||
        (uint64_t)size > (uint64_t)(ctx->inner_size - offset))
        return false;
    if (!tt_chunked(ctx))
        return tt_read_at(ctx->device, ctx->inner_abs + offset, buffer, size);
    while (size) {
        uint32_t index = (uint32_t)(offset / ctx->chunk_size);
        size_t within = (size_t)(offset % ctx->chunk_size);
        size_t take;
        if (!tt_load_chunk(ctx, index) || within >= ctx->cached_len)
            return false;
        take = ctx->cached_len - within;
        if (take > size) take = size;
        xx_rt_memcpy(out, ctx->obuf + within, take);
        out += take;
        size -= take;
        offset += (int64_t)take;
    }
    return true;
}

/* ZCTT/ECTT: header, then the whole offset table must be ordered, every
 * chunk plausibly sized, and the chunks must fit in the file. */
static bool tt_parse_chunked(tt_ctx *ctx, const uint8_t *head) {
    uint8_t block[TT_TABLE_BLOCK * 8U];
    uint32_t n = tt_le32(head + 8), done = 0U;
    uint32_t cs = tt_le32(head + 4);
    uint64_t prev = 0U, table_bytes;
    int64_t room;
    if (cs < TT_MIN_CHUNK || cs > TT_MAX_CHUNK || (cs & (cs - 1U)) != 0U ||
        n == 0U || n > TT_MAX_CHUNKS)
        return false;
    table_bytes = ((uint64_t)n + 1U) * 8U;
    if ((uint64_t)ctx->avail < 12U + table_bytes) return false;
    ctx->chunk_size = cs;
    ctx->chunk_count = n;
    ctx->table_abs = ctx->base + 12;
    ctx->data_abs = ctx->table_abs + (int64_t)table_bytes;
    room = ctx->avail - 12 - (int64_t)table_bytes;
    while (done <= n) {
        uint32_t take = n + 1U - done, i;
        if (take > TT_TABLE_BLOCK) take = TT_TABLE_BLOCK;
        if (!tt_read_at(ctx->device, ctx->table_abs + (int64_t)done * 8, block,
                        (size_t)take * 8U))
            return false;
        for (i = 0U; i < take; ++i) {
            uint64_t v = tt_le64(block + (size_t)i * 8U);
            if (done + i == 0U) {
                ctx->off0 = v;
            } else if (v <= prev || v - prev > tt_max_packed(cs)) {
                return false;
            }
            prev = v;
        }
        done += take;
    }
    if (prev - ctx->off0 > (uint64_t)room) return false;
    ctx->archive_size = 12 + (int64_t)table_bytes + (int64_t)(prev - ctx->off0);
    return true;
}

/* Inner directory header.  Everything it names must fit the inner stream. */
static bool tt_parse_inner(tt_ctx *ctx) {
    uint8_t h[16];
    uint32_t hdr;
    uint64_t end;
    if (ctx->inner_size < 12 ||
        !tt_inner_read(ctx, 0, h,
                       ctx->inner_size >= 16 ? 16U : 12U))
        return false;
    if (xx_rt_memcmp(h, "4ATT", 4U) == 0) {
        ctx->version = 4U;
        hdr = 12U;
        ctx->names_size = tt_le32(h + 4);
        ctx->count = tt_le32(h + 8);
    } else if (xx_rt_memcmp(h, "3ATT", 4U) == 0) {
        if (ctx->inner_size < 16) return false;
        ctx->version = 3U;
        hdr = 16U;
        ctx->names_size = tt_le32(h + 8);
        ctx->count = tt_le32(h + 12);
    } else {
        return false;
    }
    if (ctx->count > TT_MAX_FILES || ctx->names_size > TT_MAX_NAMES_SIZE ||
        (ctx->count && ctx->names_size < 2U))
        return false;
    ctx->entries_at = hdr;
    ctx->names_at = (int64_t)hdr + (int64_t)ctx->count * TT_ENTRY_SIZE;
    end = (uint64_t)ctx->names_at + ctx->names_size;
    if (end > (uint64_t)ctx->inner_size) return false;
    ctx->data_at = (int64_t)end;
    return true;
}

/* Decode one entry.  @p names is the name table when it is held in memory;
 * otherwise the name is read from the stream.  @p name gets TT_MAX_NAME. */
static bool tt_entry(tt_ctx *ctx, const uint8_t *e, const uint8_t *names,
                     char *name, int64_t *offset, int64_t *size, bool *bad) {
    uint64_t off = tt_le64(e + 8);
    uint32_t sz = tt_le32(e + 16);
    uint32_t at = (uint32_t)tt_le16(e + 24) * TT_NAME_PAGE + tt_le16(e + 26);
    uint32_t avail, i;
    int64_t space = ctx->inner_size - ctx->data_at;
    if (at >= ctx->names_size) return false;
    avail = ctx->names_size - at;
    if (avail > TT_MAX_NAME) avail = TT_MAX_NAME;
    if (names) {
        xx_rt_memcpy(name, names + at, avail);
    } else if (!tt_inner_read(ctx, ctx->names_at + at, name, avail)) {
        return false;
    }
    for (i = 0U; i < avail && name[i]; ++i) {}
    if (i == 0U || i == avail) return false;
    *offset = ctx->data_at + (int64_t)(off & 0x7FFFFFFFFFFFFFFFULL);
    *size = (int64_t)sz;
    *bad = off > (uint64_t)space || (uint64_t)sz > (uint64_t)space - off;
    return true;
}

/* Open the archive at the format's base.  @p deep also measures a bare
 * archive's extent by walking every entry. */
static bool tt_open(Abstractformat *format, tt_ctx *ctx, bool deep) {
    uint8_t head[16];
    int64_t total;
    xx_mem_zero(ctx, sizeof(*ctx));
    ctx->cached = -1;
    ctx->inner_size = -1;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    ctx->device = format->device;
    ctx->base = format->base_address;
    ctx->avail = total - format->base_address;
    if (ctx->avail < 12 ||
        !tt_read_at(ctx->device, ctx->base, head,
                    ctx->avail >= 16 ? 16U : 12U))
        return false;
    if (!xx_rt_memcmp(head, "4ATT", 4U) || !xx_rt_memcmp(head, "3ATT", 4U)) {
        ctx->wrapper = XX_TELLTALE_TTARCH_WRAP_NONE;
        ctx->inner_abs = ctx->base;
        ctx->inner_size = ctx->avail;
        ctx->archive_size = ctx->avail;
    } else if (!xx_rt_memcmp(head, "NCTT", 4U)) {
        uint64_t size;
        if (ctx->avail < 24) return false;
        size = tt_le64(head + 4);
        if (size < 12U || size > (uint64_t)(ctx->avail - 12)) return false;
        ctx->wrapper = XX_TELLTALE_TTARCH_WRAP_NCTT;
        ctx->inner_abs = ctx->base + 12;
        ctx->inner_size = (int64_t)size;
        ctx->archive_size = 12 + (int64_t)size;
    } else if (!xx_rt_memcmp(head, "ZCTT", 4U) ||
               !xx_rt_memcmp(head, "ECTT", 4U)) {
        ctx->wrapper = head[0] == 'Z' ? XX_TELLTALE_TTARCH_WRAP_ZCTT
                                      : XX_TELLTALE_TTARCH_WRAP_ECTT;
        if (!tt_parse_chunked(ctx, head)) return false;
        if (ctx->wrapper == XX_TELLTALE_TTARCH_WRAP_ECTT) return true;
        /* The last chunk fixes the inner size.  Chunks that do not inflate
         * (Oodle) leave a measured archive whose members are unavailable. */
        if (!tt_load_chunk(ctx, ctx->chunk_count - 1U)) {
            if (!ctx->cbuf) return false; /* allocation failure */
            return true;
        }
        ctx->inner_size = (int64_t)(ctx->chunk_count - 1U) * ctx->chunk_size +
                          (int64_t)ctx->cached_len;
        if (!tt_load_chunk(ctx, 0U)) {
            ctx->inner_size = -1;
            return true;
        }
        ctx->decodable = true;
        return tt_parse_inner(ctx);
    } else {
        return false;
    }
    ctx->decodable = true;
    if (!tt_parse_inner(ctx)) return false;
    /* The first entry must name something inside the name table. */
    if (ctx->count) {
        uint8_t e[TT_ENTRY_SIZE];
        char name[TT_MAX_NAME];
        int64_t off, size;
        bool bad;
        if (!tt_inner_read(ctx, ctx->entries_at, e, sizeof(e)) ||
            !tt_entry(ctx, e, NULL, name, &off, &size, &bad))
            return false;
    }
    if (deep && ctx->wrapper == XX_TELLTALE_TTARCH_WRAP_NONE) {
        uint8_t block[TT_TABLE_BLOCK * TT_ENTRY_SIZE];
        uint32_t done = 0U;
        int64_t end = ctx->data_at;
        while (done < ctx->count) {
            uint32_t take = ctx->count - done, i;
            if (take > TT_TABLE_BLOCK) take = TT_TABLE_BLOCK;
            if (!tt_inner_read(ctx, ctx->entries_at +
                                        (int64_t)done * TT_ENTRY_SIZE,
                               block, (size_t)take * TT_ENTRY_SIZE))
                return false;
            for (i = 0U; i < take; ++i) {
                const uint8_t *e = block + (size_t)i * TT_ENTRY_SIZE;
                uint64_t off = tt_le64(e + 8);
                uint64_t space = (uint64_t)(ctx->inner_size - ctx->data_at);
                uint32_t sz = tt_le32(e + 16);
                if (off <= space && sz <= space - off &&
                    ctx->data_at + (int64_t)(off + sz) > end)
                    end = ctx->data_at + (int64_t)(off + sz);
            }
            done += take;
        }
        ctx->archive_size = end;
    }
    return true;
}

static char tt_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool tt_stem_is(const char *s, size_t stem, const char *word) {
    size_t i;
    for (i = 0U; i < stem; ++i)
        if (!word[i] || tt_upper(s[i]) != word[i]) return false;
    return word[stem] == 0;
}

/* One path component: printable ASCII without Windows-reserved punctuation,
 * not only dots and spaces, not a device name. */
static bool tt_safe_component(const char *s, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t i, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (i = 0U; i < length; ++i) {
        char c = s[i];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*' || c == '\\')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (tt_stem_is(s, stem, devices[i])) return false;
    if (stem == 4U && s[3] >= '0' && s[3] <= '9' &&
        ((tt_upper(s[0]) == 'C' && tt_upper(s[1]) == 'O' &&
          tt_upper(s[2]) == 'M') ||
         (tt_upper(s[0]) == 'L' && tt_upper(s[1]) == 'P' &&
          tt_upper(s[2]) == 'T')))
        return false;
    return true;
}

/* A relative path of safe components separated by single '/'. */
static bool tt_safe_path(const char *name) {
    size_t start = 0U, i = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        if (name[i] == '/' || name[i] == 0) {
            if (!tt_safe_component(name + start, i - start)) return false;
            if (!name[i]) return true;
            start = i + 1U;
        }
        ++i;
    }
}

static uint32_t tt_name_hash(const char *s) {
    uint32_t h = 2166136261U;
    while (*s) {
        h ^= (uint8_t)tt_upper(*s++);
        h *= 16777619U;
    }
    return h;
}

static bool tt_names_equal(const char *a, const char *b) {
    while (*a && *b)
        if (tt_upper(*a++) != tt_upper(*b++)) return false;
    return *a == *b;
}

static void tt_stream_free(void *opaque) {
    tt_stream *s = (tt_stream *)opaque;
    uint32_t i;
    if (!s) return;
    for (i = 0U; i < s->count; ++i)
        if (s->items[i].name) xx_str_free(s->items[i].name);
    if (s->items) xx_mem_free(s->items);
    tt_ctx_release(&s->ctx);
    xx_mem_free(s);
}

/* Give every member a name no other member has (case-insensitively), by
 * appending "__2", "__3", ... to later duplicates. */
static bool tt_dedupe(tt_stream *s) {
    uint32_t cap = 16U, i;
    uint32_t *slots;
    while (cap < s->count * 2U) cap <<= 1;
    slots = (uint32_t *)xx_mem_alloc((size_t)cap * sizeof(uint32_t));
    if (!slots) return false;
    for (i = 0U; i < cap; ++i) slots[i] = UINT32_MAX;
    for (i = 0U; i < s->count; ++i) {
        uint32_t suffix = 1U;
        char *original = s->items[i].name;
        for (;;) {
            uint32_t h = tt_name_hash(s->items[i].name) & (cap - 1U);
            bool found = false;
            while (slots[h] != UINT32_MAX) {
                if (tt_names_equal(s->items[slots[h]].name, s->items[i].name)) {
                    found = true;
                    break;
                }
                h = (h + 1U) & (cap - 1U);
            }
            if (!found) {
                slots[h] = i;
                break;
            }
            if (++suffix > TT_MAX_SUFFIX) {
                xx_mem_free(slots);
                return false;
            }
            {
                char tail[24];
                char *replacement;
                xx_rt_snprintf(tail, sizeof(tail), "__%u", suffix);
                replacement = xx_str_concat(original, tail);
                if (!replacement) {
                    xx_mem_free(slots);
                    return false;
                }
                if (s->items[i].name != original)
                    xx_str_free(s->items[i].name);
                s->items[i].name = replacement;
            }
        }
        if (s->items[i].name != original) xx_str_free(original);
    }
    xx_mem_free(slots);
    return true;
}

/* Read the whole directory and build the member list. */
static bool tt_load_members(tt_stream *s) {
    tt_ctx *ctx = &s->ctx;
    uint8_t *entries = NULL, *names = NULL;
    char *name = NULL;
    uint32_t i;
    bool ok = false;
    size_t esize = (size_t)ctx->count * TT_ENTRY_SIZE;
    if (!ctx->count) return true;
    entries = (uint8_t *)xx_mem_alloc(esize);
    names = (uint8_t *)xx_mem_alloc(ctx->names_size);
    name = (char *)xx_mem_alloc(TT_MAX_NAME);
    s->items = (tt_member *)xx_mem_calloc(ctx->count, sizeof(tt_member));
    if (!entries || !names || !name || !s->items ||
        !tt_inner_read(ctx, ctx->entries_at, entries, esize) ||
        !tt_inner_read(ctx, ctx->names_at, names, ctx->names_size))
        goto done;
    for (i = 0U; i < ctx->count; ++i) {
        tt_member *m = &s->items[i];
        size_t k;
        if (!tt_entry(ctx, entries + (size_t)i * TT_ENTRY_SIZE, names, name,
                      &m->offset, &m->size, &m->bad))
            goto done;
        m->name = xx_str_dup(name);
        if (!m->name) goto done;
        s->count = i + 1U;
        for (k = 0U; m->name[k]; ++k)
            if (m->name[k] == '\\') m->name[k] = '/';
    }
    ok = tt_dedupe(s);
done:
    if (entries) xx_mem_free(entries);
    if (names) xx_mem_free(names);
    if (name) xx_mem_free(name);
    return ok;
}

static bool tt_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *tt_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool tt_set_record(xx_archive_record *record, const tt_stream *s) {
    const tt_member *m = &s->items[s->index];
    bool packed = s->ctx.wrapper == XX_TELLTALE_TTARCH_WRAP_ZCTT;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        packed ? -1
               : s->ctx.inner_abs + s->ctx.entries_at +
                     (int64_t)s->index * TT_ENTRY_SIZE;
    record->header_size = packed ? 0 : TT_ENTRY_SIZE;
    record->data_offset = packed ? -1 : s->ctx.inner_abs + m->offset;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          packed ? 8U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool tt_copy_member(tt_ctx *ctx, const tt_member *m,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (m->bad) return false;
    buffer = (uint8_t *)xx_mem_alloc(TT_COPY_BLOCK);
    if (!buffer) return false;
    while (ok && done < m->size) {
        size_t take = (size_t)((m->size - done) > (int64_t)TT_COPY_BLOCK
                                   ? TT_COPY_BLOCK
                                   : (m->size - done));
        if ((pd && xx_pd_is_stopped(pd)) ||
            !tt_inner_read(ctx, m->offset + done, buffer, take) ||
            (destination &&
             xx_io_write(destination, buffer, take) != (ssize_t)take))
            ok = false;
        done += (int64_t)take;
    }
    xx_mem_free(buffer);
    return ok;
}

void xx_telltale_ttarch_init(xx_telltale_ttarch *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TELLTALE_TTARCH_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ttarch2");
    xx_format_set_extension(&archive->format, "ttarch2");
    archive->format.check_is_valid = xx_telltale_ttarch_check_is_valid;
    archive->format.handle_base_info = xx_telltale_ttarch_handle_base_info;
    archive->format.get_format_size = xx_telltale_ttarch_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_telltale_ttarch_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_telltale_ttarch_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_telltale_ttarch_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_telltale_ttarch_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_telltale_ttarch_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_telltale_ttarch_free_archive_records_reading;
    archive->inner_size = -1;
}

xx_telltale_ttarch *xx_telltale_ttarch_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_telltale_ttarch *archive =
        (xx_telltale_ttarch *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_telltale_ttarch_init(archive, device, base_address);
    return archive;
}

void xx_telltale_ttarch_destroy(xx_telltale_ttarch *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_telltale_ttarch_free(xx_telltale_ttarch *archive) {
    if (!archive) return;
    xx_telltale_ttarch_destroy(archive);
    xx_mem_free(archive);
}

bool xx_telltale_ttarch_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    tt_ctx ctx;
    bool ok;
    (void)pd;
    ok = tt_open(format, &ctx, false);
    tt_ctx_release(&ctx);
    return ok;
}

bool xx_telltale_ttarch_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    tt_ctx ctx;
    xx_telltale_ttarch *archive;
    (void)pd;
    if (!format || !tt_open(format, &ctx, true)) {
        if (format) tt_ctx_release(&ctx);
        return false;
    }
    archive = (xx_telltale_ttarch *)format;
    archive->wrapper = ctx.wrapper;
    archive->chunk_size = ctx.chunk_size;
    archive->chunk_count = ctx.chunk_count;
    archive->inner_size = ctx.inner_size;
    archive->inner_version = ctx.inner_size >= 0 ? ctx.version : 0U;
    archive->members_unavailable = !ctx.decodable;
    archive->number_of_records = ctx.decodable ? ctx.count : 0U;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = ctx.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    tt_ctx_release(&ctx);
    return true;
}

int64_t xx_telltale_ttarch_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_telltale_ttarch_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_telltale_ttarch_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_telltale_ttarch_handle_base_info(format, pd))
               ? ((xx_telltale_ttarch *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_telltale_ttarch_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    tt_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (tt_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!tt_open(format, &stream->ctx, false) || !stream->ctx.decodable ||
        !tt_load_members(stream)) {
        tt_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        tt_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = tt_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!tt_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count) {
        if (!tt_set_record(&state->current_record, stream)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_telltale_ttarch_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_telltale_ttarch_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    tt_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (tt_stream *)state->internal_state) || !state->has_record ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!tt_set_record(&state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_telltale_ttarch_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    tt_stream *stream;
    const tt_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (tt_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = tt_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return tt_copy_member(&stream->ctx, member, NULL, pd);
    if (member->bad || !tt_safe_path(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = tt_copy_member(&stream->ctx, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_telltale_ttarch_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

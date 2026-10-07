/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EWF2-Lx01: EnCase 7+ logical evidence files. xx_ewf2_lx01.h carries the
 * field tables. Written from the structure described in the libyal EWF and
 * EWF2 format documents; no code was taken from libewf.
 *
 * Parsing order:
 *   1. the 32-byte header;
 *   2. the section chain, read backwards from the end of the device (the
 *      descriptors sit after their data and point at their predecessor).
 *      When the device carries more than the segment (an embedded or
 *      carved segment), the chain is found forwards instead: every
 *      16-byte aligned position is tested for a descriptor whose previous
 *      pointer names the descriptor found before it;
 *   3. case data / device information for the chunk size, the sector
 *      tables, and the single files ("ltree") text, whose entry category
 *      gives the file tree.
 * Chunks are decoded on demand (one cached), table entries are read one at
 * a time, so memory stays bounded by the chunk size and the ltree text.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ewf2_lx01/xx_ewf2_lx01.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested. */
#ifdef EWF2_LX01
#define XX_EWF2_LX01_FILE_TYPE XX_FILE_TYPE_EWF2_LX01
#else
#define XX_EWF2_LX01_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LX_HEADER 32U
#define LX_DESC 64U

#define LX_TYPE_DEVICE 0x01U
#define LX_TYPE_CASE 0x02U
#define LX_TYPE_TABLE 0x04U
#define LX_TYPE_KEYS 0x0BU
#define LX_TYPE_NEXT 0x0DU
#define LX_TYPE_DONE 0x0FU
#define LX_TYPE_SINGLE_FILES 0x20U

#define LX_FLAG_ENCRYPTED 0x02U

#define LX_CHUNK_COMPRESSED 0x01U
#define LX_CHUNK_CHECKSUM 0x02U
#define LX_CHUNK_PATTERN 0x04U

#define LX_OPR_SPARSE 0x04000000U

#define LX_MAX_SECTIONS (1U << 18)
#define LX_MAX_STRING_IN (1U << 20)   /* packed case / device text */
#define LX_MAX_STRING_OUT (1U << 20)  /* unpacked case / device text */
#define LX_MAX_LTREE (128U << 20)
#define LX_MAX_CHUNK (16U << 20)
#define LX_MAX_TABLE_ENTRIES (1U << 24)
#define LX_MAX_ENTRIES (1U << 20)
#define LX_MAX_DEPTH 256U
#define LX_MAX_EXTENTS 65536U
#define LX_MAX_CATEGORIES 64U
#define LX_SCAN_BLOCK (1U << 20)
#define LX_PATH_MAX 16384U
#define LX_FILL_BLOCK 65536U

/* ---------------------------------------------------------------------- */
/* Types                                                                   */

typedef struct lx_section_s {
    uint32_t type;
    uint32_t flags;
    uint32_t padding;
    int64_t data;  /**< Absolute device offset of the section data. */
    uint64_t size; /**< Data size, padding included. */
} lx_section;

typedef struct lx_layout_s {
    int64_t base;
    int64_t end; /**< Absolute end of the segment (after its last descriptor). */
    uint16_t method;
    uint32_t segment;
    bool done;
    lx_section *sections;
    uint32_t count;
    uint32_t capacity;
} lx_layout;

typedef struct lx_table_s {
    uint64_t first;
    uint32_t count;
    int64_t entries_at;
    bool has_footer;
    uint8_t verified; /**< 0 not yet, 1 good, 2 bad. */
} lx_table;

typedef struct lx_entry_s {
    uint32_t parent;
    uint32_t depth;
    uint32_t name_at, name_len; /**< UTF-16 unit range in the ltree text. */
    uint32_t be_at, be_len;
    uint32_t ha_at, ha_len;
    uint64_t size;
    int64_t du;
    uint32_t opr;
    bool has_size;
    bool is_dir;
} lx_entry;

typedef struct lx_info_s {
    lx_layout layout;
    lx_table *tables;
    uint32_t table_count;
    uint64_t chunk_end; /**< One past the highest chunk number of any table. */
    uint64_t chunk_total;
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint64_t media_size;
    bool encrypted;
    bool has_single_files;
    uint16_t *text;
    uint32_t text_units;
    lx_entry *entries;
    uint32_t entry_count;
    uint32_t *files;
    uint32_t file_count;
} lx_info;

typedef struct lx_media_s {
    xx_io_device *device;
    lx_info *info;
    uint8_t *chunk;
    size_t chunk_len;
    uint64_t chunk_index;
    bool chunk_valid;
    uint8_t *packed;
    size_t packed_capacity;
    xx_io_device *memory;
} lx_media;

typedef struct lx_sink_s {
    xx_io_device *device;
    xx_hash_context md5;
    uint64_t written;
} lx_sink;

typedef struct lx_stream_s {
    lx_info info;
    lx_media media;
    uint8_t *renamed;
    char *name;
    bool name_ok;
    size_t at;
} lx_stream;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static bool lx_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > (size_t)(1U << 20)) request = (size_t)(1U << 20);
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool lx_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }

/* ---------------------------------------------------------------------- */
/* Header and section chain                                                */

static const uint8_t lx_signature[8] = {'L', 'E', 'F', '2', 0x0D, 0x0A, 0x81, 0x00};

static bool lx_parse_header(Abstractformat *format, lx_layout *l,
                            int64_t *size_out) {
    uint8_t header[LX_HEADER];
    int64_t total, size;
    uint16_t method;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(LX_HEADER + LX_DESC) ||
        !lx_read_at(format->device, format->base_address, header, LX_HEADER))
        return false;
    if (xx_rt_memcmp(header, lx_signature, 8U) != 0 || header[8] != 2U)
        return false;
    method = (uint16_t)(header[10] | ((uint16_t)header[11] << 8U));
    if (method > 2U) return false;
    l->base = format->base_address;
    l->method = method;
    l->segment = xx_data_get_u32(header + 12, 4, 0, false);
    *size_out = size;
    return true;
}

/* A descriptor on its own: checksum, fixed size field, sane type. */
static bool lx_desc_decode(const uint8_t *raw, lx_section *s, uint64_t *prev) {
    uint32_t type;
    if (xx_adler32(raw, 60U) != xx_data_get_u32(raw + 60, 4, 0, false)) return false;
    type = xx_data_get_u32(raw, 4, 0, false);
    if (type == 0U || type > 0xFFFFU || xx_data_get_u32(raw + 24, 4, 0, false) != LX_DESC)
        return false;
    s->type = type;
    s->flags = xx_data_get_u32(raw + 4, 4, 0, false);
    s->size = xx_data_get_u64(raw + 16, 8, 0, false);
    s->padding = xx_data_get_u32(raw + 28, 4, 0, false);
    s->data = 0;
    if ((uint64_t)s->padding > s->size) return false;
    *prev = xx_data_get_u64(raw + 8, 8, 0, false);
    return true;
}

static bool lx_push_section(lx_layout *l, const lx_section *s) {
    if (l->count >= LX_MAX_SECTIONS) return false;
    if (l->count == l->capacity) {
        uint32_t grown = l->capacity ? l->capacity * 2U : 16U;
        lx_section *next;
        if (grown > LX_MAX_SECTIONS) grown = LX_MAX_SECTIONS;
        next = (lx_section *)xx_mem_realloc(l->sections,
                                            (size_t)grown * sizeof(*next));
        if (!next) return false;
        l->sections = next;
        l->capacity = grown;
    }
    l->sections[l->count++] = *s;
    return true;
}

static void lx_layout_free(lx_layout *l) {
    if (l->sections) xx_mem_free(l->sections);
    l->sections = NULL;
    l->count = l->capacity = 0U;
}

static bool lx_walk_backward(xx_io_device *device, lx_layout *l, int64_t size,
                             xx_pd_struct *pd) {
    uint8_t raw[LX_DESC];
    uint64_t rel = (uint64_t)size - LX_DESC;
    bool first = true;
    for (;;) {
        lx_section s;
        uint64_t prev, start;
        if (lx_stopped(pd) ||
            !lx_read_at(device, l->base + (int64_t)rel, raw, LX_DESC) ||
            !lx_desc_decode(raw, &s, &prev))
            return false;
        if (first) {
            if (s.type != LX_TYPE_DONE && s.type != LX_TYPE_NEXT) return false;
            l->done = s.type == LX_TYPE_DONE;
            l->end = l->base + (int64_t)rel + LX_DESC;
            first = false;
        }
        if (prev == 0U) {
            start = LX_HEADER;
        } else {
            if (prev < LX_HEADER || prev > rel || rel - prev < LX_DESC)
                return false;
            start = prev + LX_DESC;
        }
        if (start > rel || s.size > rel - start) return false;
        s.data = l->base + (int64_t)start;
        if (!lx_push_section(l, &s)) return false;
        if (prev == 0U) break;
        rel = prev;
    }
    /* Collected last to first; put them in file order. */
    {
        uint32_t i = 0U, j = l->count - 1U;
        while (i < j) {
            lx_section t = l->sections[i];
            l->sections[i++] = l->sections[j];
            l->sections[j--] = t;
        }
    }
    return true;
}

static bool lx_walk_forward(xx_io_device *device, lx_layout *l, int64_t size,
                            xx_pd_struct *pd) {
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(LX_SCAN_BLOCK + LX_DESC);
    uint64_t start = LX_HEADER, expect = 0U, pos = LX_HEADER;
    bool result = false;
    if (!buffer) return false;
    while (pos + LX_DESC <= (uint64_t)size) {
        uint64_t left = (uint64_t)size - pos;
        size_t avail = left > (uint64_t)(LX_SCAN_BLOCK + LX_DESC)
                           ? (size_t)(LX_SCAN_BLOCK + LX_DESC)
                           : (size_t)left;
        size_t i;
        bool found = false;
        if (lx_stopped(pd) ||
            !lx_read_at(device, l->base + (int64_t)pos, buffer, avail))
            break;
        for (i = 0U; i + LX_DESC <= avail; i += 16U) {
            const uint8_t *r = buffer + i;
            uint64_t p = pos + i, prev;
            lx_section s;
            if (xx_data_get_u32(r + 24, 4, 0, false) != LX_DESC || xx_data_get_u64(r + 8, 8, 0, false) != expect ||
                xx_data_get_u64(r + 16, 8, 0, false) > p - start || !lx_desc_decode(r, &s, &prev))
                continue;
            s.data = l->base + (int64_t)start;
            if (!lx_push_section(l, &s)) goto done;
            if (s.type == LX_TYPE_DONE || s.type == LX_TYPE_NEXT) {
                l->done = s.type == LX_TYPE_DONE;
                l->end = l->base + (int64_t)p + LX_DESC;
                result = true;
                goto done;
            }
            expect = p;
            start = p + LX_DESC;
            pos = start;
            found = true;
            break;
        }
        if (!found) pos += i;
    }
done:
    xx_mem_free(buffer);
    return result;
}

static bool lx_parse_layout(Abstractformat *format, lx_layout *l,
                            xx_pd_struct *pd) {
    int64_t size;
    xx_mem_zero(l, sizeof(*l));
    if (!lx_parse_header(format, l, &size)) return false;
    if (lx_walk_backward(format->device, l, size, pd)) return true;
    lx_layout_free(l);
    if (lx_stopped(pd)) return false;
    if (lx_walk_forward(format->device, l, size, pd)) return true;
    lx_layout_free(l);
    return false;
}

/* ---------------------------------------------------------------------- */
/* UTF-16 text helpers                                                     */

typedef struct lx_text_s {
    const uint16_t *t;
    uint32_t n;
    uint32_t pos;
} lx_text;

/* The next line as [*a, *b), without "\n" and a trailing "\r". */
static bool lx_next_line(lx_text *r, uint32_t *a, uint32_t *b) {
    uint32_t e;
    if (r->pos >= r->n) return false;
    *a = r->pos;
    for (e = r->pos; e < r->n && r->t[e] != 0x000AU; ++e) {
    }
    r->pos = e < r->n ? e + 1U : r->n;
    if (e > *a && r->t[e - 1U] == 0x000DU) --e;
    *b = e;
    return true;
}

static bool lx_range_is(const uint16_t *t, uint32_t a, uint32_t b,
                        const char *word) {
    uint32_t i = 0U;
    for (; a + i < b; ++i) {
        if (!word[i] || t[a + i] != (uint16_t)(uint8_t)word[i]) return false;
    }
    return word[i] == 0;
}

/* Column @p index of a tab separated range. */
static bool lx_column(const uint16_t *t, uint32_t a, uint32_t b,
                      uint32_t index, uint32_t *ca, uint32_t *cb) {
    uint32_t col = 0U, s = a, i;
    for (i = a; i <= b; ++i) {
        if (i == b || t[i] == 0x0009U) {
            if (col == index) {
                *ca = s;
                *cb = i;
                return true;
            }
            ++col;
            s = i + 1U;
        }
    }
    return false;
}

static void lx_trim(const uint16_t *t, uint32_t *a, uint32_t *b) {
    while (*a < *b && t[*a] == 0x0020U) ++*a;
    while (*b > *a && t[*b - 1U] == 0x0020U) --*b;
}

static bool lx_parse_u64(const uint16_t *t, uint32_t a, uint32_t b,
                         uint64_t *out) {
    uint64_t v = 0U;
    lx_trim(t, &a, &b);
    if (a == b) return false;
    for (; a < b; ++a) {
        uint16_t c = t[a];
        if (c < '0' || c > '9') return false;
        if (v > (UINT64_MAX - (uint64_t)(c - '0')) / 10U) return false;
        v = v * 10U + (uint64_t)(c - '0');
    }
    *out = v;
    return true;
}

static bool lx_parse_i64(const uint16_t *t, uint32_t a, uint32_t b,
                         int64_t *out) {
    uint64_t v;
    bool negative = false;
    lx_trim(t, &a, &b);
    if (a < b && t[a] == '-') {
        negative = true;
        ++a;
    }
    if (!lx_parse_u64(t, a, b, &v) || v > (uint64_t)INT64_MAX) return false;
    *out = negative ? -(int64_t)v : (int64_t)v;
    return true;
}

static bool lx_parse_hex(const uint16_t *t, uint32_t a, uint32_t b,
                         uint64_t *out) {
    uint64_t v = 0U;
    if (a == b || b - a > 16U) return false;
    for (; a < b; ++a) {
        uint16_t c = t[a];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return false;
        v = (v << 4U) | d;
    }
    *out = v;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Case data / device information                                          */

/* Unpack a small compressed UTF-16 object string into code units. */
static uint16_t *lx_read_object_string(xx_io_device *device,
                                       const lx_layout *l,
                                       const lx_section *s,
                                       uint32_t *units) {
    uint8_t *packed = NULL, *plain = NULL;
    uint16_t *text = NULL;
    size_t length = 0U, payload, i, skip = 0U;
    bool big = false;
    *units = 0U;
    payload = (size_t)(s->size - s->padding);
    if (payload == 0U || payload > LX_MAX_STRING_IN ||
        (s->flags & LX_FLAG_ENCRYPTED) != 0U)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc(payload);
    if (!packed || !lx_read_at(device, s->data, packed, payload)) goto done;
    if (l->method == 0U) {
        plain = packed;
        packed = NULL;
        length = payload;
    } else if (l->method == 1U) {
        plain = (uint8_t *)xx_mem_alloc(LX_MAX_STRING_OUT);
        if (!plain ||
            !xx_zlib_stream_decode_memory(packed, payload, plain,
                                          LX_MAX_STRING_OUT, &length))
            goto done;
    } else {
        goto done;
    }
    if (length >= 2U && plain[0] == 0xFFU && plain[1] == 0xFEU) {
        skip = 2U;
    } else if (length >= 2U && plain[0] == 0xFEU && plain[1] == 0xFFU) {
        skip = 2U;
        big = true;
    }
    length = (length - skip) / 2U;
    if (length == 0U) goto done;
    text = (uint16_t *)xx_mem_alloc(length * sizeof(uint16_t));
    if (!text) goto done;
    for (i = 0U; i < length; ++i) {
        const uint8_t *p = plain + skip + i * 2U;
        text[i] = big ? (uint16_t)((p[0] << 8U) | p[1])
                      : (uint16_t)(p[0] | (p[1] << 8U));
    }
    *units = (uint32_t)length;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return text;
}

/* Value of tag @p tag in a "1\nmain\n<tags>\n<values>" object string. */
static bool lx_object_value(const uint16_t *t, uint32_t n, const char *tag,
                            uint64_t *out) {
    lx_text r;
    uint32_t a, b, ta, tb, va, vb, ca, cb, index;
    r.t = t;
    r.n = n;
    r.pos = 0U;
    if (!lx_next_line(&r, &a, &b) || !lx_next_line(&r, &a, &b) ||
        !lx_range_is(t, a, b, "main") || !lx_next_line(&r, &ta, &tb) ||
        !lx_next_line(&r, &va, &vb))
        return false;
    for (index = 0U; index < 256U; ++index) {
        if (!lx_column(t, ta, tb, index, &ca, &cb)) return false;
        if (lx_range_is(t, ca, cb, tag)) {
            return lx_column(t, va, vb, index, &ca, &cb) &&
                   lx_parse_u64(t, ca, cb, out);
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Sector tables                                                           */

static bool lx_add_table(xx_io_device *device, lx_info *info,
                         const lx_section *s) {
    uint8_t header[32];
    lx_table table;
    uint64_t need;
    if (s->size < 32U || !lx_read_at(device, s->data, header, 32U) ||
        xx_adler32(header, 16U) != xx_data_get_u32(header + 16, 4, 0, false))
        return false;
    table.first = xx_data_get_u64(header, 8, 0, false);
    table.count = xx_data_get_u32(header + 8, 4, 0, false);
    if (table.count > LX_MAX_TABLE_ENTRIES ||
        table.first > UINT64_MAX / 2U)
        return false;
    need = 32U + (uint64_t)table.count * 16U;
    if (need > s->size) return false;
    table.entries_at = s->data + 32;
    table.has_footer = s->size - need >= 4U;
    table.verified = 0U;
    if (table.count == 0U) return true;
    {
        lx_table *next;
        if (info->table_count >= LX_MAX_SECTIONS) return false;
        next = (lx_table *)xx_mem_realloc(
            info->tables, ((size_t)info->table_count + 1U) * sizeof(*next));
        if (!next) return false;
        info->tables = next;
        info->tables[info->table_count++] = table;
    }
    if (table.first + table.count > info->chunk_end)
        info->chunk_end = table.first + table.count;
    info->chunk_total += table.count;
    return true;
}

static bool lx_verify_table(xx_io_device *device, lx_table *table) {
    uint8_t buffer[4096];
    uint64_t left = (uint64_t)table->count * 16U;
    int64_t at = table->entries_at;
    uint32_t adler = 1U;
    uint8_t stored[4];
    if (table->verified) return table->verified == 1U;
    table->verified = 2U;
    if (!table->has_footer) {
        table->verified = 1U;
        return true;
    }
    while (left) {
        size_t take = left > sizeof(buffer) ? sizeof(buffer) : (size_t)left;
        if (!lx_read_at(device, at, buffer, take)) return false;
        adler = xx_adler32_update(adler, buffer, take);
        at += (int64_t)take;
        left -= take;
    }
    if (!lx_read_at(device, at, stored, 4U) || xx_data_get_u32(stored, 4, 0, false) != adler)
        return false;
    table->verified = 1U;
    return true;
}

static bool lx_find_chunk(lx_media *m, uint64_t ci, uint8_t entry[16]) {
    uint32_t i;
    for (i = 0U; i < m->info->table_count; ++i) {
        lx_table *table = &m->info->tables[i];
        if (ci >= table->first && ci - table->first < table->count) {
            if (!lx_verify_table(m->device, table)) return false;
            return lx_read_at(m->device,
                              table->entries_at +
                                  (int64_t)((ci - table->first) * 16U),
                              entry, 16U);
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Chunks and media                                                        */

static void lx_media_free(lx_media *m) {
    if (m->memory) xx_io_close(m->memory);
    if (m->chunk) xx_mem_free(m->chunk);
    if (m->packed) xx_mem_free(m->packed);
    xx_mem_zero(m, sizeof(*m));
}

static bool lx_media_open(lx_media *m, xx_io_device *device, lx_info *info) {
    size_t cs;
    xx_mem_zero(m, sizeof(*m));
    m->device = device;
    m->info = info;
    cs = info->chunk_size;
    if (cs == 0U || cs > LX_MAX_CHUNK) return false;
    m->packed_capacity = cs + cs / 8U + 1024U;
    m->chunk = (uint8_t *)xx_mem_alloc(cs);
    m->packed = (uint8_t *)xx_mem_alloc(m->packed_capacity);
    if (!m->chunk || !m->packed) {
        lx_media_free(m);
        return false;
    }
    m->memory = xx_io_mem_open(m->chunk, cs);
    if (!m->memory) {
        lx_media_free(m);
        return false;
    }
    return true;
}

/* Decode chunk @p ci into m->chunk; *produced receives its length. */
static bool lx_decode_entry(lx_media *m, const uint8_t entry[16],
                            size_t capacity, size_t *produced,
                            xx_pd_struct *pd) {
    const lx_layout *l = &m->info->layout;
    uint64_t offset = xx_data_get_u64(entry, 8, 0, false);
    uint32_t size = xx_data_get_u32(entry + 8, 4, 0, false), flags = xx_data_get_u32(entry + 12, 4, 0, false);
    uint64_t limit = (uint64_t)(l->end - l->base);
    *produced = 0U;
    if ((flags & LX_CHUNK_COMPRESSED) && (flags & LX_CHUNK_PATTERN)) {
        size_t i;
        for (i = 0U; i < capacity; ++i) m->chunk[i] = entry[i & 7U];
        *produced = capacity;
        return true;
    }
    if (offset < LX_HEADER || offset > limit || size > limit - offset ||
        size == 0U)
        return false;
    if (flags & LX_CHUNK_COMPRESSED) {
        size_t consumed = 0U, trailer;
        int64_t out;
        if (l->method == 2U || size < 7U || size > m->packed_capacity ||
            !lx_read_at(m->device, l->base + (int64_t)offset, m->packed, size) ||
            !xx_zlib_stream_header_is_valid(m->packed, size) ||
            xx_io_seek64(m->memory, 0, SEEK_SET) != 0)
            return false;
        if (!xx_deflate_unpack_memory_to_device_ex(m->packed + 2, size - 2U,
                                                   m->memory, &consumed, false,
                                                   pd))
            return false;
        out = xx_io_tell(m->memory);
        if (out <= 0 || (uint64_t)out > capacity) return false;
        trailer = 2U + consumed;
        if (trailer > size || size - trailer < 4U) return false;
        if (((uint32_t)m->packed[trailer] << 24U |
             (uint32_t)m->packed[trailer + 1U] << 16U |
             (uint32_t)m->packed[trailer + 2U] << 8U |
             (uint32_t)m->packed[trailer + 3U]) !=
            xx_adler32(m->chunk, (size_t)out))
            return false;
        *produced = (size_t)out;
        return true;
    }
    {
        size_t data = size;
        uint8_t stored[4];
        if (flags & LX_CHUNK_CHECKSUM) {
            if (size < 5U) return false;
            data = size - 4U;
        }
        if (data > capacity ||
            !lx_read_at(m->device, l->base + (int64_t)offset, m->chunk, data))
            return false;
        if (flags & LX_CHUNK_CHECKSUM) {
            if (!lx_read_at(m->device, l->base + (int64_t)offset + (int64_t)data,
                            stored, 4U) ||
                xx_data_get_u32(stored, 4, 0, false) != xx_adler32(m->chunk, data))
                return false;
        }
        *produced = data;
        return true;
    }
}

static bool lx_load_chunk(lx_media *m, uint64_t ci, xx_pd_struct *pd) {
    uint8_t entry[16];
    size_t produced;
    size_t cs = m->info->chunk_size;
    if (m->chunk_valid && m->chunk_index == ci) return true;
    m->chunk_valid = false;
    if (ci >= m->info->chunk_end || !lx_find_chunk(m, ci, entry) ||
        !lx_decode_entry(m, entry, cs, &produced, pd))
        return false;
    /* Every chunk but the final one covers a whole chunk of media. */
    if (produced == 0U || (ci + 1U < m->info->chunk_end && produced != cs))
        return false;
    m->chunk_len = produced;
    m->chunk_index = ci;
    m->chunk_valid = true;
    return true;
}

static bool lx_sink_write(lx_sink *sink, const uint8_t *data, size_t size) {
    size_t done = 0U;
    xx_hash_update(&sink->md5, data, size);
    sink->written += size;
    if (!sink->device) return true;
    while (done < size) {
        ssize_t wrote = xx_io_write(sink->device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static bool lx_media_copy(lx_media *m, uint64_t offset, uint64_t length,
                          lx_sink *sink, xx_pd_struct *pd) {
    uint64_t cs = m->info->chunk_size;
    if (offset > UINT64_MAX - length) return false;
    while (length) {
        uint64_t ci = offset / cs, within = offset % cs, take;
        if (lx_stopped(pd) || !lx_load_chunk(m, ci, pd) ||
            within >= m->chunk_len)
            return false;
        take = m->chunk_len - within;
        if (take > length) take = length;
        if (!lx_sink_write(sink, m->chunk + within, (size_t)take)) return false;
        offset += take;
        length -= take;
    }
    return true;
}

/* Chunk size when the case data does not give it: the decoded size of the
 * first chunk (the whole media if that is the only chunk). */
static uint32_t lx_infer_chunk_size(xx_io_device *device, lx_info *info) {
    lx_media m;
    uint8_t entry[16];
    size_t produced = 0U;
    uint32_t result = 0U;
    lx_info probe = *info;
    probe.chunk_size = LX_MAX_CHUNK;
    if (info->chunk_end == 0U || !lx_media_open(&m, device, &probe)) return 0U;
    if (lx_find_chunk(&m, 0U, entry)) {
        uint32_t flags = xx_data_get_u32(entry + 12, 4, 0, false);
        if (!((flags & LX_CHUNK_COMPRESSED) && (flags & LX_CHUNK_PATTERN)) &&
            lx_decode_entry(&m, entry, LX_MAX_CHUNK, &produced, NULL))
            result = (uint32_t)produced;
    }
    lx_media_free(&m);
    /* Copy back what verification learnt about the tables. */
    info->tables = probe.tables;
    return result;
}

/* ---------------------------------------------------------------------- */
/* ltree                                                                   */

typedef struct lx_columns_s {
    uint32_t p, n, ls, be, opr, du, ha;
} lx_columns;

#define LX_NO_COLUMN 0xFFFFFFFFU

static bool lx_header_line(lx_text *r, uint32_t *children) {
    uint32_t a, b, ca, cb;
    uint64_t v;
    if (!lx_next_line(r, &a, &b) || !lx_column(r->t, a, b, 1U, &ca, &cb) ||
        !lx_parse_u64(r->t, ca, cb, &v) || v > 0xFFFFFFFFU)
        return false;
    *children = (uint32_t)v;
    return true;
}

/* Walk a "<a>\t<children>" / values tree without keeping anything. */
static bool lx_skip_tree(lx_text *r) {
    uint32_t stack[LX_MAX_DEPTH];
    uint32_t depth = 0U, children, a, b;
    if (!lx_header_line(r, &children) || !lx_next_line(r, &a, &b))
        return false;
    stack[depth++] = children;
    while (depth) {
        if (stack[depth - 1U] == 0U) {
            --depth;
            continue;
        }
        --stack[depth - 1U];
        if (!lx_header_line(r, &children) || !lx_next_line(r, &a, &b))
            return false;
        if (children) {
            if (depth >= LX_MAX_DEPTH) return false;
            stack[depth++] = children;
        }
    }
    return true;
}

static bool lx_push_entry(lx_info *info, uint32_t *capacity,
                          const lx_entry *e) {
    if (info->entry_count >= LX_MAX_ENTRIES) return false;
    if (info->entry_count == *capacity) {
        uint32_t grown = *capacity ? *capacity * 2U : 64U;
        lx_entry *next;
        if (grown > LX_MAX_ENTRIES) grown = LX_MAX_ENTRIES;
        next = (lx_entry *)xx_mem_realloc(info->entries,
                                          (size_t)grown * sizeof(*next));
        if (!next) return false;
        info->entries = next;
        *capacity = grown;
    }
    info->entries[info->entry_count++] = *e;
    return true;
}

static bool lx_entry_values(const lx_text *r, const lx_columns *c, uint32_t a,
                            uint32_t b, lx_entry *e) {
    const uint16_t *t = r->t;
    uint32_t ca, cb;
    uint64_t v;
    e->is_dir = c->p != LX_NO_COLUMN && lx_column(t, a, b, c->p, &ca, &cb) &&
                (lx_trim(t, &ca, &cb), lx_range_is(t, ca, cb, "1"));
    if (c->n != LX_NO_COLUMN && lx_column(t, a, b, c->n, &ca, &cb)) {
        e->name_at = ca;
        e->name_len = cb - ca;
    }
    if (c->ls != LX_NO_COLUMN && lx_column(t, a, b, c->ls, &ca, &cb) &&
        lx_parse_u64(t, ca, cb, &v)) {
        e->size = v;
        e->has_size = true;
    }
    if (c->be != LX_NO_COLUMN && lx_column(t, a, b, c->be, &ca, &cb)) {
        e->be_at = ca;
        e->be_len = cb - ca;
    }
    if (c->ha != LX_NO_COLUMN && lx_column(t, a, b, c->ha, &ca, &cb)) {
        e->ha_at = ca;
        e->ha_len = cb - ca;
    }
    if (c->opr != LX_NO_COLUMN && lx_column(t, a, b, c->opr, &ca, &cb) &&
        lx_parse_u64(t, ca, cb, &v))
        e->opr = (uint32_t)v;
    e->du = -1;
    if (c->du != LX_NO_COLUMN && lx_column(t, a, b, c->du, &ca, &cb)) {
        int64_t d;
        if (lx_parse_i64(t, ca, cb, &d) && d >= 0) e->du = d;
    }
    return true;
}

static bool lx_parse_entries(lx_text *r, lx_info *info) {
    const uint16_t *t = r->t;
    uint32_t stack_left[LX_MAX_DEPTH], stack_node[LX_MAX_DEPTH];
    uint32_t depth = 0U, children, a, b, ta, tb, index, capacity = 0U;
    lx_columns c;
    lx_entry e;
    if (info->entries) return false; /* one entry category only */
    /* "<count>\t1" and the type line. */
    if (!lx_next_line(r, &a, &b) || !lx_next_line(r, &ta, &tb)) return false;
    c.p = c.n = c.ls = c.be = c.opr = c.du = c.ha = LX_NO_COLUMN;
    for (index = 0U; index < 1024U; ++index) {
        uint32_t ca, cb;
        if (!lx_column(t, ta, tb, index, &ca, &cb)) break;
        if (lx_range_is(t, ca, cb, "p")) c.p = index;
        else if (lx_range_is(t, ca, cb, "n")) c.n = index;
        else if (lx_range_is(t, ca, cb, "ls")) c.ls = index;
        else if (lx_range_is(t, ca, cb, "be")) c.be = index;
        else if (lx_range_is(t, ca, cb, "opr")) c.opr = index;
        else if (lx_range_is(t, ca, cb, "du")) c.du = index;
        else if (lx_range_is(t, ca, cb, "ha")) c.ha = index;
    }
    if (c.n == LX_NO_COLUMN) return false;
    /* Category root. */
    xx_mem_zero(&e, sizeof(e));
    if (!lx_header_line(r, &children) || !lx_next_line(r, &a, &b)) return false;
    e.parent = LX_NO_COLUMN;
    lx_entry_values(r, &c, a, b, &e);
    e.is_dir = true;
    if (!lx_push_entry(info, &capacity, &e)) return false;
    stack_left[0] = children;
    stack_node[0] = 0U;
    depth = 1U;
    while (depth) {
        if (stack_left[depth - 1U] == 0U) {
            --depth;
            continue;
        }
        --stack_left[depth - 1U];
        xx_mem_zero(&e, sizeof(e));
        if (!lx_header_line(r, &children) || !lx_next_line(r, &a, &b))
            return false;
        e.parent = stack_node[depth - 1U];
        e.depth = depth;
        lx_entry_values(r, &c, a, b, &e);
        if (!lx_push_entry(info, &capacity, &e)) return false;
        if (children) {
            if (depth >= LX_MAX_DEPTH) return false;
            stack_left[depth] = children;
            stack_node[depth] = info->entry_count - 1U;
            ++depth;
        }
    }
    return true;
}

static void lx_skip_blank(lx_text *r) {
    uint32_t save = r->pos, a, b;
    if (lx_next_line(r, &a, &b) && a != b) r->pos = save;
}

static bool lx_parse_ltree(lx_info *info) {
    lx_text r;
    uint32_t a, b, i, category, categories;
    uint64_t v;
    r.t = info->text;
    r.n = info->text_units;
    r.pos = 0U;
    if (r.n && r.t[0] == 0xFEFFU) r.pos = 1U;
    if (!lx_next_line(&r, &a, &b) || !lx_parse_u64(r.t, a, b, &v) || v == 0U ||
        v > LX_MAX_CATEGORIES)
        return false;
    categories = (uint32_t)v;
    for (category = 0U; category < categories; ++category) {
        /* Blank lines between categories. */
        for (i = 0U;; ++i) {
            if (!lx_next_line(&r, &a, &b)) goto end;
            if (a != b) break;
            if (i > 16U) goto end;
        }
        if (lx_range_is(r.t, a, b, "entry")) {
            if (!lx_parse_entries(&r, info)) return false;
        } else if (lx_range_is(r.t, a, b, "rec")) {
            uint32_t ta, tb, va, vb, ca, cb;
            if (!lx_next_line(&r, &ta, &tb) || !lx_next_line(&r, &va, &vb))
                return false;
            for (i = 0U; i < 64U; ++i) {
                if (!lx_column(r.t, ta, tb, i, &ca, &cb)) break;
                if (lx_range_is(r.t, ca, cb, "tb")) {
                    if (lx_column(r.t, va, vb, i, &ca, &cb) &&
                        lx_parse_u64(r.t, ca, cb, &v))
                        info->media_size = v;
                    break;
                }
            }
        } else if (lx_range_is(r.t, a, b, "perm") ||
                   lx_range_is(r.t, a, b, "srce") ||
                   lx_range_is(r.t, a, b, "sub")) {
            uint32_t ta, tb;
            if (!lx_next_line(&r, &a, &b) || !lx_next_line(&r, &ta, &tb) ||
                !lx_skip_tree(&r))
                return false;
        } else {
            /* Unknown category: skip to its closing blank line. */
            while (lx_next_line(&r, &a, &b) && a != b) {
            }
            continue;
        }
        lx_skip_blank(&r);
    }
end:
    if (!info->entries) return false;
    /* The records: files below the root. */
    {
        uint32_t count = 0U, k;
        for (k = 1U; k < info->entry_count; ++k)
            if (!info->entries[k].is_dir) ++count;
        info->files = (uint32_t *)xx_mem_alloc(
            (size_t)(count ? count : 1U) * sizeof(uint32_t));
        if (!info->files) return false;
        for (k = 1U; k < info->entry_count; ++k)
            if (!info->entries[k].is_dir) info->files[info->file_count++] = k;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Whole-file parse                                                        */

static void lx_info_free(lx_info *info) {
    lx_layout_free(&info->layout);
    if (info->tables) xx_mem_free(info->tables);
    if (info->text) xx_mem_free(info->text);
    if (info->entries) xx_mem_free(info->entries);
    if (info->files) xx_mem_free(info->files);
    xx_mem_zero(info, sizeof(*info));
}

static bool lx_load_text(xx_io_device *device, lx_info *info,
                         const lx_section *s) {
    uint64_t payload = s->size - s->padding;
    uint8_t *raw;
    uint32_t units, i;
    if (payload < 2U || payload > LX_MAX_LTREE) return false;
    units = (uint32_t)(payload / 2U);
    raw = (uint8_t *)xx_mem_alloc((size_t)units * 2U);
    if (!raw) return false;
    if (!lx_read_at(device, s->data, raw, (size_t)units * 2U)) {
        xx_mem_free(raw);
        return false;
    }
    /* Converted in place: a u16 array over the same bytes. */
    info->text = (uint16_t *)raw;
    for (i = 0U; i < units; ++i)
        info->text[i] = (uint16_t)(raw[i * 2U] | (raw[i * 2U + 1U] << 8U));
    info->text_units = units;
    return true;
}

static bool lx_load(Abstractformat *format, lx_info *info, xx_pd_struct *pd) {
    xx_io_device *device = format->device;
    const lx_section *single = NULL;
    uint64_t spc = 0U, bps = 0U;
    bool have_spc = false, have_bps = false;
    uint32_t i;
    xx_mem_zero(info, sizeof(*info));
    if (!lx_parse_layout(format, &info->layout, pd)) return false;
    for (i = 0U; i < info->layout.count; ++i) {
        const lx_section *s = &info->layout.sections[i];
        if (s->flags & LX_FLAG_ENCRYPTED) info->encrypted = true;
        switch (s->type) {
        case LX_TYPE_CASE:
        case LX_TYPE_DEVICE:
            if ((s->type == LX_TYPE_CASE && !have_spc) ||
                (s->type == LX_TYPE_DEVICE && !have_bps)) {
                uint32_t units;
                uint16_t *t = lx_read_object_string(device, &info->layout, s,
                                                    &units);
                if (t) {
                    if (s->type == LX_TYPE_CASE)
                        have_spc = lx_object_value(t, units, "sb", &spc);
                    else
                        have_bps = lx_object_value(t, units, "bp", &bps);
                    xx_mem_free(t);
                }
            }
            break;
        case LX_TYPE_TABLE:
            if (!lx_add_table(device, info, s)) goto fail;
            break;
        case LX_TYPE_KEYS:
            info->encrypted = true;
            break;
        case LX_TYPE_SINGLE_FILES:
            if (!single) single = s;
            break;
        default:
            break;
        }
    }
    if (!have_bps || bps == 0U || bps > 65536U) bps = 512U;
    info->bytes_per_sector = (uint32_t)bps;
    if (have_spc && spc != 0U && spc <= LX_MAX_CHUNK / bps) {
        info->sectors_per_chunk = (uint32_t)spc;
        info->chunk_size = (uint32_t)(spc * bps);
    } else if (!info->encrypted) {
        info->chunk_size = lx_infer_chunk_size(device, info);
        info->sectors_per_chunk = info->chunk_size / info->bytes_per_sector;
    }
    if (single && !info->encrypted &&
        (single->flags & LX_FLAG_ENCRYPTED) == 0U) {
        if (!lx_load_text(device, info, single) || !lx_parse_ltree(info))
            goto fail;
        info->has_single_files = true;
    }
    return true;
fail:
    lx_info_free(info);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static size_t lx_put_escape(char *out, uint32_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0FU];
    out[2] = digits[c & 0x0FU];
    return 3U;
}

/* One entry name as UTF-8; characters a path cannot hold become %XX.
 * Returns false when it does not fit. */
static bool lx_append_name(const lx_info *info, const lx_entry *e, char *out,
                           size_t *at) {
    const uint16_t *t = info->text;
    uint32_t i = 0U;
    while (i < e->name_len) {
        uint32_t c = t[e->name_at + i++];
        char buffer[4];
        size_t length = 0U, k;
        if (c >= 0xD800U && c <= 0xDBFFU && i < e->name_len &&
            t[e->name_at + i] >= 0xDC00U && t[e->name_at + i] <= 0xDFFFU) {
            c = 0x10000U + ((c - 0xD800U) << 10U) +
                (uint32_t)(t[e->name_at + i] - 0xDC00U);
            ++i;
        } else if (c >= 0xD800U && c <= 0xDFFFU) {
            c = 0xFFFDU;
        }
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || c == '%') {
            if (*at + 3U >= LX_PATH_MAX) return false;
            *at += lx_put_escape(out + *at, c);
            continue;
        }
        if (c < 0x80U) {
            buffer[length++] = (char)c;
        } else if (c < 0x800U) {
            buffer[length++] = (char)(0xC0U | (c >> 6U));
            buffer[length++] = (char)(0x80U | (c & 0x3FU));
        } else if (c < 0x10000U) {
            buffer[length++] = (char)(0xE0U | (c >> 12U));
            buffer[length++] = (char)(0x80U | ((c >> 6U) & 0x3FU));
            buffer[length++] = (char)(0x80U | (c & 0x3FU));
        } else {
            buffer[length++] = (char)(0xF0U | (c >> 18U));
            buffer[length++] = (char)(0x80U | ((c >> 12U) & 0x3FU));
            buffer[length++] = (char)(0x80U | ((c >> 6U) & 0x3FU));
            buffer[length++] = (char)(0x80U | (c & 0x3FU));
        }
        if (*at + length >= LX_PATH_MAX) return false;
        for (k = 0U; k < length; ++k) out[(*at)++] = buffer[k];
    }
    return true;
}

/* '/' joined names from the first level below the root to @p index. */
static bool lx_build_path(const lx_info *info, uint32_t index, char *out,
                          size_t *length) {
    uint32_t chain[LX_MAX_DEPTH + 1U];
    uint32_t depth = 0U, node = index;
    size_t at = 0U;
    out[0] = 0;
    *length = 0U;
    while (node != 0U && node < info->entry_count && depth <= LX_MAX_DEPTH) {
        chain[depth++] = node;
        node = info->entries[node].parent;
    }
    if (node != 0U) return false;
    while (depth) {
        --depth;
        if (!lx_append_name(info, &info->entries[chain[depth]], out, &at))
            return false;
        if (depth) {
            if (at + 1U >= LX_PATH_MAX) return false;
            out[at++] = '/';
        }
    }
    out[at] = 0;
    *length = at;
    return true;
}

static uint64_t lx_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* "name%_N.ext" for the N-th record whose path repeats an earlier one;
 * '%' never appears unescaped in a converted name, so this cannot clash. */
static void lx_insert_suffix(char *name, size_t length, uint32_t index) {
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= LX_PATH_MAX) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool lx_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

/* Relative, no empty / "." / ".." / trailing dot or space components, no
 * device names, no characters Windows refuses. */
static bool lx_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7fU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                lx_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

typedef struct lx_key_s {
    uint64_t hash;
    uint32_t index;
} lx_key;

static int lx_compare_keys(const void *left, const void *right) {
    const lx_key *a = (const lx_key *)left;
    const lx_key *b = (const lx_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool lx_mark_duplicates(lx_stream *s) {
    lx_key *keys;
    uint32_t index;
    if (s->info.file_count < 2U) return true;
    keys = (lx_key *)xx_mem_alloc((size_t)s->info.file_count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < s->info.file_count; ++index) {
        size_t length = 0U;
        if (!lx_build_path(&s->info, s->info.files[index], s->name, &length))
            length = 0U;
        keys[index].hash = lx_name_hash(s->name, length);
        keys[index].index = index;
    }
    xx_rt_qsort(keys, s->info.file_count, sizeof(*keys), lx_compare_keys);
    for (index = 1U; index < s->info.file_count; ++index)
        if (keys[index].hash == keys[index - 1U].hash)
            s->renamed[keys[index].index] = 1U;
    xx_mem_free(keys);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member data                                                             */

typedef struct lx_extents_s {
    const uint16_t *t;
    uint32_t pos, end;
} lx_extents;

static bool lx_token(lx_extents *x, uint32_t *a, uint32_t *b) {
    while (x->pos < x->end && x->t[x->pos] == ' ') ++x->pos;
    if (x->pos >= x->end) return false;
    *a = x->pos;
    while (x->pos < x->end && x->t[x->pos] != ' ') ++x->pos;
    *b = x->pos;
    return true;
}

/* Walk the "be" value. With @p m, copy every extent to @p sink. */
static bool lx_walk_extents(const lx_info *info, const lx_entry *e,
                            uint64_t *count_out, uint64_t *sum_out,
                            uint64_t *first_out, lx_media *m, lx_sink *sink,
                            xx_pd_struct *pd) {
    lx_extents x;
    uint32_t a, b;
    uint64_t count, k, sum = 0U;
    x.t = info->text;
    x.pos = e->be_at;
    x.end = e->be_at + e->be_len;
    *count_out = 0U;
    *sum_out = 0U;
    *first_out = 0U;
    if (!lx_token(&x, &a, &b)) return true; /* no extents at all */
    if (!lx_parse_hex(x.t, a, b, &count) || count > LX_MAX_EXTENTS)
        return false;
    for (k = 0U; k < count; ++k) {
        uint64_t offset, size;
        if (!lx_token(&x, &a, &b)) return false;
        if (b - a == 1U && x.t[a] == 'S' && !lx_token(&x, &a, &b)) return false;
        if (!lx_parse_hex(x.t, a, b, &offset) || !lx_token(&x, &a, &b) ||
            !lx_parse_hex(x.t, a, b, &size) || sum > UINT64_MAX - size)
            return false;
        if (k == 0U) *first_out = offset;
        sum += size;
        if (m && !lx_media_copy(m, offset, size, sink, pd)) return false;
    }
    if (lx_token(&x, &a, &b)) return false;
    *count_out = count;
    *sum_out = sum;
    return true;
}

static bool lx_hex_digest(const uint16_t *t, uint32_t a, uint32_t length,
                          uint8_t digest[16]) {
    uint32_t i;
    bool nonzero = false;
    if (length != 32U) return false;
    for (i = 0U; i < 16U; ++i) {
        uint64_t v;
        if (!lx_parse_hex(t, a + i * 2U, a + i * 2U + 2U, &v)) return false;
        digest[i] = (uint8_t)v;
        if (v) nonzero = true;
    }
    return nonzero;
}

static bool lx_fill(lx_sink *sink, uint8_t value, uint64_t size,
                    xx_pd_struct *pd) {
    uint8_t block[4096];
    xx_rt_memset(block, value, sizeof(block));
    while (size) {
        size_t take = size > sizeof(block) ? sizeof(block) : (size_t)size;
        if (lx_stopped(pd) || !lx_sink_write(sink, block, take)) return false;
        size -= take;
    }
    return true;
}

static bool lx_unpack_member(lx_stream *s, uint32_t index,
                             xx_io_device *destination, xx_pd_struct *pd) {
    const lx_info *info = &s->info;
    const lx_entry *e = &info->entries[index];
    uint64_t count, sum, first, size;
    uint8_t expected[16], actual[16];
    bool sparse = (e->opr & LX_OPR_SPARSE) != 0U;
    lx_sink sink;
    if (info->encrypted || !lx_walk_extents(info, e, &count, &sum, &first, NULL,
                                            NULL, pd))
        return false;
    size = e->has_size ? e->size : sum;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    if (!xx_hash_init(&sink.md5, XX_HASH_MD5)) return false;
    if (size != 0U) {
        if (!sparse) {
            if (sum != size ||
                !lx_walk_extents(info, e, &count, &sum, &first, &s->media,
                                 &sink, pd))
                return false;
        } else {
            if (count == 0U || (sum != 1U && sum != size)) return false;
            if (e->du >= 0) {
                if (!lx_media_copy(&s->media, (uint64_t)e->du, size, &sink, pd))
                    return false;
            } else {
                lx_sink probe;
                uint8_t value;
                xx_mem_zero(&probe, sizeof(probe));
                if (!xx_hash_init(&probe.md5, XX_HASH_MD5) ||
                    !lx_media_copy(&s->media, first, 1U, &probe, pd) ||
                    !lx_load_chunk(&s->media, first / info->chunk_size, pd))
                    return false;
                value = s->media.chunk[first % info->chunk_size];
                if (!lx_fill(&sink, value, size, pd)) return false;
            }
        }
    }
    if (sink.written != size || !xx_hash_final(&sink.md5, actual, 16U))
        return false;
    if (e->ha_len && lx_hex_digest(info->text, e->ha_at, e->ha_len, expected) &&
        xx_rt_memcmp(expected, actual, 16U) != 0)
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void lx_stream_free(void *opaque) {
    lx_stream *s = (lx_stream *)opaque;
    if (!s) return;
    lx_media_free(&s->media);
    lx_info_free(&s->info);
    if (s->renamed) xx_mem_free(s->renamed);
    if (s->name) xx_mem_free(s->name);
    xx_mem_free(s);
}

static bool lx_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *lx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lx_set_record(Abstractformat *format, xx_archive_record *record,
                          lx_stream *s, size_t at) {
    const lx_entry *e = &s->info.entries[s->info.files[at]];
    uint64_t count = 0U, sum = 0U, first = 0U, size;
    size_t length = 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    s->name_ok = lx_build_path(&s->info, s->info.files[at], s->name, &length);
    if (!s->name_ok) {
        /* Too long or too deep: listed under a placeholder, never written. */
        (void)xx_rt_snprintf(s->name, LX_PATH_MAX, "%%_entry%u",
                             (unsigned)s->info.files[at]);
        length = xx_str_len(s->name);
    } else if (s->renamed[at]) {
        lx_insert_suffix(s->name, length, (uint32_t)at);
    }
    (void)lx_walk_extents(&s->info, e, &count, &sum, &first, NULL, NULL, NULL);
    size = e->has_size ? e->size : sum;
    record->header_offset = format->base_address;
    record->header_size = LX_HEADER;
    record->data_offset = format->base_address;
    record->compressed_size = (int64_t)(sum > (uint64_t)INT64_MAX ? 0U : sum);
    return xx_archive_record_set_original_name(record, s->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          sum) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          s->info.layout.method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           s->info.encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_ewf2_lx01_init(xx_ewf2_lx01 *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EWF2_LX01_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "Lx01");
    archive->format.check_is_valid = xx_ewf2_lx01_check_is_valid;
    archive->format.handle_base_info = xx_ewf2_lx01_handle_base_info;
    archive->format.get_format_size = xx_ewf2_lx01_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ewf2_lx01_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ewf2_lx01_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ewf2_lx01_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ewf2_lx01_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ewf2_lx01_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ewf2_lx01_free_archive_records_reading;
}

xx_ewf2_lx01 *xx_ewf2_lx01_create(xx_io_device *device, int64_t base_address) {
    xx_ewf2_lx01 *archive = (xx_ewf2_lx01 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ewf2_lx01_init(archive, device, base_address);
    return archive;
}

void xx_ewf2_lx01_destroy(xx_ewf2_lx01 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ewf2_lx01_free(xx_ewf2_lx01 *archive) {
    if (!archive) return;
    xx_ewf2_lx01_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ewf2_lx01_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lx_layout layout;
    bool result = lx_parse_layout(format, &layout, pd);
    lx_layout_free(&layout);
    return result;
}

bool xx_ewf2_lx01_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lx_info info;
    xx_ewf2_lx01 *archive;
    if (!format || !lx_load(format, &info, pd)) return false;
    archive = (xx_ewf2_lx01 *)format;
    archive->number_of_records = info.encrypted ? 0U : info.file_count;
    archive->number_of_entries = info.entry_count;
    archive->number_of_chunks = info.chunk_total;
    archive->media_size = info.media_size;
    archive->segment_number = info.layout.segment;
    archive->number_of_sections = info.layout.count;
    archive->sectors_per_chunk = info.sectors_per_chunk;
    archive->bytes_per_sector = info.bytes_per_sector;
    archive->chunk_size = info.chunk_size;
    archive->compression_method = info.layout.method;
    archive->is_last_segment = info.layout.done;
    archive->is_encrypted = info.encrypted;
    archive->has_single_files = info.has_single_files;
    format->version[0] = '2';
    format->version[1] = '.';
    format->version[2] = '1';
    format->version[3] = 0;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = info.layout.end - info.layout.base;
    format->is_valid = true;
    format->base_info_handled = true;
    lx_info_free(&info);
    return true;
}

int64_t xx_ewf2_lx01_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ewf2_lx01_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ewf2_lx01_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ewf2_lx01_handle_base_info(format, pd))
               ? ((xx_ewf2_lx01 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ewf2_lx01_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lx_stream *s;
    xx_archive_record_state *state;
    if (!format) return NULL;
    s = (lx_stream *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return NULL;
    if (!lx_load(format, &s->info, pd)) {
        xx_mem_free(s);
        return NULL;
    }
    if (s->info.encrypted) s->info.file_count = 0U;
    s->name = (char *)xx_mem_alloc(LX_PATH_MAX + 16U);
    s->renamed = (uint8_t *)xx_mem_calloc(
        s->info.file_count ? s->info.file_count : 1U, 1U);
    if (!s->name || !s->renamed ||
        (s->info.file_count != 0U &&
         (!lx_media_open(&s->media, format->device, &s->info) ||
          !lx_mark_duplicates(s)))) {
        lx_stream_free(s);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lx_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = s;
    state->free_internal = lx_stream_free;
    state->total_records = s->info.file_count;
    if (!lx_copy_options(&state->options, options) ||
        (s->info.file_count != 0U &&
         !lx_set_record(format, &state->current_record, s, 0U))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = s->info.file_count != 0U;
    return state;
}

const xx_archive_record *xx_ewf2_lx01_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ewf2_lx01_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    lx_stream *s;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(s = (lx_stream *)state->internal_state) ||
        ++s->at >= s->info.file_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lx_set_record(format, &state->current_record, s, s->at);
    return state->has_record;
}

bool xx_ewf2_lx01_unpack_current_archive_record(Abstractformat *format,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    lx_stream *s;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    uint32_t index;
    if (!format || !state || state->format != format || !state->has_record ||
        !(s = (lx_stream *)state->internal_state) ||
        s->at >= s->info.file_count || lx_stopped(pd))
        return false;
    index = s->info.files[s->at];
    path_option = lx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return lx_unpack_member(s, index, NULL, pd);
    if (!s->name_ok || !lx_safe_name(s->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", s->name)
               : xx_str_concat(base, s->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = lx_unpack_member(s, index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ewf2_lx01_free_archive_records_reading(Abstractformat *format,
                                               xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

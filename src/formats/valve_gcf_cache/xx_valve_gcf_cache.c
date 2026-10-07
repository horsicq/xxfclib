/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Valve Steam game cache (.gcf) reader.  xx_valve_gcf_cache.h carries the
 * table layout.  Written from the published structure description of the
 * format (the table sequence also used by HLLib/GCFScape); no code from
 * those projects is used.
 *
 * Every table is range-checked against the device before it is read; its
 * size is bounded by the counts, which must themselves fit in the file, so
 * allocations never exceed the input size.  Every chain walk (parents,
 * block entries, data blocks, block entry map) is step-bounded, so a cyclic
 * or dangling link ends the walk with an error instead of spinning.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/valve_gcf_cache/xx_valve_gcf_cache.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef VALVE_GCF_CACHE
#define XX_VALVE_GCF_CACHE_FILE_TYPE XX_FILE_TYPE_VALVE_GCF_CACHE
#else
#define XX_VALVE_GCF_CACHE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GCF_HEADER_SIZE 44
#define GCF_BLOCK_ENTRY_HEADER_SIZE 32
#define GCF_BLOCK_ENTRY_SIZE 28
#define GCF_FRAG_HEADER_SIZE 16
#define GCF_BEM_HEADER_SIZE 20
#define GCF_BEM_ENTRY_SIZE 8
#define GCF_DIR_HEADER_SIZE 56
#define GCF_DIR_ENTRY_SIZE 28
#define GCF_DIR_MAP_HEADER_SIZE 8
#define GCF_CHECKSUM_HEADER_SIZE 8

#define GCF_FLAG_FILE 0x00004000U
#define GCF_FLAG_ENCRYPTED 0x00000100U
#define GCF_NONE 0xFFFFFFFFU

#define GCF_MIN_BLOCK_SIZE 0x200U
#define GCF_MAX_BLOCK_SIZE 0x100000U
#define GCF_MAX_BLOCKS 0x1000000U
#define GCF_MAX_ITEMS 0x1000000U
#define GCF_MAX_DIRECTORY 0x40000000U
#define GCF_MAX_DEPTH 512U
#define GCF_MAX_PATH 4096U

typedef struct gcf_layout_s {
    uint32_t minor;
    uint32_t file_size;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t item_count;
    uint32_t name_size;
    int64_t entries_offset;
    int64_t frag_offset;
    int64_t bem_offset;      /**< Block entry map header; minor < 6. */
    uint32_t bem_first;
    int64_t dir_offset;
    int64_t dir_needed;      /**< Header + all directory tables. */
    int64_t map_offset;      /**< First directory map entry, or -1. */
    int64_t data_offset;     /**< First data block, relative to base. */
    int64_t format_size;
} gcf_layout;

typedef struct gcf_member_s {
    char *name;
    uint32_t item;
    uint32_t first_entry;
    uint32_t flags;
    int64_t size;
    bool folder;
    bool unsafe;
    bool incomplete;
} gcf_member;

typedef struct gcf_stream_s {
    gcf_layout layout;
    gcf_member *items;
    size_t count;
    size_t index;
    uint8_t *entries;       /**< block_count x 28 bytes. */
    uint8_t *frag;          /**< block_count x 4 bytes. */
    uint32_t *visit;        /**< Per data block: epoch of its last use. */
    uint32_t epoch;
    uint64_t encrypted;
    uint64_t incomplete;
} gcf_stream;

static void xx_valve_gcf_cache_vtable_destroy(Abstractformat *self);

static bool gcf_within(int64_t span, int64_t offset, uint64_t size) {
    return offset >= 0 && offset <= span && size <= (uint64_t)(span - offset);
}

static bool gcf_read_abs(xx_io_device *dev, int64_t offset, uint8_t *out,
                         size_t size) {
    size_t done = 0U;
    if (!dev || offset < 0 || xx_io_seek64(dev, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(dev, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool gcf_read(Abstractformat *self, int64_t span, int64_t offset,
                     uint8_t *out, size_t size) {
    return gcf_within(span, offset, size) &&
           gcf_read_abs(self->device, self->base_address + offset, out, size);
}

static uint8_t *gcf_read_table(Abstractformat *self, int64_t span,
                               int64_t offset, uint64_t size) {
    uint8_t *buffer;
    if (!gcf_within(span, offset, size) || size > (uint64_t)SIZE_MAX - 1U)
        return NULL;
    buffer = (uint8_t *)xx_mem_alloc(size ? (size_t)size : 1U);
    if (!buffer) return NULL;
    if (size && !gcf_read_abs(self->device, self->base_address + offset,
                              buffer, (size_t)size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    return buffer;
}

/* The data block header: minor >= 5 starts with "last version played". */
static bool gcf_try_data_header(Abstractformat *self, int64_t span,
                                gcf_layout *l, int64_t at) {
    uint8_t h[24];
    size_t size = l->minor >= 5U ? 24U : 20U;
    const uint8_t *p = l->minor >= 5U ? h + 4 : h;
    uint32_t first;
    if (!gcf_read(self, span, at, h, size)) return false;
    if (xx_data_get_u32(p, 4, 0, false) != l->block_count || xx_data_get_u32(p + 4, 4, 0, false) != l->block_size ||
        xx_data_get_u32(p + 12, 4, 0, false) > l->block_count)
        return false;
    first = xx_data_get_u32(p + 8, 4, 0, false);
    if ((int64_t)first < at + (int64_t)size) return false;
    if (first > span) return false;
    l->data_offset = first;
    return true;
}

/* Locates the directory map, checksum section and data block header.  The
 * directory's own size field and the sum of its tables normally agree; both
 * are tried, with and without the directory map header, and a candidate is
 * accepted only when the data block header behind it repeats the block
 * count and block size of the file header. */
static bool gcf_locate_tail(Abstractformat *self, int64_t span, gcf_layout *l,
                            uint32_t directory_size) {
    int64_t ends[3];
    uint64_t map_size = (uint64_t)l->item_count * 4U;
    int e, m;
    ends[0] = l->dir_offset + l->dir_needed;
    ends[1] = l->dir_offset + (int64_t)directory_size;
    ends[2] = l->dir_offset + GCF_DIR_HEADER_SIZE + (int64_t)directory_size;
    for (e = 0; e < 3; ++e) {
        if (e && ends[e] == ends[0]) continue;
        if (ends[e] < l->dir_offset + l->dir_needed) continue;
        for (m = 0; m < 2; ++m) {
            /* m == 0: the layout of this version; m == 1: the other one. */
            bool with_header = (l->minor >= 5U) == (m == 0);
            int64_t map = ends[e] + (with_header ? GCF_DIR_MAP_HEADER_SIZE : 0);
            int64_t checksum = map + (int64_t)map_size;
            uint8_t ch[GCF_CHECKSUM_HEADER_SIZE];
            int64_t data_header;
            if (!gcf_within(span, ends[e], (uint64_t)(checksum - ends[e]))) continue;
            if (!gcf_read(self, span, checksum, ch, sizeof(ch))) continue;
            data_header = checksum + GCF_CHECKSUM_HEADER_SIZE + (int64_t)xx_data_get_u32(ch + 4, 4, 0, false);
            if (data_header > span) continue;
            if (gcf_try_data_header(self, span, l, data_header)) {
                l->map_offset = map;
                return true;
            }
        }
    }
    return false;
}

static bool gcf_layout_read(Abstractformat *self, int64_t span, gcf_layout *l) {
    uint8_t h[GCF_HEADER_SIZE], b[GCF_BLOCK_ENTRY_HEADER_SIZE];
    uint8_t f[GCF_FRAG_HEADER_SIZE], d[GCF_DIR_HEADER_SIZE];
    uint64_t needed;
    int64_t at;
    uint32_t directory_size, info1, copies, locals, files;

    xx_rt_memset(l, 0, sizeof(*l));
    if (!gcf_read(self, span, 0, h, sizeof(h))) return false;
    l->minor = xx_data_get_u32(h + 8, 4, 0, false);
    if (xx_data_get_u32(h, 4, 0, false) != 1U || xx_data_get_u32(h + 4, 4, 0, false) != 1U ||
        (l->minor != 3U && l->minor != 5U && l->minor != 6U))
        return false;
    l->file_size = xx_data_get_u32(h + 28, 4, 0, false);
    l->block_size = xx_data_get_u32(h + 32, 4, 0, false);
    l->block_count = xx_data_get_u32(h + 36, 4, 0, false);
    if (l->block_size < GCF_MIN_BLOCK_SIZE || l->block_size > GCF_MAX_BLOCK_SIZE ||
        l->block_count > GCF_MAX_BLOCKS)
        return false;

    if (!gcf_read(self, span, GCF_HEADER_SIZE, b, sizeof(b)) ||
        xx_data_get_u32(b, 4, 0, false) != l->block_count || xx_data_get_u32(b + 4, 4, 0, false) > l->block_count)
        return false;
    l->entries_offset = GCF_HEADER_SIZE + GCF_BLOCK_ENTRY_HEADER_SIZE;
    at = l->entries_offset + (int64_t)l->block_count * GCF_BLOCK_ENTRY_SIZE;
    if (!gcf_read(self, span, at, f, sizeof(f)) || xx_data_get_u32(f, 4, 0, false) != l->block_count)
        return false;
    l->frag_offset = at + GCF_FRAG_HEADER_SIZE;
    at = l->frag_offset + (int64_t)l->block_count * 4;
    l->bem_offset = -1;
    if (l->minor < 6U) {
        uint8_t m[GCF_BEM_HEADER_SIZE];
        if (!gcf_read(self, span, at, m, sizeof(m)) ||
            xx_data_get_u32(m, 4, 0, false) != l->block_count)
            return false;
        l->bem_first = xx_data_get_u32(m + 4, 4, 0, false);
        l->bem_offset = at + GCF_BEM_HEADER_SIZE;
        at = l->bem_offset + (int64_t)l->block_count * GCF_BEM_ENTRY_SIZE;
    }

    l->dir_offset = at;
    if (!gcf_read(self, span, at, d, sizeof(d))) return false;
    l->item_count = xx_data_get_u32(d + 12, 4, 0, false);
    files = xx_data_get_u32(d + 16, 4, 0, false);
    directory_size = xx_data_get_u32(d + 24, 4, 0, false);
    l->name_size = xx_data_get_u32(d + 28, 4, 0, false);
    info1 = xx_data_get_u32(d + 32, 4, 0, false);
    copies = xx_data_get_u32(d + 36, 4, 0, false);
    locals = xx_data_get_u32(d + 40, 4, 0, false);
    if (l->item_count == 0U || l->item_count > GCF_MAX_ITEMS ||
        files > l->item_count || info1 > GCF_MAX_ITEMS ||
        copies > l->item_count || locals > l->item_count ||
        directory_size > GCF_MAX_DIRECTORY || l->name_size > GCF_MAX_DIRECTORY)
        return false;
    needed = GCF_DIR_HEADER_SIZE + (uint64_t)l->item_count * GCF_DIR_ENTRY_SIZE +
             l->name_size +
             4U * ((uint64_t)info1 + l->item_count + copies + locals);
    if (needed > GCF_MAX_DIRECTORY || !gcf_within(span, at, needed)) return false;
    l->dir_needed = (int64_t)needed;
    if (!gcf_locate_tail(self, span, l, directory_size)) return false;
    if (l->minor >= 6U && l->map_offset < l->dir_offset + l->dir_needed + GCF_DIR_MAP_HEADER_SIZE)
        return false;

    {
        int64_t data_end = l->data_offset + (int64_t)l->block_count * l->block_size;
        int64_t size = (int64_t)l->file_size;
        if (size < l->data_offset) size = data_end;
        if (size > span) size = span;
        l->format_size = size;
    }
    return true;
}

/* ------------------------------------------------------------ names ---- */

static bool gcf_component_safe(const char *s, size_t len) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    char stem[8];
    size_t i, stem_len = 0U;
    if (!len || (len == 1U && s[0] == '.') ||
        (len == 2U && s[0] == '.' && s[1] == '.') ||
        s[len - 1U] == '.' || s[len - 1U] == ' ')
        return false;
    for (i = 0U; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32U || c == 127U || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == '/' ||
            c == '\\')
            return false;
    }
    while (stem_len < len && s[stem_len] != '.' && s[stem_len] != ' ' &&
           stem_len < sizeof(stem) - 1U) {
        char c = s[stem_len];
        stem[stem_len++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_len] = '\0';
    if (stem_len < len && s[stem_len] != '.' && s[stem_len] != ' ')
        return true; /* stem longer than any device name */
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (!xx_rt_strcmp(stem, devices[i])) return false;
    if (stem_len == 4U &&
        (!xx_rt_strncmp(stem, "COM", 3U) || !xx_rt_strncmp(stem, "LPT", 3U)) &&
        stem[3] >= '0' && stem[3] <= '9')
        return false;
    return true;
}

static uint32_t gcf_hash(const char *s) {
    uint32_t h = 2166136261U;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        h = (h ^ c) * 16777619U;
    }
    return h;
}

static bool gcf_names_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == *b;
}

typedef struct gcf_names_s {
    uint32_t *slots;  /**< member index + 1; 0 = empty. */
    size_t mask;
} gcf_names;

static bool gcf_names_find(const gcf_names *t, const gcf_member *items,
                           const char *name) {
    size_t i = gcf_hash(name) & t->mask;
    while (t->slots[i]) {
        if (gcf_names_equal(items[t->slots[i] - 1U].name, name)) return true;
        i = (i + 1U) & t->mask;
    }
    return false;
}

static void gcf_names_insert(gcf_names *t, const gcf_member *items, size_t index) {
    size_t i = gcf_hash(items[index].name) & t->mask;
    while (t->slots[i]) i = (i + 1U) & t->mask;
    t->slots[i] = (uint32_t)(index + 1U);
}

/* ----------------------------------------------------------- parsing --- */

static void gcf_stream_free(void *pointer) {
    gcf_stream *s = (gcf_stream *)pointer;
    size_t i;
    if (!s) return;
    for (i = 0U; i < s->count; ++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items);
    xx_mem_free(s->entries);
    xx_mem_free(s->frag);
    xx_mem_free(s->visit);
    xx_mem_free(s);
}

/* Builds "a/b/c" for item `item` by walking parents up to the root (item
 * 0).  Returns NULL on a broken or cyclic chain. */
static char *gcf_build_path(const uint8_t *dir_entries, const char *names,
                            uint32_t name_size, uint32_t items, uint32_t item,
                            bool *unsafe) {
    char buffer[GCF_MAX_PATH];
    size_t pos = sizeof(buffer) - 1U;
    uint32_t cur = item, depth = 0U;
    buffer[pos] = '\0';
    *unsafe = false;
    while (cur != 0U) {
        const uint8_t *e;
        uint32_t off;
        size_t len = 0U;
        if (cur >= items || ++depth > GCF_MAX_DEPTH) return NULL;
        e = dir_entries + (size_t)cur * GCF_DIR_ENTRY_SIZE;
        off = xx_data_get_u32(e, 4, 0, false);
        if (off >= name_size) return NULL;
        while (off + len < name_size && names[off + len]) ++len;
        if (off + len >= name_size) return NULL; /* unterminated */
        if (!gcf_component_safe(names + off, len)) *unsafe = true;
        if (pos != sizeof(buffer) - 1U) {
            if (pos < 1U) return NULL;
            buffer[--pos] = '/';
        }
        if (pos < len + 1U) return NULL;
        pos -= len;
        xx_rt_memcpy(buffer + pos, names + off, len);
        cur = xx_data_get_u32(e + 16, 4, 0, false);
        if (cur == GCF_NONE) return NULL;
    }
    if (pos == sizeof(buffer) - 1U) return NULL;
    {
        size_t i;
        for (i = pos; buffer[i]; ++i) {
            unsigned char c = (unsigned char)buffer[i];
            if (c < 32U || c == 127U) buffer[i] = '_';
        }
    }
    return xx_str_dup(buffer + pos);
}

static bool gcf_add_member(gcf_stream *s, gcf_names *table, gcf_member *m) {
    char *original = m->name;
    unsigned suffix = 1U;
    while (gcf_names_find(table, s->items, m->name)) {
        char tail[24];
        char *next;
        if (suffix >= 1000000U) return false;
        xx_rt_snprintf(tail, sizeof(tail), "__%u", ++suffix);
        next = xx_str_concat(original, tail);
        if (!next) return false;
        if (m->name != original) xx_str_free(m->name);
        m->name = next;
    }
    if (m->name != original) xx_str_free(original);
    s->items[s->count] = *m;
    gcf_names_insert(table, s->items, s->count);
    ++s->count;
    return true;
}

static gcf_stream *gcf_parse(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total, span;
    gcf_stream *s;
    gcf_layout *l;
    uint8_t *dir = NULL;
    uint32_t *first = NULL;
    gcf_names table = {NULL, 0U};
    uint32_t i;
    uint64_t budget;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    s = (gcf_stream *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_mem_zero(s, sizeof(*s));
    l = &s->layout;
    if (!gcf_layout_read(self, span, l)) goto fail;

    s->entries = gcf_read_table(self, span, l->entries_offset,
                                (uint64_t)l->block_count * GCF_BLOCK_ENTRY_SIZE);
    s->frag = gcf_read_table(self, span, l->frag_offset,
                             (uint64_t)l->block_count * 4U);
    dir = gcf_read_table(self, span, l->dir_offset, (uint64_t)l->dir_needed);
    first = (uint32_t *)xx_mem_alloc((size_t)l->item_count * sizeof(uint32_t));
    if (!s->entries || !s->frag || !dir || !first) goto fail;
    for (i = 0U; i < l->item_count; ++i) first[i] = GCF_NONE;

    if (l->minor >= 6U) {
        uint8_t *map = gcf_read_table(self, span, l->map_offset,
                                      (uint64_t)l->item_count * 4U);
        if (!map) goto fail;
        for (i = 0U; i < l->item_count; ++i) {
            uint32_t v = xx_data_get_u32(map + (size_t)i * 4U, 4, 0, false);
            first[i] = v < l->block_count ? v : GCF_NONE;
        }
        xx_mem_free(map);
    } else {
        /* Older caches: the block entry map lists every block entry in
         * order; the first one naming an item starts that item's chain. */
        uint8_t *bem = gcf_read_table(self, span, l->bem_offset,
                                      (uint64_t)l->block_count * GCF_BEM_ENTRY_SIZE);
        uint32_t idx = l->bem_first, steps = 0U;
        if (!bem) goto fail;
        while (idx < l->block_count && steps++ < l->block_count) {
            uint32_t owner = xx_data_get_u32(s->entries + (size_t)idx * GCF_BLOCK_ENTRY_SIZE + 24, 4, 0, false);
            if (owner < l->item_count && first[owner] == GCF_NONE) first[owner] = idx;
            idx = xx_data_get_u32(bem + (size_t)idx * GCF_BEM_ENTRY_SIZE + 4, 4, 0, false);
        }
        xx_mem_free(bem);
    }

    s->items = (gcf_member *)xx_mem_alloc((size_t)l->item_count * sizeof(gcf_member));
    {
        size_t cap = 16U;
        while (cap < (size_t)l->item_count * 2U) cap <<= 1U;
        table.slots = (uint32_t *)xx_mem_alloc(cap * sizeof(uint32_t));
        table.mask = cap - 1U;
    }
    if (!s->items || !table.slots) goto fail;
    xx_mem_zero(table.slots, (table.mask + 1U) * sizeof(uint32_t));

    budget = (uint64_t)l->block_count * 2U + l->item_count;
    {
        const uint8_t *entries = dir + GCF_DIR_HEADER_SIZE;
        const char *names = (const char *)(entries + (size_t)l->item_count * GCF_DIR_ENTRY_SIZE);
        for (i = 1U; i < l->item_count; ++i) {
            const uint8_t *e = entries + (size_t)i * GCF_DIR_ENTRY_SIZE;
            gcf_member m;
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            xx_mem_zero(&m, sizeof(m));
            m.item = i;
            m.flags = xx_data_get_u32(e + 12, 4, 0, false);
            m.folder = (m.flags & GCF_FLAG_FILE) == 0U;
            m.size = m.folder ? 0 : (int64_t)xx_data_get_u32(e + 4, 4, 0, false);
            m.first_entry = first[i];
            m.name = gcf_build_path(entries, names, l->name_size, l->item_count,
                                    i, &m.unsafe);
            if (!m.name) goto fail;
            if (!m.folder) {
                /* Walk the block entry chain once to see whether the whole
                 * item is present; partially downloaded caches are common. */
                uint32_t idx = m.first_entry;
                int64_t covered = 0;
                while (covered < m.size) {
                    const uint8_t *be;
                    uint32_t off, size;
                    if (idx >= l->block_count || budget == 0U) break;
                    --budget;
                    be = s->entries + (size_t)idx * GCF_BLOCK_ENTRY_SIZE;
                    off = xx_data_get_u32(be + 4, 4, 0, false);
                    size = xx_data_get_u32(be + 8, 4, 0, false);
                    if ((int64_t)off != covered || size == 0U ||
                        (int64_t)size > m.size - covered)
                        break;
                    covered += size;
                    idx = xx_data_get_u32(be + 16, 4, 0, false);
                }
                if (budget == 0U && covered < m.size) {
                    xx_str_free(m.name);
                    goto fail;
                }
                m.incomplete = covered != m.size;
                if (m.flags & GCF_FLAG_ENCRYPTED) ++s->encrypted;
                else if (m.incomplete) ++s->incomplete;
            }
            if (!gcf_add_member(s, &table, &m)) {
                xx_str_free(m.name);
                goto fail;
            }
        }
    }
    xx_mem_free(table.slots);
    xx_mem_free(first);
    xx_mem_free(dir);
    return s;
fail:
    xx_mem_free(table.slots);
    xx_mem_free(first);
    xx_mem_free(dir);
    gcf_stream_free(s);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_valve_gcf_cache_init(xx_valve_gcf_cache *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VALVE_GCF_CACHE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-valve-gcf");
    xx_format_set_extension(&archive->format, "gcf");
    archive->format.check_is_valid = xx_valve_gcf_cache_check_is_valid;
    archive->format.handle_base_info = xx_valve_gcf_cache_handle_base_info;
    archive->format.get_format_size = xx_valve_gcf_cache_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_valve_gcf_cache_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_valve_gcf_cache_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_valve_gcf_cache_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_valve_gcf_cache_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_valve_gcf_cache_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_valve_gcf_cache_free_archive_records_reading;
    archive->format.destroy = xx_valve_gcf_cache_vtable_destroy;
}

xx_valve_gcf_cache *xx_valve_gcf_cache_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_valve_gcf_cache *archive =
        (xx_valve_gcf_cache *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_valve_gcf_cache_init(archive, device, base_address);
    return archive;
}

void xx_valve_gcf_cache_destroy(xx_valve_gcf_cache *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_valve_gcf_cache_free(xx_valve_gcf_cache *archive) {
    if (!archive) return;
    xx_valve_gcf_cache_destroy(archive);
    xx_mem_free(archive);
}

static void xx_valve_gcf_cache_vtable_destroy(Abstractformat *self) {
    xx_valve_gcf_cache_destroy((xx_valve_gcf_cache *)self);
}

/* -------------------------------------------------------------- format -- */

/* Header-level check: every table header is read and cross-checked (block
 * counts repeated in four places, the data block header found behind the
 * directory), but no table is loaded. */
bool xx_valve_gcf_cache_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    gcf_layout layout;
    int64_t total;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    return gcf_layout_read(self, total - self->base_address, &layout);
}

bool xx_valve_gcf_cache_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_valve_gcf_cache *archive = (xx_valve_gcf_cache *)self;
    gcf_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = gcf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->layout.format_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->minor_version = stream->layout.minor;
    archive->block_size = stream->layout.block_size;
    archive->block_count = stream->layout.block_count;
    archive->encrypted_members = stream->encrypted;
    archive->incomplete_members = stream->incomplete;
    gcf_stream_free(stream);
    return true;
}

int64_t xx_valve_gcf_cache_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_valve_gcf_cache_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_valve_gcf_cache *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool gcf_set_record(Abstractformat *self, const gcf_stream *s,
                           xx_archive_record *record, const gcf_member *m) {
    int64_t data = -1;
    if (!m->folder && m->first_entry < s->layout.block_count) {
        uint32_t block = xx_data_get_u32(s->entries + (size_t)m->first_entry *
                                                  GCF_BLOCK_ENTRY_SIZE + 12, 4, 0, false);
        if (block < s->layout.block_count)
            data = self->base_address + s->layout.data_offset +
                   (int64_t)block * s->layout.block_size;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address + s->layout.dir_offset +
                            GCF_DIR_HEADER_SIZE +
                            (int64_t)m->item * GCF_DIR_ENTRY_SIZE;
    record->header_size = GCF_DIR_ENTRY_SIZE;
    record->data_offset = data < 0 ? 0 : data;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, m->folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           (m->flags & GCF_FLAG_ENCRYPTED) != 0U);
}

static bool gcf_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *gcf_get_option(const xx_list_s *options, uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_valve_gcf_cache_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    gcf_stream *stream;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    stream = gcf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gcf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = gcf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!gcf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !gcf_set_record(self, stream, &state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_valve_gcf_cache_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_valve_gcf_cache_archive_record_move_to_next(Abstractformat *self,
                                                    xx_archive_record_state *state,
                                                    xx_pd_struct *pd) {
    gcf_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (gcf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = gcf_set_record(self, stream, &state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

/* Streams one item block by block to `output` (NULL = verify only). */
static bool gcf_copy_item(Abstractformat *self, gcf_stream *s,
                          const gcf_member *m, xx_io_device *output,
                          xx_pd_struct *pd) {
    const gcf_layout *l = &s->layout;
    int64_t span = xx_io_total_size(self->device) - self->base_address;
    uint8_t *buffer;
    uint32_t idx = m->first_entry, entry_steps = 0U, block_steps = 0U;
    int64_t written = 0;
    bool ok = true;

    if (m->incomplete || (m->flags & GCF_FLAG_ENCRYPTED)) return false;
    if (m->size == 0) return true;
    /* A data block may appear once per item: a fragmentation map loop
     * would otherwise repeat a block's bytes until the size runs out. */
    if (!s->visit) {
        s->visit = (uint32_t *)xx_mem_alloc(
            (size_t)(l->block_count ? l->block_count : 1U) * sizeof(uint32_t));
        if (!s->visit) return false;
        xx_mem_zero(s->visit, (size_t)(l->block_count ? l->block_count : 1U) *
                                  sizeof(uint32_t));
        s->epoch = 0U;
    }
    if (++s->epoch == 0U) {
        xx_mem_zero(s->visit, (size_t)l->block_count * sizeof(uint32_t));
        s->epoch = 1U;
    }
    buffer = (uint8_t *)xx_mem_alloc(l->block_size);
    if (!buffer) return false;
    while (ok && written < m->size) {
        const uint8_t *be;
        uint32_t block, left;
        if (idx >= l->block_count || entry_steps++ >= l->block_count) {
            ok = false;
            break;
        }
        be = s->entries + (size_t)idx * GCF_BLOCK_ENTRY_SIZE;
        left = xx_data_get_u32(be + 8, 4, 0, false);
        block = xx_data_get_u32(be + 12, 4, 0, false);
        if ((int64_t)xx_data_get_u32(be + 4, 4, 0, false) != written || left == 0U ||
            (int64_t)left > m->size - written) {
            ok = false;
            break;
        }
        while (left) {
            uint32_t n = left < l->block_size ? left : l->block_size;
            int64_t at;
            if (block >= l->block_count || block_steps++ >= l->block_count ||
                s->visit[block] == s->epoch || (pd && xx_pd_is_stopped(pd))) {
                ok = false;
                break;
            }
            s->visit[block] = s->epoch;
            at = l->data_offset + (int64_t)block * l->block_size;
            if (!gcf_read(self, span, at, buffer, n)) {
                ok = false;
                break;
            }
            if (output) {
                size_t done = 0U;
                while (done < n) {
                    ssize_t sent = xx_io_write(output, buffer + done, n - done);
                    if (sent <= 0 || (size_t)sent > n - done) {
                        ok = false;
                        break;
                    }
                    done += (size_t)sent;
                }
                if (!ok) break;
            }
            left -= n;
            written += n;
            block = xx_data_get_u32(s->frag + (size_t)block * 4U, 4, 0, false);
        }
        idx = xx_data_get_u32(be + 16, 4, 0, false);
    }
    xx_mem_free(buffer);
    return ok && written == m->size;
}

bool xx_valve_gcf_cache_unpack_current_archive_record(Abstractformat *self,
                                                      xx_archive_record_state *state,
                                                      xx_pd_struct *pd) {
    gcf_stream *stream;
    const gcf_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    xx_io_device *output;
    bool result;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (gcf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (member->unsafe) return false;
    if (!member->folder &&
        (member->incomplete || (member->flags & GCF_FLAG_ENCRYPTED)))
        return false;

    path_option = gcf_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        return member->folder || gcf_copy_item(self, stream, member, NULL, pd);
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
        base_path[xx_str_len(base_path) - 1U] != '\\')
        target_path = xx_str_concat3(base_path, "/", member->name);
    else
        target_path = xx_str_concat(base_path, member->name);
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    output = xx_io_file_open(target_path, "wb");
    if (!output) {
        xx_str_free(target_path);
        return false;
    }
    created = true;
    result = gcf_copy_item(self, stream, member, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_valve_gcf_cache_free_archive_records_reading(Abstractformat *self,
                                                     xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

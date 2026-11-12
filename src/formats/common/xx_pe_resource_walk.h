/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded walk of a PE resource directory for carrier readers that select
 * resources by type and name (the resource-archive helper selects by data
 * signature and walks three levels only). Every data leaf is visited with
 * its type and name resolved: numbered entries give an id and a NULL name,
 * string entries the name as ASCII (other characters become '_'). Two-level
 * trees (type, name, data) and three-level ones (type, name, language, data)
 * are both walked; deeper ones are not. The name pointers handed to the
 * visitor are valid for that call only. */
#ifndef XX_PE_RESOURCE_WALK_PRIVATE_H
#define XX_PE_RESOURCE_WALK_PRIVATE_H
#include "xx_pe_resource_locator.h"

#define RW_MAX_NAME 260U
#define RW_MAX_VISITS 16384U

typedef struct rw_walk {
    xx_sfx_resource_section sections[96];
    unsigned count;
    int64_t total, root, image_end;
    uint32_t size;
} rw_walk;

typedef struct rw_leaf_info {
    const char *type_name; /* NULL for a numbered type */
    uint32_t type_id;      /* 0 for a string-named type */
    const char *name;      /* NULL for a numbered entry */
    uint32_t name_id;      /* 0 for a string-named entry */
    int64_t at; /* the data, relative to the carrier's base */
    uint32_t bytes;
} rw_leaf_info;

/* False stops the walk as a failure. */
typedef bool (*rw_visitor)(Abstractformat *f, pm_stream *s, const rw_leaf_info *leaf, void *context, xx_pd_struct *pd);

/* The PE headers: the sections and the resource directory, read as the
 * resource-archive helper reads them. */
static XXFC_MAYBE_UNUSED bool rw_open(Abstractformat *f, rw_walk *w, xx_pd_struct *pd)
{
    uint8_t dos[64], coff[24], opt[136], row[40];
    int64_t cabinet, cabinet_end;
    uint32_t pe, rva, size;
    uint16_t count, opt_size, kind;
    unsigned i;
    w->total = pm_available(f);
    if (carrier_stop(pd) || !carrier_pe(f, &w->image_end, &cabinet, &cabinet_end, pd) || !pm_read(f, 0, dos, sizeof(dos))) return false;
    pe = xx_data_get_u32(dos + 60, 4, 0, false);
    if (!pm_read(f, pe, coff, sizeof(coff))) return false;
    count = xx_data_get_u16(coff + 6, 2, 0, false);
    opt_size = xx_data_get_u16(coff + 20, 2, 0, false);
    if (!count || count > 96 || opt_size < 120 || !pm_read(f, (int64_t)pe + 24, opt, 120)) return false;
    kind = xx_data_get_u16(opt, 2, 0, false);
    if (kind == 0x10b) {
        rva = xx_data_get_u32(opt + 112, 4, 0, false);
        size = xx_data_get_u32(opt + 116, 4, 0, false);
    } else if (kind == 0x20b && opt_size >= 136 && pm_read(f, (int64_t)pe + 24, opt, 136)) {
        rva = xx_data_get_u32(opt + 128, 4, 0, false);
        size = xx_data_get_u32(opt + 132, 4, 0, false);
    } else {
        return false;
    }
    if (!rva || size < 16) return false;
    for (i = 0; i < count; ++i) {
        if (carrier_stop(pd) || !pm_read(f, (int64_t)pe + 24 + opt_size + (int64_t)i * 40, row, 40)) return false;
        w->sections[i].va = xx_data_get_u32(row + 12, 4, 0, false);
        w->sections[i].raw = xx_data_get_u32(row + 20, 4, 0, false);
        w->sections[i].raw_size = xx_data_get_u32(row + 16, 4, 0, false);
    }
    w->count = count;
    w->size = size;
    return xx_sfx_resource_raw(w->sections, count, w->total, rva, size, &w->root);
}

/* A string entry's name as ASCII; false for numbered entries and over-long
 * names. */
static XXFC_MAYBE_UNUSED bool rw_entry_name(Abstractformat *f, const rw_walk *w, uint32_t field, char *name, size_t *length)
{
    uint8_t raw[2U * RW_MAX_NAME];
    uint32_t relative = field & 0x7fffffffU;
    uint16_t n;
    size_t i;
    if (!(field & 0x80000000U) || relative > w->size || w->size - relative < 2U || !pm_read(f, w->root + relative, raw, 2)) return false;
    n = xx_data_get_u16(raw, 2, 0, false);
    if (!n || n >= RW_MAX_NAME || w->size - relative - 2U < (uint32_t)n * 2U || !pm_read(f, w->root + relative + 2, raw, (size_t)n * 2U)) return false;
    for (i = 0; i < n; ++i) {
        uint16_t c = xx_data_get_u16(raw + 2U * i, 2, 0, false);
        name[i] = c >= 0x20U && c < 0x7FU ? (char)c : '_';
    }
    name[n] = 0;
    *length = n;
    return true;
}

/* The data under a name entry: a leaf, or the first leaf of its language
 * directory. */
static XXFC_MAYBE_UNUSED bool rw_leaf(Abstractformat *f, const rw_walk *w, uint32_t value, int64_t *at, uint32_t *bytes)
{
    uint8_t entry[8], leaf[16];
    uint32_t relative = value & 0x7fffffffU;
    unsigned n;
    if (value & 0x80000000U) {
        if (!xx_sfx_resource_table(f, w->root, w->size, relative, &n) || !n || !pm_read(f, w->root + relative + 16, entry, 8)) return false;
        value = xx_data_get_u32(entry + 4, 4, 0, false);
        relative = value & 0x7fffffffU;
        if (value & 0x80000000U) return false;
    }
    if (relative > w->size || w->size - relative < 16U || !pm_read(f, w->root + relative, leaf, 16)) return false;
    *bytes = xx_data_get_u32(leaf + 4, 4, 0, false);
    return *bytes > 0 && xx_sfx_resource_raw(w->sections, w->count, w->total, xx_data_get_u32(leaf, 4, 0, false), *bytes, at);
}

/* Every data leaf of every type, with the type and name resolved. False
 * when the directory is unreadable, the walk was stopped, or the visitor
 * refused. */
static XXFC_MAYBE_UNUSED bool rw_walk_leaves(Abstractformat *f, pm_stream *s, rw_walk *w, rw_visitor visit, void *context, xx_pd_struct *pd)
{
    uint8_t entry[8];
    char type_name[RW_MAX_NAME], name[RW_MAX_NAME];
    unsigned n0, i, visits = 0;
    if (!xx_sfx_resource_table(f, w->root, w->size, 0, &n0)) return false;
    for (i = 0; i < n0; ++i) {
        rw_leaf_info leaf;
        uint32_t directory;
        size_t length;
        unsigned n1, j;
        if (carrier_stop(pd) || !pm_read(f, w->root + 16 + (int64_t)i * 8, entry, 8)) return false;
        leaf.type_id = xx_data_get_u32(entry, 4, 0, false);
        leaf.type_name = rw_entry_name(f, w, leaf.type_id, type_name, &length) ? type_name : NULL;
        if (leaf.type_name) leaf.type_id = 0;
        directory = xx_data_get_u32(entry + 4, 4, 0, false);
        if (!(directory & 0x80000000U) || !xx_sfx_resource_table(f, w->root, w->size, directory & 0x7fffffffU, &n1)) continue;
        for (j = 0; j < n1; ++j) {
            if (carrier_stop(pd) || ++visits > RW_MAX_VISITS || !pm_read(f, w->root + (directory & 0x7fffffffU) + 16 + (int64_t)j * 8, entry, 8)) return false;
            leaf.name_id = xx_data_get_u32(entry, 4, 0, false);
            leaf.name = rw_entry_name(f, w, leaf.name_id, name, &length) ? name : NULL;
            if (leaf.name) leaf.name_id = 0;
            if (!rw_leaf(f, w, xx_data_get_u32(entry + 4, 4, 0, false), &leaf.at, &leaf.bytes)) continue;
            if (!visit(f, s, &leaf, context, pd)) return false;
        }
    }
    return !carrier_stop(pd);
}
#endif

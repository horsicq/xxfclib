/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of Apple's published classic HFS layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hfs/xx_hfs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#ifdef HFS
#define HFS_TYPE XX_FILE_TYPE_HFS
#else
#define HFS_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define HFS_OBJECTS 100000U
#define HFS_RECORDS 200000U
#define HFS_NODES 131072U
#define HFS_COPY 65536U
#define HFS_PATH 4096U
#define HFS_WORK 4000000U
#define HFS_PATH_MEMORY (UINT64_C(64) * 1024U * 1024U)
typedef struct hfs_run_s { uint32_t logical; uint16_t start, count; } hfs_run;
typedef struct hfs_fork_s { uint32_t size, physical, first, count; } hfs_fork;
typedef struct hfs_key_s { uint32_t id; uint16_t block; uint8_t fork, length, name[31]; } hfs_key;
typedef struct hfs_overflow_s { hfs_key key; uint8_t extents[12]; bool used; } hfs_overflow;
typedef struct hfs_entry_s {
    uint32_t id, parent, children;
    uint16_t type, flags, valence, record_size;
    uint64_t header;
    uint8_t name[32], extents[24], path_state, depth;
    hfs_fork data, resource;
    char *path;
} hfs_entry;
typedef struct hfs_thread_s { uint32_t id, parent; uint16_t type; uint8_t name[32]; } hfs_thread;
typedef struct hfs_member_s {
    char *name; uint64_t header; uint16_t header_size, flags;
    bool directory, resource; hfs_fork fork;
} hfs_member;
typedef struct hfs_view_s {
    xx_io_device *device; int64_t base; uint64_t bytes, heap, path_bytes, retained_memory;
    uint32_t unit_size, files, folders, work, run_count;
    uint16_t units, attributes, root_files, root_folders;
    uint8_t *bitmap, *claimed;
    hfs_run *runs;
    hfs_overflow *overflow; size_t overflow_count;
    hfs_entry *entries; size_t entry_count;
    hfs_thread *threads; size_t thread_count;
    hfs_member *members; size_t count, index, capacity;
    uint32_t *names; size_t name_capacity;
    char volume_name[82];
} hfs_view;
typedef struct hfs_node_s { uint8_t bytes[512]; uint16_t count, offsets[250]; } hfs_node;
typedef struct hfs_tree_s {
    hfs_view *view; hfs_fork fork; bool catalog;
    uint32_t nodes, free_nodes, root, records, first, last, leaf_count;
    uint16_t depth;
    uint8_t *bitmap, *visited;
    uint32_t previous[9], next[9];
} hfs_tree;
static uint16_t hfs_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t hfs_u32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static bool hfs_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool hfs_work(hfs_view *v, xx_pd_struct *pd) { return !hfs_stopped(pd) && ++v->work <= HFS_WORK; }
static bool hfs_bit(const uint8_t *map, uint32_t n) { return (map[n / 8U] & (0x80U >> (n % 8U))) != 0U; }
static bool hfs_read(const hfs_view *v, uint64_t offset, void *buffer, size_t size, xx_pd_struct *pd) {
    int64_t saved; size_t done = 0U; bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) || hfs_stopped(pd)) return false;
    saved = xx_io_tell(v->device); if (saved < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !hfs_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)buffer + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, saved, SEEK_SET)) ok = false;
    return ok && !hfs_stopped(pd);
}
static uint64_t hfs_address(const hfs_view *v, const hfs_fork *fork, uint32_t offset, size_t *available) {
    uint32_t block = offset / v->unit_size, within = offset % v->unit_size, lo = 0U, hi = fork->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2U;
        const hfs_run *run = v->runs + fork->first + mid;
        if (block < run->logical) hi = mid;
        else if (block - run->logical >= run->count) lo = mid + 1U;
        else {
            uint64_t bytes = (uint64_t)(run->count - (block - run->logical)) * v->unit_size - within;
            *available = bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
            return v->heap + (uint64_t)(run->start + block - run->logical) * v->unit_size + within;
        }
    }
    *available = 0U; return UINT64_MAX;
}
static bool hfs_fork_read(hfs_view *v, const hfs_fork *fork, uint32_t offset, void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (offset > fork->size || size > fork->size - offset) return false;
    while (done < size) {
        size_t part; uint64_t address = hfs_address(v, fork, offset + (uint32_t)done, &part);
        if (!part) return false;
        if (part > size - done) part = size - done;
        if (!hfs_read(v, address, (uint8_t *)buffer + done, part, pd)) return false;
        done += part;
    }
    return true;
}
/* MacRoman Unicode mapping and RelString sort weights are format facts.
 * The latter follow Apple's UCStringCompareData.h, encoded here as a compact
 * non-ASCII weight range; no external filesystem implementation is linked. */
static const uint16_t hfs_macroman[128] = {
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    0x00EA,0x00EB,0x00ED,0x00EE,0x00EF,0x00EC,0x00F1,0x00F3,0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
    0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
    0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
    0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
    0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
};
static uint16_t hfs_sort(uint8_t c) {
    static const uint16_t weights[89] = {
        0x4108,0x410C,0x4310,0x4502,0x4E0A,0x4F08,0x5508,0x4182,0x4104,0x4186,0x4108,0x410A,0x410C,0x4310,0x4502,0x4584,
        0x4586,0x4588,0x4982,0x4984,0x4986,0x4988,0x4E0A,0x4F82,0x4F84,0x4F86,0x4F08,0x4F0A,0x5582,0x5584,0x5586,0x5508,
        0xA000,0xA100,0xA200,0xA300,0xA400,0xA500,0xA600,0x5382,0xA800,0xA900,0xAA00,0xAB00,0xAC00,0xAD00,0x4114,0x4F0E,
        0xB000,0xB100,0xB200,0xB300,0xB400,0xB500,0xB600,0xB700,0xB800,0xB900,0xBA00,0x4192,0x4F92,0xBD00,0x4114,0x4F0E,
        0xC000,0xC100,0xC200,0xC300,0xC400,0xC500,0xC600,0x2206,0x2208,0xC900,0x2000,0x4104,0x410A,0x4F0A,0x4F14,0x4F14,
        0xD000,0xD100,0x2202,0x2204,0x2702,0x2704,0xD600,0xD700,0x5988
    };
    if (c >= 0x80U && c <= 0xD8U) return weights[c - 0x80U];
    if (c == 0x60U) return 0x4180U;
    if (c >= 'a' && c <= 'z') c -= 32U;
    return (uint16_t)(c << 8);
}
static int hfs_key_compare(const hfs_key *a, const hfs_key *b, bool catalog) {
    size_t i, n;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    if (!catalog) {
        if (a->fork != b->fork) return a->fork < b->fork ? -1 : 1;
        return a->block == b->block ? 0 : (a->block < b->block ? -1 : 1);
    }
    n = a->length < b->length ? a->length : b->length;
    for (i = 0U; i < n; ++i) {
        uint16_t x = hfs_sort(a->name[i]), y = hfs_sort(b->name[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return a->length == b->length ? 0 : (a->length < b->length ? -1 : 1);
}
static bool hfs_key_read(const uint8_t *raw, size_t size, bool catalog, bool index, hfs_key *key, size_t *payload) {
    size_t length = raw[0];
    xx_mem_zero(key, sizeof(*key));
    *payload = (length + 2U) & ~(size_t)1U;
    if (*payload > size) return false;
    if (catalog) {
        if (size < 8U || length < 6U || length > 37U || (index && length != 37U) || raw[1] || raw[6] > 31U || raw[6] > length - 6U) return false;
        key->id = hfs_u32(raw + 2); key->length = raw[6]; xx_mem_copy(key->name, raw + 7, key->length);
    } else {
        if (size < 8U || length != 7U || (raw[1] != 0U && raw[1] != 0xFFU)) return false;
        key->id = hfs_u32(raw + 2); key->fork = raw[1]; key->block = hfs_u16(raw + 6);
    }
    return key->id != 0U;
}
static bool hfs_extent_valid(hfs_view *v, const uint8_t *data) {
    unsigned i; bool end = false;
    for (i = 0U; i < 3U; ++i) {
        uint32_t start = hfs_u16(data + i * 4U), count = hfs_u16(data + i * 4U + 2U);
        if (!count) { if (start) return false; end = true; }
        else if (end || start >= v->units || count > v->units - start) return false;
    }
    return true;
}
static bool hfs_extent_add(hfs_view *v, hfs_fork *fork, const uint8_t *data, uint32_t target, uint32_t *logical, xx_pd_struct *pd) {
    unsigned i;
    if (!hfs_extent_valid(v, data)) return false;
    for (i = 0U; i < 3U; ++i) {
        uint32_t start = hfs_u16(data + i * 4U), count = hfs_u16(data + i * 4U + 2U), k;
        if (!count) break;
        if (*logical > target || count > target - *logical || v->run_count >= v->units) return false;
        for (k = start; k < start + count; ++k) {
            if (!hfs_work(v, pd) || !hfs_bit(v->bitmap, k) || v->claimed[k]) return false;
            v->claimed[k] = 1U;
        }
        v->runs[v->run_count].start = (uint16_t)start; v->runs[v->run_count].count = (uint16_t)count;
        v->runs[v->run_count].logical = *logical; ++v->run_count; ++fork->count; *logical += count;
    }
    return true;
}
static hfs_overflow *hfs_overflow_find(hfs_view *v, uint32_t id, uint8_t type, uint32_t block) {
    hfs_key key; size_t lo = 0U, hi = v->overflow_count;
    if (block > UINT16_MAX) return NULL;
    xx_mem_zero(&key, sizeof(key)); key.id = id; key.fork = type; key.block = (uint16_t)block;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U; int c = hfs_key_compare(&key, &v->overflow[mid].key, false);
        if (!c) return v->overflow + mid;
        if (c < 0) hi = mid; else lo = mid + 1U;
    }
    return NULL;
}
static bool hfs_fork_build(hfs_view *v, hfs_fork *fork, uint32_t id, uint8_t type, const uint8_t *first, bool bootstrap, xx_pd_struct *pd) {
    uint32_t logical = 0U, target;
    if (fork->size > fork->physical || fork->physical % v->unit_size) return false;
    target = fork->physical / v->unit_size; if (target > v->units) return false;
    fork->first = v->run_count; fork->count = 0U;
    if (!hfs_extent_add(v, fork, first, target, &logical, pd)) return false;
    while (logical < target) {
        uint32_t before = logical; hfs_overflow *extra = bootstrap ? NULL : hfs_overflow_find(v, id, type, logical);
        if (!extra || extra->used || !hfs_work(v, pd)) return false;
        extra->used = true;
        if (!hfs_extent_add(v, fork, extra->extents, target, &logical, pd) || logical == before) return false;
    }
    return true;
}
static bool hfs_node_read(hfs_tree *tree, uint32_t id, hfs_node *node, xx_pd_struct *pd) {
    uint32_t i, previous = 0U, table;
    if (id >= tree->nodes || !hfs_work(tree->view, pd) || !hfs_fork_read(tree->view, &tree->fork, id * 512U, node->bytes, 512U, pd)) return false;
    node->count = hfs_u16(node->bytes + 10);
    if (hfs_u16(node->bytes + 12) || node->count > 248U) return false;
    table = 512U - 2U * (node->count + 1U);
    for (i = 0U; i <= node->count; ++i) {
        uint16_t at = hfs_u16(node->bytes + 510U - i * 2U);
        if (at < 14U || at > table || (at & 1U) || (i && at <= previous)) return false;
        node->offsets[i] = at; previous = at;
    }
    return node->offsets[0] == 14U;
}
static bool hfs_leaf(hfs_tree *tree, const hfs_key *key, const uint8_t *data, size_t size, uint64_t address) {
    hfs_view *v = tree->view;
    if (!tree->catalog) {
        hfs_overflow *extra;
        if (size != 12U || !hfs_extent_valid(v, data) || !hfs_u16(data + 2) || v->overflow_count >= v->units || key->id < 3U || key->id == 3U || (key->id < 16U && key->id != 4U && key->id != 5U) || (key->id != 5U && !key->block)) return false;
        extra = v->overflow + v->overflow_count++; extra->key = *key; xx_mem_copy(extra->extents, data, 12U); return true;
    } else {
        uint16_t type;
        if (size < 2U || (type = hfs_u16(data)) < 0x100U || type > 0x400U || (type & 0xFFU)) return false;
        if (type >= 0x300U) {
            hfs_thread *thread;
            if (key->length || size != 46U || !data[14] || data[14] > 31U || v->thread_count >= (size_t)v->files + v->folders + 1U) return false;
            thread = v->threads + v->thread_count++; thread->id = key->id; thread->parent = hfs_u32(data + 10); thread->type = type;
            xx_mem_copy(thread->name, data + 14, 32U); return thread->parent != 0U;
        } else {
            hfs_entry *entry;
            if (!key->length || size != (type == 0x100U ? 70U : 102U) || v->entry_count >= (size_t)v->files + v->folders + 1U) return false;
            entry = v->entries + v->entry_count++; entry->type = type; entry->parent = key->id; entry->name[0] = key->length;
            xx_mem_copy(entry->name + 1, key->name, key->length); entry->header = address; entry->record_size = (uint16_t)size;
            if (type == 0x100U) { entry->flags = hfs_u16(data + 2); entry->valence = hfs_u16(data + 4); entry->id = hfs_u32(data + 6); }
            else {
                /* A live catalog key identifies the file. Bit7 was described
                 * as 'used' in early Apple documentation, but healthy classic
                 * volumes produced by hfsutils leave it clear. */
                if ((data[2] & 0x7CU) || data[3]) return false;
                entry->flags = data[2]; entry->id = hfs_u32(data + 20);
                entry->data.size = hfs_u32(data + 26); entry->data.physical = hfs_u32(data + 30);
                entry->resource.size = hfs_u32(data + 36); entry->resource.physical = hfs_u32(data + 40);
                xx_mem_copy(entry->extents, data + 74, 24U);
            }
            return entry->id == 2U ? type == 0x100U && key->id == 1U : entry->id >= 16U;
        }
    }
}
static bool hfs_walk_node(hfs_tree *tree, uint32_t id, uint16_t height, hfs_key *first, hfs_key *last, xx_pd_struct *pd) {
    hfs_node node; hfs_key previous; uint16_t i; bool have = false;
    if (!id || id >= tree->nodes || !height || height > 8U || !hfs_bit(tree->bitmap, id) || tree->visited[id] || !hfs_node_read(tree, id, &node, pd)) return false;
    tree->visited[id] = 1U;
    if (!node.count || node.bytes[9] != height || node.bytes[8] != (height == 1U ? 0xFFU : 0U) ||
        hfs_u32(node.bytes + 4) != tree->previous[height] || (tree->previous[height] && tree->next[height] != id)) return false;
    if (height == 1U && !tree->previous[1] && id != tree->first) return false;
    tree->previous[height] = id; tree->next[height] = hfs_u32(node.bytes);
    if (tree->next[height] >= tree->nodes) return false;
    for (i = 0U; i < node.count; ++i) {
        size_t payload, size = node.offsets[i + 1U] - node.offsets[i];
        const uint8_t *record = node.bytes + node.offsets[i]; hfs_key key, end;
        if (!hfs_work(tree->view, pd) || !hfs_key_read(record, size, tree->catalog, height != 1U, &key, &payload) || (have && hfs_key_compare(&previous, &key, tree->catalog) >= 0)) return false;
        if (height == 1U) {
            size_t available; uint64_t address = hfs_address(tree->view, &tree->fork, id * 512U + node.offsets[i] + (uint32_t)payload, &available);
            if (!available || ++tree->leaf_count > tree->records || !hfs_leaf(tree, &key, record + payload, size - payload, address)) return false;
            end = key;
        } else {
            hfs_key child_first;
            if (size - payload != 4U || !hfs_walk_node(tree, hfs_u32(record + payload), height - 1U, &child_first, &end, pd) || hfs_key_compare(&child_first, &key, tree->catalog)) return false;
        }
        if (!have) *first = key;
        previous = end; *last = end; have = true;
    }
    return true;
}
static bool hfs_tree_read(hfs_view *v, const hfs_fork *fork, bool catalog, xx_pd_struct *pd) {
    hfs_tree tree; hfs_node header, map; hfs_key first, last;
    uint32_t map_node, previous = 0U, covered, i, free_count = 0U; bool ok = false;
    const uint8_t *record;
    xx_mem_zero(&tree, sizeof(tree)); tree.view = v; tree.fork = *fork; tree.catalog = catalog;
    tree.nodes = fork->size / 512U;
    if (!tree.nodes || tree.nodes > HFS_NODES || fork->size % 512U || !hfs_node_read(&tree, 0U, &header, pd) || header.bytes[8] != 1U || header.bytes[9] || hfs_u32(header.bytes + 4) || header.count != 3U ||
        header.offsets[1] - header.offsets[0] != 106U || header.offsets[2] - header.offsets[1] != 128U || header.offsets[3] - header.offsets[2] != 256U) goto done;
    record = header.bytes + 14; tree.depth = hfs_u16(record); tree.root = hfs_u32(record + 2); tree.records = hfs_u32(record + 6);
    tree.first = hfs_u32(record + 10); tree.last = hfs_u32(record + 14); tree.free_nodes = hfs_u32(record + 26);
    if (hfs_u16(record + 18) != 512U || hfs_u16(record + 20) != (catalog ? 37U : 7U) || hfs_u32(record + 22) != tree.nodes || tree.depth > 8U || tree.records > HFS_RECORDS || tree.root >= tree.nodes || tree.first >= tree.nodes || tree.last >= tree.nodes || tree.free_nodes >= tree.nodes) goto done;
    tree.bitmap = (uint8_t *)xx_mem_alloc((tree.nodes + 7U) / 8U); tree.visited = (uint8_t *)xx_mem_alloc(tree.nodes);
    if (!tree.bitmap || !tree.visited) goto done;
    xx_mem_zero(tree.bitmap, (tree.nodes + 7U) / 8U); xx_mem_zero(tree.visited, tree.nodes); tree.visited[0] = 2U;
    covered = tree.nodes < 2048U ? tree.nodes : 2048U;
    xx_mem_copy(tree.bitmap, header.bytes + header.offsets[2], (covered + 7U) / 8U);
    map_node = hfs_u32(header.bytes);
    while (map_node) {
        uint32_t more, map_bytes;
        if (map_node >= tree.nodes || tree.visited[map_node] || covered >= tree.nodes || !hfs_node_read(&tree, map_node, &map, pd) || map.bytes[8] != 2U || map.bytes[9] || map.count != 1U || hfs_u32(map.bytes + 4) != previous) goto done;
        map_bytes = map.offsets[1] - map.offsets[0];
        /* Apple specifies494 bitmap bytes; hfsutils produces the healthy
         *492-byte variant with two unused bytes before the offset table. */
        if (map_bytes != 492U && map_bytes != 494U) goto done;
        tree.visited[map_node] = 2U; more = tree.nodes - covered; if (more > map_bytes * 8U) more = map_bytes * 8U;
        xx_mem_copy(tree.bitmap + covered / 8U, map.bytes + 14, (more + 7U) / 8U); covered += more;
        previous = map_node; map_node = hfs_u32(map.bytes);
    }
    if (covered < tree.nodes || !hfs_bit(tree.bitmap, 0U)) goto done;
    if (!tree.depth) {
        if (catalog || tree.root || tree.records || tree.first || tree.last) goto done;
    } else if (!tree.root || !tree.first || !tree.last || !tree.records || !hfs_walk_node(&tree, tree.root, tree.depth, &first, &last, pd) || tree.leaf_count != tree.records || tree.previous[1] != tree.last) goto done;
    for (i = 1U; i <= tree.depth; ++i) if (tree.next[i]) goto done;
    for (i = 0U; i < tree.nodes; ++i) {
        if (!hfs_work(v, pd) || hfs_bit(tree.bitmap, i) != (tree.visited[i] != 0U)) goto done;
        if (!tree.visited[i]) ++free_count;
    }
    ok = free_count == tree.free_nodes;
done:
    xx_mem_free(tree.bitmap); xx_mem_free(tree.visited); return ok;
}
static int hfs_entry_compare(const void *a, const void *b) {
    uint32_t x = ((const hfs_entry *)a)->id, y = ((const hfs_entry *)b)->id;
    return x == y ? 0 : (x < y ? -1 : 1);
}
static int hfs_thread_compare(const void *a, const void *b) {
    uint32_t x = ((const hfs_thread *)a)->id, y = ((const hfs_thread *)b)->id;
    return x == y ? 0 : (x < y ? -1 : 1);
}
static hfs_entry *hfs_entry_find(hfs_view *v, uint32_t id) {
    size_t lo = 0U, hi = v->entry_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (v->entries[mid].id == id) return v->entries + mid;
        if (v->entries[mid].id > id) hi = mid; else lo = mid + 1U;
    }
    return NULL;
}
static hfs_thread *hfs_thread_find(hfs_view *v, uint32_t id) {
    size_t lo = 0U, hi = v->thread_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (v->threads[mid].id == id) return v->threads + mid;
        if (v->threads[mid].id > id) hi = mid; else lo = mid + 1U;
    }
    return NULL;
}
static size_t hfs_utf8(char *output, uint32_t c) {
    if (c < 0x80U) { output[0] = (char)c; return 1U; }
    if (c < 0x800U) { output[0] = (char)(0xC0U | (c >> 6)); output[1] = (char)(0x80U | (c & 63U)); return 2U; }
    output[0] = (char)(0xE0U | (c >> 12)); output[1] = (char)(0x80U | ((c >> 6) & 63U)); output[2] = (char)(0x80U | (c & 63U)); return 3U;
}
static uint32_t hfs_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text; uint32_t c = *p++;
    if (c >= 0xE0U) { c = (c & 15U) << 12; c |= (*p++ & 63U) << 6; c |= *p++ & 63U; }
    else if (c >= 0xC0U) { c = (c & 31U) << 6; c |= *p++ & 63U; }
    *text = (const char *)p;
    if (c >= 'a' && c <= 'z') c -= 32U;
    else if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) c -= 32U;
    else if (c == 0xFFU) c = 0x178U;
    else if (c == 0x153U) c = 0x152U;
    else if (c == 0x131U) c = 'I';
    return c;
}
static bool hfs_equal(const char *a, const char *b) {
    while (*a && *b) if (hfs_next(&a) != hfs_next(&b)) return false;
    return !*a && !*b;
}
static uint32_t hfs_hash(const char *text) {
    uint32_t hash = UINT32_C(2166136261);
    while (*text) { hash ^= hfs_next(&text); hash *= UINT32_C(16777619); }
    return hash;
}
static bool hfs_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    size_t n = 0U, i; char stem[16];
    while (name[n] && name[n] != '.' && n + 1U < sizeof(stem)) { stem[n] = name[n]; ++n; }
    stem[n] = 0;
    if (name[n] && name[n] != '.') return false;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) if (hfs_equal(stem, devices[i])) return true;
    return n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm') ||
         (stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't'));
}
static bool hfs_name(const uint8_t *raw, char *output, bool host) {
    size_t n = raw[0], i, at = 0U;
    if (!n || n > (host ? 31U : 27U)) return false;
    for (i = 1U; i <= n; ++i) {
        uint32_t c = raw[i] < 0x80U ? raw[i] : hfs_macroman[raw[i] - 0x80U];
        if (!c || raw[i] == ':') return false;
        if (host && (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) c = '_';
        at += hfs_utf8(output + at, c);
    }
    if (host) for (i = at; i && (output[i - 1U] == '.' || output[i - 1U] == ' '); --i) output[i - 1U] = '_';
    output[at] = 0;
    if (host && hfs_device_name(output)) { for (i = at + 1U; i; --i) output[i] = output[i - 1U]; output[0] = '_'; }
    return true;
}
static bool hfs_append(hfs_view *v, const hfs_entry *entry, const char *parent, const char *component,
    bool directory, bool resource, hfs_member **result, xx_pd_struct *pd) {
    unsigned attempt; char leaf[128];
    if (v->count >= v->capacity) return false;
    for (attempt = 0U; attempt < HFS_OBJECTS; ++attempt) {
        char *path; size_t slot; bool taken = false;
        if (!hfs_work(v, pd)) return false;
        if (!attempt) xx_rt_snprintf(leaf, sizeof(leaf), "%s%s", component, resource ? ".rsrc" : "");
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s%s~%u", component, resource ? ".rsrc" : "", attempt + 1U);
        path = *parent ? xx_str_concat3(parent, "/", leaf) : xx_str_dup(leaf);
        if (!path) return false;
        if (xx_str_len(path) >= HFS_PATH) { xx_str_free(path); return false; }
        slot = hfs_hash(path) & (v->name_capacity - 1U);
        while (v->names[slot]) {
            if (!hfs_work(v, pd)) { xx_str_free(path); return false; }
            if (hfs_equal(path, v->members[v->names[slot] - 1U].name)) { taken = true; break; }
            slot = (slot + 1U) & (v->name_capacity - 1U);
        }
        if (!taken) {
            hfs_member *member = v->members + v->count;
            size_t bytes = xx_str_len(path) + 1U;
            if (bytes > HFS_PATH_MEMORY - v->path_bytes) { xx_str_free(path); return false; }
            v->path_bytes += bytes;
            member->name = path; member->header = entry->header; member->header_size = entry->record_size;
            member->flags = entry->flags; member->directory = directory; member->resource = resource;
            member->fork = resource ? entry->resource : entry->data;
            v->names[slot] = (uint32_t)++v->count; if (result) *result = member; return true;
        }
        xx_str_free(path);
    }
    return false;
}
static bool hfs_folder_path(hfs_view *v, hfs_entry *entry, unsigned depth, xx_pd_struct *pd) {
    hfs_entry *parent; hfs_member *member; char component[96];
    if (entry->type != 0x100U || depth > 64U || entry->path_state == 1U || !hfs_work(v, pd)) return false;
    if (entry->path_state == 2U) return true;
    entry->path_state = 1U;
    if (entry->id == 2U) {
        if (v->path_bytes >= HFS_PATH_MEMORY) return false;
        entry->path = xx_str_dup(""); entry->path_state = 2U;
        if (entry->path) ++v->path_bytes; return entry->path != NULL;
    }
    parent = hfs_entry_find(v, entry->parent);
    if (!parent || !hfs_folder_path(v, parent, depth + 1U, pd) || parent->depth >= 64U || !hfs_name(entry->name, component, true) ||
        !hfs_append(v, entry, parent->path, component, true, false, &member, pd)) return false;
    entry->depth = (uint8_t)(parent->depth + 1U);
    {
        size_t bytes = xx_str_len(member->name) + 1U;
        if (bytes > HFS_PATH_MEMORY - v->path_bytes) return false;
        entry->path = xx_str_dup(member->name); entry->path_state = 2U;
        if (entry->path) v->path_bytes += bytes; return entry->path != NULL;
    }
}
static bool hfs_catalog_finish(hfs_view *v, const uint8_t *volume_name, xx_pd_struct *pd) {
    size_t i; uint32_t files = 0U, folders = 0U, root_files = 0U, root_folders = 0U;
    xx_rt_qsort(v->entries, v->entry_count, sizeof(*v->entries), hfs_entry_compare);
    xx_rt_qsort(v->threads, v->thread_count, sizeof(*v->threads), hfs_thread_compare);
    if (!v->entry_count || v->entries[0].id != 2U || v->entries[0].name[0] != volume_name[0] || xx_mem_compare(v->entries[0].name + 1, volume_name + 1, volume_name[0])) return false;
    for (i = 0U; i < v->thread_count; ++i) {
        hfs_thread *thread = v->threads + i; hfs_entry *entry = hfs_entry_find(v, thread->id);
        if (!hfs_work(v, pd) || (i && thread->id == v->threads[i - 1U].id) || !entry || thread->type != (entry->type == 0x100U ? 0x300U : 0x400U) ||
            thread->parent != entry->parent || thread->name[0] != entry->name[0] || xx_mem_compare(thread->name + 1, entry->name + 1, entry->name[0])) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hfs_entry *entry = v->entries + i; hfs_entry *parent = entry->id == 2U ? NULL : hfs_entry_find(v, entry->parent);
        hfs_thread *thread = hfs_thread_find(v, entry->id); char component[96];
        if (!hfs_work(v, pd) || (i && entry->id == v->entries[i - 1U].id) || !hfs_name(entry->name, component, true) ||
            (entry->id != 2U && (!parent || parent->type != 0x100U)) ||
            ((entry->type == 0x100U || (entry->flags & 2U)) && !thread)) return false;
        if (parent) { if (++parent->children > UINT16_MAX) return false; }
        if (entry->type == 0x100U) { if (entry->id != 2U) { ++folders; if (entry->parent == 2U) ++root_folders; } }
        else {
            ++files; if (entry->parent == 2U) ++root_files;
            if (!hfs_fork_build(v, &entry->data, entry->id, 0U, entry->extents, false, pd) ||
                !hfs_fork_build(v, &entry->resource, entry->id, 0xFFU, entry->extents + 12, false, pd)) return false;
        }
    }
    if (files != v->files || folders != v->folders || root_files != v->root_files || root_folders != v->root_folders) return false;
    for (i = 0U; i < v->entry_count; ++i) {
        hfs_entry *entry = v->entries + i;
        if (entry->type == 0x100U && (entry->children != entry->valence || !hfs_folder_path(v, entry, 0U, pd))) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hfs_entry *entry = v->entries + i; hfs_entry *parent; char component[96];
        if (entry->type != 0x200U) continue;
        parent = hfs_entry_find(v, entry->parent);
        if (!parent || !parent->path || !hfs_name(entry->name, component, true) || !hfs_append(v, entry, parent->path, component, false, false, NULL, pd)) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hfs_entry *entry = v->entries + i; hfs_entry *parent; char component[96];
        if (entry->type != 0x200U || !entry->resource.physical) continue;
        parent = hfs_entry_find(v, entry->parent);
        if (!parent || !hfs_name(entry->name, component, true) || !hfs_append(v, entry, parent->path, component, false, true, NULL, pd)) return false;
    }
    for (i = 0U; i < v->overflow_count; ++i) {
        hfs_overflow *extra = v->overflow + i;
        if (!extra->used && extra->key.id == 5U && extra->key.fork == 0U && (v->attributes & 0x200U)) {
            hfs_fork bad; uint32_t logical = extra->key.block;
            xx_mem_zero(&bad, sizeof(bad)); bad.first = v->run_count;
            if (!hfs_extent_add(v, &bad, extra->extents, v->units, &logical, pd)) return false;
            extra->used = true;
        }
        if (!extra->used) return false;
    }
    return !hfs_stopped(pd);
}
static void hfs_view_free(void *pointer) {
    hfs_view *v = (hfs_view *)pointer; size_t i;
    if (!v) return;
    for (i = 0U; i < v->entry_count; ++i) xx_str_free(v->entries[i].path);
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v->bitmap); xx_mem_free(v->claimed); xx_mem_free(v->runs); xx_mem_free(v->overflow);
    xx_mem_free(v->entries); xx_mem_free(v->threads); xx_mem_free(v->members); xx_mem_free(v->names); xx_mem_free(v);
}
static hfs_view *hfs_parse(Abstractformat *self, xx_pd_struct *pd) {
    hfs_view *v; uint8_t mdb[162], backup[162]; hfs_fork extents, catalog;
    int64_t total; uint32_t bitmap_sector, objects, free_count = 0U, i; uint64_t end, at;
    if (!self || !self->device || self->base_address < 0 || hfs_stopped(pd) || (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (hfs_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL; xx_mem_zero(v, sizeof(*v));
    v->device = self->device; v->base = self->base_address; v->bytes = (uint64_t)(total - v->base);
    if (!hfs_read(v, 1024U, mdb, sizeof(mdb), pd) || hfs_u16(mdb) != 0x4244U || hfs_u16(mdb + 124) == 0x482BU || hfs_u16(mdb + 124) == 0x4858U) goto fail;
    v->attributes = hfs_u16(mdb + 10); v->units = hfs_u16(mdb + 18); v->unit_size = hfs_u32(mdb + 20); v->heap = (uint64_t)hfs_u16(mdb + 28) * 512U;
    bitmap_sector = hfs_u16(mdb + 14); v->files = hfs_u32(mdb + 84); v->folders = hfs_u32(mdb + 88);
    v->root_files = hfs_u16(mdb + 12); v->root_folders = hfs_u16(mdb + 82);
    if ((v->attributes & 0x6800U) || !v->units || v->unit_size < 512U || v->unit_size > 16U * 1024U * 1024U || v->unit_size % 512U ||
        bitmap_sector < 3U || (uint64_t)bitmap_sector * 512U + ((v->units + 4095U) / 4096U) * 512U > v->heap || hfs_u16(mdb + 16) >= v->units ||
        v->files >= HFS_OBJECTS || v->folders >= HFS_OBJECTS || v->files + v->folders >= HFS_OBJECTS || !hfs_name(mdb + 36, v->volume_name, false)) goto fail;
    end = v->heap + (uint64_t)v->units * v->unit_size; if (end > v->bytes || end > (uint64_t)(INT64_MAX - v->base)) goto fail;
    objects = v->files + v->folders + 1U; v->capacity = (size_t)v->files * 2U + v->folders;
    v->bitmap = (uint8_t *)xx_mem_alloc((v->units + 7U) / 8U); v->claimed = (uint8_t *)xx_mem_alloc(v->units);
    v->runs = (hfs_run *)xx_mem_alloc((size_t)v->units * sizeof(*v->runs));
    v->overflow = (hfs_overflow *)xx_mem_alloc((size_t)v->units * sizeof(*v->overflow));
    v->entries = (hfs_entry *)xx_mem_alloc((size_t)objects * sizeof(*v->entries)); v->threads = (hfs_thread *)xx_mem_alloc((size_t)objects * sizeof(*v->threads));
    v->members = (hfs_member *)xx_mem_alloc((v->capacity ? v->capacity : 1U) * sizeof(*v->members));
    v->name_capacity = 8U; while (v->name_capacity < v->capacity * 2U) v->name_capacity *= 2U;
    v->names = (uint32_t *)xx_mem_alloc(v->name_capacity * sizeof(*v->names));
    if (!v->bitmap || !v->claimed || !v->runs || !v->overflow || !v->entries || !v->threads || !v->members || !v->names) goto fail;
    xx_mem_zero(v->claimed, v->units); xx_mem_zero(v->overflow, (size_t)v->units * sizeof(*v->overflow));
    xx_mem_zero(v->entries, (size_t)objects * sizeof(*v->entries)); xx_mem_zero(v->threads, (size_t)objects * sizeof(*v->threads));
    xx_mem_zero(v->members, (v->capacity ? v->capacity : 1U) * sizeof(*v->members)); xx_mem_zero(v->names, v->name_capacity * sizeof(*v->names));
    if (!hfs_read(v, (uint64_t)bitmap_sector * 512U, v->bitmap, (v->units + 7U) / 8U, pd)) goto fail;
    for (i = 0U; i < v->units; ++i) if (!hfs_bit(v->bitmap, i)) ++free_count;
    if (free_count != hfs_u16(mdb + 34)) goto fail;
    xx_mem_zero(&extents, sizeof(extents)); extents.size = hfs_u32(mdb + 130); extents.physical = extents.size;
    xx_mem_zero(&catalog, sizeof(catalog)); catalog.size = hfs_u32(mdb + 146); catalog.physical = catalog.size;
    if (!hfs_fork_build(v, &extents, 3U, 0U, mdb + 134, true, pd) || !hfs_tree_read(v, &extents, false, pd) ||
        !hfs_fork_build(v, &catalog, 4U, 0U, mdb + 150, false, pd) || !hfs_tree_read(v, &catalog, true, pd) || !hfs_catalog_finish(v, mdb + 36, pd)) goto fail;
    /* A backup MDB is optional; a matching geometry locates the volume end
     * without treating a caller's prefix or trailing overlay as disk bytes. */
    for (at = end; at <= end + v->unit_size && at <= v->bytes && v->bytes - at >= 1024U; at += 512U) {
        if (!hfs_work(v, pd) || !hfs_read(v, at, backup, sizeof(backup), pd)) goto fail;
        if (hfs_u16(backup) == 0x4244U && hfs_u16(backup + 14) == bitmap_sector && hfs_u16(backup + 18) == v->units && hfs_u32(backup + 20) == v->unit_size &&
            hfs_u16(backup + 28) == hfs_u16(mdb + 28) && backup[36] == mdb[36] && !xx_mem_compare(backup + 37, mdb + 37, mdb[36])) { end = at + 1024U; break; }
    }
    v->bytes = end;
    v->retained_memory = sizeof(*v) + (uint64_t)v->units * sizeof(*v->runs) + (uint64_t)objects * sizeof(*v->entries) +
        (uint64_t)(v->capacity ? v->capacity : 1U) * sizeof(*v->members) + v->path_bytes;
    xx_mem_free(v->names); v->names = NULL;
    xx_mem_free(v->overflow); v->overflow = NULL; xx_mem_free(v->threads); v->threads = NULL;
    xx_mem_free(v->bitmap); v->bitmap = NULL; xx_mem_free(v->claimed); v->claimed = NULL;
    return v;
fail:
    hfs_view_free(v); return NULL;
}
static void hfs_vtable_destroy(Abstractformat *self) { xx_hfs_destroy((xx_hfs *)self); }
void xx_hfs_init(xx_hfs *v, xx_io_device *device, int64_t base) {
    if (!v) return; xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->format.endian = XX_ENDIAN_BIG; v->format.file_type = HFS_TYPE; v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-hfs-fs"); xx_format_set_extension(&v->format, "img");
    v->format.check_is_valid = xx_hfs_check_is_valid; v->format.handle_base_info = xx_hfs_handle_base_info;
    v->format.get_format_size = xx_hfs_get_format_size; v->format.get_number_of_archive_records = xx_hfs_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_hfs_create_archive_records_reading; v->format.get_current_archive_record = xx_hfs_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_hfs_archive_record_move_to_next; v->format.unpack_current_archive_record = xx_hfs_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_hfs_free_archive_records_reading; v->format.destroy = hfs_vtable_destroy;
}
xx_hfs *xx_hfs_create(xx_io_device *device, int64_t base) { xx_hfs *v = (xx_hfs *)xx_mem_alloc(sizeof(*v)); if (v) xx_hfs_init(v, device, base); return v; }
void xx_hfs_destroy(xx_hfs *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_hfs_free(xx_hfs *v) { if (v) { xx_hfs_destroy(v); xx_mem_free(v); } }
bool xx_hfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { hfs_view *v = hfs_parse(self, pd); bool ok = v != NULL; hfs_view_free(v); return ok; }
bool xx_hfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hfs *volume = (xx_hfs *)self; hfs_view *v = hfs_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = v->count; volume->volume_size = v->bytes; volume->allocation_block_size = v->unit_size;
    volume->allocation_block_count = v->units; volume->file_count = v->files; volume->folder_count = v->folders;
    xx_mem_copy(volume->volume_name, v->volume_name, sizeof(volume->volume_name));
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count; total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; hfs_view_free(v); return true;
}
int64_t xx_hfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_hfs_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_hfs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_hfs_handle_base_info(self, pd) ? ((xx_hfs *)self)->number_of_records : 0U; }
static bool hfs_record(xx_archive_record *record, hfs_view *v, const hfs_member *member) {
    size_t available;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)member->header; record->header_size = member->header_size;
    record->data_offset = member->fork.count ? v->base + (int64_t)hfs_address(v, &member->fork, 0U, &available) : -1;
    record->compressed_size = member->fork.size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->fork.size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->fork.size) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->flags) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->directory);
}
static bool hfs_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy; if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_hfs_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    hfs_view *v = hfs_parse(self, pd); xx_archive_record_state *state; if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { hfs_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = hfs_view_free; state->total_records = (int64_t)v->count;
    if (!hfs_options(&state->options, options) || (v->count && !hfs_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_hfs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) { return self && state && state->format == self && state->has_record ? &state->current_record : NULL; }
bool xx_hfs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    hfs_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (hfs_view *)state->internal_state) || hfs_stopped(pd)) return false;
    if (v->index + 1U >= v->count) { xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); state->has_record = false; return false; }
    if (!hfs_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t hfs_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
    const xx_var *var = xx_format_resolve_extra_parameter(self, options, id);
    if (!var) return fallback;
    switch (var->type) {
        case XX_VAR_TYPE_UINT8: case XX_VAR_TYPE_UINT16: case XX_VAR_TYPE_UINT32: case XX_VAR_TYPE_UINT64: return xx_var_get_u64(var);
        case XX_VAR_TYPE_INT8: case XX_VAR_TYPE_INT16: case XX_VAR_TYPE_INT32: case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(var); return value < 0 ? fallback : (uint64_t)value;
        }
        default: return fallback;
    }
}
static bool hfs_extract_limits(Abstractformat *self, xx_archive_record_state *state, const hfs_member *member, size_t *buffer_size) {
    const hfs_view *v = (const hfs_view *)state->internal_state;
    *buffer_size = member->fork.size < HFS_COPY ? member->fork.size : HFS_COPY;
    return member->fork.size <= hfs_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        v->retained_memory + sizeof(*state) + *buffer_size <= hfs_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_hfs_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    hfs_view *v; const hfs_member *member; uint8_t *buffer; uint32_t done = 0U; size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record || !(v = (hfs_view *)state->internal_state) || v->index >= v->count || hfs_stopped(pd)) return false;
    member = v->members + v->index;
    if (!hfs_extract_limits(self, state, member, &buffer_size)) return false;
    if (member->directory) return true;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < member->fork.size) {
        size_t part, written = 0U; uint64_t address = hfs_address(v, &member->fork, done, &part);
        if (!part) { ok = false; break; }
        if (part > buffer_size) part = buffer_size;
        if (part > member->fork.size - done) part = member->fork.size - done;
        if (!hfs_read(v, address, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !hfs_stopped(pd)) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; } written += (size_t)got;
        }
        if (!ok || hfs_stopped(pd)) { ok = false; break; } done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok && !hfs_stopped(pd);
}
static xx_io_device *hfs_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination); *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_hfs.tmp.%u", attempt); candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (hfs_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; } xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_hfs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    hfs_view *v; const xx_var *parameter, *overwrite_parameter; const char *base = NULL;
    char *owned = NULL, *path = NULL, *staged = NULL; size_t buffer_size; bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record || !(v = (hfs_view *)state->internal_state) || v->index >= v->count || hfs_stopped(pd)) return false;
    if (!hfs_extract_limits(self, state, v->members + v->index, &buffer_size)) return false;
    parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE); overwrite = overwrite_parameter && xx_var_get_bool(overwrite_parameter);
    if (!parameter) return xx_hfs_extract_record_to_device(self, state, NULL, pd);
    if (parameter->type == XX_VAR_TYPE_STRING || parameter->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(parameter);
    else if (parameter->type == XX_VAR_TYPE_WSTRING || parameter->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(parameter)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path) goto done;
    if (v->members[v->index].directory) { ok = !hfs_stopped(pd) && xx_store_create_dirs_a(path, true); goto done; }
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = hfs_stage(path, &staged); if (!output) goto done;
        ok = xx_hfs_extract_record_to_device(self, state, output, pd); if (xx_io_close(output)) ok = false;
    }
    if (hfs_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(staged, path, overwrite);
done:
    if (!ok && staged) xx_io_file_remove_a(staged);
    xx_str_free(staged); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_hfs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }

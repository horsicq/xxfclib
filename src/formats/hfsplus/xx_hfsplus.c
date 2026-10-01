/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of Apple's published TN1150 disk layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hfsplus/xx_hfsplus.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xx_hfsplus_unicode.h"
#include "xx_hfsplus_hostfold.h"
#ifdef HFSPLUS
#define HP_TYPE XX_FILE_TYPE_HFSPLUS
#else
#define HP_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define HP_OBJECTS 100000U
#define HP_RECORDS 200000U
#define HP_NODES 131072U
#define HP_UNITS 16777216U
#define HP_RUNS 400000U
#define HP_COPY 65536U
#define HP_COMPONENT 128U
#define HP_PATH 4096U
#define HP_WORK 64000000U
#define HP_PATH_MEMORY (UINT64_C(64) * 1024U * 1024U)
typedef struct hp_run_s { uint32_t logical, start, count; } hp_run;
typedef struct hp_fork_s { uint64_t size; uint32_t blocks, first, count; } hp_fork;
typedef struct hp_name_s { uint16_t length, text[255]; } hp_name;
typedef struct hp_key_s { uint32_t id, block; uint8_t fork; hp_name name; } hp_key;
typedef struct hp_overflow_s { uint32_t id, block; uint8_t fork, extents[64]; bool used; } hp_overflow;
typedef struct hp_entry_s {
    uint32_t id, parent, children, valence;
    uint16_t type, flags, record_size;
    uint64_t header;
    hp_name name;
    uint8_t extents[128], path_state, depth;
    hp_fork data, resource;
    char *path;
} hp_entry;
typedef struct hp_thread_s { uint32_t id, parent; uint16_t type; hp_name name; } hp_thread;
typedef struct hp_member_s {
    char *name; uint64_t header; uint16_t header_size, flags;
    bool directory, resource; hp_fork fork;
} hp_member;
typedef struct hp_view_s {
    xx_io_device *device; int64_t base; uint64_t bytes, path_bytes, retained_memory;
    uint32_t unit_size, units, files, folders, attributes, work, run_count, run_capacity, overflow_capacity;
    uint16_t signature, version;
    bool bitmap_ready, binary;
    uint8_t *bitmap, *claimed;
    hp_run *runs;
    hp_overflow *overflow; size_t overflow_count;
    hp_entry *entries; size_t entry_count;
    hp_thread *threads; size_t thread_count;
    hp_member *members; size_t count, index, capacity;
    uint32_t *names; size_t name_capacity;
    char volume_name[1024];
} hp_view;
typedef struct hp_node_s { uint8_t *bytes; uint16_t count; } hp_node;
typedef struct hp_tree_s {
    hp_view *view; hp_fork fork; unsigned type;
    uint32_t nodes, free_nodes, root, records, first, last, leaf_count, attributes;
    uint16_t depth, node_size, max_key;
    uint8_t *bitmap, *visited;
    uint32_t previous[9], next[9];
} hp_tree;
static uint16_t hp_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t hp_u32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t hp_u64(const uint8_t *p) { return ((uint64_t)hp_u32(p) << 32) | hp_u32(p + 4); }
static bool hp_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool hp_work(hp_view *v, xx_pd_struct *pd) { return !hp_stopped(pd) && ++v->work <= HP_WORK; }
static bool hp_bit(const uint8_t *map, uint32_t n) { return (map[n / 8U] & (0x80U >> (n % 8U))) != 0U; }
static void hp_set(uint8_t *map, uint32_t n) { map[n / 8U] |= (uint8_t)(0x80U >> (n % 8U)); }
static bool hp_read(const hp_view *v, uint64_t offset, void *buffer, size_t size, xx_pd_struct *pd) {
    int64_t saved; size_t done = 0U; bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) || hp_stopped(pd)) return false;
    saved = xx_io_tell(v->device); if (saved < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !hp_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)buffer + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, saved, SEEK_SET)) ok = false;
    return ok && !hp_stopped(pd);
}
static uint64_t hp_address(const hp_view *v, const hp_fork *fork, uint64_t offset, size_t *available) {
    uint64_t block64 = offset / v->unit_size;
    uint32_t block, within = (uint32_t)(offset % v->unit_size), lo = 0U, hi = fork->count;
    if (block64 > UINT32_MAX) { *available = 0U; return UINT64_MAX; }
    block = (uint32_t)block64;
    while (lo < hi) {
        uint32_t middle = lo + (hi - lo) / 2U;
        const hp_run *run = v->runs + fork->first + middle;
        if (block < run->logical) hi = middle;
        else if (block - run->logical >= run->count) lo = middle + 1U;
        else {
            uint64_t bytes = (uint64_t)(run->count - (block - run->logical)) * v->unit_size - within;
            *available = bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
            return (uint64_t)(run->start + block - run->logical) * v->unit_size + within;
        }
    }
    *available = 0U; return UINT64_MAX;
}
static bool hp_fork_read(hp_view *v, const hp_fork *fork, uint64_t offset, void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (offset > fork->size || size > fork->size - offset) return false;
    while (done < size) {
        size_t part; uint64_t address = hp_address(v, fork, offset + done, &part);
        if (!part) return false;
        if (part > size - done) part = size - done;
        if (!hp_read(v, address, (uint8_t *)buffer + done, part, pd)) return false;
        done += part;
    }
    return !hp_stopped(pd);
}
static bool hp_name_read(const uint8_t *raw, size_t size, hp_name *name) {
    size_t i;
    if (size < 2U || (name->length = hp_u16(raw)) > 255U || (size_t)name->length * 2U > size - 2U) return false;
    for (i = 0U; i < name->length; ++i) name->text[i] = hp_u16(raw + 2U + i * 2U);
    for (i = 0U; i < name->length; ++i) {
        uint16_t c = name->text[i];
        if (c >= 0xD800U && c <= 0xDBFFU) { if (++i >= name->length || name->text[i] < 0xDC00U || name->text[i] > 0xDFFFU) return false; }
        else if (c >= 0xDC00U && c <= 0xDFFFU) return false;
    }
    return true;
}
static int hp_name_compare(const hp_name *a, const hp_name *b, bool binary) {
    size_t i = 0U, j = 0U;
    if (binary) {
        size_t n = a->length < b->length ? a->length : b->length, k;
        for (k = 0U; k < n; ++k) if (a->text[k] != b->text[k]) return a->text[k] < b->text[k] ? -1 : 1;
        return a->length == b->length ? 0 : (a->length < b->length ? -1 : 1);
    }
    for (;;) {
        uint16_t x = 0U, y = 0U; bool end_x, end_y;
        while (i < a->length) { x = hp_fold(a->text[i++]); if (x) break; }
        while (j < b->length) { y = hp_fold(b->text[j++]); if (y) break; }
        end_x = x == 0U; end_y = y == 0U;
        if (end_x || end_y) return end_x == end_y ? 0 : (end_x ? -1 : 1);
        if (x != y) return x < y ? -1 : 1;
    }
}
static int hp_key_compare(const hp_key *a, const hp_key *b, unsigned type, bool binary) {
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    if (type != 1U) {
        if (a->fork != b->fork) return a->fork < b->fork ? -1 : 1;
        return a->block == b->block ? 0 : (a->block < b->block ? -1 : 1);
    }
    return hp_name_compare(&a->name, &b->name, binary);
}
static bool hp_key_read(const hp_tree *tree, const uint8_t *raw, size_t size, bool index, hp_key *key, size_t *payload) {
    size_t length;
    if (size < 2U) return false;
    length = hp_u16(raw); xx_mem_zero(key, sizeof(*key));
    *payload = (index && !(tree->attributes & 4U) ? tree->max_key : length) + 2U;
    if (*payload > size || (length & 1U) || length > tree->max_key) return false;
    if (tree->type == 1U) {
        if (length < 6U || length > 516U || !hp_name_read(raw + 6U, length - 4U, &key->name) || length != 6U + (size_t)key->name.length * 2U) return false;
        key->id = hp_u32(raw + 2U);
    } else {
        if (length != 10U || (raw[2] != 0U && raw[2] != 0xFFU)) return false;
        key->fork = raw[2]; key->id = hp_u32(raw + 4U); key->block = hp_u32(raw + 8U);
    }
    return key->id != 0U;
}
static bool hp_extent_valid(const hp_view *v, const uint8_t *data) {
    unsigned i; bool end = false;
    for (i = 0U; i < 8U; ++i) {
        uint32_t start = hp_u32(data + i * 8U), count = hp_u32(data + i * 8U + 4U);
        if (!count) { if (start) return false; end = true; }
        else if (end || start >= v->units || count > v->units - start) return false;
    }
    return true;
}
static bool hp_extent_add(hp_view *v, hp_fork *fork, const uint8_t *data, uint32_t target, uint32_t *logical, xx_pd_struct *pd) {
    unsigned i;
    if (!hp_extent_valid(v, data)) return false;
    for (i = 0U; i < 8U; ++i) {
        uint32_t start = hp_u32(data + i * 8U), count = hp_u32(data + i * 8U + 4U), k;
        if (!count) break;
        if (*logical > target || count > target - *logical || v->run_count >= v->run_capacity) return false;
        for (k = start; k < start + count; ++k) {
            if (!hp_work(v, pd) || (v->bitmap_ready && !hp_bit(v->bitmap, k)) || hp_bit(v->claimed, k)) return false;
            hp_set(v->claimed, k);
        }
        v->runs[v->run_count].start = start; v->runs[v->run_count].count = count;
        v->runs[v->run_count].logical = *logical; ++v->run_count; ++fork->count; *logical += count;
    }
    return true;
}
static hp_overflow *hp_overflow_find(hp_view *v, uint32_t id, uint8_t type, uint32_t block) {
    size_t lo = 0U, hi = v->overflow_count;
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2U;
        hp_overflow *extra = v->overflow + middle;
        int compare = id == extra->id ? (type == extra->fork ? (block == extra->block ? 0 : (block < extra->block ? -1 : 1)) : (type < extra->fork ? -1 : 1)) : (id < extra->id ? -1 : 1);
        if (!compare) return extra;
        if (compare < 0) hi = middle; else lo = middle + 1U;
    }
    return NULL;
}
static bool hp_fork_build(hp_view *v, hp_fork *fork, uint32_t id, uint8_t type, const uint8_t *first, bool bootstrap, xx_pd_struct *pd) {
    uint32_t logical = 0U, target = fork->blocks;
    if (target > v->units || fork->size > (uint64_t)target * v->unit_size) return false;
    fork->first = v->run_count; fork->count = 0U;
    if (!hp_extent_add(v, fork, first, target, &logical, pd)) return false;
    while (logical < target) {
        uint32_t before = logical; hp_overflow *extra = bootstrap ? NULL : hp_overflow_find(v, id, type, logical);
        if (!extra || extra->used || !hp_work(v, pd)) return false;
        extra->used = true;
        if (!hp_extent_add(v, fork, extra->extents, target, &logical, pd) || logical == before) return false;
    }
    return !hp_stopped(pd);
}
static void hp_fork_decode(hp_fork *fork, const uint8_t *raw) {
    xx_mem_zero(fork, sizeof(*fork)); fork->size = hp_u64(raw); fork->blocks = hp_u32(raw + 12U);
}
static uint16_t hp_offset(const hp_tree *tree, const hp_node *node, unsigned index) { return hp_u16(node->bytes + tree->node_size - 2U - index * 2U); }
static bool hp_node_read(hp_tree *tree, uint32_t id, hp_node *node, xx_pd_struct *pd) {
    uint32_t i, previous = 0U, table;
    if (id >= tree->nodes || !hp_work(tree->view, pd) || !hp_fork_read(tree->view, &tree->fork, (uint64_t)id * tree->node_size, node->bytes, tree->node_size, pd)) return false;
    node->count = hp_u16(node->bytes + 10);
    if (node->count > (tree->node_size - 16U) / 4U) return false;
    table = tree->node_size - 2U * (node->count + 1U);
    for (i = 0U; i <= node->count; ++i) {
        uint16_t at = hp_offset(tree, node, i);
        if (at < 14U || at > table || (at & 1U) || (i && at <= previous)) return false;
        previous = at;
    }
    return hp_offset(tree, node, 0U) == 14U;
}
static bool hp_leaf(hp_tree *tree, const hp_key *key, const uint8_t *data, size_t size, uint64_t address) {
    hp_view *v = tree->view;
    if (tree->type == 0U) {
        hp_overflow *extra;
        if (size != 64U || !hp_extent_valid(v, data) || !hp_u32(data + 4U) || v->overflow_count >= v->overflow_capacity || key->id < 4U ||
            (key->id < 16U && key->id != 4U && key->id != 5U && key->id != 6U && key->id != 7U && key->id != 8U) || (key->id != 5U && !key->block)) return false;
        extra = v->overflow + v->overflow_count++; extra->id = key->id; extra->fork = key->fork; extra->block = key->block;
        xx_mem_copy(extra->extents, data, 64U); return true;
    }
    if (tree->type != 1U) return false; /* Nonempty extended-attribute trees are outside this reader. */
    {
        uint16_t type;
        if (size < 2U || (type = hp_u16(data)) < 1U || type > 4U) return false;
        if (type >= 3U) {
            hp_thread *thread;
            if (key->name.length || size < 10U || v->thread_count >= (size_t)v->files + v->folders + 1U) return false;
            thread = v->threads + v->thread_count++;
            if (!hp_name_read(data + 8U, size - 8U, &thread->name) || !thread->name.length || size != 10U + (size_t)thread->name.length * 2U) return false;
            thread->id = key->id; thread->parent = hp_u32(data + 4U); thread->type = type; return thread->parent != 0U;
        } else {
            hp_entry *entry; uint16_t mode;
            if (!key->name.length || size != (type == 1U ? 88U : 248U) || v->entry_count >= (size_t)v->files + v->folders + 1U) return false;
            entry = v->entries + v->entry_count++; entry->type = type; entry->parent = key->id; entry->name = key->name;
            entry->flags = hp_u16(data + 2U); entry->id = hp_u32(data + 8U); entry->header = address; entry->record_size = (uint16_t)size;
            mode = hp_u16(data + 42U) & 0170000U;
            /* Refuse protected/compressed/attribute and hard-link features before exposing any members. */
            if ((entry->flags & 0x006CU) || (data[41] & 0x20U) || (mode && mode != (type == 1U ? 0040000U : 0100000U) && !(type == 2U && mode == 0120000U))) return false;
            if (type == 1U) entry->valence = hp_u32(data + 4U);
            else {
                if ((hp_u32(data + 48U) == UINT32_C(0x686C6E6B) && hp_u32(data + 52U) == UINT32_C(0x6866732B)) ||
                    (hp_u32(data + 48U) == UINT32_C(0x66647270) && hp_u32(data + 52U) == UINT32_C(0x4D414353))) return false;
                hp_fork_decode(&entry->data, data + 88U); hp_fork_decode(&entry->resource, data + 168U);
                xx_mem_copy(entry->extents, data + 104U, 64U); xx_mem_copy(entry->extents + 64U, data + 184U, 64U);
            }
            return entry->id == 2U ? type == 1U && key->id == 1U : entry->id >= 16U;
        }
    }
}
static bool hp_walk_node(hp_tree *tree, uint32_t id, uint16_t height, hp_key *first, hp_key *last, xx_pd_struct *pd) {
    hp_node node; hp_key previous; uint16_t i; bool have = false, ok = false;
    if (!id || id >= tree->nodes || !height || height > 8U || !hp_bit(tree->bitmap, id) || tree->visited[id]) return false;
    node.bytes = (uint8_t *)xx_mem_alloc(tree->node_size); if (!node.bytes) return false;
    if (!hp_node_read(tree, id, &node, pd)) goto done;
    tree->visited[id] = 1U;
    if (!node.count || node.bytes[9] != height || node.bytes[8] != (height == 1U ? 0xFFU : 0U) ||
        hp_u32(node.bytes + 4U) != tree->previous[height] || (tree->previous[height] && tree->next[height] != id)) goto done;
    if (height == 1U && !tree->previous[1] && id != tree->first) goto done;
    tree->previous[height] = id; tree->next[height] = hp_u32(node.bytes);
    if (tree->next[height] >= tree->nodes) goto done;
    for (i = 0U; i < node.count; ++i) {
        size_t payload, size = hp_offset(tree, &node, i + 1U) - hp_offset(tree, &node, i);
        const uint8_t *record = node.bytes + hp_offset(tree, &node, i); hp_key key, end;
        if (!hp_work(tree->view, pd) || !hp_key_read(tree, record, size, height != 1U, &key, &payload) ||
            (have && hp_key_compare(&previous, &key, tree->type, tree->view->binary) >= 0)) goto done;
        if (height == 1U) {
            size_t available; uint64_t address = hp_address(tree->view, &tree->fork, (uint64_t)id * tree->node_size + hp_offset(tree, &node, i) + payload, &available);
            if (!available || ++tree->leaf_count > tree->records || !hp_leaf(tree, &key, record + payload, size - payload, address)) goto done;
            end = key;
        } else {
            hp_key child_first;
            if (size - payload != 4U || !hp_walk_node(tree, hp_u32(record + payload), height - 1U, &child_first, &end, pd) ||
                hp_key_compare(&child_first, &key, tree->type, tree->view->binary)) goto done;
        }
        if (!have) *first = key;
        previous = end; *last = end; have = true;
    }
    ok = true;
done:
    xx_mem_free(node.bytes); return ok;
}
static bool hp_tree_read(hp_view *v, const hp_fork *fork, unsigned type, xx_pd_struct *pd) {
    hp_tree tree; hp_node header, map; hp_key first, last;
    uint32_t map_node, previous = 0U, covered, i, free_count = 0U, map_bytes; bool ok = false;
    uint8_t probe[120]; const uint8_t *record;
    xx_mem_zero(&tree, sizeof(tree)); xx_mem_zero(&header, sizeof(header)); xx_mem_zero(&map, sizeof(map)); tree.view = v; tree.fork = *fork; tree.type = type;
    if (!hp_fork_read(v, fork, 0U, probe, sizeof(probe), pd)) goto done;
    tree.node_size = hp_u16(probe + 32U);
    if (tree.node_size < 512U || tree.node_size > 32768U || (tree.node_size & (tree.node_size - 1U)) || fork->size % tree.node_size ||
        fork->size / tree.node_size > HP_NODES || !(tree.nodes = (uint32_t)(fork->size / tree.node_size))) goto done;
    header.bytes = (uint8_t *)xx_mem_alloc(tree.node_size); map.bytes = (uint8_t *)xx_mem_alloc(tree.node_size);
    if (!header.bytes || !map.bytes || !hp_node_read(&tree, 0U, &header, pd) || header.bytes[8] != 1U || header.bytes[9] || hp_u32(header.bytes + 4U) || header.count != 3U ||
        hp_offset(&tree, &header, 1U) - hp_offset(&tree, &header, 0U) != 106U || hp_offset(&tree, &header, 2U) - hp_offset(&tree, &header, 1U) != 128U) goto done;
    record = header.bytes + 14U; tree.depth = hp_u16(record); tree.root = hp_u32(record + 2U); tree.records = hp_u32(record + 6U);
    tree.first = hp_u32(record + 10U); tree.last = hp_u32(record + 14U); tree.free_nodes = hp_u32(record + 26U);
    tree.max_key = hp_u16(record + 20U); tree.attributes = hp_u32(record + 38U);
    if (tree.max_key != (type == 1U ? 516U : (type == 0U ? 10U : 266U)) || hp_u32(record + 22U) != tree.nodes ||
        record[36] || !(tree.attributes & 2U) || tree.depth > 8U || tree.records > HP_RECORDS || tree.root >= tree.nodes || tree.first >= tree.nodes || tree.last >= tree.nodes || tree.free_nodes >= tree.nodes ||
        (type == 1U && tree.node_size < 2048U) || (type == 2U && tree.records)) goto done;
    if (type == 1U) {
        if (v->signature == 0x4858U) { if (record[37] != 0xCFU && record[37] != 0xBCU) goto done; v->binary = record[37] == 0xBCU; }
        else v->binary = false; /* TN1150 reserves keyCompareType outside HFSX. */
    }
    tree.bitmap = (uint8_t *)xx_mem_alloc((tree.nodes + 7U) / 8U); tree.visited = (uint8_t *)xx_mem_alloc(tree.nodes);
    if (!tree.bitmap || !tree.visited) goto done;
    xx_mem_zero(tree.bitmap, (tree.nodes + 7U) / 8U); xx_mem_zero(tree.visited, tree.nodes); tree.visited[0] = 2U;
    map_bytes = hp_offset(&tree, &header, 3U) - hp_offset(&tree, &header, 2U);
    if (!map_bytes || map_bytes > tree.node_size - 256U) goto done;
    covered = tree.nodes < map_bytes * 8U ? tree.nodes : map_bytes * 8U;
    xx_mem_copy(tree.bitmap, header.bytes + hp_offset(&tree, &header, 2U), (covered + 7U) / 8U);
    map_node = hp_u32(header.bytes);
    while (map_node) {
        uint32_t more;
        if (map_node >= tree.nodes || tree.visited[map_node] || covered >= tree.nodes || !hp_node_read(&tree, map_node, &map, pd) ||
            map.bytes[8] != 2U || map.bytes[9] || map.count != 1U || hp_u32(map.bytes + 4U) != previous) goto done;
        map_bytes = hp_offset(&tree, &map, 1U) - hp_offset(&tree, &map, 0U);
        if (!map_bytes || map_bytes > tree.node_size - 18U || (covered & 7U)) goto done;
        tree.visited[map_node] = 2U; more = tree.nodes - covered; if (more > map_bytes * 8U) more = map_bytes * 8U;
        xx_mem_copy(tree.bitmap + covered / 8U, map.bytes + 14U, (more + 7U) / 8U); covered += more;
        previous = map_node; map_node = hp_u32(map.bytes);
    }
    if (covered < tree.nodes || !hp_bit(tree.bitmap, 0U)) goto done;
    if (!tree.depth) { if (type == 1U || tree.root || tree.records || tree.first || tree.last) goto done; }
    else if (!tree.root || !tree.first || !tree.last || !tree.records || !hp_walk_node(&tree, tree.root, tree.depth, &first, &last, pd) || tree.leaf_count != tree.records || tree.previous[1] != tree.last) goto done;
    for (i = 1U; i <= tree.depth; ++i) if (tree.next[i]) goto done;
    for (i = 0U; i < tree.nodes; ++i) {
        if (!hp_work(v, pd) || hp_bit(tree.bitmap, i) != (tree.visited[i] != 0U)) goto done;
        if (!tree.visited[i]) ++free_count;
    }
    ok = free_count == tree.free_nodes && !hp_stopped(pd);
done:
    xx_mem_free(header.bytes); xx_mem_free(map.bytes); xx_mem_free(tree.bitmap); xx_mem_free(tree.visited); return ok;
}
static int hp_entry_compare(const void *a, const void *b) {
    uint32_t x = ((const hp_entry *)a)->id, y = ((const hp_entry *)b)->id;
    return x == y ? 0 : (x < y ? -1 : 1);
}
static int hp_thread_compare(const void *a, const void *b) {
    uint32_t x = ((const hp_thread *)a)->id, y = ((const hp_thread *)b)->id;
    return x == y ? 0 : (x < y ? -1 : 1);
}
static hp_entry *hp_entry_find(hp_view *v, uint32_t id) {
    size_t lo = 0U, hi = v->entry_count;
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2U;
        if (v->entries[middle].id == id) return v->entries + middle;
        if (v->entries[middle].id > id) hi = middle; else lo = middle + 1U;
    }
    return NULL;
}
static hp_thread *hp_thread_find(hp_view *v, uint32_t id) {
    size_t lo = 0U, hi = v->thread_count;
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2U;
        if (v->threads[middle].id == id) return v->threads + middle;
        if (v->threads[middle].id > id) hi = middle; else lo = middle + 1U;
    }
    return NULL;
}
static size_t hp_utf8(char *output, uint32_t c) {
    if (c < 0x80U) { output[0] = (char)c; return 1U; }
    if (c < 0x800U) { output[0] = (char)(0xC0U | (c >> 6)); output[1] = (char)(0x80U | (c & 63U)); return 2U; }
    if (c < 0x10000U) { output[0] = (char)(0xE0U | (c >> 12)); output[1] = (char)(0x80U | ((c >> 6) & 63U)); output[2] = (char)(0x80U | (c & 63U)); return 3U; }
    output[0] = (char)(0xF0U | (c >> 18)); output[1] = (char)(0x80U | ((c >> 12) & 63U)); output[2] = (char)(0x80U | ((c >> 6) & 63U)); output[3] = (char)(0x80U | (c & 63U)); return 4U;
}
typedef struct hp_host_iter_s { const char *text; uint32_t pending[3]; unsigned at, count; } hp_host_iter;
static bool hp_host_next(hp_host_iter *it, uint32_t *result) {
    const unsigned char *p; uint32_t c; size_t lo, hi;
    if (it->at < it->count) { *result = it->pending[it->at++]; return true; }
    if (!*it->text) return false;
    p = (const unsigned char *)it->text; c = *p++;
    if (c >= 0xF0U) { c = (c & 7U) << 18; c |= (*p++ & 63U) << 12; c |= (*p++ & 63U) << 6; c |= *p++ & 63U; }
    else if (c >= 0xE0U) { c = (c & 15U) << 12; c |= (*p++ & 63U) << 6; c |= *p++ & 63U; }
    else if (c >= 0xC0U) { c = (c & 31U) << 6; c |= *p++ & 63U; }
    it->text = (const char *)p; lo = 0U; hi = sizeof(hp_host_folds) / sizeof(hp_host_folds[0]);
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2U;
        if (hp_host_folds[middle].from == c) {
            const hp_host_fold *fold = hp_host_folds + middle;
            it->pending[0] = fold->to[0]; it->pending[1] = fold->to[1]; it->pending[2] = fold->to[2];
            it->at = 1U; it->count = fold->length; *result = fold->to[0]; return true;
        }
        if (hp_host_folds[middle].from > c) hi = middle; else lo = middle + 1U;
    }
    it->at = it->count = 0U; *result = c; return true;
}
static bool hp_equal(const char *a, const char *b) {
    hp_host_iter left, right; uint32_t x, y; bool have_x, have_y;
    xx_mem_zero(&left, sizeof(left)); xx_mem_zero(&right, sizeof(right)); left.text = a; right.text = b;
    for (;;) {
        have_x = hp_host_next(&left, &x); have_y = hp_host_next(&right, &y);
        if (!have_x || !have_y) return have_x == have_y;
        if (x != y) return false;
    }
}
static uint32_t hp_hash(const char *text) {
    hp_host_iter it; uint32_t c, hash = UINT32_C(2166136261);
    xx_mem_zero(&it, sizeof(it)); it.text = text;
    while (hp_host_next(&it, &c)) { hash ^= c; hash *= UINT32_C(16777619); }
    return hash;
}
static bool hp_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    size_t n = 0U, i; char stem[16];
    while (name[n] && name[n] != '.' && n + 1U < sizeof(stem)) { stem[n] = name[n]; ++n; }
    stem[n] = 0;
    if (name[n] && name[n] != '.') return false;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) if (hp_equal(stem, devices[i])) return true;
    return n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm') ||
         (stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't'));
}
static bool hp_name_output(const hp_name *name, char *output, bool host) {
    size_t i, at = 0U;
    if (!name->length || name->length > 255U) return false;
    for (i = 0U; i < name->length; ++i) {
        uint32_t c = name->text[i]; size_t length; char encoded[4];
        if (c >= 0xD800U && c <= 0xDBFFU) { if (++i >= name->length || name->text[i] < 0xDC00U || name->text[i] > 0xDFFFU) return false; c = 0x10000U + ((c - 0xD800U) << 10) + name->text[i] - 0xDC00U; }
        else if (c >= 0xDC00U && c <= 0xDFFFU) return false;
        if (c < 0x20U || c == 0x7FU || (host && (c == ':' || c == '/' || c == '\\' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*'))) c = '_';
        length = hp_utf8(encoded, c);
        if (host && at + length > HP_COMPONENT) break;
        xx_mem_copy(output + at, encoded, length); at += length;
    }
    if (host) for (i = at; i && (output[i - 1U] == '.' || output[i - 1U] == ' '); --i) output[i - 1U] = '_';
    output[at] = 0;
    if (host && hp_device_name(output)) { for (i = at + 1U; i; --i) output[i] = output[i - 1U]; output[0] = '_'; }
    return true;
}
static bool hp_append(hp_view *v, const hp_entry *entry, const char *parent, const char *component,
    bool directory, bool resource, hp_member **result, xx_pd_struct *pd) {
    unsigned attempt; char leaf[264];
    if (v->count >= v->capacity) return false;
    for (attempt = 0U; attempt < HP_OBJECTS; ++attempt) {
        char *path; size_t slot; bool taken = false;
        if (!hp_work(v, pd)) return false;
        if (!attempt) xx_rt_snprintf(leaf, sizeof(leaf), "%s%s", component, resource ? ".rsrc" : "");
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s%s~%u", component, resource ? ".rsrc" : "", attempt + 1U);
        path = *parent ? xx_str_concat3(parent, "/", leaf) : xx_str_dup(leaf);
        if (!path) return false;
        if (xx_str_len(path) >= HP_PATH) { xx_str_free(path); return false; }
        slot = hp_hash(path) & (v->name_capacity - 1U);
        while (v->names[slot]) {
            if (!hp_work(v, pd)) { xx_str_free(path); return false; }
            if (hp_equal(path, v->members[v->names[slot] - 1U].name)) { taken = true; break; }
            slot = (slot + 1U) & (v->name_capacity - 1U);
        }
        if (!taken) {
            hp_member *member = v->members + v->count;
            size_t bytes = xx_str_len(path) + 1U;
            if (bytes > HP_PATH_MEMORY - v->path_bytes) { xx_str_free(path); return false; }
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
static bool hp_folder_path(hp_view *v, hp_entry *entry, unsigned depth, xx_pd_struct *pd) {
    hp_entry *parent; hp_member *member; char component[1024];
    if (entry->type != 1U || depth > 64U || entry->path_state == 1U || !hp_work(v, pd)) return false;
    if (entry->path_state == 2U) return true;
    entry->path_state = 1U;
    if (entry->id == 2U) {
        if (v->path_bytes >= HP_PATH_MEMORY) return false;
        entry->path = xx_str_dup(""); entry->path_state = 2U;
        if (entry->path) ++v->path_bytes; return entry->path != NULL;
    }
    parent = hp_entry_find(v, entry->parent);
    if (!parent || !hp_folder_path(v, parent, depth + 1U, pd) || parent->depth >= 64U || !hp_name_output(&entry->name, component, true) ||
        !hp_append(v, entry, parent->path, component, true, false, &member, pd)) return false;
    entry->depth = (uint8_t)(parent->depth + 1U);
    {
        size_t bytes = xx_str_len(member->name) + 1U;
        if (bytes > HP_PATH_MEMORY - v->path_bytes) return false;
        entry->path = xx_str_dup(member->name); entry->path_state = 2U;
        if (entry->path) v->path_bytes += bytes; return entry->path != NULL;
    }
}
static bool hp_catalog_finish(hp_view *v, xx_pd_struct *pd) {
    size_t i; uint32_t files = 0U, folders = 0U;
    xx_rt_qsort(v->entries, v->entry_count, sizeof(*v->entries), hp_entry_compare);
    xx_rt_qsort(v->threads, v->thread_count, sizeof(*v->threads), hp_thread_compare);
    if (!v->entry_count || v->entries[0].id != 2U || !hp_name_output(&v->entries[0].name, v->volume_name, false)) return false;
    for (i = 0U; i < v->thread_count; ++i) {
        hp_thread *thread = v->threads + i; hp_entry *entry = hp_entry_find(v, thread->id);
        if (!hp_work(v, pd) || (i && thread->id == v->threads[i - 1U].id) || !entry || thread->type != (entry->type == 1U ? 3U : 4U) ||
            thread->parent != entry->parent || thread->name.length != entry->name.length || xx_mem_compare(thread->name.text, entry->name.text, (size_t)entry->name.length * 2U)) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hp_entry *entry = v->entries + i; hp_entry *parent = entry->id == 2U ? NULL : hp_entry_find(v, entry->parent);
        hp_thread *thread = hp_thread_find(v, entry->id); char component[1024];
        if (!hp_work(v, pd) || (i && entry->id == v->entries[i - 1U].id) || !hp_name_output(&entry->name, component, true) ||
            (entry->id != 2U && (!parent || parent->type != 1U)) || ((entry->type == 1U || (entry->flags & 2U)) && !thread)) return false;
        if (parent) ++parent->children;
        if (entry->type == 1U) { if (entry->id != 2U) ++folders; }
        else {
            ++files;
            if (!hp_fork_build(v, &entry->data, entry->id, 0U, entry->extents, false, pd) ||
                !hp_fork_build(v, &entry->resource, entry->id, 0xFFU, entry->extents + 64U, false, pd)) return false;
        }
    }
    if (files != v->files || folders != v->folders) return false;
    for (i = 0U; i < v->entry_count; ++i) {
        hp_entry *entry = v->entries + i;
        if (entry->type == 1U && (entry->children != entry->valence || !hp_folder_path(v, entry, 0U, pd))) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hp_entry *entry = v->entries + i; hp_entry *parent; char component[1024];
        if (entry->type != 2U) continue;
        parent = hp_entry_find(v, entry->parent);
        if (!parent || !parent->path || !hp_name_output(&entry->name, component, true) || !hp_append(v, entry, parent->path, component, false, false, NULL, pd)) return false;
    }
    for (i = 0U; i < v->entry_count; ++i) {
        hp_entry *entry = v->entries + i; hp_entry *parent; char component[1024];
        if (entry->type != 2U || !entry->resource.blocks) continue;
        parent = hp_entry_find(v, entry->parent);
        if (!parent || !hp_name_output(&entry->name, component, true) || !hp_append(v, entry, parent->path, component, false, true, NULL, pd)) return false;
    }
    for (i = 0U; i < v->overflow_count; ++i) {
        hp_overflow *extra = v->overflow + i;
        if (!extra->used && extra->id == 5U && extra->fork == 0U && (v->attributes & 0x200U)) {
            hp_fork bad; uint32_t logical = extra->block;
            xx_mem_zero(&bad, sizeof(bad)); bad.first = v->run_count;
            if (!hp_extent_add(v, &bad, extra->extents, v->units, &logical, pd)) return false;
            extra->used = true;
        }
        if (!extra->used) return false;
    }
    return !hp_stopped(pd);
}
static void hp_view_free(void *pointer) {
    hp_view *v = (hp_view *)pointer; size_t i;
    if (!v) return;
    for (i = 0U; i < v->entry_count; ++i) xx_str_free(v->entries[i].path);
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v->bitmap); xx_mem_free(v->claimed); xx_mem_free(v->runs); xx_mem_free(v->overflow);
    xx_mem_free(v->entries); xx_mem_free(v->threads); xx_mem_free(v->members); xx_mem_free(v->names); xx_mem_free(v);
}
static hp_view *hp_parse(Abstractformat *self, xx_pd_struct *pd) {
    hp_view *v; uint8_t header[512]; hp_fork special[5];
    int64_t total; uint32_t objects, map_size, free_count = 0U, i, begin, tail; uint64_t end;
    if (!self || !self->device || self->base_address < 0 || hp_stopped(pd) || (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (hp_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL; xx_mem_zero(v, sizeof(*v));
    v->device = self->device; v->base = self->base_address; v->bytes = (uint64_t)(total - v->base);
    if (!hp_read(v, 1024U, header, sizeof(header), pd)) goto fail;
    v->signature = hp_u16(header); v->version = hp_u16(header + 2U); v->attributes = hp_u32(header + 4U);
    v->units = hp_u32(header + 44U); v->unit_size = hp_u32(header + 40U); v->files = hp_u32(header + 32U); v->folders = hp_u32(header + 36U);
    if (!((v->signature == 0x482BU && v->version == 4U) || (v->signature == 0x4858U && v->version == 5U)) ||
        !(v->attributes & 0x100U) || (v->attributes & UINT32_C(0x40006800)) || !v->units || v->units > HP_UNITS ||
        v->unit_size < 512U || v->unit_size > 16U * 1024U * 1024U || (v->unit_size & (v->unit_size - 1U)) ||
        v->files >= HP_OBJECTS || v->folders >= HP_OBJECTS || v->files + v->folders >= HP_OBJECTS) goto fail;
    end = (uint64_t)v->units * v->unit_size;
    if (end < 4096U || end > v->bytes || end > (uint64_t)(INT64_MAX - v->base)) goto fail;
    v->bytes = end; objects = v->files + v->folders + 1U; v->capacity = (size_t)v->files * 2U + v->folders;
    map_size = (v->units + 7U) / 8U; v->run_capacity = v->units < HP_RUNS ? v->units : HP_RUNS;
    v->overflow_capacity = v->units < HP_RECORDS ? v->units : HP_RECORDS;
    v->bitmap = (uint8_t *)xx_mem_alloc(map_size); v->claimed = (uint8_t *)xx_mem_alloc(map_size);
    v->runs = (hp_run *)xx_mem_alloc((size_t)v->run_capacity * sizeof(*v->runs));
    v->overflow = (hp_overflow *)xx_mem_alloc((size_t)v->overflow_capacity * sizeof(*v->overflow));
    v->entries = (hp_entry *)xx_mem_alloc((size_t)objects * sizeof(*v->entries)); v->threads = (hp_thread *)xx_mem_alloc((size_t)objects * sizeof(*v->threads));
    v->members = (hp_member *)xx_mem_alloc((v->capacity ? v->capacity : 1U) * sizeof(*v->members));
    v->name_capacity = 8U; while (v->name_capacity < v->capacity * 2U) v->name_capacity *= 2U;
    v->names = (uint32_t *)xx_mem_alloc(v->name_capacity * sizeof(*v->names));
    if (!v->bitmap || !v->claimed || !v->runs || !v->overflow || !v->entries || !v->threads || !v->members || !v->names) goto fail;
    xx_mem_zero(v->claimed, map_size); xx_mem_zero(v->overflow, (size_t)v->overflow_capacity * sizeof(*v->overflow));
    xx_mem_zero(v->entries, (size_t)objects * sizeof(*v->entries)); xx_mem_zero(v->threads, (size_t)objects * sizeof(*v->threads));
    xx_mem_zero(v->members, (v->capacity ? v->capacity : 1U) * sizeof(*v->members)); xx_mem_zero(v->names, v->name_capacity * sizeof(*v->names));
    begin = (1536U + v->unit_size - 1U) / v->unit_size; tail = (1024U + v->unit_size - 1U) / v->unit_size;
    if (begin + tail >= v->units) goto fail;
    for (i = 0U; i < begin; ++i) hp_set(v->claimed, i);
    for (i = v->units - tail; i < v->units; ++i) hp_set(v->claimed, i);
    for (i = 0U; i < 5U; ++i) hp_fork_decode(special + i, header + 112U + i * 80U);
    if (!hp_fork_build(v, special + 1U, 3U, 0U, header + 208U, true, pd) || !hp_tree_read(v, special + 1U, 0U, pd) ||
        !hp_fork_build(v, special, 6U, 0U, header + 128U, false, pd) || special[0].size < map_size || !hp_fork_read(v, special, 0U, v->bitmap, map_size, pd)) goto fail;
    v->bitmap_ready = true;
    for (i = 0U; i < v->units; ++i) {
        if (!hp_work(v, pd) || (hp_bit(v->claimed, i) && !hp_bit(v->bitmap, i))) goto fail;
        if (!hp_bit(v->bitmap, i)) ++free_count;
    }
    if (free_count != hp_u32(header + 48U)) goto fail;
    if (!hp_fork_build(v, special + 2U, 4U, 0U, header + 288U, false, pd) || !hp_tree_read(v, special + 2U, 1U, pd) ||
        !hp_fork_build(v, special + 3U, 8U, 0U, header + 368U, false, pd) || (special[3].size && !hp_tree_read(v, special + 3U, 2U, pd)) ||
        !hp_fork_build(v, special + 4U, 7U, 0U, header + 448U, false, pd) || !hp_catalog_finish(v, pd)) goto fail;
    v->retained_memory = sizeof(*v) + (uint64_t)v->run_capacity * sizeof(*v->runs) + (uint64_t)objects * sizeof(*v->entries) +
        (uint64_t)(v->capacity ? v->capacity : 1U) * sizeof(*v->members) + v->path_bytes;
    xx_mem_free(v->names); v->names = NULL; xx_mem_free(v->overflow); v->overflow = NULL; xx_mem_free(v->threads); v->threads = NULL;
    xx_mem_free(v->bitmap); v->bitmap = NULL; xx_mem_free(v->claimed); v->claimed = NULL;
    return v;
fail:
    hp_view_free(v); return NULL;
}

static void hp_vtable_destroy(Abstractformat *self) { xx_hfsplus_destroy((xx_hfsplus *)self); }
void xx_hfsplus_init(xx_hfsplus *v, xx_io_device *device, int64_t base) {
    if (!v) return; xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->format.endian = XX_ENDIAN_BIG; v->format.file_type = HP_TYPE; v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-hfsplus-fs"); xx_format_set_extension(&v->format, "img");
    v->format.check_is_valid = xx_hfsplus_check_is_valid; v->format.handle_base_info = xx_hfsplus_handle_base_info;
    v->format.get_format_size = xx_hfsplus_get_format_size; v->format.get_number_of_archive_records = xx_hfsplus_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_hfsplus_create_archive_records_reading; v->format.get_current_archive_record = xx_hfsplus_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_hfsplus_archive_record_move_to_next; v->format.unpack_current_archive_record = xx_hfsplus_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_hfsplus_free_archive_records_reading; v->format.destroy = hp_vtable_destroy;
}
xx_hfsplus *xx_hfsplus_create(xx_io_device *device, int64_t base) { xx_hfsplus *v = (xx_hfsplus *)xx_mem_alloc(sizeof(*v)); if (v) xx_hfsplus_init(v, device, base); return v; }
void xx_hfsplus_destroy(xx_hfsplus *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_hfsplus_free(xx_hfsplus *v) { if (v) { xx_hfsplus_destroy(v); xx_mem_free(v); } }
bool xx_hfsplus_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { hp_view *v = hp_parse(self, pd); bool ok = v != NULL; hp_view_free(v); return ok; }
bool xx_hfsplus_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hfsplus *volume = (xx_hfsplus *)self; hp_view *v = hp_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = v->count; volume->volume_size = v->bytes; volume->allocation_block_size = v->unit_size;
    volume->allocation_block_count = v->units; volume->file_count = v->files; volume->folder_count = v->folders; volume->signature = v->signature; volume->version = v->version; volume->case_sensitive = v->binary;
    xx_mem_copy(volume->volume_name, v->volume_name, sizeof(volume->volume_name));
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count; total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; hp_view_free(v); return true;
}
int64_t xx_hfsplus_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_hfsplus_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_hfsplus_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_hfsplus_handle_base_info(self, pd) ? ((xx_hfsplus *)self)->number_of_records : 0U; }
static bool hp_record(xx_archive_record *record, hp_view *v, const hp_member *member) {
    size_t available;
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)member->header; record->header_size = member->header_size;
    record->data_offset = member->fork.count ? v->base + (int64_t)hp_address(v, &member->fork, 0U, &available) : -1;
    record->compressed_size = member->fork.size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->fork.size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->fork.size) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->flags) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->directory);
}
static bool hp_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy; if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_hfsplus_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    hp_view *v = hp_parse(self, pd); xx_archive_record_state *state; if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { hp_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = hp_view_free; state->total_records = (int64_t)v->count;
    if (!hp_options(&state->options, options) || (v->count && !hp_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_hfsplus_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) { return self && state && state->format == self && state->has_record ? &state->current_record : NULL; }
bool xx_hfsplus_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    hp_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (hp_view *)state->internal_state) || hp_stopped(pd)) return false;
    if (v->index + 1U >= v->count) { xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); state->has_record = false; return false; }
    if (!hp_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t hp_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
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
static bool hp_extract_limits(Abstractformat *self, xx_archive_record_state *state, const hp_member *member, size_t *buffer_size) {
    const hp_view *v = (const hp_view *)state->internal_state;
    *buffer_size = (size_t)(member->fork.size < HP_COPY ? member->fork.size : HP_COPY);
    return member->fork.size <= hp_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        v->retained_memory + sizeof(*state) + *buffer_size <= hp_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_hfsplus_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    hp_view *v; const hp_member *member; uint8_t *buffer; uint64_t done = 0U; size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record || !(v = (hp_view *)state->internal_state) || v->index >= v->count || hp_stopped(pd)) return false;
    member = v->members + v->index;
    if (!hp_extract_limits(self, state, member, &buffer_size)) return false;
    if (member->directory) return true;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < member->fork.size) {
        size_t part, written = 0U; uint64_t address = hp_address(v, &member->fork, done, &part);
        if (!part) { ok = false; break; }
        if (part > buffer_size) part = buffer_size;
        if (part > member->fork.size - done) part = (size_t)(member->fork.size - done);
        if (!hp_read(v, address, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !hp_stopped(pd)) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; } written += (size_t)got;
        }
        if (!ok || hp_stopped(pd)) { ok = false; break; } done += part;
    }
    xx_mem_free(buffer); return ok && !hp_stopped(pd);
}
static xx_io_device *hp_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination); *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_hfsplus.tmp.%u", attempt); candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (hp_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; } xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_hfsplus_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    hp_view *v; const xx_var *parameter, *overwrite_parameter; const char *base = NULL;
    char *owned = NULL, *path = NULL, *staged = NULL; size_t buffer_size; bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record || !(v = (hp_view *)state->internal_state) || v->index >= v->count || hp_stopped(pd)) return false;
    if (!hp_extract_limits(self, state, v->members + v->index, &buffer_size)) return false;
    parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE); overwrite = overwrite_parameter && xx_var_get_bool(overwrite_parameter);
    if (!parameter) return xx_hfsplus_extract_record_to_device(self, state, NULL, pd);
    if (parameter->type == XX_VAR_TYPE_STRING || parameter->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(parameter);
    else if (parameter->type == XX_VAR_TYPE_WSTRING || parameter->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(parameter)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path) goto done;
    if (v->members[v->index].directory) { ok = !hp_stopped(pd) && xx_store_create_dirs_a(path, true); goto done; }
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = hp_stage(path, &staged); if (!output) goto done;
        ok = xx_hfsplus_extract_record_to_device(self, state, output, pd); if (xx_io_close(output)) ok = false;
    }
    if (hp_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(staged, path, overwrite);
done:
    if (!ok && staged) xx_io_file_remove_a(staged);
    xx_str_free(staged); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_hfsplus_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }

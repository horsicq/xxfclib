/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * APFS. The on-disk layout and the scope of this reader are documented in
 * include/xxfclib/formats/apfs/xx_apfs.h.
 *
 * Every block this reader touches is named by an attacker-controlled address,
 * so the order is always: bound the block number against the device, read,
 * verify the Fletcher-64 in obj_phys_t, check the object type, and only then
 * believe a field. The B-tree walk caps depth and node count and refuses to
 * revisit a block, so a node that points back into the tree ends a branch
 * instead of looping.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apfs/xx_apfs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_APFS lands the alias is
 * defined and this picks it up with no further change. */
#ifdef APFS
#define XX_APFS_FILE_TYPE XX_FILE_TYPE_APFS
#else
#define XX_APFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- on-disk sizes and offsets ------------------------------------------ */

#define XX_APFS_BTREE_NODE_DATA 56U
#define XX_APFS_BTREE_INFO_SIZE 40U
#define XX_APFS_OMAP_KEY_SIZE 16U
#define XX_APFS_OMAP_VAL_SIZE 16U
#define XX_APFS_INODE_VAL_MIN 92U
#define XX_APFS_DREC_VAL_MIN 18U
#define XX_APFS_EXTENT_VAL_MIN 16U

/* btn_flags. */
#define XX_APFS_BTNODE_ROOT 0x0001U
#define XX_APFS_BTNODE_LEAF 0x0002U
#define XX_APFS_BTNODE_FIXED_KV_SIZE 0x0004U

/* omap_val_t.ov_flags. */
#define XX_APFS_OMAP_VAL_DELETED 0x00000001U

/* apfs_superblock_t.apfs_incompatible_features. */
#define XX_APFS_INCOMPAT_CASE_INSENSITIVE UINT64_C(0x00000001)
#define XX_APFS_INCOMPAT_NORMALIZATION_INSENSITIVE UINT64_C(0x00000002)

/* j_inode_val_t extended field types. */
#define XX_APFS_INO_EXT_TYPE_NAME 4U
#define XX_APFS_INO_EXT_TYPE_DSTREAM 8U

/* j_drec_val_t.flags, low 4 bits: the POSIX d_type. */
#define XX_APFS_DT_DIR 4U
#define XX_APFS_DT_REG 8U

/* --- budgets ------------------------------------------------------------ */

#define XX_APFS_MIN_BLOCK_SIZE 512U
#define XX_APFS_MAX_BLOCK_SIZE 65536U
#define XX_APFS_MAX_NODES 40000U
#define XX_APFS_MAX_DEPTH 10U
#define XX_APFS_MAX_OMAP 200000U
#define XX_APFS_MAX_INODES 200000U
#define XX_APFS_MAX_DRECS 200000U
#define XX_APFS_MAX_ENTRIES 100000U
#define XX_APFS_MAX_NAME 512U
#define XX_APFS_MAX_PATH 4096U
#define XX_APFS_MAX_CHECKPOINT_BLOCKS 8192U

typedef struct xx_apfs_omap_entry_s {
    uint64_t oid;
    uint64_t xid;
    int64_t block;
} xx_apfs_omap_entry;

typedef struct xx_apfs_inode_s {
    uint64_t oid;
    uint64_t parent;
    uint64_t size;
    uint16_t mode;
    bool has_size;
    bool compressed;      /**< A com.apple.decmpfs xattr was seen. */
    uint32_t extent_count;
    uint64_t extent_logical;
    uint64_t extent_length;
    uint64_t extent_block;
} xx_apfs_inode;

typedef struct xx_apfs_drec_s {
    uint64_t parent;
    uint64_t child;
    char *name;
    uint8_t dtype;
    size_t next;          /**< 1-based index of the next entry in this dir. */
} xx_apfs_drec;

typedef struct xx_apfs_entry_s {
    char *name;
    bool is_folder;
    uint64_t size;
    int64_t data_offset;
    int64_t data_size;
    bool extractable;
} xx_apfs_entry;

/* Open-addressed uint64 -> 1-based index map; key 0 is never a real oid. */
typedef struct xx_apfs_map_s {
    uint64_t *keys;
    size_t *values;
    size_t capacity;
    size_t count;
} xx_apfs_map;

typedef struct xx_apfs_nx_s {
    uint32_t block_size;
    uint64_t block_count;
    uint64_t xid;
    uint8_t uuid[XX_APFS_UUID_SIZE];
    uint64_t features;
    uint64_t readonly_compatible_features;
    uint64_t incompatible_features;
    uint64_t omap_oid;
    int64_t xp_desc_base;
    uint32_t xp_desc_blocks;
    uint32_t xp_desc_index;
    uint32_t xp_desc_len;
    uint32_t max_file_systems;
    uint64_t newest_mounted_version;
    uint64_t fs_oid[XX_APFS_MAX_VOLUMES];
} xx_apfs_nx;

typedef struct xx_apfs_private_s {
    xx_apfs_nx nx;
    int64_t input_size;
    int64_t base_address;
    int64_t super_offset;
    bool from_checkpoint;

    /* Container object map. */
    xx_apfs_omap_entry *omap;
    size_t omap_count;
    size_t omap_capacity;
    xx_apfs_map omap_index;

    /* Per-volume scratch, reused for each volume in turn. */
    xx_apfs_omap_entry *vomap;
    size_t vomap_count;
    size_t vomap_capacity;
    xx_apfs_map vomap_index;

    xx_apfs_inode *inodes;
    size_t inode_count;
    size_t inode_capacity;
    xx_apfs_map inode_index;

    xx_apfs_drec *drecs;
    size_t drec_count;
    size_t drec_capacity;
    xx_apfs_map drec_head;

    xx_apfs_entry *entries;
    size_t entry_count;
    size_t entry_capacity;

    xx_apfs_map visited;
    size_t nodes;
    size_t bad_nodes;
    size_t unsupported;

    uint32_t volume_count;
    xx_apfs_volume_info volumes[XX_APFS_MAX_VOLUMES];
    /** Incompatible features of the volume currently being walked. */
    uint64_t current_volume_features;

    xx_io_device *device;
    xx_pd_struct *pd;
} xx_apfs_private;

typedef struct xx_apfs_archive_stream_s {
    xx_apfs_private *parsed;
    size_t index;
} xx_apfs_archive_stream;

typedef bool (*xx_apfs_leaf_cb)(xx_apfs_private *parsed, void *ctx,
                                const uint8_t *key, uint32_t key_len,
                                const uint8_t *value, uint32_t value_len);

static void xx_apfs_vtable_destroy(Abstractformat *self);
static void xx_apfs_private_free(xx_apfs_private *parsed);

/* ------------------------------------------------------------- helpers -- */

static bool xx_apfs_read_at(xx_io_device *device, int64_t offset, void *data,
                            size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_apfs_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_apfs_stopped(const xx_apfs_private *parsed) {
    return parsed && parsed->pd && xx_pd_is_stopped(parsed->pd);
}

/* APFS's Fletcher-64. The two running sums are kept modulo 2^32 - 1 over the
 * block's 32-bit little-endian words; the stored checksum is the pair of
 * complements that makes the whole block sum to zero. `data` must point just
 * past the 8-byte checksum field and `size` must be a multiple of 4. */
uint64_t xx_apfs_fletcher64(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    const uint64_t modulus = UINT64_C(0xFFFFFFFF);
    uint64_t sum1 = 0U;
    uint64_t sum2 = 0U;
    uint64_t check1;
    uint64_t check2;
    size_t index;
    if (!bytes) return 0U;
    for (index = 0U; index + 4U <= size; index += 4U) {
        uint64_t word = (uint64_t)bytes[index] |
                        ((uint64_t)bytes[index + 1U] << 8U) |
                        ((uint64_t)bytes[index + 2U] << 16U) |
                        ((uint64_t)bytes[index + 3U] << 24U);
        sum1 = (sum1 + word) % modulus;
        sum2 = (sum2 + sum1) % modulus;
    }
    check1 = modulus - ((sum1 + sum2) % modulus);
    check2 = modulus - ((sum1 + check1) % modulus);
    return (check2 << 32U) | check1;
}

/* --- uint64 -> index map ------------------------------------------------ */

static void xx_apfs_map_cleanup(xx_apfs_map *map) {
    if (!map) return;
    if (map->keys) xx_mem_free(map->keys);
    if (map->values) xx_mem_free(map->values);
    xx_mem_zero(map, sizeof(*map));
}

static size_t xx_apfs_map_slot(size_t capacity, uint64_t key) {
    uint64_t hash = key;
    hash = (hash ^ (hash >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    hash = (hash ^ (hash >> 27U)) * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31U;
    return (size_t)hash & (capacity - 1U);
}

static bool xx_apfs_map_grow(xx_apfs_map *map) {
    size_t capacity = map->capacity ? map->capacity * 2U : 256U;
    uint64_t *keys;
    size_t *values;
    size_t index;
    if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(*values)) {
        return false;
    }
    keys = (uint64_t *)xx_mem_calloc(capacity, sizeof(*keys));
    values = (size_t *)xx_mem_calloc(capacity, sizeof(*values));
    if (!keys || !values) {
        if (keys) xx_mem_free(keys);
        if (values) xx_mem_free(values);
        return false;
    }
    for (index = 0U; index < map->capacity; ++index) {
        size_t slot;
        if (map->values[index] == 0U) continue;
        slot = xx_apfs_map_slot(capacity, map->keys[index]);
        while (values[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
        keys[slot] = map->keys[index];
        values[slot] = map->values[index];
    }
    if (map->keys) xx_mem_free(map->keys);
    if (map->values) xx_mem_free(map->values);
    map->keys = keys;
    map->values = values;
    map->capacity = capacity;
    return true;
}

static size_t xx_apfs_map_get(const xx_apfs_map *map, uint64_t key) {
    size_t slot;
    if (!map || map->capacity == 0U) return 0U;
    slot = xx_apfs_map_slot(map->capacity, key);
    while (map->values[slot] != 0U) {
        if (map->keys[slot] == key) return map->values[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return 0U;
}

static bool xx_apfs_map_put(xx_apfs_map *map, uint64_t key, size_t value) {
    size_t slot;
    if (!map || value == 0U) return false;
    if ((map->count + 1U) * 4U >= map->capacity * 3U) {
        if (!xx_apfs_map_grow(map)) return false;
    }
    slot = xx_apfs_map_slot(map->capacity, key);
    while (map->values[slot] != 0U) {
        if (map->keys[slot] == key) {
            map->values[slot] = value;
            return true;
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    map->keys[slot] = key;
    map->values[slot] = value;
    ++map->count;
    return true;
}

/* --- object reads ------------------------------------------------------- */

/* Read one block by block number, verifying its Fletcher-64 and, when
 * expected_type is non-zero, the low 16 bits of o_type. */
static bool xx_apfs_read_object(xx_apfs_private *parsed, uint64_t block,
                                uint8_t *buffer, uint32_t expected_type) {
    int64_t offset;
    uint32_t block_size = parsed->nx.block_size;
    uint64_t stored;
    if (block_size == 0U) return false;
    if (block > (uint64_t)INT64_MAX / block_size) return false;
    offset = (int64_t)(block * block_size);
    if (offset > INT64_MAX - parsed->base_address) return false;
    offset += parsed->base_address;
    if (!xx_apfs_range_within(parsed->input_size, offset, (int64_t)block_size)) {
        return false;
    }
    if (!xx_apfs_read_at(parsed->device, offset, buffer, block_size)) return false;
    stored = xx_data_get_u64(buffer, block_size, 0U, false);
    if (stored != xx_apfs_fletcher64(buffer + 8, (size_t)block_size - 8U)) {
        return false;
    }
    if (expected_type != 0U) {
        uint32_t type = xx_data_get_u32(buffer, block_size, 24U, false);
        if ((type & 0xFFFFU) != expected_type) return false;
    }
    return true;
}

/* --- B-tree walk -------------------------------------------------------- */

/* One node of an APFS B-tree. key_size / value_size are the fixed sizes taken
 * from the root node's btree_info_t; they are ignored for a variable-size
 * tree. Every offset out of the table of contents is bounded against the key
 * and value areas before it is dereferenced. */
static void xx_apfs_walk_btree(xx_apfs_private *parsed, uint64_t block,
                               unsigned depth, uint32_t key_size,
                               uint32_t value_size, xx_apfs_leaf_cb callback,
                               void *ctx) {
    uint8_t *node;
    uint32_t block_size = parsed->nx.block_size;
    uint16_t flags;
    uint32_t nkeys;
    uint32_t table_off;
    uint32_t table_len;
    uint32_t toc_base;
    uint32_t key_base;
    uint32_t value_end;
    uint32_t entry_size;
    uint32_t index;
    bool leaf;
    bool fixed;
    uint64_t *children = NULL;
    uint32_t child_count = 0U;

    if (depth > XX_APFS_MAX_DEPTH || xx_apfs_stopped(parsed)) return;
    if (parsed->nodes >= XX_APFS_MAX_NODES) return;
    if (xx_apfs_map_get(&parsed->visited, block) != 0U) return;
    if (!xx_apfs_map_put(&parsed->visited, block, 1U)) return;
    ++parsed->nodes;

    node = (uint8_t *)xx_mem_alloc(block_size);
    if (!node) return;
    /* Both OBJECT_TYPE_BTREE (a root) and OBJECT_TYPE_BTREE_NODE occur, so
     * the type is checked by hand rather than through read_object(). */
    if (!xx_apfs_read_object(parsed, block, node, 0U)) {
        ++parsed->bad_nodes;
        xx_mem_free(node);
        return;
    }
    {
        uint32_t type = xx_data_get_u32(node, block_size, 24U, false) & 0xFFFFU;
        if (type != XX_APFS_OBJECT_TYPE_BTREE &&
            type != XX_APFS_OBJECT_TYPE_BTREE_NODE) {
            ++parsed->bad_nodes;
            xx_mem_free(node);
            return;
        }
    }

    flags = xx_data_get_u16(node, block_size, 32U, false);
    nkeys = xx_data_get_u32(node, block_size, 36U, false);
    table_off = xx_data_get_u16(node, block_size, 40U, false);
    table_len = xx_data_get_u16(node, block_size, 42U, false);
    leaf = (flags & XX_APFS_BTNODE_LEAF) != 0U;
    fixed = (flags & XX_APFS_BTNODE_FIXED_KV_SIZE) != 0U;

    value_end = block_size;
    if ((flags & XX_APFS_BTNODE_ROOT) != 0U) {
        if (block_size < XX_APFS_BTREE_NODE_DATA + XX_APFS_BTREE_INFO_SIZE) {
            ++parsed->bad_nodes;
            xx_mem_free(node);
            return;
        }
        value_end = block_size - XX_APFS_BTREE_INFO_SIZE;
        /* bt_fixed lives at the start of btree_info_t: flags, node_size,
         * key_size, val_size. */
        key_size = xx_data_get_u32(node, block_size, (size_t)value_end + 8U,
                                   false);
        value_size = xx_data_get_u32(node, block_size, (size_t)value_end + 12U,
                                     false);
    }

    entry_size = fixed ? 4U : 8U;
    toc_base = XX_APFS_BTREE_NODE_DATA + table_off;
    if (table_off > block_size || table_len > block_size - toc_base ||
        toc_base + table_len > value_end) {
        ++parsed->bad_nodes;
        xx_mem_free(node);
        return;
    }
    if (nkeys > table_len / entry_size) {
        ++parsed->bad_nodes;
        xx_mem_free(node);
        return;
    }
    key_base = toc_base + table_len;
    if (fixed && (key_size == 0U || key_size > block_size)) {
        ++parsed->bad_nodes;
        xx_mem_free(node);
        return;
    }

    if (!leaf) {
        children = (uint64_t *)xx_mem_calloc(nkeys ? nkeys : 1U,
                                             sizeof(*children));
        if (!children) {
            xx_mem_free(node);
            return;
        }
    }

    for (index = 0U; index < nkeys; ++index) {
        uint32_t at = toc_base + index * entry_size;
        uint32_t key_off;
        uint32_t key_len;
        uint32_t val_off;
        uint32_t val_len;
        uint32_t key_start;
        uint32_t val_start;
        if (xx_apfs_stopped(parsed)) break;
        if (fixed) {
            key_off = xx_data_get_u16(node, block_size, at, false);
            val_off = xx_data_get_u16(node, block_size, at + 2U, false);
            key_len = key_size;
            val_len = leaf ? value_size : 8U;
        } else {
            key_off = xx_data_get_u16(node, block_size, at, false);
            key_len = xx_data_get_u16(node, block_size, at + 2U, false);
            val_off = xx_data_get_u16(node, block_size, at + 4U, false);
            val_len = xx_data_get_u16(node, block_size, at + 6U, false);
        }
        /* Keys grow up from key_base; values grow DOWN from value_end. */
        if (key_off > value_end - key_base || key_len > value_end - key_base - key_off) {
            ++parsed->bad_nodes;
            continue;
        }
        key_start = key_base + key_off;
        if (val_off > value_end || val_len > val_off) {
            ++parsed->bad_nodes;
            continue;
        }
        val_start = value_end - val_off;
        if (val_start < key_start + key_len) {
            ++parsed->bad_nodes;
            continue;
        }
        if (leaf) {
            if (callback && !callback(parsed, ctx, node + key_start, key_len,
                                      node + val_start, val_len)) {
                break;
            }
        } else {
            if (val_len < 8U) {
                ++parsed->bad_nodes;
                continue;
            }
            children[child_count++] =
                xx_data_get_u64(node, block_size, val_start, false);
        }
    }
    xx_mem_free(node);
    node = NULL;

    if (children) {
        for (index = 0U; index < child_count; ++index) {
            xx_apfs_walk_btree(parsed, children[index], depth + 1U, key_size,
                               value_size, callback, ctx);
        }
        xx_mem_free(children);
    }
}

/* --- object maps -------------------------------------------------------- */

typedef struct xx_apfs_omap_ctx_s {
    xx_apfs_omap_entry **table;
    size_t *count;
    size_t *capacity;
    xx_apfs_map *index;
    uint64_t max_xid;
} xx_apfs_omap_ctx;

static bool xx_apfs_omap_cb(xx_apfs_private *parsed, void *raw_ctx,
                            const uint8_t *key, uint32_t key_len,
                            const uint8_t *value, uint32_t value_len) {
    xx_apfs_omap_ctx *ctx = (xx_apfs_omap_ctx *)raw_ctx;
    /* The omap walk needs no reader state; the signature is shared with the
     * other b-tree callbacks, which do. */
    (void)parsed;
    uint64_t oid;
    uint64_t xid;
    uint32_t vflags;
    int64_t paddr;
    size_t slot;
    if (key_len < XX_APFS_OMAP_KEY_SIZE || value_len < XX_APFS_OMAP_VAL_SIZE) {
        return true;
    }
    oid = xx_data_get_u64(key, key_len, 0U, false);
    xid = xx_data_get_u64(key, key_len, 8U, false);
    vflags = xx_data_get_u32(value, value_len, 0U, false);
    paddr = (int64_t)xx_data_get_u64(value, value_len, 8U, false);
    if (oid == 0U || paddr <= 0) return true;
    if ((vflags & XX_APFS_OMAP_VAL_DELETED) != 0U) return true;
    /* A mapping newer than the checkpoint being read belongs to a
     * transaction that was never committed for this view of the container. */
    if (ctx->max_xid != 0U && xid > ctx->max_xid) return true;

    slot = xx_apfs_map_get(ctx->index, oid);
    if (slot != 0U) {
        xx_apfs_omap_entry *existing = &(*ctx->table)[slot - 1U];
        if (xid >= existing->xid) {
            existing->xid = xid;
            existing->block = paddr;
        }
        return true;
    }
    if (*ctx->count >= XX_APFS_MAX_OMAP) return false;
    if (*ctx->count == *ctx->capacity) {
        size_t grown_capacity = *ctx->capacity ? *ctx->capacity * 2U : 64U;
        xx_apfs_omap_entry *grown;
        if (grown_capacity < *ctx->capacity ||
            grown_capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_apfs_omap_entry *)xx_mem_realloc(
            *ctx->table, grown_capacity * sizeof(*grown));
        if (!grown) return false;
        *ctx->table = grown;
        *ctx->capacity = grown_capacity;
    }
    (*ctx->table)[*ctx->count].oid = oid;
    (*ctx->table)[*ctx->count].xid = xid;
    (*ctx->table)[*ctx->count].block = paddr;
    ++(*ctx->count);
    if (!xx_apfs_map_put(ctx->index, oid, *ctx->count)) {
        --(*ctx->count);
        return false;
    }
    return true;
}

/* Read an omap_phys_t at a physical oid and walk its mapping tree. */
static bool xx_apfs_load_omap(xx_apfs_private *parsed, uint64_t omap_oid,
                              xx_apfs_omap_entry **table, size_t *count,
                              size_t *capacity, xx_apfs_map *index,
                              uint64_t max_xid) {
    uint8_t *omap;
    uint64_t tree_oid;
    xx_apfs_omap_ctx ctx;
    if (omap_oid == 0U) return false;
    omap = (uint8_t *)xx_mem_alloc(parsed->nx.block_size);
    if (!omap) return false;
    if (!xx_apfs_read_object(parsed, omap_oid, omap,
                             XX_APFS_OBJECT_TYPE_OMAP)) {
        xx_mem_free(omap);
        return false;
    }
    tree_oid = xx_data_get_u64(omap, parsed->nx.block_size, 48U, false);
    xx_mem_free(omap);
    if (tree_oid == 0U) return false;
    ctx.table = table;
    ctx.count = count;
    ctx.capacity = capacity;
    ctx.index = index;
    ctx.max_xid = max_xid;
    xx_apfs_walk_btree(parsed, tree_oid, 0U, XX_APFS_OMAP_KEY_SIZE,
                       XX_APFS_OMAP_VAL_SIZE, xx_apfs_omap_cb, &ctx);
    return *count != 0U;
}

static int64_t xx_apfs_omap_lookup(const xx_apfs_omap_entry *table,
                                   const xx_apfs_map *index, uint64_t oid) {
    size_t slot = xx_apfs_map_get(index, oid);
    return slot != 0U ? table[slot - 1U].block : -1;
}

/* --- container superblock ----------------------------------------------- */

static bool xx_apfs_decode_nx(const uint8_t *raw, uint32_t block_size,
                              xx_apfs_nx *out) {
    size_t index;
    uint32_t max_fs;
    if (xx_data_get_u32(raw, block_size, 32U, false) != XX_APFS_NX_MAGIC) {
        return false;
    }
    xx_mem_zero(out, sizeof(*out));
    out->block_size = xx_data_get_u32(raw, block_size, 36U, false);
    out->block_count = xx_data_get_u64(raw, block_size, 40U, false);
    out->features = xx_data_get_u64(raw, block_size, 48U, false);
    out->readonly_compatible_features =
        xx_data_get_u64(raw, block_size, 56U, false);
    out->incompatible_features = xx_data_get_u64(raw, block_size, 64U, false);
    xx_mem_copy(out->uuid, raw + 72, XX_APFS_UUID_SIZE);
    out->xid = xx_data_get_u64(raw, block_size, 16U, false);
    out->xp_desc_blocks = xx_data_get_u32(raw, block_size, 104U, false);
    out->xp_desc_base = (int64_t)xx_data_get_u64(raw, block_size, 112U, false);
    out->xp_desc_index = xx_data_get_u32(raw, block_size, 136U, false);
    out->xp_desc_len = xx_data_get_u32(raw, block_size, 140U, false);
    out->omap_oid = xx_data_get_u64(raw, block_size, 160U, false);
    out->max_file_systems = xx_data_get_u32(raw, block_size, 180U, false);
    if (block_size >= 1392U) {
        out->newest_mounted_version =
            xx_data_get_u64(raw, block_size, 1384U, false);
    }
    max_fs = out->max_file_systems;
    if (max_fs > XX_APFS_MAX_VOLUMES) max_fs = XX_APFS_MAX_VOLUMES;
    for (index = 0U; index < max_fs; ++index) {
        out->fs_oid[index] = xx_data_get_u64(raw, block_size,
                                             184U + index * 8U, false);
    }
    if (out->block_size != block_size) return false;
    if (out->block_count == 0U) return false;
    if (out->max_file_systems == 0U ||
        out->max_file_systems > XX_APFS_MAX_VOLUMES) return false;
    return true;
}

/* Find the container superblock. Block 0 is the fallback copy; the authority
 * is the newest valid nx_superblock_t in the checkpoint descriptor area, so
 * that area is scanned and the highest transaction id wins. */
static bool xx_apfs_load_container(xx_apfs_private *parsed) {
    /* A block can be 64 KiB, which is too much to put on the stack in a path
     * that also recurses, so both buffers are heap allocated. */
    uint8_t *probe;
    uint8_t *block = NULL;
    uint32_t block_size;
    xx_apfs_nx candidate;
    uint32_t index;
    uint32_t scan_count;
    uint64_t scan_base;

    probe = (uint8_t *)xx_mem_alloc(XX_APFS_MAX_BLOCK_SIZE);
    if (!probe) return false;

    /* Block 0 is read at the smallest legal size first, only to learn the
     * real block size from nx_block_size. */
    if (!xx_apfs_range_within(parsed->input_size, parsed->base_address,
                              (int64_t)XX_APFS_MIN_BLOCK_SIZE) ||
        !xx_apfs_read_at(parsed->device, parsed->base_address, probe,
                         XX_APFS_MIN_BLOCK_SIZE)) {
        goto reject;
    }
    if (xx_data_get_u32(probe, XX_APFS_MIN_BLOCK_SIZE, 32U, false) !=
        XX_APFS_NX_MAGIC) {
        goto reject;
    }
    block_size = xx_data_get_u32(probe, XX_APFS_MIN_BLOCK_SIZE, 36U, false);
    if (block_size < XX_APFS_MIN_BLOCK_SIZE ||
        block_size > XX_APFS_MAX_BLOCK_SIZE ||
        (block_size & (block_size - 1U)) != 0U) goto reject;

    if (!xx_apfs_range_within(parsed->input_size, parsed->base_address,
                              (int64_t)block_size) ||
        !xx_apfs_read_at(parsed->device, parsed->base_address, probe,
                         block_size)) {
        goto reject;
    }
    if (xx_data_get_u64(probe, block_size, 0U, false) !=
        xx_apfs_fletcher64(probe + 8, (size_t)block_size - 8U)) {
        goto reject;
    }
    if ((xx_data_get_u32(probe, block_size, 24U, false) & 0xFFFFU) !=
        XX_APFS_OBJECT_TYPE_NX_SUPERBLOCK) goto reject;
    if (!xx_apfs_decode_nx(probe, block_size, &parsed->nx)) goto reject;
    parsed->super_offset = parsed->base_address;
    parsed->from_checkpoint = false;
    xx_mem_free(probe);
    probe = NULL;

    /* The high bit of nx_xp_desc_blocks marks a tree-shaped descriptor area,
     * which this reader does not follow; block 0 then stands. */
    if ((parsed->nx.xp_desc_blocks & 0x80000000U) != 0U) return true;
    if (parsed->nx.xp_desc_blocks == 0U ||
        parsed->nx.xp_desc_blocks > XX_APFS_MAX_CHECKPOINT_BLOCKS ||
        parsed->nx.xp_desc_base <= 0) return true;

    block = (uint8_t *)xx_mem_alloc(block_size);
    if (!block) return true;
    /* The extent of the scan is fixed BEFORE the loop. The loop replaces
     * parsed->nx whenever it finds a newer checkpoint, and a forged
     * nx_xp_desc_blocks in that newer copy would otherwise become the loop
     * bound - up to 2^32 iterations from one mutated field. */
    scan_count = parsed->nx.xp_desc_blocks;
    scan_base = (uint64_t)parsed->nx.xp_desc_base;
    for (index = 0U; index < scan_count; ++index) {
        uint64_t number = scan_base + index;
        uint64_t xid;
        if (xx_apfs_stopped(parsed)) break;
        if (!xx_apfs_read_object(parsed, number, block,
                                 XX_APFS_OBJECT_TYPE_NX_SUPERBLOCK)) {
            continue;  /* a checkpoint_map_phys_t, or an unused slot */
        }
        xid = xx_data_get_u64(block, block_size, 16U, false);
        if (xid <= parsed->nx.xid) continue;
        if (!xx_apfs_decode_nx(block, block_size, &candidate)) continue;
        parsed->nx = candidate;
        parsed->super_offset =
            parsed->base_address + (int64_t)(number * block_size);
        parsed->from_checkpoint = true;
    }
    xx_mem_free(block);
    return true;
reject:
    if (probe) xx_mem_free(probe);
    return false;
}

/* --- file-system records ------------------------------------------------ */

static xx_apfs_inode *xx_apfs_inode_for(xx_apfs_private *parsed, uint64_t oid) {
    size_t slot = xx_apfs_map_get(&parsed->inode_index, oid);
    if (slot != 0U) return &parsed->inodes[slot - 1U];
    if (parsed->inode_count >= XX_APFS_MAX_INODES) return NULL;
    if (parsed->inode_count == parsed->inode_capacity) {
        size_t capacity = parsed->inode_capacity ? parsed->inode_capacity * 2U
                                                 : 64U;
        xx_apfs_inode *grown;
        if (capacity < parsed->inode_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return NULL;
        grown = (xx_apfs_inode *)xx_mem_realloc(parsed->inodes,
                                                capacity * sizeof(*grown));
        if (!grown) return NULL;
        parsed->inodes = grown;
        parsed->inode_capacity = capacity;
    }
    xx_mem_zero(&parsed->inodes[parsed->inode_count], sizeof(xx_apfs_inode));
    parsed->inodes[parsed->inode_count].oid = oid;
    ++parsed->inode_count;
    if (!xx_apfs_map_put(&parsed->inode_index, oid, parsed->inode_count)) {
        --parsed->inode_count;
        return NULL;
    }
    return &parsed->inodes[parsed->inode_count - 1U];
}

/* j_inode_val_t carries a trailing xf_blob_t of extended fields. The one that
 * matters here is INO_EXT_TYPE_DSTREAM, which holds the real file size; the
 * fixed part of the inode only has a size field for compressed files. */
static bool xx_apfs_inode_dstream_size(const uint8_t *value, uint32_t value_len,
                                       uint64_t *out_size) {
    uint32_t num_exts;
    uint32_t used_data;
    uint32_t headers;
    uint32_t data_at;
    uint32_t index;
    uint32_t cursor;
    if (value_len < XX_APFS_INODE_VAL_MIN + 4U) return false;
    num_exts = xx_data_get_u16(value, value_len, XX_APFS_INODE_VAL_MIN, false);
    used_data = xx_data_get_u16(value, value_len,
                                XX_APFS_INODE_VAL_MIN + 2U, false);
    (void)used_data;
    if (num_exts == 0U || num_exts > 64U) return false;
    headers = XX_APFS_INODE_VAL_MIN + 4U;
    if ((uint32_t)num_exts * 4U > value_len - headers) return false;
    data_at = headers + num_exts * 4U;
    cursor = data_at;
    for (index = 0U; index < num_exts; ++index) {
        uint8_t x_type = value[headers + index * 4U];
        uint16_t x_size = xx_data_get_u16(value, value_len,
                                          headers + index * 4U + 2U, false);
        uint32_t padded;
        if (x_size > value_len - cursor) return false;
        if (x_type == XX_APFS_INO_EXT_TYPE_DSTREAM && x_size >= 8U) {
            if (out_size) {
                *out_size = xx_data_get_u64(value, value_len, cursor, false);
            }
            return true;
        }
        padded = ((uint32_t)x_size + 7U) & ~UINT32_C(7);
        if (padded > value_len - cursor) return false;
        cursor += padded;
    }
    return false;
}

static bool xx_apfs_drec_add(xx_apfs_private *parsed, uint64_t parent,
                             uint64_t child, uint8_t dtype,
                             const uint8_t *name, size_t name_len) {
    xx_apfs_drec *entry;
    char *copy;
    size_t index;
    size_t head;
    if (parsed->drec_count >= XX_APFS_MAX_DRECS) return false;
    /* The stored length includes the terminating NUL. */
    while (name_len != 0U && name[name_len - 1U] == 0U) --name_len;
    if (name_len == 0U || name_len > XX_APFS_MAX_NAME) return true;
    for (index = 0U; index < name_len; ++index) {
        if (name[index] < 32U || name[index] == '/' || name[index] == '\\') {
            return true;
        }
    }
    if ((name_len == 1U && name[0] == '.') ||
        (name_len == 2U && name[0] == '.' && name[1] == '.')) return true;

    if (parsed->drec_count == parsed->drec_capacity) {
        size_t capacity = parsed->drec_capacity ? parsed->drec_capacity * 2U
                                                : 64U;
        xx_apfs_drec *grown;
        if (capacity < parsed->drec_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_apfs_drec *)xx_mem_realloc(parsed->drecs,
                                               capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->drecs = grown;
        parsed->drec_capacity = capacity;
    }
    copy = (char *)xx_mem_alloc(name_len + 1U);
    if (!copy) return false;
    xx_mem_copy(copy, name, name_len);
    copy[name_len] = '\0';

    entry = &parsed->drecs[parsed->drec_count];
    xx_mem_zero(entry, sizeof(*entry));
    entry->parent = parent;
    entry->child = child;
    entry->dtype = dtype;
    entry->name = copy;
    head = xx_apfs_map_get(&parsed->drec_head, parent);
    entry->next = head;
    ++parsed->drec_count;
    if (!xx_apfs_map_put(&parsed->drec_head, parent, parsed->drec_count)) {
        xx_str_free(copy);
        entry->name = NULL;
        --parsed->drec_count;
        return false;
    }
    return true;
}

static bool xx_apfs_fs_cb(xx_apfs_private *parsed, void *ctx,
                          const uint8_t *key, uint32_t key_len,
                          const uint8_t *value, uint32_t value_len) {
    uint64_t header;
    uint64_t oid;
    uint32_t type;
    (void)ctx;
    if (key_len < 8U) return true;
    header = xx_data_get_u64(key, key_len, 0U, false);
    oid = header & UINT64_C(0x0FFFFFFFFFFFFFFF);
    type = (uint32_t)(header >> 60U);

    if (type == XX_APFS_TYPE_INODE) {
        xx_apfs_inode *inode;
        uint64_t size = 0U;
        if (value_len < XX_APFS_INODE_VAL_MIN) return true;
        inode = xx_apfs_inode_for(parsed, oid);
        if (!inode) return false;
        inode->parent = xx_data_get_u64(value, value_len, 0U, false);
        inode->mode = xx_data_get_u16(value, value_len, 80U, false);
        if (xx_apfs_inode_dstream_size(value, value_len, &size)) {
            inode->size = size;
            inode->has_size = true;
        }
        return true;
    }
    if (type == XX_APFS_TYPE_DIR_REC) {
        /* The key is either j_drec_hashed_key_t (u32 length-and-hash) or the
         * plain j_drec_key_t (u16 length). The volume's incompatible feature
         * bits say which, but the key length settles it either way, so both
         * layouts are checked and the one that fits exactly is used. */
        uint32_t name_len = 0U;
        uint32_t name_at = 0U;
        bool hashed_first =
            (parsed->current_volume_features &
             (XX_APFS_INCOMPAT_CASE_INSENSITIVE |
              XX_APFS_INCOMPAT_NORMALIZATION_INSENSITIVE)) != 0U;
        uint32_t hashed_len = 0U;
        uint32_t plain_len = 0U;
        uint8_t dtype;
        uint64_t child;
        if (key_len >= 12U) {
            hashed_len = xx_data_get_u32(key, key_len, 8U, false) & 0x3FFU;
        }
        if (key_len >= 10U) {
            plain_len = xx_data_get_u16(key, key_len, 8U, false);
        }
        if (hashed_first && hashed_len != 0U && 12U + hashed_len == key_len) {
            name_len = hashed_len;
            name_at = 12U;
        } else if (plain_len != 0U && 10U + plain_len == key_len) {
            name_len = plain_len;
            name_at = 10U;
        } else if (hashed_len != 0U && 12U + hashed_len == key_len) {
            name_len = hashed_len;
            name_at = 12U;
        } else {
            return true;
        }
        if (value_len < XX_APFS_DREC_VAL_MIN) return true;
        child = xx_data_get_u64(value, value_len, 0U, false);
        dtype = (uint8_t)(xx_data_get_u16(value, value_len, 16U, false) & 0xFU);
        return xx_apfs_drec_add(parsed, oid, child, dtype, key + name_at,
                                name_len);
    }
    if (type == XX_APFS_TYPE_FILE_EXTENT) {
        xx_apfs_inode *inode;
        if (key_len < 16U || value_len < XX_APFS_EXTENT_VAL_MIN) return true;
        inode = xx_apfs_inode_for(parsed, oid);
        if (!inode) return false;
        ++inode->extent_count;
        if (inode->extent_count > 1U) return true;
        inode->extent_logical = xx_data_get_u64(key, key_len, 8U, false);
        inode->extent_length = xx_data_get_u64(value, value_len, 0U, false) &
                               UINT64_C(0x00FFFFFFFFFFFFFF);
        inode->extent_block = xx_data_get_u64(value, value_len, 8U, false);
        return true;
    }
    if (type == XX_APFS_TYPE_XATTR) {
        /* Only one xattr matters here: com.apple.decmpfs marks a file whose
         * bytes are compressed with zlib, LZVN or LZFSE. LZFSE and LZVN are
         * not implemented in this library, so no decmpfs file is extracted
         * and all of them are reported as unsupported. */
        static const char decmpfs[] = "com.apple.decmpfs";
        uint32_t name_len;
        xx_apfs_inode *inode;
        if (key_len < 10U) return true;
        name_len = xx_data_get_u16(key, key_len, 8U, false);
        if (name_len == 0U || 10U + name_len != key_len) return true;
        if (name_len - 1U != sizeof(decmpfs) - 1U) return true;
        if (xx_rt_memcmp(key + 10U, decmpfs, sizeof(decmpfs) - 1U) != 0) {
            return true;
        }
        inode = xx_apfs_inode_for(parsed, oid);
        if (!inode) return false;
        inode->compressed = true;
        return true;
    }
    return true;
}

/* --- listing ------------------------------------------------------------ */

static char *xx_apfs_join(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_APFS_MAX_PATH ||
        name_size > XX_APFS_MAX_PATH - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_mem_copy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_mem_copy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_mem_copy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

static bool xx_apfs_entry_append(xx_apfs_private *parsed,
                                 xx_apfs_entry *entry) {
    if (parsed->entry_count >= XX_APFS_MAX_ENTRIES) return false;
    if (parsed->entry_count == parsed->entry_capacity) {
        size_t capacity = parsed->entry_capacity ? parsed->entry_capacity * 2U
                                                 : 64U;
        xx_apfs_entry *grown;
        if (capacity < parsed->entry_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_apfs_entry *)xx_mem_realloc(parsed->entries,
                                                capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->entry_capacity = capacity;
    }
    parsed->entries[parsed->entry_count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Decide whether a file's bytes can be copied verbatim. Only the simplest
 * shape qualifies: one extent, starting at logical zero, long enough for the
 * whole file, not compressed. */
static void xx_apfs_resolve_data(xx_apfs_private *parsed,
                                 const xx_apfs_inode *inode,
                                 xx_apfs_entry *entry) {
    int64_t offset;
    uint64_t block_size = parsed->nx.block_size;
    entry->data_offset = -1;
    entry->data_size = 0;
    entry->extractable = false;
    if (!inode || !inode->has_size) return;
    if (inode->compressed) return;
    if (inode->size == 0U) {
        entry->data_offset = parsed->super_offset;
        entry->data_size = 0;
        entry->extractable = inode->extent_count == 0U;
        return;
    }
    if (inode->extent_count != 1U || inode->extent_logical != 0U) return;
    if (inode->extent_block == 0U) return;  /* a sparse hole */
    if (inode->extent_length < inode->size) return;
    if (block_size == 0U ||
        inode->extent_block > (uint64_t)INT64_MAX / block_size) return;
    offset = (int64_t)(inode->extent_block * block_size);
    if (offset > INT64_MAX - parsed->base_address) return;
    offset += parsed->base_address;
    if (inode->size > (uint64_t)INT64_MAX) return;
    if (!xx_apfs_range_within(parsed->input_size, offset,
                              (int64_t)inode->size)) return;
    entry->data_offset = offset;
    entry->data_size = (int64_t)inode->size;
    entry->extractable = true;
}

/* Breadth-first expansion from the volume's root directory. Visited object
 * ids bound the walk, so a directory that names itself or an id cycle cannot
 * loop. */
static void xx_apfs_build_listing(xx_apfs_private *parsed, const char *prefix,
                                  uint64_t *out_records) {
    typedef struct {
        uint64_t oid;
        char *path;
        unsigned depth;
    } queue_item;
    queue_item *queue;
    size_t head = 0U;
    size_t tail = 0U;
    size_t capacity = 64U;
    size_t start_count = parsed->entry_count;
    xx_apfs_map seen;

    xx_mem_zero(&seen, sizeof(seen));
    queue = (queue_item *)xx_mem_calloc(capacity, sizeof(*queue));
    if (!queue) return;
    queue[tail].oid = XX_APFS_ROOT_DIR_INO;
    queue[tail].path = (char *)prefix;
    queue[tail].depth = 0U;
    ++tail;
    (void)xx_apfs_map_put(&seen, XX_APFS_ROOT_DIR_INO, 1U);

    while (head < tail) {
        queue_item current = queue[head++];
        size_t cursor = xx_apfs_map_get(&parsed->drec_head, current.oid);
        if (xx_apfs_stopped(parsed)) break;
        while (cursor != 0U) {
            const xx_apfs_drec *drec = &parsed->drecs[cursor - 1U];
            char *path;
            xx_apfs_entry entry;
            bool is_dir;
            cursor = drec->next;
            if (parsed->entry_count >= XX_APFS_MAX_ENTRIES) break;
            path = xx_apfs_join(current.path, drec->name);
            if (!path) continue;
            is_dir = drec->dtype == XX_APFS_DT_DIR;

            xx_mem_zero(&entry, sizeof(entry));
            entry.name = path;
            entry.is_folder = is_dir;
            entry.data_offset = -1;
            if (!is_dir) {
                size_t slot = xx_apfs_map_get(&parsed->inode_index, drec->child);
                const xx_apfs_inode *inode =
                    slot != 0U ? &parsed->inodes[slot - 1U] : NULL;
                if (inode) entry.size = inode->size;
                if (drec->dtype == XX_APFS_DT_REG) {
                    xx_apfs_resolve_data(parsed, inode, &entry);
                    entry.name = path;
                    entry.is_folder = false;
                    entry.size = inode ? inode->size : 0U;
                }
                if (!entry.extractable) ++parsed->unsupported;
            }
            if (!xx_apfs_entry_append(parsed, &entry)) {
                xx_str_free(path);
                continue;
            }
            if (is_dir && current.depth < XX_APFS_MAX_PATH / 8U &&
                xx_apfs_map_get(&seen, drec->child) == 0U) {
                if (!xx_apfs_map_put(&seen, drec->child, 1U)) continue;
                if (tail == capacity) {
                    size_t grown_capacity = capacity * 2U;
                    queue_item *grown;
                    if (grown_capacity < capacity ||
                        grown_capacity > SIZE_MAX / sizeof(*grown)) break;
                    grown = (queue_item *)xx_mem_realloc(
                        queue, grown_capacity * sizeof(*grown));
                    if (!grown) break;
                    queue = grown;
                    capacity = grown_capacity;
                }
                queue[tail].oid = drec->child;
                /* The path string is owned by the entry array, which outlives
                 * this traversal; only the pointer value is borrowed. */
                queue[tail].path = parsed->entries[parsed->entry_count - 1U].name;
                queue[tail].depth = current.depth + 1U;
                ++tail;
            }
        }
    }
    xx_mem_free(queue);
    xx_apfs_map_cleanup(&seen);
    if (out_records) *out_records = parsed->entry_count - start_count;
}

/* --- volumes ------------------------------------------------------------ */

/* Free the scratch that belongs to a single volume, so the next volume
 * starts from an empty object map, inode table and directory index. */
static void xx_apfs_reset_volume_scratch(xx_apfs_private *parsed) {
    size_t index;
    for (index = 0U; index < parsed->drec_count; ++index) {
        if (parsed->drecs[index].name) xx_str_free(parsed->drecs[index].name);
    }
    if (parsed->drecs) xx_mem_free(parsed->drecs);
    if (parsed->inodes) xx_mem_free(parsed->inodes);
    if (parsed->vomap) xx_mem_free(parsed->vomap);
    parsed->drecs = NULL;
    parsed->drec_count = 0U;
    parsed->drec_capacity = 0U;
    parsed->inodes = NULL;
    parsed->inode_count = 0U;
    parsed->inode_capacity = 0U;
    parsed->vomap = NULL;
    parsed->vomap_count = 0U;
    parsed->vomap_capacity = 0U;
    xx_apfs_map_cleanup(&parsed->drec_head);
    xx_apfs_map_cleanup(&parsed->inode_index);
    xx_apfs_map_cleanup(&parsed->vomap_index);
}

static void xx_apfs_load_volume(xx_apfs_private *parsed, uint32_t slot,
                                uint64_t fs_oid) {
    xx_apfs_volume_info *info = &parsed->volumes[slot];
    uint8_t *block;
    int64_t volume_block;
    uint32_t block_size = parsed->nx.block_size;
    size_t index;
    int64_t tree_block;

    xx_mem_zero(info, sizeof(*info));
    info->oid = fs_oid;
    info->block = -1;

    volume_block = xx_apfs_omap_lookup(parsed->omap, &parsed->omap_index,
                                       fs_oid);
    if (volume_block <= 0) return;
    block = (uint8_t *)xx_mem_alloc(block_size);
    if (!block) return;
    if (!xx_apfs_read_object(parsed, (uint64_t)volume_block, block,
                             XX_APFS_OBJECT_TYPE_FS)) {
        xx_mem_free(block);
        return;
    }
    if (xx_data_get_u32(block, block_size, 32U, false) !=
        XX_APFS_VOLUME_MAGIC) {
        xx_mem_free(block);
        return;
    }
    info->block = volume_block;
    info->superblock_valid = true;
    info->incompatible_features = xx_data_get_u64(block, block_size, 56U, false);
    info->omap_oid = xx_data_get_u64(block, block_size, 128U, false);
    info->root_tree_oid = xx_data_get_u64(block, block_size, 136U, false);
    info->num_files = xx_data_get_u64(block, block_size, 184U, false);
    info->num_directories = xx_data_get_u64(block, block_size, 192U, false);
    xx_mem_copy(info->uuid, block + 240, XX_APFS_UUID_SIZE);
    info->fs_flags = xx_data_get_u64(block, block_size, 264U, false);
    if (block_size >= 704U + XX_APFS_VOLNAME_SIZE) {
        for (index = 0U; index < XX_APFS_VOLNAME_SIZE; ++index) {
            uint8_t ch = block[704U + index];
            if (ch == 0U) break;
            /* The volume name is UTF-8; only control bytes are rejected. */
            if (ch < 32U) break;
            info->name[index] = (char)ch;
        }
        info->name[index] = '\0';
    }
    xx_mem_free(block);

    parsed->current_volume_features = info->incompatible_features;
    if (info->omap_oid == 0U || info->root_tree_oid == 0U) return;
    if (!xx_apfs_load_omap(parsed, info->omap_oid, &parsed->vomap,
                           &parsed->vomap_count, &parsed->vomap_capacity,
                           &parsed->vomap_index, parsed->nx.xid)) {
        return;
    }
    tree_block = xx_apfs_omap_lookup(parsed->vomap, &parsed->vomap_index,
                                     info->root_tree_oid);
    if (tree_block <= 0) return;
    xx_apfs_walk_btree(parsed, (uint64_t)tree_block, 0U, 0U, 0U,
                       xx_apfs_fs_cb, NULL);
    info->tree_reached = true;
    xx_apfs_build_listing(parsed, info->name[0] ? info->name : "volume",
                          &info->records);
}

/* --- parse -------------------------------------------------------------- */

static void xx_apfs_private_cleanup(xx_apfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->drec_count; ++index) {
        if (parsed->drecs[index].name) xx_str_free(parsed->drecs[index].name);
    }
    for (index = 0U; index < parsed->entry_count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->omap) xx_mem_free(parsed->omap);
    if (parsed->vomap) xx_mem_free(parsed->vomap);
    if (parsed->inodes) xx_mem_free(parsed->inodes);
    if (parsed->drecs) xx_mem_free(parsed->drecs);
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_apfs_map_cleanup(&parsed->omap_index);
    xx_apfs_map_cleanup(&parsed->vomap_index);
    xx_apfs_map_cleanup(&parsed->inode_index);
    xx_apfs_map_cleanup(&parsed->drec_head);
    xx_apfs_map_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->super_offset = -1;
}

static void xx_apfs_private_free(xx_apfs_private *parsed) {
    if (!parsed) return;
    xx_apfs_private_cleanup(parsed);
    xx_mem_free(parsed);
}

static bool xx_apfs_parse(Abstractformat *self, xx_apfs_private *parsed,
                          xx_pd_struct *pd, bool deep) {
    uint32_t index;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->super_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    parsed->device = self->device;
    parsed->pd = pd;
    parsed->base_address = self->base_address;
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_apfs_load_container(parsed)) {
        xx_apfs_private_cleanup(parsed);
        return false;
    }
    if (!deep) return true;

    /* The container object map turns the virtual nx_fs_oid entries into block
     * addresses. Without it no volume can be reached, but the container is
     * still correctly identified. */
    if (!xx_apfs_load_omap(parsed, parsed->nx.omap_oid, &parsed->omap,
                           &parsed->omap_count, &parsed->omap_capacity,
                           &parsed->omap_index, parsed->nx.xid)) {
        return true;
    }
    for (index = 0U; index < parsed->nx.max_file_systems &&
                     index < XX_APFS_MAX_VOLUMES; ++index) {
        if (parsed->nx.fs_oid[index] == 0U) continue;
        if (xx_apfs_stopped(parsed)) break;
        /* Each volume gets a fresh visited set: the container omap tree and a
         * volume's trees are disjoint, and sharing the set would silently
         * skip a block two volumes happen to both reference. */
        xx_apfs_map_cleanup(&parsed->visited);
        parsed->nodes = 0U;
        xx_apfs_reset_volume_scratch(parsed);
        xx_apfs_load_volume(parsed, parsed->volume_count,
                            parsed->nx.fs_oid[index]);
        ++parsed->volume_count;
    }
    xx_apfs_reset_volume_scratch(parsed);
    return true;
}

/* --- archive record plumbing -------------------------------------------- */

static bool xx_apfs_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_apfs_find_option(const xx_list_s *options,
                                         uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_apfs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) return false;
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' || component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static bool xx_apfs_populate_record(xx_archive_record *record,
                                    const xx_apfs_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(entry->data_size > 0 ? entry->data_size : 0)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_apfs_archive_stream_free(void *pointer) {
    xx_apfs_archive_stream *stream = (xx_apfs_archive_stream *)pointer;
    if (!stream) return;
    if (stream->parsed) xx_apfs_private_free(stream->parsed);
    xx_mem_free(stream);
}

/* --- public API --------------------------------------------------------- */

void xx_apfs_init(xx_apfs *apfs, xx_io_device *dev, int64_t base_address) {
    if (!apfs) return;
    xx_mem_zero(apfs, sizeof(*apfs));
    xx_format_init(&apfs->format, dev, base_address);
    apfs->format.endian = XX_ENDIAN_LITTLE;
    apfs->format.file_type = XX_APFS_FILE_TYPE;
    apfs->format.format_type = XX_TYPE_ARCHIVE;
    apfs->format.is_archive = true;
    apfs->format.os = XX_OS_MACOS;
    xx_format_set_mime_type(&apfs->format, "application/x-apfs-image");
    xx_format_set_extension(&apfs->format, "apfs");
    apfs->format.check_is_valid = xx_apfs_check_is_valid;
    apfs->format.handle_base_info = xx_apfs_handle_base_info;
    apfs->format.get_format_size = xx_apfs_get_format_size;
    apfs->format.get_number_of_archive_records =
        xx_apfs_get_number_of_archive_records;
    apfs->format.create_archive_records_reading =
        xx_apfs_create_archive_records_reading;
    apfs->format.get_current_archive_record = xx_apfs_get_current_archive_record;
    apfs->format.unpack_current_archive_record =
        xx_apfs_unpack_current_archive_record;
    apfs->format.archive_record_move_to_next = xx_apfs_archive_record_move_to_next;
    apfs->format.free_archive_records_reading =
        xx_apfs_free_archive_records_reading;
    apfs->format.destroy = xx_apfs_vtable_destroy;
    apfs->super_offset = -1;
}

xx_apfs *xx_apfs_create(xx_io_device *dev, int64_t base_address) {
    xx_apfs *apfs = (xx_apfs *)xx_mem_alloc(sizeof(*apfs));
    if (apfs) xx_apfs_init(apfs, dev, base_address);
    return apfs;
}

void xx_apfs_destroy(xx_apfs *apfs) {
    if (!apfs) return;
    if (apfs->internal) {
        xx_apfs_private_free((xx_apfs_private *)apfs->internal);
        apfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&apfs->format);
}

static void xx_apfs_vtable_destroy(Abstractformat *self) {
    xx_apfs_destroy((xx_apfs *)self);
}

void xx_apfs_free(xx_apfs *apfs) {
    if (!apfs) return;
    xx_apfs_destroy(apfs);
    xx_mem_free(apfs);
}

bool xx_apfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    /* Identification stops at the container superblock; walking every volume
     * to answer a probe would read the whole image. */
    xx_apfs_private *probe = (xx_apfs_private *)xx_mem_alloc(sizeof(*probe));
    bool result;
    if (!probe) return false;
    result = xx_apfs_parse(self, probe, pd, false);
    xx_apfs_private_free(probe);
    return result;
}

bool xx_apfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_apfs_private *parsed;
    xx_apfs *apfs = (xx_apfs *)self;
    int64_t total_size;
    uint32_t index;
    if (!self || !apfs) return false;
    parsed = (xx_apfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_apfs_parse(self, parsed, pd, true)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (apfs->internal) xx_apfs_private_free((xx_apfs_private *)apfs->internal);
    apfs->internal = parsed;

    apfs->block_size = parsed->nx.block_size;
    apfs->block_count = parsed->nx.block_count;
    apfs->xid = parsed->nx.xid;
    xx_mem_copy(apfs->uuid, parsed->nx.uuid, XX_APFS_UUID_SIZE);
    apfs->features = parsed->nx.features;
    apfs->readonly_compatible_features =
        parsed->nx.readonly_compatible_features;
    apfs->incompatible_features = parsed->nx.incompatible_features;
    apfs->omap_oid = parsed->nx.omap_oid;
    apfs->max_file_systems = parsed->nx.max_file_systems;
    apfs->next_version = (uint32_t)parsed->nx.newest_mounted_version;
    apfs->super_offset = parsed->super_offset;
    apfs->from_checkpoint = parsed->from_checkpoint;
    apfs->omap_entries = parsed->omap_count;
    apfs->volume_count = parsed->volume_count;
    for (index = 0U; index < parsed->volume_count &&
                     index < XX_APFS_MAX_VOLUMES; ++index) {
        apfs->volumes[index] = parsed->volumes[index];
    }
    apfs->number_of_records = parsed->entry_count;
    apfs->number_of_members = parsed->entry_count;
    apfs->number_of_unsupported = parsed->unsupported;

    total_size = xx_io_total_size(self->device);
    if (parsed->nx.block_count <=
            (uint64_t)INT64_MAX / (parsed->nx.block_size ? parsed->nx.block_size
                                                         : 1U) &&
        (int64_t)(parsed->nx.block_count * parsed->nx.block_size) <=
            total_size - self->base_address) {
        self->format_size =
            (int64_t)(parsed->nx.block_count * parsed->nx.block_size);
        if (total_size > self->base_address + self->format_size) {
            self->overlay_offset = self->base_address + self->format_size;
            self->overlay_size = total_size - self->overlay_offset;
        } else {
            self->overlay_offset = -1;
            self->overlay_size = 0;
        }
    } else {
        self->format_size = total_size >= 0 ? total_size - self->base_address : -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->entry_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_apfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_apfs_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_apfs *)self)->number_of_records;
}

xx_archive_record_state *xx_apfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_apfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_apfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    stream->parsed = (xx_apfs_private *)xx_mem_alloc(sizeof(*stream->parsed));
    if (!stream->parsed || !xx_apfs_copy_options(&state->options, options) ||
        !xx_apfs_parse(self, stream->parsed, pd, true)) {
        xx_apfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_apfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed->entry_count;
    if (stream->parsed->entry_count != 0U &&
        xx_apfs_populate_record(&state->current_record,
                                &stream->parsed->entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_apfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_apfs_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_apfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_apfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed->entry_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_apfs_populate_record(&state->current_record,
                                 &stream->parsed->entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_apfs_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_apfs_entry *entry;
    xx_apfs_archive_stream *stream;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_apfs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed->entry_count) return false;
    entry = &stream->parsed->entries[stream->index];
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_apfs_safe_name(name)) return false;
    /* Compressed (decmpfs), sparse and multi-extent files are refused rather
     * than written out with the wrong bytes. */
    if (!entry->is_folder && !entry->extractable) return false;

    option = xx_apfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return entry->is_folder ||
               (entry->data_offset >= 0 && entry->data_size >= 0 &&
                entry->data_offset <= total &&
                entry->data_size <= total - entry->data_offset);
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat(base, "/");
        if (!destination) goto cleanup;
        {
            char *joined = xx_str_concat(destination, name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device, entry->data_offset,
                                                entry->data_size, destination,
                                                pd);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_apfs_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint32_t xx_apfs_get_block_size(const xx_apfs *apfs) {
    return apfs ? apfs->block_size : 0U;
}
uint32_t xx_apfs_get_volume_count(const xx_apfs *apfs) {
    return apfs ? apfs->volume_count : 0U;
}
const xx_apfs_volume_info *xx_apfs_get_volume(const xx_apfs *apfs,
                                              uint32_t index) {
    if (!apfs || index >= apfs->volume_count ||
        index >= XX_APFS_MAX_VOLUMES) return NULL;
    return &apfs->volumes[index];
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Btrfs. The on-disk layout and the scope of this reader are documented in
 * include/xxfclib/formats/btrfs/xx_btrfs.h.
 *
 * Every tree block is reached through an attacker-controlled logical address,
 * so the order here is always: translate through the chunk map, bound the
 * physical range against the device, read, verify the CRC32C, and only then
 * believe a single field. The walk additionally caps depth, total nodes and
 * total records, and refuses to revisit a block it has already read.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/btrfs/xx_btrfs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_BTRFS lands the alias is
 * defined and this picks it up with no further change. */
#ifdef BTRFS
#define XX_BTRFS_FILE_TYPE XX_FILE_TYPE_BTRFS
#else
#define XX_BTRFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- on-disk sizes ------------------------------------------------------ */

#define XX_BTRFS_HEADER_SIZE 101U
#define XX_BTRFS_ITEM_SIZE 25U
#define XX_BTRFS_KEY_PTR_SIZE 33U
#define XX_BTRFS_DISK_KEY_SIZE 17U
#define XX_BTRFS_CHUNK_HEAD_SIZE 48U
#define XX_BTRFS_STRIPE_SIZE 32U
#define XX_BTRFS_DIR_ITEM_HEAD_SIZE 30U
#define XX_BTRFS_INODE_ITEM_SIZE 160U
#define XX_BTRFS_ROOT_ITEM_MIN_SIZE 239U
#define XX_BTRFS_EXTENT_INLINE_HEAD 21U
#define XX_BTRFS_EXTENT_REG_SIZE 53U
#define XX_BTRFS_SYS_CHUNK_ARRAY_OFFSET 811U
#define XX_BTRFS_SYS_CHUNK_ARRAY_MAX 2048U

/* --- key types ---------------------------------------------------------- */

#define XX_BTRFS_KEY_INODE_ITEM 1U
#define XX_BTRFS_KEY_DIR_ITEM 84U
#define XX_BTRFS_KEY_EXTENT_DATA 108U
#define XX_BTRFS_KEY_ROOT_ITEM 132U
#define XX_BTRFS_KEY_CHUNK_ITEM 228U

#define XX_BTRFS_FS_TREE_OBJECTID UINT64_C(5)
#define XX_BTRFS_FIRST_CHUNK_TREE_OBJECTID UINT64_C(256)

/* btrfs_dir_item.type, the POSIX file types. */
#define XX_BTRFS_FT_REG_FILE 1U
#define XX_BTRFS_FT_DIR 2U
#define XX_BTRFS_FT_SYMLINK 7U

/* btrfs_file_extent_item.type. */
#define XX_BTRFS_EXTENT_INLINE 0U
#define XX_BTRFS_EXTENT_REG 1U
#define XX_BTRFS_EXTENT_PREALLOC 2U

/* btrfs_chunk.type profile bits. */
#define XX_BTRFS_BG_RAID0 UINT64_C(8)
#define XX_BTRFS_BG_RAID1 UINT64_C(16)
#define XX_BTRFS_BG_DUP UINT64_C(32)
#define XX_BTRFS_BG_RAID10 UINT64_C(64)
#define XX_BTRFS_BG_RAID5 UINT64_C(128)
#define XX_BTRFS_BG_RAID6 UINT64_C(256)
#define XX_BTRFS_BG_RAID1C3 UINT64_C(512)
#define XX_BTRFS_BG_RAID1C4 UINT64_C(1024)
/* Profiles whose stripes each hold a complete copy of the chunk. */
#define XX_BTRFS_BG_MIRRORED                                                  \
    (XX_BTRFS_BG_RAID1 | XX_BTRFS_BG_DUP | XX_BTRFS_BG_RAID1C3 |              \
     XX_BTRFS_BG_RAID1C4)
/* Profiles this reader refuses, because a block is spread across stripes. */
#define XX_BTRFS_BG_STRIPED                                                   \
    (XX_BTRFS_BG_RAID0 | XX_BTRFS_BG_RAID10 | XX_BTRFS_BG_RAID5 |             \
     XX_BTRFS_BG_RAID6)

/* --- budgets ------------------------------------------------------------ */

#define XX_BTRFS_MAX_CHUNKS 8192U
#define XX_BTRFS_MAX_STRIPES 8U
#define XX_BTRFS_MAX_NODES 40000U
#define XX_BTRFS_MAX_DEPTH 12U
#define XX_BTRFS_MAX_INODES 200000U
#define XX_BTRFS_MAX_DIRENTS 200000U
#define XX_BTRFS_MAX_ENTRIES 100000U
#define XX_BTRFS_MAX_NAME 255U
#define XX_BTRFS_MAX_PATH 4096U
#define XX_BTRFS_MIN_NODE_SIZE 4096U
#define XX_BTRFS_MAX_NODE_SIZE 65536U
#define XX_BTRFS_COPY_CHUNK (64 * 1024)

typedef struct xx_btrfs_stripe_s {
    uint64_t devid;
    uint64_t offset;
} xx_btrfs_stripe;

typedef struct xx_btrfs_chunk_s {
    uint64_t logical;
    uint64_t length;
    uint64_t type;
    uint16_t num_stripes;
    xx_btrfs_stripe stripes[XX_BTRFS_MAX_STRIPES];
} xx_btrfs_chunk;

typedef struct xx_btrfs_super_s {
    uint8_t fsid[XX_BTRFS_UUID_SIZE];
    uint8_t metadata_uuid[XX_BTRFS_UUID_SIZE];
    char label[XX_BTRFS_LABEL_SIZE + 1U];
    uint64_t bytenr;
    uint64_t generation;
    uint64_t root;
    uint64_t chunk_root;
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t root_dir_objectid;
    uint64_t num_devices;
    uint64_t devid;
    uint64_t incompat_flags;
    uint64_t compat_ro_flags;
    uint32_t sector_size;
    uint32_t node_size;
    uint32_t sys_chunk_array_size;
    uint16_t csum_type;
    uint8_t root_level;
    uint8_t chunk_root_level;
    bool csum_verified;
} xx_btrfs_super;

/* One inode plus, when it has exactly one, the extent that describes it. */
typedef struct xx_btrfs_inode_s {
    uint64_t objectid;
    uint64_t size;
    uint32_t mode;
    uint32_t extent_count;
    uint8_t extent_type;
    uint8_t compression;
    uint64_t disk_bytenr;
    uint64_t disk_offset;
    uint64_t num_bytes;
    uint64_t ram_bytes;
    uint64_t logical_offset;
    int64_t inline_phys;    /**< Device offset of inline payload, or -1. */
    uint64_t inline_size;   /**< Stored (possibly compressed) inline length. */
} xx_btrfs_inode;

typedef struct xx_btrfs_dirent_s {
    uint64_t parent;
    uint64_t child;
    char *name;
    uint8_t ftype;
    uint8_t loc_type;  /**< Key type of the target: INODE_ITEM or ROOT_ITEM. */
    size_t next;       /**< 1-based index of the next entry in this parent. */
} xx_btrfs_dirent;

typedef struct xx_btrfs_entry_s {
    char *name;
    bool is_folder;
    uint64_t size;
    int64_t data_offset;
    int64_t data_size;
    uint32_t method;
    bool extractable;
} xx_btrfs_entry;

/* Open-addressed uint64 -> 1-based index map. Key 0 means "empty", which is
 * safe because objectid 0 is never a real btrfs object. */
typedef struct xx_btrfs_map_s {
    uint64_t *keys;
    size_t *values;
    size_t capacity;
    size_t count;
} xx_btrfs_map;

typedef struct xx_btrfs_private_s {
    xx_btrfs_super super;
    int64_t input_size;
    int64_t base_address;
    int64_t super_offset;

    xx_btrfs_chunk *chunks;
    size_t chunk_count;

    xx_btrfs_inode *inodes;
    size_t inode_count;
    size_t inode_capacity;
    xx_btrfs_map inode_map;

    xx_btrfs_dirent *dirents;
    size_t dirent_count;
    size_t dirent_capacity;
    xx_btrfs_map dirent_head;  /**< parent objectid -> 1-based dirent index. */

    xx_btrfs_entry *entries;
    size_t entry_count;
    size_t entry_capacity;

    xx_btrfs_map visited;      /**< Tree blocks already read, by logical addr. */
    size_t nodes;
    size_t bad_nodes;          /**< Blocks rejected by bounds or checksum. */
    uint64_t fs_tree_root;
    uint8_t fs_tree_level;
    size_t unsupported;
    xx_io_device *device;
    xx_pd_struct *pd;
} xx_btrfs_private;

typedef struct xx_btrfs_archive_stream_s {
    xx_btrfs_private *parsed;
    size_t index;
} xx_btrfs_archive_stream;

/* Per-item callback used by the generic tree walk. data_phys is the device
 * offset of the item payload, which inline extents need. */
typedef bool (*xx_btrfs_item_cb)(xx_btrfs_private *parsed, void *ctx,
                                 uint64_t objectid, uint8_t type,
                                 uint64_t offset, const uint8_t *data,
                                 uint32_t size, int64_t data_phys);

static void xx_btrfs_vtable_destroy(Abstractformat *self);
static void xx_btrfs_private_free(xx_btrfs_private *parsed);

/* ------------------------------------------------------------- helpers -- */

static bool xx_btrfs_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_btrfs_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_btrfs_stopped(const xx_btrfs_private *parsed) {
    return parsed && parsed->pd && xx_pd_is_stopped(parsed->pd);
}

/* --- uint64 -> index map ------------------------------------------------ */

static void xx_btrfs_map_cleanup(xx_btrfs_map *map) {
    if (!map) return;
    if (map->keys) xx_mem_free(map->keys);
    if (map->values) xx_mem_free(map->values);
    xx_mem_zero(map, sizeof(*map));
}

static size_t xx_btrfs_map_slot(size_t capacity, uint64_t key) {
    /* Btrfs objectids and logical addresses are both dense and aligned, so
     * the raw value clusters badly; fold it with a 64-bit odd multiplier. */
    uint64_t hash = key;
    hash = (hash ^ (hash >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    hash = (hash ^ (hash >> 27U)) * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31U;
    return (size_t)hash & (capacity - 1U);
}

static bool xx_btrfs_map_grow(xx_btrfs_map *map) {
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
        slot = xx_btrfs_map_slot(capacity, map->keys[index]);
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

/* Look up key; returns the stored 1-based value, or 0 when absent. */
static size_t xx_btrfs_map_get(const xx_btrfs_map *map, uint64_t key) {
    size_t slot;
    if (!map || map->capacity == 0U) return 0U;
    slot = xx_btrfs_map_slot(map->capacity, key);
    while (map->values[slot] != 0U) {
        if (map->keys[slot] == key) return map->values[slot];
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return 0U;
}

/* Insert or overwrite key. Returns false only on allocation failure. */
static bool xx_btrfs_map_put(xx_btrfs_map *map, uint64_t key, size_t value) {
    size_t slot;
    if (!map || value == 0U) return false;
    if ((map->count + 1U) * 4U >= map->capacity * 3U) {
        if (!xx_btrfs_map_grow(map)) return false;
    }
    slot = xx_btrfs_map_slot(map->capacity, key);
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

/* --- chunk map ---------------------------------------------------------- */

static bool xx_btrfs_chunk_add(xx_btrfs_private *parsed, uint64_t logical,
                               const uint8_t *chunk, size_t available) {
    xx_btrfs_chunk entry;
    uint16_t num_stripes;
    size_t index;
    size_t needed;
    if (!parsed || !chunk || available < XX_BTRFS_CHUNK_HEAD_SIZE) return false;
    num_stripes = xx_data_get_u16(chunk, available, 44U, false);
    if (num_stripes == 0U) return false;
    needed = XX_BTRFS_CHUNK_HEAD_SIZE +
             (size_t)num_stripes * XX_BTRFS_STRIPE_SIZE;
    if (needed > available) return false;
    if (parsed->chunk_count >= XX_BTRFS_MAX_CHUNKS) return false;

    xx_mem_zero(&entry, sizeof(entry));
    entry.logical = logical;
    entry.length = xx_data_get_u64(chunk, available, 0U, false);
    entry.type = xx_data_get_u64(chunk, available, 24U, false);
    entry.num_stripes = num_stripes;
    if (entry.length == 0U || entry.logical > UINT64_MAX - entry.length) {
        return false;
    }
    /* Only the first XX_BTRFS_MAX_STRIPES stripes are retained; a mirrored
     * profile never needs more than one of them and a striped profile is
     * refused outright at translation time. */
    for (index = 0U; index < num_stripes && index < XX_BTRFS_MAX_STRIPES;
         ++index) {
        size_t at = XX_BTRFS_CHUNK_HEAD_SIZE + index * XX_BTRFS_STRIPE_SIZE;
        entry.stripes[index].devid = xx_data_get_u64(chunk, available, at, false);
        entry.stripes[index].offset =
            xx_data_get_u64(chunk, available, at + 8U, false);
    }

    /* A duplicate logical start replaces the earlier mapping: the chunk tree
     * is authoritative over the superblock's bootstrap copy. */
    for (index = 0U; index < parsed->chunk_count; ++index) {
        if (parsed->chunks[index].logical == logical) {
            parsed->chunks[index] = entry;
            return true;
        }
    }
    parsed->chunks[parsed->chunk_count++] = entry;
    return true;
}

/* Translate a logical address to a device offset, and report how many bytes
 * remain contiguous from there. Refuses striped profiles and any chunk whose
 * stripes all live on some other device. */
static bool xx_btrfs_logical_to_physical(const xx_btrfs_private *parsed,
                                         uint64_t logical, int64_t *out_offset,
                                         uint64_t *out_run) {
    size_t index;
    if (!parsed || !out_offset) return false;
    for (index = 0U; index < parsed->chunk_count; ++index) {
        const xx_btrfs_chunk *chunk = &parsed->chunks[index];
        uint64_t delta;
        size_t stripe;
        if (logical < chunk->logical ||
            logical - chunk->logical >= chunk->length) continue;
        if ((chunk->type & XX_BTRFS_BG_STRIPED) != 0U) return false;
        delta = logical - chunk->logical;
        for (stripe = 0U;
             stripe < chunk->num_stripes && stripe < XX_BTRFS_MAX_STRIPES;
             ++stripe) {
            uint64_t physical;
            int64_t absolute;
            if (chunk->stripes[stripe].devid != parsed->super.devid) {
                /* Another device's copy; a mirrored profile keeps looking,
                 * a single-stripe chunk simply is not here. */
                continue;
            }
            physical = chunk->stripes[stripe].offset;
            if (physical > UINT64_MAX - delta) continue;
            physical += delta;
            if (physical > (uint64_t)INT64_MAX) continue;
            absolute = (int64_t)physical;
            if (absolute > INT64_MAX - parsed->base_address) continue;
            absolute += parsed->base_address;
            *out_offset = absolute;
            if (out_run) *out_run = chunk->length - delta;
            return true;
            /* Note: for a mirrored profile only the first stripe on this
             * device is used. There is no way to tell which mirror is the
             * good one without reading both, and the checksum check on the
             * chosen copy is the guard against a bad read. */
        }
        /* The chunk covers this address but none of its stripes live on the
         * device being read - a multi-device volume. */
        return false;
    }
    return false;
}

/* --- superblock --------------------------------------------------------- */

static bool xx_btrfs_decode_super(const uint8_t *raw, int64_t relative_offset,
                                  xx_btrfs_super *out) {
    size_t index;
    uint32_t stored;
    if (!raw || !out) return false;
    xx_mem_zero(out, sizeof(*out));
    if (xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 64U, false) !=
        XX_BTRFS_MAGIC) {
        return false;
    }
    out->bytenr = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 48U, false);
    /* Each copy records the offset it was written to; a mismatch means the
     * block was moved or forged. */
    if (relative_offset >= 0 && out->bytenr != (uint64_t)relative_offset) {
        return false;
    }
    out->csum_type = xx_data_get_u16(raw, XX_BTRFS_SUPER_SIZE, 196U, false);
    if (out->csum_type == XX_BTRFS_CSUM_CRC32C) {
        /* xx_crc32c_calc() complements both ends, so seed 0 is the standard
         * CRC-32C that btrfs stores - the canonical check value for
         * "123456789" is 0xE3069283 with this seeding. */
        stored = xx_data_get_u32(raw, XX_BTRFS_SUPER_SIZE, 0U, false);
        if (stored != xx_crc32c_calc(0U, raw + 32,
                                     (size_t)XX_BTRFS_SUPER_SIZE - 32U)) {
            return false;
        }
        out->csum_verified = true;
    }

    xx_mem_copy(out->fsid, raw + 32, XX_BTRFS_UUID_SIZE);
    xx_mem_copy(out->metadata_uuid, raw + 571, XX_BTRFS_UUID_SIZE);
    out->generation = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 72U, false);
    out->root = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 80U, false);
    out->chunk_root = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 88U, false);
    out->total_bytes = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 112U, false);
    out->bytes_used = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 120U, false);
    out->root_dir_objectid =
        xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 128U, false);
    out->num_devices = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 136U, false);
    out->sector_size = xx_data_get_u32(raw, XX_BTRFS_SUPER_SIZE, 144U, false);
    out->node_size = xx_data_get_u32(raw, XX_BTRFS_SUPER_SIZE, 148U, false);
    out->sys_chunk_array_size =
        xx_data_get_u32(raw, XX_BTRFS_SUPER_SIZE, 160U, false);
    out->compat_ro_flags = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 180U, false);
    out->incompat_flags = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 188U, false);
    out->root_level = raw[198];
    out->chunk_root_level = raw[199];
    out->devid = xx_data_get_u64(raw, XX_BTRFS_SUPER_SIZE, 201U, false);

    /* The label is a fixed 256-byte field; it is NUL padded but a hostile
     * image need not terminate it, so the copy is bounded and terminated
     * here and any control byte truncates it. */
    for (index = 0U; index < XX_BTRFS_LABEL_SIZE; ++index) {
        uint8_t ch = raw[299U + index];
        if (ch == 0U || ch < 32U) break;
        out->label[index] = (char)ch;
    }
    out->label[index] = '\0';

    if (out->node_size < XX_BTRFS_MIN_NODE_SIZE ||
        out->node_size > XX_BTRFS_MAX_NODE_SIZE ||
        (out->node_size & (out->node_size - 1U)) != 0U) return false;
    if (out->sector_size < 512U || out->sector_size > out->node_size ||
        (out->sector_size & (out->sector_size - 1U)) != 0U) return false;
    if (out->sys_chunk_array_size > XX_BTRFS_SYS_CHUNK_ARRAY_MAX) return false;
    if (out->root_level > XX_BTRFS_MAX_DEPTH ||
        out->chunk_root_level > XX_BTRFS_MAX_DEPTH) return false;
    if (out->num_devices == 0U) return false;
    return true;
}

/* Try the primary superblock and then the two mirrors that can plausibly fit
 * on this device. The first copy that decodes wins. */
static bool xx_btrfs_load_super(xx_btrfs_private *parsed) {
    static const int64_t candidates[3] = {XX_BTRFS_SUPER_OFFSET,
                                          XX_BTRFS_SUPER_MIRROR1,
                                          XX_BTRFS_SUPER_MIRROR2};
    uint8_t *raw;
    size_t index;
    bool found = false;
    raw = (uint8_t *)xx_mem_alloc(XX_BTRFS_SUPER_SIZE);
    if (!raw) return false;
    for (index = 0U; index < 3U && !found; ++index) {
        int64_t at = candidates[index];
        if (at > INT64_MAX - parsed->base_address) continue;
        at += parsed->base_address;
        if (!xx_btrfs_range_within(parsed->input_size, at,
                                   (int64_t)XX_BTRFS_SUPER_SIZE)) continue;
        if (!xx_btrfs_read_at(parsed->device, at, raw, XX_BTRFS_SUPER_SIZE)) {
            continue;
        }
        if (xx_btrfs_decode_super(raw, candidates[index], &parsed->super)) {
            parsed->super_offset = at;
            found = true;
        }
    }
    xx_mem_free(raw);
    return found;
}

/* The superblock's sys_chunk_array bootstraps the chunk tree: it is a run of
 * (disk_key, btrfs_chunk) pairs with no padding. */
static bool xx_btrfs_parse_sys_chunks(xx_btrfs_private *parsed,
                                      const uint8_t *raw) {
    size_t cursor = 0U;
    size_t limit = parsed->super.sys_chunk_array_size;
    while (cursor + XX_BTRFS_DISK_KEY_SIZE + XX_BTRFS_CHUNK_HEAD_SIZE <= limit) {
        const uint8_t *key = raw + XX_BTRFS_SYS_CHUNK_ARRAY_OFFSET + cursor;
        uint64_t objectid = xx_data_get_u64(key, XX_BTRFS_DISK_KEY_SIZE, 0U, false);
        uint8_t type = key[8];
        uint64_t logical = xx_data_get_u64(key, XX_BTRFS_DISK_KEY_SIZE, 9U, false);
        const uint8_t *chunk = key + XX_BTRFS_DISK_KEY_SIZE;
        size_t available = limit - cursor - XX_BTRFS_DISK_KEY_SIZE;
        uint16_t num_stripes;
        size_t consumed;
        if (type != XX_BTRFS_KEY_CHUNK_ITEM ||
            objectid != XX_BTRFS_FIRST_CHUNK_TREE_OBJECTID) return false;
        num_stripes = xx_data_get_u16(chunk, available, 44U, false);
        if (num_stripes == 0U) return false;
        consumed = XX_BTRFS_CHUNK_HEAD_SIZE +
                   (size_t)num_stripes * XX_BTRFS_STRIPE_SIZE;
        if (consumed > available) return false;
        if (!xx_btrfs_chunk_add(parsed, logical, chunk, available)) return false;
        cursor += XX_BTRFS_DISK_KEY_SIZE + consumed;
    }
    return parsed->chunk_count != 0U;
}

/* --- tree walking ------------------------------------------------------- */

/* Read one tree block, verifying its bounds, checksum, self-reported logical
 * address and fsid. Returns false for any of those, and the caller then
 * abandons that subtree rather than trusting its contents. */
static bool xx_btrfs_read_node(xx_btrfs_private *parsed, uint64_t logical,
                               uint8_t *node, int64_t *out_phys) {
    int64_t physical = 0;
    uint64_t run = 0U;
    uint32_t stored;
    uint32_t node_size = parsed->super.node_size;
    if (!xx_btrfs_logical_to_physical(parsed, logical, &physical, &run)) {
        return false;
    }
    if (run < node_size) return false;
    if (!xx_btrfs_range_within(parsed->input_size, physical, (int64_t)node_size)) {
        return false;
    }
    if (!xx_btrfs_read_at(parsed->device, physical, node, node_size)) return false;
    if (parsed->super.csum_type != XX_BTRFS_CSUM_CRC32C) return false;
    stored = xx_data_get_u32(node, node_size, 0U, false);
    if (stored != xx_crc32c_calc(0U, node + 32, (size_t)node_size - 32U)) {
        return false;
    }
    if (xx_data_get_u64(node, node_size, 48U, false) != logical) return false;
    /* An image with the METADATA_UUID incompat bit stores the metadata uuid
     * in tree blocks; without it, the fsid. Accept either. */
    if (xx_rt_memcmp(node + 32, parsed->super.fsid, XX_BTRFS_UUID_SIZE) != 0 &&
        xx_rt_memcmp(node + 32, parsed->super.metadata_uuid,
                     XX_BTRFS_UUID_SIZE) != 0) {
        return false;
    }
    if (out_phys) *out_phys = physical;
    return true;
}

/* Depth-first walk. Every visited logical address is remembered, so a block
 * that points back into the tree ends that branch instead of looping. */
static void xx_btrfs_walk_tree(xx_btrfs_private *parsed, uint64_t logical,
                               unsigned depth, xx_btrfs_item_cb callback,
                               void *ctx) {
    uint8_t *node;
    uint32_t node_size = parsed->super.node_size;
    uint32_t nritems;
    uint8_t level;
    int64_t physical = 0;
    uint32_t index;

    if (depth > XX_BTRFS_MAX_DEPTH || xx_btrfs_stopped(parsed)) return;
    if (parsed->nodes >= XX_BTRFS_MAX_NODES) return;
    if (xx_btrfs_map_get(&parsed->visited, logical) != 0U) return;
    if (!xx_btrfs_map_put(&parsed->visited, logical, 1U)) return;
    ++parsed->nodes;

    node = (uint8_t *)xx_mem_alloc(node_size);
    if (!node) return;
    if (!xx_btrfs_read_node(parsed, logical, node, &physical)) {
        ++parsed->bad_nodes;
        xx_mem_free(node);
        return;
    }
    nritems = xx_data_get_u32(node, node_size, 96U, false);
    level = node[100];

    if (level == 0U) {
        /* Leaf. Each item's payload must lie inside the node, after the item
         * array; anything else is a forged offset/size pair. */
        uint32_t max_items =
            (node_size - XX_BTRFS_HEADER_SIZE) / XX_BTRFS_ITEM_SIZE;
        if (nritems > max_items) {
            ++parsed->bad_nodes;
            xx_mem_free(node);
            return;
        }
        for (index = 0U; index < nritems; ++index) {
            size_t at = XX_BTRFS_HEADER_SIZE + (size_t)index * XX_BTRFS_ITEM_SIZE;
            uint64_t objectid = xx_data_get_u64(node, node_size, at, false);
            uint8_t type = node[at + 8U];
            uint64_t offset = xx_data_get_u64(node, node_size, at + 9U, false);
            uint32_t data_off = xx_data_get_u32(node, node_size, at + 17U, false);
            uint32_t data_len = xx_data_get_u32(node, node_size, at + 21U, false);
            size_t start;
            if (xx_btrfs_stopped(parsed)) break;
            if (data_off > node_size - XX_BTRFS_HEADER_SIZE ||
                data_len > node_size - XX_BTRFS_HEADER_SIZE - data_off) {
                ++parsed->bad_nodes;
                continue;
            }
            start = XX_BTRFS_HEADER_SIZE + data_off;
            if (start < XX_BTRFS_HEADER_SIZE +
                            (size_t)nritems * XX_BTRFS_ITEM_SIZE) {
                ++parsed->bad_nodes;
                continue;
            }
            if (callback && !callback(parsed, ctx, objectid, type, offset,
                                      node + start, data_len,
                                      physical + (int64_t)start)) {
                break;
            }
        }
    } else {
        /* Internal node. The child addresses are copied out before the
         * recursion so the buffer can be released first, which keeps peak
         * memory at one node per level instead of one per branch. */
        uint32_t max_ptrs =
            (node_size - XX_BTRFS_HEADER_SIZE) / XX_BTRFS_KEY_PTR_SIZE;
        uint64_t *children;
        if (nritems > max_ptrs || nritems == 0U) {
            ++parsed->bad_nodes;
            xx_mem_free(node);
            return;
        }
        children = (uint64_t *)xx_mem_alloc((size_t)nritems * sizeof(*children));
        if (!children) {
            xx_mem_free(node);
            return;
        }
        for (index = 0U; index < nritems; ++index) {
            size_t at = XX_BTRFS_HEADER_SIZE +
                        (size_t)index * XX_BTRFS_KEY_PTR_SIZE;
            children[index] = xx_data_get_u64(node, node_size,
                                              at + XX_BTRFS_DISK_KEY_SIZE, false);
        }
        xx_mem_free(node);
        node = NULL;
        for (index = 0U; index < nritems; ++index) {
            xx_btrfs_walk_tree(parsed, children[index], depth + 1U, callback,
                               ctx);
        }
        xx_mem_free(children);
        return;
    }
    xx_mem_free(node);
}

/* --- collectors --------------------------------------------------------- */

static bool xx_btrfs_chunk_item_cb(xx_btrfs_private *parsed, void *ctx,
                                   uint64_t objectid, uint8_t type,
                                   uint64_t offset, const uint8_t *data,
                                   uint32_t size, int64_t data_phys) {
    (void)ctx;
    (void)data_phys;
    if (type != XX_BTRFS_KEY_CHUNK_ITEM ||
        objectid != XX_BTRFS_FIRST_CHUNK_TREE_OBJECTID) return true;
    /* A chunk that will not decode is skipped, not fatal: the map built so
     * far still translates everything it already covers. */
    (void)xx_btrfs_chunk_add(parsed, offset, data, size);
    return true;
}

typedef struct xx_btrfs_root_ctx_s {
    uint64_t bytenr;
    uint8_t level;
    bool found;
} xx_btrfs_root_ctx;

static bool xx_btrfs_root_item_cb(xx_btrfs_private *parsed, void *ctx,
                                  uint64_t objectid, uint8_t type,
                                  uint64_t offset, const uint8_t *data,
                                  uint32_t size, int64_t data_phys) {
    xx_btrfs_root_ctx *out = (xx_btrfs_root_ctx *)ctx;
    (void)parsed;
    (void)data_phys;
    if (type != XX_BTRFS_KEY_ROOT_ITEM ||
        objectid != XX_BTRFS_FS_TREE_OBJECTID) return true;
    if (size < XX_BTRFS_ROOT_ITEM_MIN_SIZE) return true;
    /* The live FS_TREE root item has key offset 0; a non-zero offset names a
     * snapshot of it, which this reader does not follow. */
    if (offset != 0U && out->found) return true;
    out->bytenr = xx_data_get_u64(data, size, XX_BTRFS_INODE_ITEM_SIZE + 16U,
                                  false);
    out->level = data[238];
    out->found = true;
    return offset != 0U;
}

static xx_btrfs_inode *xx_btrfs_inode_for(xx_btrfs_private *parsed,
                                          uint64_t objectid) {
    size_t slot = xx_btrfs_map_get(&parsed->inode_map, objectid);
    if (slot != 0U) return &parsed->inodes[slot - 1U];
    if (parsed->inode_count >= XX_BTRFS_MAX_INODES) return NULL;
    if (parsed->inode_count == parsed->inode_capacity) {
        size_t capacity = parsed->inode_capacity ? parsed->inode_capacity * 2U
                                                 : 64U;
        xx_btrfs_inode *grown;
        if (capacity < parsed->inode_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return NULL;
        grown = (xx_btrfs_inode *)xx_mem_realloc(parsed->inodes,
                                                 capacity * sizeof(*grown));
        if (!grown) return NULL;
        parsed->inodes = grown;
        parsed->inode_capacity = capacity;
    }
    xx_mem_zero(&parsed->inodes[parsed->inode_count], sizeof(xx_btrfs_inode));
    parsed->inodes[parsed->inode_count].objectid = objectid;
    parsed->inodes[parsed->inode_count].inline_phys = -1;
    ++parsed->inode_count;
    if (!xx_btrfs_map_put(&parsed->inode_map, objectid, parsed->inode_count)) {
        --parsed->inode_count;
        return NULL;
    }
    return &parsed->inodes[parsed->inode_count - 1U];
}

static bool xx_btrfs_dirent_add(xx_btrfs_private *parsed, uint64_t parent,
                                uint64_t child, uint8_t ftype, uint8_t loc_type,
                                const uint8_t *name, size_t name_len) {
    xx_btrfs_dirent *entry;
    char *copy;
    size_t index;
    size_t head;
    if (parsed->dirent_count >= XX_BTRFS_MAX_DIRENTS) return false;
    if (name_len == 0U || name_len > XX_BTRFS_MAX_NAME) return true;
    /* A btrfs name is one path component; a separator or a control byte in
     * it would silently re-parent the entry on extraction. */
    for (index = 0U; index < name_len; ++index) {
        if (name[index] < 32U || name[index] == '/' || name[index] == '\\') {
            return true;
        }
    }
    if ((name_len == 1U && name[0] == '.') ||
        (name_len == 2U && name[0] == '.' && name[1] == '.')) return true;

    if (parsed->dirent_count == parsed->dirent_capacity) {
        size_t capacity = parsed->dirent_capacity ? parsed->dirent_capacity * 2U
                                                  : 64U;
        xx_btrfs_dirent *grown;
        if (capacity < parsed->dirent_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_btrfs_dirent *)xx_mem_realloc(parsed->dirents,
                                                  capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->dirents = grown;
        parsed->dirent_capacity = capacity;
    }
    copy = (char *)xx_mem_alloc(name_len + 1U);
    if (!copy) return false;
    xx_mem_copy(copy, name, name_len);
    copy[name_len] = '\0';

    entry = &parsed->dirents[parsed->dirent_count];
    xx_mem_zero(entry, sizeof(*entry));
    entry->parent = parent;
    entry->child = child;
    entry->ftype = ftype;
    entry->loc_type = loc_type;
    entry->name = copy;
    head = xx_btrfs_map_get(&parsed->dirent_head, parent);
    entry->next = head;
    ++parsed->dirent_count;
    if (!xx_btrfs_map_put(&parsed->dirent_head, parent, parsed->dirent_count)) {
        xx_str_free(copy);
        entry->name = NULL;
        --parsed->dirent_count;
        return false;
    }
    return true;
}

static bool xx_btrfs_fs_item_cb(xx_btrfs_private *parsed, void *ctx,
                                uint64_t objectid, uint8_t type,
                                uint64_t offset, const uint8_t *data,
                                uint32_t size, int64_t data_phys) {
    (void)ctx;
    if (type == XX_BTRFS_KEY_INODE_ITEM) {
        xx_btrfs_inode *inode;
        if (size < XX_BTRFS_INODE_ITEM_SIZE) return true;
        inode = xx_btrfs_inode_for(parsed, objectid);
        if (!inode) return false;
        inode->size = xx_data_get_u64(data, size, 16U, false);
        inode->mode = xx_data_get_u32(data, size, 52U, false);
        return true;
    }
    if (type == XX_BTRFS_KEY_DIR_ITEM) {
        /* Several btrfs_dir_item can share one item when their name hashes
         * collide, so the payload is a packed run, not a single record. */
        uint32_t cursor = 0U;
        while (cursor + XX_BTRFS_DIR_ITEM_HEAD_SIZE <= size) {
            const uint8_t *item = data + cursor;
            uint32_t remaining = size - cursor;
            uint64_t child = xx_data_get_u64(item, remaining, 0U, false);
            uint8_t loc_type = item[8];
            uint16_t data_len = xx_data_get_u16(item, remaining, 25U, false);
            uint16_t name_len = xx_data_get_u16(item, remaining, 27U, false);
            uint8_t ftype = item[29];
            uint32_t consumed;
            if ((uint32_t)name_len + (uint32_t)data_len >
                remaining - XX_BTRFS_DIR_ITEM_HEAD_SIZE) break;
            consumed = XX_BTRFS_DIR_ITEM_HEAD_SIZE + name_len + data_len;
            if (!xx_btrfs_dirent_add(parsed, objectid, child, ftype, loc_type,
                                     item + XX_BTRFS_DIR_ITEM_HEAD_SIZE,
                                     name_len)) {
                return false;
            }
            cursor += consumed;
        }
        return true;
    }
    if (type == XX_BTRFS_KEY_EXTENT_DATA) {
        xx_btrfs_inode *inode;
        uint8_t extent_type;
        if (size < XX_BTRFS_EXTENT_INLINE_HEAD) return true;
        inode = xx_btrfs_inode_for(parsed, objectid);
        if (!inode) return false;
        ++inode->extent_count;
        /* Only the first extent is retained. Anything with more than one is
         * reported as not extractable rather than partially assembled. */
        if (inode->extent_count > 1U) return true;
        extent_type = data[20];
        inode->extent_type = extent_type;
        inode->compression = data[16];
        inode->logical_offset = offset;
        inode->ram_bytes = xx_data_get_u64(data, size, 8U, false);
        if (extent_type == XX_BTRFS_EXTENT_INLINE) {
            inode->inline_phys = data_phys + (int64_t)XX_BTRFS_EXTENT_INLINE_HEAD;
            inode->inline_size = size - XX_BTRFS_EXTENT_INLINE_HEAD;
        } else if (size >= XX_BTRFS_EXTENT_REG_SIZE) {
            inode->disk_bytenr = xx_data_get_u64(data, size, 21U, false);
            inode->disk_offset = xx_data_get_u64(data, size, 37U, false);
            inode->num_bytes = xx_data_get_u64(data, size, 45U, false);
        } else {
            inode->extent_type = 0xFFU;  /* truncated; refuse extraction */
        }
        return true;
    }
    return true;
}

/* --- listing ------------------------------------------------------------ */

static char *xx_btrfs_join(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_BTRFS_MAX_PATH ||
        name_size > XX_BTRFS_MAX_PATH - prefix_size -
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

static bool xx_btrfs_entry_append(xx_btrfs_private *parsed,
                                  xx_btrfs_entry *entry) {
    if (parsed->entry_count >= XX_BTRFS_MAX_ENTRIES) return false;
    if (parsed->entry_count == parsed->entry_capacity) {
        size_t capacity = parsed->entry_capacity ? parsed->entry_capacity * 2U
                                                 : 64U;
        xx_btrfs_entry *grown;
        if (capacity < parsed->entry_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_btrfs_entry *)xx_mem_realloc(parsed->entries,
                                                 capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->entry_capacity = capacity;
    }
    parsed->entries[parsed->entry_count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Decide whether a file's single extent can be handed to the generic byte
 * copier, and if so where its bytes live. Anything compressed, preallocated,
 * split across extents or not starting at logical offset 0 is refused. */
static void xx_btrfs_resolve_data(xx_btrfs_private *parsed,
                                  const xx_btrfs_inode *inode,
                                  xx_btrfs_entry *entry) {
    entry->data_offset = -1;
    entry->data_size = 0;
    entry->extractable = false;
    entry->method = inode ? inode->compression : 0U;
    if (!inode) return;
    if (inode->size == 0U) {
        entry->data_offset = parsed->super_offset;
        entry->data_size = 0;
        entry->extractable = inode->extent_count == 0U;
        return;
    }
    if (inode->extent_count != 1U) return;
    if (inode->compression != XX_BTRFS_COMPRESS_NONE) return;
    if (inode->logical_offset != 0U) return;
    if (inode->extent_type == XX_BTRFS_EXTENT_INLINE) {
        if (inode->inline_phys < 0 || inode->inline_size < inode->size) return;
        if (!xx_btrfs_range_within(parsed->input_size, inode->inline_phys,
                                   (int64_t)inode->size)) return;
        entry->data_offset = inode->inline_phys;
        entry->data_size = (int64_t)inode->size;
        entry->extractable = true;
        return;
    }
    if (inode->extent_type != XX_BTRFS_EXTENT_REG) return;
    if (inode->disk_bytenr == 0U) return;  /* a hole */
    if (inode->num_bytes < inode->size) return;
    {
        uint64_t logical = inode->disk_bytenr;
        int64_t physical = 0;
        uint64_t run = 0U;
        if (logical > UINT64_MAX - inode->disk_offset) return;
        logical += inode->disk_offset;
        if (!xx_btrfs_logical_to_physical(parsed, logical, &physical, &run)) {
            return;
        }
        if (run < inode->size) return;
        if (!xx_btrfs_range_within(parsed->input_size, physical,
                                   (int64_t)inode->size)) return;
        entry->data_offset = physical;
        entry->data_size = (int64_t)inode->size;
        entry->extractable = true;
    }
}

/* Breadth-first expansion of the directory graph from the root inode. The
 * visited set is keyed by inode number, so a hard link cycle or a directory
 * that names itself cannot loop; the depth cap bounds path length. */
static void xx_btrfs_build_listing(xx_btrfs_private *parsed) {
    typedef struct {
        uint64_t objectid;
        char *path;
        unsigned depth;
    } queue_item;
    queue_item *queue;
    size_t head = 0U;
    size_t tail = 0U;
    size_t capacity = 64U;
    xx_btrfs_map seen;
    uint64_t root = parsed->super.root_dir_objectid;

    xx_mem_zero(&seen, sizeof(seen));
    queue = (queue_item *)xx_mem_calloc(capacity, sizeof(*queue));
    if (!queue) return;
    if (root == 0U) root = XX_BTRFS_FIRST_CHUNK_TREE_OBJECTID;
    queue[tail].objectid = root;
    queue[tail].path = NULL;
    queue[tail].depth = 0U;
    ++tail;
    (void)xx_btrfs_map_put(&seen, root, 1U);

    while (head < tail) {
        queue_item current = queue[head++];
        size_t cursor = xx_btrfs_map_get(&parsed->dirent_head, current.objectid);
        if (xx_btrfs_stopped(parsed)) break;
        while (cursor != 0U) {
            const xx_btrfs_dirent *dirent = &parsed->dirents[cursor - 1U];
            char *path;
            xx_btrfs_entry entry;
            bool is_dir;
            cursor = dirent->next;
            if (parsed->entry_count >= XX_BTRFS_MAX_ENTRIES) break;
            /* A DIR_ITEM whose location is a ROOT_ITEM is a nested subvolume;
             * its tree is a different FS_TREE and is not followed here. */
            if (dirent->loc_type != XX_BTRFS_KEY_INODE_ITEM) {
                ++parsed->unsupported;
                continue;
            }
            path = xx_btrfs_join(current.path, dirent->name);
            if (!path) continue;
            is_dir = dirent->ftype == XX_BTRFS_FT_DIR;

            xx_mem_zero(&entry, sizeof(entry));
            entry.name = path;
            entry.is_folder = is_dir;
            entry.data_offset = -1;
            if (!is_dir) {
                size_t slot = xx_btrfs_map_get(&parsed->inode_map, dirent->child);
                const xx_btrfs_inode *inode =
                    slot != 0U ? &parsed->inodes[slot - 1U] : NULL;
                if (inode) entry.size = inode->size;
                if (dirent->ftype == XX_BTRFS_FT_REG_FILE) {
                    xx_btrfs_resolve_data(parsed, inode, &entry);
                    entry.name = path;
                    entry.is_folder = false;
                    entry.size = inode ? inode->size : 0U;
                }
                if (!entry.extractable) ++parsed->unsupported;
            }
            if (!xx_btrfs_entry_append(parsed, &entry)) {
                xx_str_free(path);
                continue;
            }
            /* entry now owns path; the queue borrows the copy stored in the
             * entry array, which outlives the traversal. */
            if (is_dir && current.depth + 1U <= XX_BTRFS_MAX_DEPTH * 4U &&
                xx_btrfs_map_get(&seen, dirent->child) == 0U) {
                if (!xx_btrfs_map_put(&seen, dirent->child, 1U)) continue;
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
                queue[tail].objectid = dirent->child;
                queue[tail].path = parsed->entries[parsed->entry_count - 1U].name;
                queue[tail].depth = current.depth + 1U;
                ++tail;
            }
        }
    }
    xx_mem_free(queue);
    xx_btrfs_map_cleanup(&seen);
}

/* --- parse -------------------------------------------------------------- */

static void xx_btrfs_private_cleanup(xx_btrfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->dirent_count; ++index) {
        if (parsed->dirents[index].name) xx_str_free(parsed->dirents[index].name);
    }
    for (index = 0U; index < parsed->entry_count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->chunks) xx_mem_free(parsed->chunks);
    if (parsed->inodes) xx_mem_free(parsed->inodes);
    if (parsed->dirents) xx_mem_free(parsed->dirents);
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_btrfs_map_cleanup(&parsed->inode_map);
    xx_btrfs_map_cleanup(&parsed->dirent_head);
    xx_btrfs_map_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->super_offset = -1;
}

static void xx_btrfs_private_free(xx_btrfs_private *parsed) {
    if (!parsed) return;
    xx_btrfs_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* Parse the volume. The superblock alone is enough to succeed: a volume whose
 * trees cannot be walked still identifies, with an empty listing. */
static bool xx_btrfs_parse(Abstractformat *self, xx_btrfs_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t *raw = NULL;
    xx_btrfs_root_ctx root_ctx;
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
    if (!xx_btrfs_load_super(parsed)) goto fail;

    parsed->chunks = (xx_btrfs_chunk *)xx_mem_calloc(XX_BTRFS_MAX_CHUNKS,
                                                     sizeof(*parsed->chunks));
    if (!parsed->chunks) goto fail;

    /* Re-read the accepted superblock so the system chunk array can be parsed
     * out of it; the decode above kept only scalars. */
    raw = (uint8_t *)xx_mem_alloc(XX_BTRFS_SUPER_SIZE);
    if (!raw || !xx_btrfs_read_at(parsed->device, parsed->super_offset, raw,
                                  XX_BTRFS_SUPER_SIZE)) goto fail;
    if (!xx_btrfs_parse_sys_chunks(parsed, raw)) {
        /* No bootstrap map: identification succeeds, nothing is walkable. */
        xx_mem_free(raw);
        return true;
    }
    xx_mem_free(raw);
    raw = NULL;

    if (!parsed->super.csum_verified) {
        /* XXHASH64 / SHA256 / BLAKE2B are not implemented here, and walking
         * unverified tree blocks would mean trusting forged pointers. */
        return true;
    }

    xx_btrfs_walk_tree(parsed, parsed->super.chunk_root, 0U,
                       xx_btrfs_chunk_item_cb, NULL);

    xx_mem_zero(&root_ctx, sizeof(root_ctx));
    xx_btrfs_walk_tree(parsed, parsed->super.root, 0U, xx_btrfs_root_item_cb,
                       &root_ctx);
    if (!root_ctx.found || root_ctx.bytenr == 0U) return true;
    parsed->fs_tree_root = root_ctx.bytenr;
    parsed->fs_tree_level = root_ctx.level;

    xx_btrfs_walk_tree(parsed, parsed->fs_tree_root, 0U, xx_btrfs_fs_item_cb,
                       NULL);
    xx_btrfs_build_listing(parsed);
    return true;
fail:
    if (raw) xx_mem_free(raw);
    xx_btrfs_private_cleanup(parsed);
    return false;
}

/* --- archive record plumbing -------------------------------------------- */

static bool xx_btrfs_copy_options(xx_list_s *destination,
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

static const xx_var *xx_btrfs_find_option(const xx_list_s *options,
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

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for. */
static bool xx_btrfs_safe_name(const char *name) {
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

static bool xx_btrfs_populate_record(xx_archive_record *record,
                                     const xx_btrfs_entry *entry) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)(entry->data_size > 0
                                                         ? entry->data_size : 0)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          entry->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_btrfs_archive_stream_free(void *pointer) {
    xx_btrfs_archive_stream *stream = (xx_btrfs_archive_stream *)pointer;
    if (!stream) return;
    if (stream->parsed) xx_btrfs_private_free(stream->parsed);
    xx_mem_free(stream);
}

/* --- public API --------------------------------------------------------- */

void xx_btrfs_init(xx_btrfs *btrfs, xx_io_device *dev, int64_t base_address) {
    if (!btrfs) return;
    xx_mem_zero(btrfs, sizeof(*btrfs));
    xx_format_init(&btrfs->format, dev, base_address);
    btrfs->format.endian = XX_ENDIAN_LITTLE;
    btrfs->format.file_type = XX_BTRFS_FILE_TYPE;
    btrfs->format.format_type = XX_TYPE_ARCHIVE;
    btrfs->format.is_archive = true;
    xx_format_set_mime_type(&btrfs->format, "application/x-btrfs-image");
    xx_format_set_extension(&btrfs->format, "btrfs");
    btrfs->format.check_is_valid = xx_btrfs_check_is_valid;
    btrfs->format.handle_base_info = xx_btrfs_handle_base_info;
    btrfs->format.get_format_size = xx_btrfs_get_format_size;
    btrfs->format.get_number_of_archive_records =
        xx_btrfs_get_number_of_archive_records;
    btrfs->format.create_archive_records_reading =
        xx_btrfs_create_archive_records_reading;
    btrfs->format.get_current_archive_record =
        xx_btrfs_get_current_archive_record;
    btrfs->format.unpack_current_archive_record =
        xx_btrfs_unpack_current_archive_record;
    btrfs->format.archive_record_move_to_next =
        xx_btrfs_archive_record_move_to_next;
    btrfs->format.free_archive_records_reading =
        xx_btrfs_free_archive_records_reading;
    btrfs->format.destroy = xx_btrfs_vtable_destroy;
    btrfs->super_offset = -1;
}

xx_btrfs *xx_btrfs_create(xx_io_device *dev, int64_t base_address) {
    xx_btrfs *btrfs = (xx_btrfs *)xx_mem_alloc(sizeof(*btrfs));
    if (btrfs) xx_btrfs_init(btrfs, dev, base_address);
    return btrfs;
}

void xx_btrfs_destroy(xx_btrfs *btrfs) {
    if (!btrfs) return;
    if (btrfs->internal) {
        xx_btrfs_private_free((xx_btrfs_private *)btrfs->internal);
        btrfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&btrfs->format);
}

static void xx_btrfs_vtable_destroy(Abstractformat *self) {
    xx_btrfs_destroy((xx_btrfs *)self);
}

void xx_btrfs_free(xx_btrfs *btrfs) {
    if (!btrfs) return;
    xx_btrfs_destroy(btrfs);
    xx_mem_free(btrfs);
}

bool xx_btrfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    /* Identification only needs the superblock, so the tree walk is skipped
     * here: xx_btrfs_parse() would otherwise read the whole volume just to
     * answer a probe. */
    xx_btrfs_private probe;
    bool result;
    xx_mem_zero(&probe, sizeof(probe));
    probe.input_size = -1;
    probe.super_offset = -1;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    probe.device = self->device;
    probe.pd = pd;
    probe.base_address = self->base_address;
    probe.input_size = xx_io_total_size(self->device);
    result = xx_btrfs_load_super(&probe);
    xx_btrfs_private_cleanup(&probe);
    return result;
}

bool xx_btrfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_btrfs_private *parsed;
    xx_btrfs *btrfs = (xx_btrfs *)self;
    int64_t total_size;
    size_t index;
    if (!self || !btrfs) return false;
    parsed = (xx_btrfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_btrfs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (btrfs->internal) xx_btrfs_private_free((xx_btrfs_private *)btrfs->internal);
    btrfs->internal = parsed;

    xx_mem_copy(btrfs->fsid, parsed->super.fsid, XX_BTRFS_UUID_SIZE);
    xx_mem_copy(btrfs->metadata_uuid, parsed->super.metadata_uuid,
                XX_BTRFS_UUID_SIZE);
    for (index = 0U; index <= XX_BTRFS_LABEL_SIZE; ++index) {
        btrfs->label[index] = parsed->super.label[index];
        if (!parsed->super.label[index]) break;
    }
    btrfs->label[XX_BTRFS_LABEL_SIZE] = '\0';
    btrfs->generation = parsed->super.generation;
    btrfs->root_tree = parsed->super.root;
    btrfs->chunk_root = parsed->super.chunk_root;
    btrfs->total_bytes = parsed->super.total_bytes;
    btrfs->bytes_used = parsed->super.bytes_used;
    btrfs->num_devices = parsed->super.num_devices;
    btrfs->devid = parsed->super.devid;
    btrfs->incompat_flags = parsed->super.incompat_flags;
    btrfs->compat_ro_flags = parsed->super.compat_ro_flags;
    btrfs->sector_size = parsed->super.sector_size;
    btrfs->node_size = parsed->super.node_size;
    btrfs->csum_type = parsed->super.csum_type;
    btrfs->root_level = parsed->super.root_level;
    btrfs->chunk_root_level = parsed->super.chunk_root_level;
    btrfs->super_offset = parsed->super_offset;
    btrfs->csum_verified = parsed->super.csum_verified;
    btrfs->number_of_chunks = parsed->chunk_count;
    btrfs->fs_tree_root = parsed->fs_tree_root;
    btrfs->number_of_unsupported = parsed->unsupported;
    btrfs->number_of_records = parsed->entry_count;
    btrfs->number_of_members = parsed->entry_count;

    total_size = xx_io_total_size(self->device);
    /* total_bytes is the sum across every device in the volume, so it is only
     * meaningful as a size for a single-device image. */
    if (parsed->super.num_devices == 1U &&
        parsed->super.total_bytes <= (uint64_t)INT64_MAX &&
        (int64_t)parsed->super.total_bytes <= total_size - self->base_address) {
        self->format_size = (int64_t)parsed->super.total_bytes;
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

int64_t xx_btrfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_btrfs_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_btrfs *)self)->number_of_records;
}

xx_archive_record_state *xx_btrfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_btrfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_btrfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    stream->parsed = (xx_btrfs_private *)xx_mem_alloc(sizeof(*stream->parsed));
    if (!stream->parsed || !xx_btrfs_copy_options(&state->options, options) ||
        !xx_btrfs_parse(self, stream->parsed, pd)) {
        xx_btrfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_btrfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed->entry_count;
    if (stream->parsed->entry_count != 0U &&
        xx_btrfs_populate_record(&state->current_record,
                                 &stream->parsed->entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_btrfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_btrfs_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_btrfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_btrfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed->entry_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_btrfs_populate_record(&state->current_record,
                                  &stream->parsed->entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_btrfs_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_btrfs_entry *entry;
    xx_btrfs_archive_stream *stream;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_btrfs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed->entry_count) return false;
    entry = &stream->parsed->entries[stream->index];
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_btrfs_safe_name(name)) return false;
    /* A file this reader cannot reconstruct byte for byte - compressed,
     * multi-extent, preallocated or a hole - is refused outright rather than
     * written out truncated or filled with the wrong bytes. */
    if (!entry->is_folder && !entry->extractable) return false;

    option = xx_btrfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        if (!result) xx_rt_remove(destination);
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

void xx_btrfs_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

const char *xx_btrfs_get_label(const xx_btrfs *btrfs) {
    return btrfs ? btrfs->label : NULL;
}
uint32_t xx_btrfs_get_node_size(const xx_btrfs *btrfs) {
    return btrfs ? btrfs->node_size : 0U;
}
uint64_t xx_btrfs_get_number_of_chunks(const xx_btrfs *btrfs) {
    return btrfs ? btrfs->number_of_chunks : 0U;
}
bool xx_btrfs_get_csum_verified(const xx_btrfs *btrfs) {
    return btrfs ? btrfs->csum_verified : false;
}

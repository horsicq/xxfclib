/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Scope, stated plainly so that nothing here is silently wrong.
 *
 * Implemented: the superblock, the master node (newest valid copy across
 * LEB 1 and LEB 2), a full walk of the committed index B-tree, the inode,
 * directory-entry and data leaf nodes those branches point at, path
 * reconstruction from the root inode, and extraction of regular files with
 * the none / LZO / zlib / zstd compressors.
 *
 * NOT implemented, and therefore not reported: the journal. UBIFS writes new
 * nodes into log buds and only folds them into the index at commit time, so
 * anything written since the last commit is invisible here. The same applies
 * to the LPT, the orphan area, extended attributes (xent leaves are counted
 * but not turned into members) and the recovery paths a mounting kernel would
 * run. An encrypted (UBIFS_FLG_ENCRYPTION) or authenticated
 * (UBIFS_FLG_AUTHENTICATION) filesystem is rejected outright rather than
 * half-parsed: authentication changes the branch stride, and encryption makes
 * the names and contents meaningless without a key.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ubifs/xx_ubifs.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_UBIFS exists in the enum. */
#ifdef UBIFS
#define XX_UBIFS_FILE_TYPE XX_FILE_TYPE_UBIFS
#else
#define XX_UBIFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UBIFS_NODE_MAGIC UINT32_C(0x06101831)

#define XX_UBIFS_CH_SZ 24
#define XX_UBIFS_SB_NODE_SZ 4096
#define XX_UBIFS_MST_NODE_SZ 512
#define XX_UBIFS_IDX_NODE_SZ 28
#define XX_UBIFS_BRANCH_SZ 12
#define XX_UBIFS_SK_LEN 8
#define XX_UBIFS_BRANCH_STRIDE (XX_UBIFS_BRANCH_SZ + XX_UBIFS_SK_LEN)
#define XX_UBIFS_INO_NODE_SZ 160
#define XX_UBIFS_DENT_NODE_SZ 56
#define XX_UBIFS_DATA_NODE_SZ 48
#define XX_UBIFS_BLOCK_SIZE 4096U
#define XX_UBIFS_BLOCK_SHIFT 12
#define XX_UBIFS_MAX_NODE_SZ 4256U
#define XX_UBIFS_ROOT_INO 1U
#define XX_UBIFS_MAX_NLEN 255U

#define XX_UBIFS_SB_LNUM 0U
#define XX_UBIFS_MST_LNUM 1U

/* ch.node_type */
#define XX_UBIFS_INO_NODE 0U
#define XX_UBIFS_DATA_NODE 1U
#define XX_UBIFS_DENT_NODE 2U
#define XX_UBIFS_XENT_NODE 3U
#define XX_UBIFS_SB_NODE 6U
#define XX_UBIFS_MST_NODE 7U
#define XX_UBIFS_IDX_NODE 9U

/* Key types, the top three bits of the key's second word. */
#define XX_UBIFS_INO_KEY 0U
#define XX_UBIFS_DATA_KEY 1U
#define XX_UBIFS_DENT_KEY 2U
#define XX_UBIFS_XENT_KEY 3U
#define XX_UBIFS_KEY_TYPE_SHIFT 29
#define XX_UBIFS_KEY_VALUE_MASK UINT32_C(0x1FFFFFFF)

/* Compressors. */
#define XX_UBIFS_COMPR_NONE 0U
#define XX_UBIFS_COMPR_LZO 1U
#define XX_UBIFS_COMPR_ZLIB 2U
#define XX_UBIFS_COMPR_ZSTD 3U

/* Superblock flags. */
#define XX_UBIFS_FLG_ENCRYPTION UINT32_C(0x10)
#define XX_UBIFS_FLG_AUTHENTICATION UINT32_C(0x20)

/* Directory entry types, as stored in ubifs_dent_node.type. */
#define XX_UBIFS_ITYPE_REG 0U
#define XX_UBIFS_ITYPE_DIR 1U
#define XX_UBIFS_ITYPE_LNK 2U

/* Bounds. Every index pointer, node length and child count below comes
 * straight out of the image, so each one is checked against a cap that a
 * real filesystem stays far beneath. */
#define XX_UBIFS_MIN_LEB_SIZE 1024U
#define XX_UBIFS_MAX_LEB_SIZE (16U * 1024U * 1024U)
#define XX_UBIFS_MAX_LEB_CNT 1048576U
#define XX_UBIFS_MAX_INDEX_NODES 1000000U
#define XX_UBIFS_MAX_LEAVES 2000000U
#define XX_UBIFS_MAX_ENTRIES 200000U
#define XX_UBIFS_MAX_DEPTH 64U
#define XX_UBIFS_MAX_TREE_DEPTH 32U
#define XX_UBIFS_MAX_PATH 4096U
#define XX_UBIFS_MAX_CHILD_CNT 2048U
#define XX_UBIFS_MAX_FILE_SIZE INT64_C(0x4000000000) /* 256 GiB */

/* One level-0 index branch: where a leaf node lives and what it is keyed by.
 * Keeping the key rather than the node means the tree is walked once and
 * every later lookup is a binary search over this array. */
typedef struct xx_ubifs_leaf_s {
    uint32_t inum;  /**< Key word 0. For a dent this is the PARENT inode. */
    uint32_t type;  /**< Key type, 0 ino / 1 data / 2 dent / 3 xent. */
    uint32_t value; /**< Block number for data, name hash for dent. */
    uint32_t lnum;
    uint32_t offs;
    uint32_t len;
} xx_ubifs_leaf;

typedef struct xx_ubifs_entry_s {
    char *name;           /**< Full path from the root, '/' separated. */
    char *link_target;    /**< Symlink target, or NULL. */
    uint32_t inum;
    uint64_t size;
    uint32_t mode;
    uint16_t compr_type;  /**< The inode's default compressor. */
    bool is_folder;
    bool is_regular;
    int64_t header_offset; /**< Device offset of the directory entry node. */
    int64_t header_size;
} xx_ubifs_entry;

/* Open-addressing set of already-visited index positions, keyed by
 * (lnum << 32) | offs. The index is a graph of raw pointers, so a crafted
 * image can point a node at itself or at an ancestor. */
typedef struct xx_ubifs_visited_s {
    uint64_t *slots;
    size_t capacity;
    size_t count;
} xx_ubifs_visited;

typedef struct xx_ubifs_private_s {
    xx_ubifs_leaf *leaves;
    size_t leaf_count;
    size_t leaf_capacity;
    xx_ubifs_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    xx_ubifs_visited visited;
    size_t index_nodes;

    uint32_t leb_size;
    uint32_t leb_cnt;
    uint32_t min_io_size;
    uint32_t fanout;
    uint32_t fmt_version;
    uint32_t sb_flags;
    uint16_t default_compr;
    uint64_t highest_inum;
    uint32_t root_lnum;
    uint32_t root_offs;
    uint32_t root_len;

    int64_t input_size;
    int64_t archive_end;
} xx_ubifs_private;

typedef struct xx_ubifs_archive_stream_s {
    xx_ubifs_private parsed;
    size_t index;
} xx_ubifs_archive_stream;

static void xx_ubifs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers --- */

/* Every read goes through the 64-bit seek: a UBIFS volume carved out of a
 * large UBI image is addressed from its own base, but that base can still sit
 * past 2 GiB in the enclosing device, and xx_io_seek() takes a 32-bit long on
 * Win64. */
static bool xx_ubifs_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_ubifs_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* UBIFS seeds CRC-32 with 0xFFFFFFFF and does not complement the result. */
static uint32_t xx_ubifs_crc(const void *data, size_t size) {
    return xx_crc32_calc(0U, data, size) ^ UINT32_C(0xFFFFFFFF);
}

/* Absolute device offset of a position inside the volume image, where
 * logical erase block N begins at N * leb_size. Returns -1 when the position
 * is outside the geometry the superblock declared. */
static int64_t xx_ubifs_position(Abstractformat *self,
                                 const xx_ubifs_private *parsed, uint32_t lnum,
                                 uint32_t offs, uint32_t length) {
    int64_t offset;
    if (lnum >= parsed->leb_cnt || offs >= parsed->leb_size ||
        length > parsed->leb_size - offs) {
        return -1;
    }
    offset = self->base_address + (int64_t)lnum * parsed->leb_size + offs;
    if (!xx_ubifs_range_within(parsed->input_size, offset, (int64_t)length)) {
        return -1;
    }
    return offset;
}

/* Read one node, validate its common header and its CRC, and report the
 * declared length. @p expected_type is checked unless it is 0xFF. */
static bool xx_ubifs_read_node(Abstractformat *self,
                               const xx_ubifs_private *parsed, uint32_t lnum,
                               uint32_t offs, uint8_t expected_type,
                               uint8_t *buffer, size_t buffer_size,
                               uint32_t *out_len) {
    uint32_t length;
    int64_t offset;
    if (buffer_size < XX_UBIFS_CH_SZ) return false;
    offset = xx_ubifs_position(self, parsed, lnum, offs, XX_UBIFS_CH_SZ);
    if (offset < 0 ||
        !xx_ubifs_read_at(self->device, offset, buffer, XX_UBIFS_CH_SZ)) {
        return false;
    }
    if (xx_data_get_u32(buffer, XX_UBIFS_CH_SZ, 0U, false) !=
        XX_UBIFS_NODE_MAGIC) {
        return false;
    }
    length = xx_data_get_u32(buffer, XX_UBIFS_CH_SZ, 16U, false);
    if (length < XX_UBIFS_CH_SZ || length > buffer_size) return false;
    if (expected_type != 0xFFU &&
        xx_data_get_u8(buffer, XX_UBIFS_CH_SZ, 20U) != expected_type) {
        return false;
    }
    offset = xx_ubifs_position(self, parsed, lnum, offs, length);
    if (offset < 0 ||
        !xx_ubifs_read_at(self->device, offset, buffer, length)) {
        return false;
    }
    /* The header CRC covers the node from the sequence number to its end,
     * skipping the magic and the CRC field itself. */
    if (xx_data_get_u32(buffer, length, 4U, false) !=
        xx_ubifs_crc(buffer + 8, length - 8U)) {
        return false;
    }
    if (out_len) *out_len = length;
    return true;
}

/* ------------------------------------------------------------ visited ----- */

static void xx_ubifs_visited_cleanup(xx_ubifs_visited *visited) {
    if (!visited) return;
    if (visited->slots) xx_mem_free(visited->slots);
    xx_mem_zero(visited, sizeof(*visited));
}

static size_t xx_ubifs_visited_slot(const xx_ubifs_visited *visited,
                                    uint64_t key) {
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (visited->capacity - 1U);
}

static bool xx_ubifs_visited_grow(xx_ubifs_visited *visited) {
    uint64_t *slots;
    size_t capacity = visited->capacity ? visited->capacity * 2U : 256U;
    size_t index;
    xx_ubifs_visited grown;
    if (capacity < visited->capacity || capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (uint64_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    grown.slots = slots;
    grown.capacity = capacity;
    grown.count = visited->count;
    for (index = 0U; index < visited->capacity; ++index) {
        uint64_t stored = visited->slots[index];
        size_t slot;
        if (stored == 0U) continue;
        slot = xx_ubifs_visited_slot(&grown, stored - 1U);
        while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = stored;
    }
    if (visited->slots) xx_mem_free(visited->slots);
    *visited = grown;
    return true;
}

/* Record @p key and report whether it had already been seen. Allocation
 * failure reports "seen", so the walk stops rather than continuing with a set
 * that can no longer remember anything. */
static bool xx_ubifs_visited_mark(xx_ubifs_visited *visited, uint64_t key) {
    size_t slot;
    if (!visited) return true;
    if ((visited->count + 1U) * 4U >= visited->capacity * 3U) {
        if (!xx_ubifs_visited_grow(visited)) return true;
    }
    slot = xx_ubifs_visited_slot(visited, key);
    while (visited->slots[slot] != 0U) {
        if (visited->slots[slot] == key + 1U) return true;
        slot = (slot + 1U) & (visited->capacity - 1U);
    }
    visited->slots[slot] = key + 1U;
    ++visited->count;
    return false;
}

/* ------------------------------------------------------------- cleanup --- */

static void xx_ubifs_private_cleanup(xx_ubifs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->entry_count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
        if (parsed->entries[index].link_target) {
            xx_str_free(parsed->entries[index].link_target);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->leaves) xx_mem_free(parsed->leaves);
    xx_ubifs_visited_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* -------------------------------------------------------- superblock ----- */

static bool xx_ubifs_parse_superblock(Abstractformat *self,
                                      xx_ubifs_private *parsed) {
    uint8_t node[XX_UBIFS_SB_NODE_SZ];
    uint32_t length = 0U;
    uint32_t leb_size;
    uint32_t leb_cnt;
    int64_t span;

    /* The superblock is the one node whose position is fixed and whose
     * geometry is not yet known, so it is read directly rather than through
     * xx_ubifs_read_node(). */
    if (!xx_ubifs_range_within(parsed->input_size, self->base_address,
                               XX_UBIFS_SB_NODE_SZ) ||
        !xx_ubifs_read_at(self->device, self->base_address, node,
                          sizeof(node))) {
        return false;
    }
    if (xx_data_get_u32(node, sizeof(node), 0U, false) != XX_UBIFS_NODE_MAGIC ||
        xx_data_get_u8(node, sizeof(node), 20U) != XX_UBIFS_SB_NODE) {
        return false;
    }
    length = xx_data_get_u32(node, sizeof(node), 16U, false);
    if (length != XX_UBIFS_SB_NODE_SZ) return false;
    if (xx_data_get_u32(node, sizeof(node), 4U, false) !=
        xx_ubifs_crc(node + 8, length - 8U)) {
        return false;
    }
    /* Only the simple key format exists; anything else would change the key
     * width and therefore the branch stride. */
    if (xx_data_get_u8(node, sizeof(node), 27U) != 0U) return false;

    parsed->sb_flags = xx_data_get_u32(node, sizeof(node), 28U, false);
    parsed->min_io_size = xx_data_get_u32(node, sizeof(node), 32U, false);
    leb_size = xx_data_get_u32(node, sizeof(node), 36U, false);
    leb_cnt = xx_data_get_u32(node, sizeof(node), 40U, false);
    parsed->fanout = xx_data_get_u32(node, sizeof(node), 72U, false);
    parsed->fmt_version = xx_data_get_u32(node, sizeof(node), 80U, false);
    parsed->default_compr = xx_data_get_u16(node, sizeof(node), 84U, false);

    /* An authenticated filesystem inserts a hash after every branch key, so
     * the stride this reader assumes would be wrong; an encrypted one has
     * names and contents this reader cannot make sense of. Both are refused
     * rather than misreported. */
    if ((parsed->sb_flags &
         (XX_UBIFS_FLG_ENCRYPTION | XX_UBIFS_FLG_AUTHENTICATION)) != 0U) {
        return false;
    }
    if (leb_size < XX_UBIFS_MIN_LEB_SIZE || leb_size > XX_UBIFS_MAX_LEB_SIZE ||
        (leb_size & 7U) != 0U) {
        return false;
    }
    /* The superblock, both master blocks and the log occupy the first few
     * blocks, so a usable filesystem always has more than that. */
    if (leb_cnt < 6U || leb_cnt > XX_UBIFS_MAX_LEB_CNT) return false;
    if (parsed->fanout < 3U || parsed->fanout > XX_UBIFS_MAX_CHILD_CNT) {
        return false;
    }
    if (parsed->min_io_size < 8U || parsed->min_io_size > leb_size) {
        return false;
    }
    parsed->leb_size = leb_size;
    parsed->leb_cnt = leb_cnt;
    span = (int64_t)leb_cnt * leb_size;
    /* A truncated image is common - a dump often stops at the last used
     * block - so the declared span is clamped to what the device holds
     * instead of being treated as a fatal mismatch. */
    if (span > parsed->input_size - self->base_address) {
        span = parsed->input_size - self->base_address;
        parsed->leb_cnt = (uint32_t)(span / leb_size);
        if (parsed->leb_cnt < 4U) return false;
        span = (int64_t)parsed->leb_cnt * leb_size;
    }
    parsed->archive_end = self->base_address + span;
    return true;
}

/* ------------------------------------------------------- master node ----- */

/* The master node is rewritten on every commit, at increasing min_io_size
 * aligned offsets inside LEB 1, and mirrored into LEB 2. The live copy is the
 * one with the highest commit number, so both blocks are scanned and the best
 * candidate kept. */
static bool xx_ubifs_parse_master(Abstractformat *self,
                                  xx_ubifs_private *parsed) {
    uint8_t node[XX_UBIFS_MST_NODE_SZ];
    uint64_t best_cmt = 0U;
    bool found = false;
    uint32_t lnum;
    uint32_t step = parsed->min_io_size;

    if (step < 8U) step = 8U;
    for (lnum = XX_UBIFS_MST_LNUM; lnum <= XX_UBIFS_MST_LNUM + 1U; ++lnum) {
        uint32_t offs;
        uint32_t steps = 0U;
        if (lnum >= parsed->leb_cnt) break;
        for (offs = 0U;
             offs + XX_UBIFS_MST_NODE_SZ <= parsed->leb_size && steps < 65536U;
             offs += step, ++steps) {
            uint32_t length = 0U;
            uint64_t cmt_no;
            uint32_t root_lnum;
            uint32_t root_offs;
            uint32_t root_len;
            if (!xx_ubifs_read_node(self, parsed, lnum, offs, XX_UBIFS_MST_NODE,
                                    node, sizeof(node), &length) ||
                length != XX_UBIFS_MST_NODE_SZ) {
                continue;
            }
            cmt_no = xx_data_get_u64(node, sizeof(node), 32U, false);
            root_lnum = xx_data_get_u32(node, sizeof(node), 48U, false);
            root_offs = xx_data_get_u32(node, sizeof(node), 52U, false);
            root_len = xx_data_get_u32(node, sizeof(node), 56U, false);
            if (root_lnum >= parsed->leb_cnt || root_offs >= parsed->leb_size ||
                root_len < XX_UBIFS_IDX_NODE_SZ ||
                root_len > parsed->leb_size - root_offs) {
                continue;
            }
            if (found && cmt_no < best_cmt) continue;
            best_cmt = cmt_no;
            found = true;
            parsed->root_lnum = root_lnum;
            parsed->root_offs = root_offs;
            parsed->root_len = root_len;
            parsed->highest_inum =
                xx_data_get_u64(node, sizeof(node), 24U, false);
        }
    }
    return found;
}

/* --------------------------------------------------------- index walk ---- */

static bool xx_ubifs_append_leaf(xx_ubifs_private *parsed,
                                 const xx_ubifs_leaf *leaf) {
    xx_ubifs_leaf *grown;
    size_t capacity;
    if (parsed->leaf_count >= XX_UBIFS_MAX_LEAVES) return false;
    if (parsed->leaf_count == parsed->leaf_capacity) {
        capacity = parsed->leaf_capacity ? parsed->leaf_capacity * 2U : 64U;
        if (capacity > SIZE_MAX / sizeof(*parsed->leaves)) return false;
        grown = (xx_ubifs_leaf *)xx_mem_realloc(
            parsed->leaves, capacity * sizeof(*parsed->leaves));
        if (!grown) return false;
        parsed->leaves = grown;
        parsed->leaf_capacity = capacity;
    }
    parsed->leaves[parsed->leaf_count++] = *leaf;
    return true;
}

/* Depth-first walk of the committed index. Every position is marked before
 * it is entered, so a self-referential or back-pointing branch terminates;
 * the depth cap bounds the recursion independently of that, and the node
 * budget bounds the total work. A malformed subtree ends the walk for that
 * subtree only, leaving whatever was already collected usable. */
static bool xx_ubifs_walk_index(Abstractformat *self, xx_ubifs_private *parsed,
                                uint32_t lnum, uint32_t offs, uint32_t length,
                                unsigned depth, xx_pd_struct *pd) {
    uint8_t *node;
    uint32_t actual = 0U;
    uint32_t child_cnt;
    uint32_t level;
    uint32_t child;
    bool result = true;

    if (depth > XX_UBIFS_MAX_TREE_DEPTH) return true;
    if (parsed->index_nodes >= XX_UBIFS_MAX_INDEX_NODES) return true;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (xx_ubifs_visited_mark(&parsed->visited,
                              ((uint64_t)lnum << 32U) | offs)) {
        return true;
    }
    ++parsed->index_nodes;
    /* The branch array is bounded by the declared node length, which is in
     * turn bounded by the erase block, so the allocation is capped by the
     * geometry rather than by a raw child count. */
    if (length < XX_UBIFS_IDX_NODE_SZ || length > parsed->leb_size) {
        return true;
    }
    node = (uint8_t *)xx_mem_alloc(length);
    if (!node) return false;
    if (!xx_ubifs_read_node(self, parsed, lnum, offs, XX_UBIFS_IDX_NODE, node,
                            length, &actual) ||
        actual != length) {
        xx_mem_free(node);
        return true;
    }
    child_cnt = xx_data_get_u16(node, length, 24U, false);
    level = xx_data_get_u16(node, length, 26U, false);
    /* The child count has to agree exactly with the node length: a branch is
     * 12 bytes plus the 8-byte on-flash key, never the 16-byte padded key
     * that standalone nodes carry. */
    if (child_cnt == 0U || child_cnt > XX_UBIFS_MAX_CHILD_CNT ||
        level > XX_UBIFS_MAX_TREE_DEPTH ||
        (uint64_t)XX_UBIFS_IDX_NODE_SZ +
                (uint64_t)child_cnt * XX_UBIFS_BRANCH_STRIDE >
            (uint64_t)length) {
        xx_mem_free(node);
        return true;
    }
    for (child = 0U; child < child_cnt; ++child) {
        size_t base = (size_t)XX_UBIFS_IDX_NODE_SZ +
                      (size_t)child * XX_UBIFS_BRANCH_STRIDE;
        uint32_t child_lnum = xx_data_get_u32(node, length, base, false);
        uint32_t child_offs = xx_data_get_u32(node, length, base + 4U, false);
        uint32_t child_len = xx_data_get_u32(node, length, base + 8U, false);
        uint32_t key0 = xx_data_get_u32(node, length, base + 12U, false);
        uint32_t key1 = xx_data_get_u32(node, length, base + 16U, false);
        if (level > 0U) {
            if (!xx_ubifs_walk_index(self, parsed, child_lnum, child_offs,
                                     child_len, depth + 1U, pd)) {
                result = false;
                break;
            }
        } else {
            xx_ubifs_leaf leaf;
            leaf.inum = key0;
            leaf.type = key1 >> XX_UBIFS_KEY_TYPE_SHIFT;
            leaf.value = key1 & XX_UBIFS_KEY_VALUE_MASK;
            leaf.lnum = child_lnum;
            leaf.offs = child_offs;
            leaf.len = child_len;
            /* A branch that cannot address a node is dropped here rather than
             * carried into the lookup tables. */
            if (child_len < XX_UBIFS_CH_SZ ||
                child_len > XX_UBIFS_MAX_NODE_SZ ||
                xx_ubifs_position(self, parsed, child_lnum, child_offs,
                                  child_len) < 0) {
                continue;
            }
            if (!xx_ubifs_append_leaf(parsed, &leaf)) break;
        }
    }
    xx_mem_free(node);
    return result;
}

/* ------------------------------------------------------- leaf lookup ----- */

static bool xx_ubifs_leaf_less(const xx_ubifs_leaf *left,
                               const xx_ubifs_leaf *right) {
    if (left->inum != right->inum) return left->inum < right->inum;
    if (left->type != right->type) return left->type < right->type;
    return left->value < right->value;
}

static void xx_ubifs_sift_down(xx_ubifs_leaf *items, size_t start,
                               size_t count) {
    size_t root = start;
    while (root * 2U + 1U < count) {
        size_t child = root * 2U + 1U;
        xx_ubifs_leaf swap;
        if (child + 1U < count &&
            xx_ubifs_leaf_less(&items[child], &items[child + 1U])) {
            ++child;
        }
        if (!xx_ubifs_leaf_less(&items[root], &items[child])) return;
        swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

/* Heapsort again: attacker-sized input, no recursion, no scratch buffer. */
static void xx_ubifs_sort_leaves(xx_ubifs_leaf *items, size_t count) {
    size_t index;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) {
        xx_ubifs_sift_down(items, index, count);
    }
    for (index = count; index-- > 1U;) {
        xx_ubifs_leaf swap = items[0];
        items[0] = items[index];
        items[index] = swap;
        xx_ubifs_sift_down(items, 0U, index);
    }
}

/* First leaf at or after the given key, by binary search over the sorted
 * array. */
static size_t xx_ubifs_lower_bound(const xx_ubifs_private *parsed,
                                   uint32_t inum, uint32_t type,
                                   uint32_t value) {
    size_t low = 0U;
    size_t high = parsed->leaf_count;
    xx_ubifs_leaf probe;
    probe.inum = inum;
    probe.type = type;
    probe.value = value;
    probe.lnum = 0U;
    probe.offs = 0U;
    probe.len = 0U;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (xx_ubifs_leaf_less(&parsed->leaves[middle], &probe)) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

static const xx_ubifs_leaf *xx_ubifs_find_leaf(const xx_ubifs_private *parsed,
                                               uint32_t inum, uint32_t type,
                                               uint32_t value) {
    size_t index = xx_ubifs_lower_bound(parsed, inum, type, value);
    if (index >= parsed->leaf_count) return NULL;
    if (parsed->leaves[index].inum != inum ||
        parsed->leaves[index].type != type ||
        parsed->leaves[index].value != value) {
        return NULL;
    }
    return &parsed->leaves[index];
}

/* ------------------------------------------------------------- entries --- */

static bool xx_ubifs_append_entry(xx_ubifs_private *parsed,
                                  xx_ubifs_entry *entry) {
    xx_ubifs_entry *grown;
    size_t capacity;
    if (!entry || !entry->name || parsed->entry_count >= XX_UBIFS_MAX_ENTRIES) {
        return false;
    }
    if (parsed->entry_count == parsed->entry_capacity) {
        capacity = parsed->entry_capacity ? parsed->entry_capacity * 2U : 32U;
        if (capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_ubifs_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->entry_capacity = capacity;
    }
    parsed->entries[parsed->entry_count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

static char *xx_ubifs_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_UBIFS_MAX_PATH ||
        name_size > XX_UBIFS_MAX_PATH - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_rt_memcpy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_rt_memcpy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* A single path component out of a directory entry node. */
static bool xx_ubifs_plausible_component(const char *name, size_t length) {
    size_t index;
    if (!name || length == 0U || length > XX_UBIFS_MAX_NLEN) return false;
    if (name[0] == '.' &&
        (length == 1U || (length == 2U && name[1] == '.'))) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

/* Load the inode node for @p inum and fill in the parts of an entry that come
 * from it. Missing or malformed inodes leave the entry with zeroed metadata
 * rather than failing the directory: an index that lost an inode leaf still
 * has a usable name for it. */
static void xx_ubifs_apply_inode(Abstractformat *self,
                                 const xx_ubifs_private *parsed,
                                 xx_ubifs_entry *entry) {
    uint8_t node[XX_UBIFS_MAX_NODE_SZ];
    const xx_ubifs_leaf *leaf =
        xx_ubifs_find_leaf(parsed, entry->inum, XX_UBIFS_INO_KEY, 0U);
    uint32_t length = 0U;
    uint32_t data_len;
    if (!leaf) return;
    if (!xx_ubifs_read_node(self, parsed, leaf->lnum, leaf->offs,
                            XX_UBIFS_INO_NODE, node, sizeof(node), &length) ||
        length < XX_UBIFS_INO_NODE_SZ) {
        return;
    }
    entry->size = xx_data_get_u64(node, length, 48U, false);
    entry->mode = xx_data_get_u32(node, length, 104U, false);
    entry->compr_type = xx_data_get_u16(node, length, 132U, false);
    data_len = xx_data_get_u32(node, length, 112U, false);
    if (entry->size > (uint64_t)XX_UBIFS_MAX_FILE_SIZE) {
        entry->size = (uint64_t)XX_UBIFS_MAX_FILE_SIZE;
    }
    /* A symlink keeps its target inline in the inode, uncompressed. */
    if (!entry->is_folder && !entry->is_regular && data_len != 0U &&
        data_len <= length - XX_UBIFS_INO_NODE_SZ &&
        data_len < XX_UBIFS_MAX_PATH) {
        char *target = (char *)xx_mem_alloc(data_len + 1U);
        if (target) {
            xx_rt_memcpy(target, node + XX_UBIFS_INO_NODE_SZ, data_len);
            target[data_len] = '\0';
            if (entry->link_target) xx_str_free(entry->link_target);
            entry->link_target = target;
        }
    }
}

/* Walk one directory. Directory entries are keyed by their parent inode, so
 * every child is a contiguous run in the sorted leaf array. Recursion is
 * bounded by the depth cap, and the visited set keeps a directory that names
 * itself - directly or through a loop of hard-linked directories - from
 * recurring. */
static bool xx_ubifs_walk_directory(Abstractformat *self,
                                    xx_ubifs_private *parsed, uint32_t inum,
                                    const char *prefix, unsigned depth,
                                    xx_pd_struct *pd) {
    size_t index = xx_ubifs_lower_bound(parsed, inum, XX_UBIFS_DENT_KEY, 0U);
    if (depth > XX_UBIFS_MAX_DEPTH) return true;
    /* The inode-number space is separate from the index-position space used
     * for the tree walk, so it is tagged to keep the two from colliding. */
    if (xx_ubifs_visited_mark(&parsed->visited,
                              UINT64_C(0x8000000000000000) | inum)) {
        return true;
    }
    for (; index < parsed->leaf_count; ++index) {
        const xx_ubifs_leaf *leaf = &parsed->leaves[index];
        uint8_t node[XX_UBIFS_MAX_NODE_SZ];
        uint32_t length = 0U;
        uint32_t name_len;
        uint64_t target;
        uint8_t type;
        char component[XX_UBIFS_MAX_NLEN + 1U];
        char *full_name;
        xx_ubifs_entry entry;

        if (leaf->inum != inum || leaf->type != XX_UBIFS_DENT_KEY) break;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (parsed->entry_count >= XX_UBIFS_MAX_ENTRIES) return true;
        if (!xx_ubifs_read_node(self, parsed, leaf->lnum, leaf->offs,
                                XX_UBIFS_DENT_NODE, node, sizeof(node),
                                &length) ||
            length < XX_UBIFS_DENT_NODE_SZ) {
            continue;
        }
        target = xx_data_get_u64(node, length, 40U, false);
        type = xx_data_get_u8(node, length, 49U);
        name_len = xx_data_get_u16(node, length, 50U, false);
        /* The name is NUL terminated on disk, so the node must hold one more
         * byte than the declared length. */
        if (name_len == 0U || name_len > XX_UBIFS_MAX_NLEN ||
            (uint64_t)XX_UBIFS_DENT_NODE_SZ + name_len + 1U > (uint64_t)length) {
            continue;
        }
        if (target == 0U || target > UINT32_MAX) continue;
        xx_rt_memcpy(component, node + XX_UBIFS_DENT_NODE_SZ, name_len);
        component[name_len] = '\0';
        if (!xx_ubifs_plausible_component(component, name_len)) continue;
        full_name = xx_ubifs_join_name(prefix, component);
        if (!full_name) continue;

        xx_mem_zero(&entry, sizeof(entry));
        entry.name = full_name;
        entry.inum = (uint32_t)target;
        entry.is_folder = (type == XX_UBIFS_ITYPE_DIR);
        entry.is_regular = (type == XX_UBIFS_ITYPE_REG);
        entry.header_offset =
            xx_ubifs_position(self, parsed, leaf->lnum, leaf->offs, length);
        entry.header_size = XX_UBIFS_DENT_NODE_SZ + name_len + 1;
        xx_ubifs_apply_inode(self, parsed, &entry);
        if (!entry.is_regular) entry.size = entry.is_folder ? 0U : entry.size;
        if (!xx_ubifs_append_entry(parsed, &entry)) {
            xx_str_free(full_name);
            if (entry.link_target) xx_str_free(entry.link_target);
            return true;
        }
        if (type == XX_UBIFS_ITYPE_DIR) {
            /* The entry now owns the name; the recursion only borrows it. */
            const char *child_prefix =
                parsed->entries[parsed->entry_count - 1U].name;
            if (!xx_ubifs_walk_directory(self, parsed, (uint32_t)target,
                                         child_prefix, depth + 1U, pd)) {
                return false;
            }
        }
    }
    return true;
}

/* --------------------------------------------------------------- parse --- */

static bool xx_ubifs_parse(Abstractformat *self, xx_ubifs_private *parsed,
                           xx_pd_struct *pd) {
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_ubifs_parse_superblock(self, parsed) ||
        !xx_ubifs_parse_master(self, parsed)) {
        goto fail;
    }
    if (!xx_ubifs_walk_index(self, parsed, parsed->root_lnum,
                             parsed->root_offs, parsed->root_len, 0U, pd)) {
        goto fail;
    }
    xx_ubifs_sort_leaves(parsed->leaves, parsed->leaf_count);
    /* An index with no leaves at all is not a filesystem this reader can say
     * anything useful about. */
    if (parsed->leaf_count == 0U) goto fail;
    if (!xx_ubifs_walk_directory(self, parsed, XX_UBIFS_ROOT_INO, "", 0U, pd)) {
        goto fail;
    }
    return true;
fail:
    xx_ubifs_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------- extraction --- */

/* UBIFS stores a compressed block as the bare output of the compressor, with
 * no container of its own; the uncompressed length is the data node's size
 * field, so every decoder here is asked for an exact number of bytes. */
static bool xx_ubifs_decompress(uint16_t compr_type, const uint8_t *input,
                                size_t input_size, uint8_t *output,
                                size_t output_size, size_t *written) {
    size_t produced = 0U;
    if (!input || !output || !written) return false;
    *written = 0U;
    switch (compr_type) {
        case XX_UBIFS_COMPR_NONE:
            if (input_size != output_size) return false;
            if (output_size != 0U) xx_rt_memcpy(output, input, output_size);
            *written = output_size;
            return true;
        case XX_UBIFS_COMPR_LZO:
            if (!xx_lzo1x_decompress(input, input_size, output, output_size,
                                     &produced)) {
                return false;
            }
            break;
        case XX_UBIFS_COMPR_ZLIB:
            /* The kernel's "deflate" crypto compressor produces a RAW deflate
             * stream - zlib_deflateInit2 is called with negative window bits -
             * so there is no zlib header to consume. The wrapped form is
             * tried as a fallback only because it costs nothing and a
             * third-party image builder could plausibly emit it. */
            if (!xx_deflate_decompress_memory(input, input_size, output,
                                              output_size, &produced, false)) {
                produced = 0U;
                if (!xx_zlib_stream_header_is_valid(input, input_size) ||
                    !xx_zlib_stream_decode_memory(input, input_size, output,
                                                  output_size, &produced)) {
                    return false;
                }
            }
            break;
        case XX_UBIFS_COMPR_ZSTD:
            if (!xx_zstd_decompress_memory(input, input_size, output,
                                           output_size, &produced)) {
                return false;
            }
            break;
        default:
            return false;
    }
    if (produced != output_size) return false;
    *written = produced;
    return true;
}

/* How many bytes of an entry this reader is willing to produce. The inode's
 * size field is attacker-controlled and a sparse file costs nothing to
 * declare, so it is clamped to the span of the image: a file larger than the
 * filesystem that holds it is refused rather than turned into an unbounded
 * run of zeros. This does mean a legitimately sparse file bigger than its own
 * volume is not fully extracted. */
static uint64_t xx_ubifs_extract_limit(Abstractformat *self,
                                       const xx_ubifs_private *parsed) {
    if (parsed->archive_end <= self->base_address) return 0U;
    return (uint64_t)(parsed->archive_end - self->base_address);
}

/* Write one regular file out block by block. A block the index does not
 * mention is a hole and expands to zeros, which is what reading the file on a
 * mounted filesystem would return. */
static bool xx_ubifs_extract_entry(Abstractformat *self,
                                   const xx_ubifs_private *parsed,
                                   const xx_ubifs_entry *entry,
                                   xx_io_device *destination,
                                   xx_pd_struct *pd) {
    uint8_t node[XX_UBIFS_MAX_NODE_SZ];
    uint8_t plain[XX_UBIFS_BLOCK_SIZE];
    uint64_t remaining;
    uint32_t block = 0U;
    if (!self || !parsed || !entry || !destination) return false;
    remaining = entry->size;
    if (remaining > xx_ubifs_extract_limit(self, parsed)) return false;
    while (remaining != 0U) {
        const xx_ubifs_leaf *leaf;
        uint32_t length = 0U;
        uint32_t plain_size;
        uint16_t compr_type;
        size_t produced = 0U;
        size_t emit = remaining < XX_UBIFS_BLOCK_SIZE
                          ? (size_t)remaining
                          : (size_t)XX_UBIFS_BLOCK_SIZE;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (block > XX_UBIFS_KEY_VALUE_MASK) return false;
        leaf = xx_ubifs_find_leaf(parsed, entry->inum, XX_UBIFS_DATA_KEY,
                                  block);
        if (!leaf) {
            /* Sparse block. */
            xx_rt_memset(plain, 0, sizeof(plain));
            if (xx_io_write(destination, plain, emit) != (ssize_t)emit) {
                return false;
            }
            remaining -= emit;
            ++block;
            continue;
        }
        if (!xx_ubifs_read_node(self, parsed, leaf->lnum, leaf->offs,
                                XX_UBIFS_DATA_NODE, node, sizeof(node),
                                &length) ||
            length < XX_UBIFS_DATA_NODE_SZ) {
            return false;
        }
        plain_size = xx_data_get_u32(node, length, 40U, false);
        compr_type = xx_data_get_u16(node, length, 44U, false);
        if (plain_size == 0U || plain_size > XX_UBIFS_BLOCK_SIZE) return false;
        if (!xx_ubifs_decompress(compr_type, node + XX_UBIFS_DATA_NODE_SZ,
                                 length - XX_UBIFS_DATA_NODE_SZ, plain,
                                 plain_size, &produced)) {
            return false;
        }
        /* The inode's size decides how much of the last block is real; a
         * block that decodes shorter than the file still owes those bytes, so
         * the shortfall is zero filled rather than silently dropped. */
        if (emit > produced) {
            xx_rt_memset(plain + produced, 0, emit - produced);
        }
        if (xx_io_write(destination, plain, emit) != (ssize_t)emit) {
            return false;
        }
        remaining -= emit;
        ++block;
    }
    return true;
}

/* --------------------------------------------------------------- records - */

static bool xx_ubifs_copy_options(xx_list_s *destination,
                                  const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *xx_ubifs_find_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_ubifs_populate_record(xx_archive_record *record,
                                     const xx_ubifs_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    /* A file's bytes are scattered across data nodes, so there is no single
     * payload range to point at. */
    record->data_offset = -1;
    record->compressed_size = (int64_t)entry->size;
    if (!xx_archive_record_set_original_name(record, entry->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        entry->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        entry->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        entry->compr_type) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        entry->mode) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         entry->is_folder)) {
        return false;
    }
    if (entry->link_target &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                        entry->link_target)) {
        return false;
    }
    return true;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though UBIFS may legally carry it. */
static bool xx_ubifs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static void xx_ubifs_archive_stream_free(void *pointer) {
    xx_ubifs_archive_stream *stream = (xx_ubifs_archive_stream *)pointer;
    if (!stream) return;
    xx_ubifs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ----------------------------------------------------------- lifecycle --- */

void xx_ubifs_init(xx_ubifs *ubifs, xx_io_device *dev, int64_t base_address) {
    if (!ubifs) return;
    xx_mem_zero(ubifs, sizeof(*ubifs));
    xx_format_init(&ubifs->format, dev, base_address);
    ubifs->format.endian = XX_ENDIAN_LITTLE;
    ubifs->format.file_type = XX_UBIFS_FILE_TYPE;
    ubifs->format.format_type = XX_TYPE_ARCHIVE;
    ubifs->format.is_archive = true;
    xx_format_set_mime_type(&ubifs->format, "application/x-ubifs");
    xx_format_set_extension(&ubifs->format, "ubifs");
    ubifs->format.check_is_valid = xx_ubifs_check_is_valid;
    ubifs->format.handle_base_info = xx_ubifs_handle_base_info;
    ubifs->format.get_format_size = xx_ubifs_get_format_size;
    ubifs->format.get_number_of_archive_records =
        xx_ubifs_get_number_of_archive_records;
    ubifs->format.create_archive_records_reading =
        xx_ubifs_create_archive_records_reading;
    ubifs->format.get_current_archive_record =
        xx_ubifs_get_current_archive_record;
    ubifs->format.unpack_current_archive_record =
        xx_ubifs_unpack_current_archive_record;
    ubifs->format.archive_record_move_to_next =
        xx_ubifs_archive_record_move_to_next;
    ubifs->format.free_archive_records_reading =
        xx_ubifs_free_archive_records_reading;
    ubifs->format.destroy = xx_ubifs_vtable_destroy;
    ubifs->archive_end = -1;
}

xx_ubifs *xx_ubifs_create(xx_io_device *dev, int64_t base_address) {
    xx_ubifs *ubifs = (xx_ubifs *)xx_mem_alloc(sizeof(*ubifs));
    if (ubifs) xx_ubifs_init(ubifs, dev, base_address);
    return ubifs;
}

void xx_ubifs_destroy(xx_ubifs *ubifs) {
    if (!ubifs) return;
    if (ubifs->internal) {
        xx_ubifs_private_cleanup((xx_ubifs_private *)ubifs->internal);
        xx_mem_free(ubifs->internal);
        ubifs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ubifs->format);
}

static void xx_ubifs_vtable_destroy(Abstractformat *self) {
    xx_ubifs_destroy((xx_ubifs *)self);
}

void xx_ubifs_free(xx_ubifs *ubifs) {
    if (!ubifs) return;
    xx_ubifs_destroy(ubifs);
    xx_mem_free(ubifs);
}

bool xx_ubifs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ubifs_private parsed;
    bool result = xx_ubifs_parse(self, &parsed, pd);
    xx_ubifs_private_cleanup(&parsed);
    return result;
}

bool xx_ubifs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ubifs_private *parsed;
    xx_ubifs *ubifs = (xx_ubifs *)self;
    int64_t total_size;
    if (!self || !ubifs) return false;
    parsed = (xx_ubifs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_ubifs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ubifs->internal) {
        xx_ubifs_private_cleanup((xx_ubifs_private *)ubifs->internal);
        xx_mem_free(ubifs->internal);
    }
    ubifs->internal = parsed;
    ubifs->number_of_records = parsed->entry_count;
    ubifs->number_of_members = parsed->entry_count;
    ubifs->leb_size = parsed->leb_size;
    ubifs->leb_cnt = parsed->leb_cnt;
    ubifs->min_io_size = parsed->min_io_size;
    ubifs->fanout = parsed->fanout;
    ubifs->fmt_version = parsed->fmt_version;
    ubifs->sb_flags = parsed->sb_flags;
    ubifs->default_compr = parsed->default_compr;
    ubifs->highest_inum = parsed->highest_inum;
    ubifs->leaf_count = parsed->leaf_count;
    ubifs->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->entry_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ubifs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ubifs_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ubifs *)self)->number_of_records;
}

xx_archive_record_state *xx_ubifs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ubifs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ubifs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_ubifs_copy_options(&state->options, options) ||
        !xx_ubifs_parse(self, &stream->parsed, pd)) {
        xx_ubifs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ubifs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.entry_count;
    if (stream->parsed.entry_count != 0U &&
        xx_ubifs_populate_record(&state->current_record,
                                 &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ubifs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ubifs_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ubifs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ubifs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.entry_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ubifs_populate_record(&state->current_record,
                                  &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_ubifs_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_ubifs_archive_stream *stream;
    const xx_ubifs_entry *entry;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ubifs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.entry_count) return false;
    entry = &stream->parsed.entries[stream->index];
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!xx_ubifs_safe_name(name)) return false;

    option = xx_ubifs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether every data block this entry needs is
         * present and addressable, without writing anything. */
        uint64_t remaining = entry->size;
        uint32_t block = 0U;
        if (!entry->is_regular) return true;
        if (remaining > xx_ubifs_extract_limit(self, &stream->parsed)) {
            return false;
        }
        while (remaining != 0U && block <= XX_UBIFS_KEY_VALUE_MASK) {
            const xx_ubifs_leaf *leaf = xx_ubifs_find_leaf(
                &stream->parsed, entry->inum, XX_UBIFS_DATA_KEY, block);
            if (leaf && xx_ubifs_position(self, &stream->parsed, leaf->lnum,
                                          leaf->offs, leaf->len) < 0) {
                return false;
            }
            remaining -= remaining < XX_UBIFS_BLOCK_SIZE
                             ? remaining
                             : XX_UBIFS_BLOCK_SIZE;
            ++block;
        }
        return true;
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
        destination_path = xx_str_concat3(base, "/", name);
    } else {
        destination_path = xx_str_concat(base, name);
    }
    if (!destination_path) goto cleanup;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination_path, true);
        goto cleanup;
    }
    /* Symlinks, devices, sockets and fifos have no byte stream to write; the
     * link target travels with the record as metadata instead. */
    if (!entry->is_regular) {
        result = true;
        goto cleanup;
    }
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    created = destination != NULL;
    if (!destination) goto cleanup;
    result =
        xx_ubifs_extract_entry(self, &stream->parsed, entry, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_ubifs_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* --------------------------------------------------------- accessors --- */

uint64_t xx_ubifs_get_number_of_records(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->number_of_records : 0U;
}
uint64_t xx_ubifs_get_number_of_members(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->number_of_members : 0U;
}
uint32_t xx_ubifs_get_leb_size(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->leb_size : 0U;
}
uint32_t xx_ubifs_get_leb_count(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->leb_cnt : 0U;
}
uint32_t xx_ubifs_get_format_version(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->fmt_version : 0U;
}
uint16_t xx_ubifs_get_default_compression(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->default_compr : 0U;
}
int64_t xx_ubifs_get_archive_end(const xx_ubifs *ubifs) {
    return ubifs ? ubifs->archive_end : -1;
}

const char *xx_ubifs_compression_to_string(uint32_t compr_type) {
    switch (compr_type) {
        case XX_UBIFS_COMPR_NONE: return "None";
        case XX_UBIFS_COMPR_LZO: return "LZO";
        case XX_UBIFS_COMPR_ZLIB: return "zlib";
        case XX_UBIFS_COMPR_ZSTD: return "zstd";
        default: return "Unknown";
    }
}

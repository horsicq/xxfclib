/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/*
 * EXT IS NOT ONE FILESYSTEM.  ext2, ext3 and ext4 share a superblock and an
 * inode table and differ only in the feature words, so one reader covers all
 * three - but two of the structures inside have *two* incompatible layouts
 * and both are live in the wild:
 *
 *   i_block, 60 bytes at +0x28 of every inode.  Without EXT4_EXTENTS_FL
 *   (0x00080000) it is the ext2 block map - 12 direct u32 block numbers then
 *   a single, a double and a triple indirect block.  With the flag it is the
 *   inline root of an extent tree whose nodes start with magic 0xF30A.  An
 *   ext4 image written by mke2fs uses extents for its files and the block map
 *   for nothing, but an ext2 or ext3 image, or an ext4 image built with
 *   -O ^extents, uses the block map throughout.  Both paths are implemented.
 *
 *   The directory entry's name length.  With INCOMPAT_FILETYPE it is a u8 at
 *   +6 followed by a u8 file type; without it the same two bytes are one
 *   little-endian u16 name length and there is no type byte.  The type byte
 *   is never trusted here anyway: the child's inode is read regardless,
 *   because its size and flags are needed, so i_mode decides the type.
 *
 * A hashed directory (EXT4_INDEX_FL) needs no special case.  Its interior
 * nodes are stored in records whose inode field is zero, and a zero inode is
 * an unused slot that a linear read must skip - so reading every data block
 * of the directory linearly and skipping zero-inode records visits exactly
 * the directory's real entries, once each.
 *
 * DETECTION.  The magic lives at +0x38 of a superblock that itself starts at
 * byte 1024, i.e. 1080 bytes into the volume.  That is far past any magic
 * prefilter, so check_is_valid() reads the superblock from the device.
 *
 * NOT IMPLEMENTED, deliberately: journal replay (the image is read as it lies
 * on disk, which is what every offline reader does), extended attributes,
 * INCOMPAT_INLINE_DATA, encryption, and INCOMPAT_META_BG's relocated
 * descriptor tables.  Images that require those are rejected at parse rather
 * than read approximately.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ext/xx_ext.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_EXT_SUPERBLOCK_OFFSET 1024
#define XX_EXT_SUPERBLOCK_SIZE 1024
#define XX_EXT_INODE_CORE_SIZE 128U
#define XX_EXT_ROOT_INODE 2U
#define XX_EXT_MIN_BLOCK_SIZE 1024U
#define XX_EXT_MAX_BLOCK_SIZE 65536U
#define XX_EXT_MIN_DESC_SIZE 32U

#define XX_EXT_MAX_ENTRIES 200000U
#define XX_EXT_MAX_DEPTH 64U
#define XX_EXT_MAX_EXTENT_DEPTH 5U
#define XX_EXT_MAX_RUNS 65536U
#define XX_EXT_MAX_NAME 255U
#define XX_EXT_MAX_PATH 4096U
#define XX_EXT_MAX_GROUPS 1048576U

/* i_mode's type field. */
#define XX_EXT_S_IFMT 0xF000U
#define XX_EXT_S_IFREG 0x8000U
#define XX_EXT_S_IFDIR 0x4000U
#define XX_EXT_S_IFLNK 0xA000U

/* i_flags bits that change how i_block is read. */
#define XX_EXT_FL_EXTENTS 0x00080000U
#define XX_EXT_FL_INLINE_DATA 0x10000000U
#define XX_EXT_FL_ENCRYPT 0x00000800U

/* An extent whose length exceeds this is uninitialised: the blocks are
 * allocated but read as zeros, and the real length is len - 32768. */
#define XX_EXT_EXTENT_INIT_MAX 32768U

/* ------------------------------------------------------------ structures - */

/** The superblock fields this reader uses, already decoded and validated. */
typedef struct xx_ext_super_s {
    uint32_t inode_count;
    uint64_t block_count;
    uint32_t first_data_block;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t group_count;
    uint32_t rev_level;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint32_t pointers_per_block; /**< block_size / 4, the indirect fan-out. */
} xx_ext_super;

/** One member of the listing. The block map is not kept: extraction re-reads
 * the inode, which keeps the parse's memory proportional to the file count
 * rather than to the number of blocks the image describes. */
typedef struct xx_ext_entry_s {
    char *name;
    char *link_target; /**< Owned, symlinks only. */
    uint32_t inode;
    uint64_t size;
    int64_t data_offset; /**< Byte offset of the first mapped block, or -1. */
    bool is_folder;
    bool is_link;
} xx_ext_entry;

/** One contiguous stretch of a file. physical == 0 marks a hole, which reads
 * as zeros and occupies nothing on disk. */
typedef struct xx_ext_run_s {
    uint64_t logical;
    uint64_t physical;
    uint64_t count;
} xx_ext_run;

typedef struct xx_ext_runs_s {
    xx_ext_run *items;
    size_t count;
    size_t capacity;
} xx_ext_runs;

/* Open-addressing set of inode numbers already descended into. Directory
 * entries name inodes freely, so a hostile - or merely corrupt - image can
 * describe a directory that contains itself. Slot value 0 means empty, so
 * inode + 1 is stored. */
typedef struct xx_ext_visited_s {
    uint32_t *slots;
    size_t capacity;
    size_t count;
} xx_ext_visited;

typedef struct xx_ext_private_s {
    xx_io_device *device; /**< Borrowed from the format. */
    int64_t base_address;
    int64_t input_size;
    int64_t archive_end;
    xx_ext_super super;
    xx_ext_entry *entries;
    size_t count;
    size_t capacity;
    xx_ext_visited visited;
    uint8_t *block; /**< One block of scratch, used only by extraction. */
} xx_ext_private;

typedef struct xx_ext_archive_stream_s {
    xx_ext_private parsed;
    size_t index;
} xx_ext_archive_stream;

static void xx_ext_vtable_destroy(Abstractformat *self);
static bool xx_ext_walk_directory(xx_ext_private *parsed, uint32_t inode,
                                  const char *prefix, unsigned depth,
                                  xx_pd_struct *pd);

/* ------------------------------------------------------------- plumbing - */

/* xx_io_seek() takes a long, which is 32 bit on 64-bit Windows and would cap
 * this reader at 2 GiB; ext images routinely exceed that, so every read goes
 * through the 64-bit seek. */
static bool xx_ext_read_at(xx_io_device *device, int64_t offset, void *data,
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

/** True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_ext_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/** Byte offset of a block within the device, or -1 if it is out of range. */
static int64_t xx_ext_block_offset(const xx_ext_private *parsed,
                                   uint64_t block) {
    int64_t offset;
    if (!parsed || block == 0U || block >= parsed->super.block_count) return -1;
    if (block > (uint64_t)INT64_MAX / parsed->super.block_size) return -1;
    offset = (int64_t)(block * parsed->super.block_size);
    if (offset > INT64_MAX - parsed->base_address) return -1;
    offset += parsed->base_address;
    if (!xx_ext_range_within(parsed->input_size, offset,
                             (int64_t)parsed->super.block_size)) {
        return -1;
    }
    return offset;
}

static bool xx_ext_read_block(const xx_ext_private *parsed, uint64_t block,
                              uint8_t *buffer) {
    int64_t offset = xx_ext_block_offset(parsed, block);
    if (offset < 0 || !buffer) return false;
    return xx_ext_read_at(parsed->device, offset, buffer,
                          parsed->super.block_size);
}

/* -------------------------------------------------------- visited inodes - */

static void xx_ext_visited_cleanup(xx_ext_visited *visited) {
    if (!visited) return;
    if (visited->slots) xx_mem_free(visited->slots);
    xx_mem_zero(visited, sizeof(*visited));
}

static size_t xx_ext_visited_slot(const xx_ext_visited *visited,
                                  uint32_t inode) {
    uint64_t key = (uint64_t)inode;
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (visited->capacity - 1U);
}

static bool xx_ext_visited_grow(xx_ext_visited *visited) {
    uint32_t *slots;
    size_t capacity = visited->capacity ? visited->capacity * 2U : 256U;
    size_t index;
    xx_ext_visited grown;
    if (capacity < visited->capacity || capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    grown.slots = slots;
    grown.capacity = capacity;
    grown.count = visited->count;
    for (index = 0U; index < visited->capacity; ++index) {
        uint32_t stored = visited->slots[index];
        size_t slot;
        if (stored == 0U) continue;
        slot = xx_ext_visited_slot(&grown, stored - 1U);
        while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = stored;
    }
    if (visited->slots) xx_mem_free(visited->slots);
    *visited = grown;
    return true;
}

/* Record inode and report whether it had already been seen. Allocation
 * failure is reported as "seen" so the walk stops rather than descending with
 * a set that can no longer remember anything. */
static bool xx_ext_visited_mark(xx_ext_visited *visited, uint32_t inode) {
    size_t slot;
    if (!visited || inode == 0U || inode == UINT32_MAX) return true;
    if ((visited->count + 1U) * 4U >= visited->capacity * 3U) {
        if (!xx_ext_visited_grow(visited)) return true;
    }
    slot = xx_ext_visited_slot(visited, inode);
    while (visited->slots[slot] != 0U) {
        if (visited->slots[slot] == inode + 1U) return true;
        slot = (slot + 1U) & (visited->capacity - 1U);
    }
    visited->slots[slot] = inode + 1U;
    ++visited->count;
    return false;
}

/* --------------------------------------------------------------- runs ---- */

static void xx_ext_runs_cleanup(xx_ext_runs *runs) {
    if (!runs) return;
    if (runs->items) xx_mem_free(runs->items);
    xx_mem_zero(runs, sizeof(*runs));
}

/* Append one stretch, merging it into the previous one when the two are
 * adjacent both logically and physically - a plain file laid out by mke2fs
 * collapses to a single run, so the cap below is never approached by a
 * well-formed image. */
static bool xx_ext_runs_append(xx_ext_runs *runs, uint64_t logical,
                               uint64_t physical, uint64_t count) {
    xx_ext_run *grown;
    size_t capacity;
    if (!runs || count == 0U) return runs != NULL;
    if (logical > UINT64_MAX - count) return false;
    if (physical != 0U && physical > UINT64_MAX - count) return false;
    if (runs->count != 0U) {
        xx_ext_run *last = &runs->items[runs->count - 1U];
        if (last->logical + last->count == logical &&
            ((last->physical == 0U && physical == 0U) ||
             (last->physical != 0U && physical != 0U &&
              last->physical + last->count == physical))) {
            last->count += count;
            return true;
        }
    }
    if (runs->count >= XX_EXT_MAX_RUNS) return false;
    if (runs->count == runs->capacity) {
        capacity = runs->capacity ? runs->capacity * 2U : 16U;
        if (capacity > XX_EXT_MAX_RUNS) capacity = XX_EXT_MAX_RUNS;
        if (capacity <= runs->count ||
            capacity > SIZE_MAX / sizeof(*runs->items)) {
            return false;
        }
        grown = (xx_ext_run *)xx_mem_realloc(runs->items,
                                             capacity * sizeof(*runs->items));
        if (!grown) return false;
        runs->items = grown;
        runs->capacity = capacity;
    }
    runs->items[runs->count].logical = logical;
    runs->items[runs->count].physical = physical;
    runs->items[runs->count].count = count;
    ++runs->count;
    return true;
}

/* --------------------------------------------------------- extent trees -- */

/* Walk one extent tree node. `node` is either the 60-byte inline root taken
 * from i_block or a freshly read block; `budget` bounds the recursion
 * independently of the depth the node claims, so a tree that lies about its
 * depth still terminates. */
static bool xx_ext_collect_extents(const xx_ext_private *parsed,
                                   const uint8_t *node, size_t node_size,
                                   unsigned budget, uint64_t block_total,
                                   xx_ext_runs *runs, xx_pd_struct *pd) {
    uint16_t magic;
    uint16_t entries;
    uint16_t depth;
    uint16_t index;
    size_t room;

    if (!parsed || !node || node_size < 12U || !runs) return false;
    if (budget == 0U) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    magic = xx_data_get_u16(node, node_size, 0U, false);
    entries = xx_data_get_u16(node, node_size, 2U, false);
    depth = xx_data_get_u16(node, node_size, 6U, false);
    if (magic != XX_EXT_EXTENT_MAGIC) return false;
    if (depth > XX_EXT_MAX_EXTENT_DEPTH) return false;
    room = (node_size - 12U) / 12U;
    if ((size_t)entries > room) return false;

    for (index = 0U; index < entries; ++index) {
        size_t at = 12U + (size_t)index * 12U;
        uint32_t logical = xx_data_get_u32(node, node_size, at, false);
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (depth == 0U) {
            /* Leaf: a real extent. */
            uint16_t length = xx_data_get_u16(node, node_size, at + 4U, false);
            uint64_t start = xx_data_get_u32(node, node_size, at + 8U, false);
            start |= (uint64_t)xx_data_get_u16(node, node_size, at + 6U, false)
                     << 32U;
            if (length > XX_EXT_EXTENT_INIT_MAX) {
                /* Uninitialised: allocated but defined to read as zeros, so
                 * it is emitted as a hole rather than copied from disk. */
                length = (uint16_t)(length - XX_EXT_EXTENT_INIT_MAX);
                start = 0U;
            }
            if (length == 0U) continue;
            if (logical >= block_total) continue;
            if ((uint64_t)length > block_total - logical) {
                length = (uint16_t)(block_total - logical);
            }
            if (start != 0U &&
                (start >= parsed->super.block_count ||
                 (uint64_t)length > parsed->super.block_count - start)) {
                return false;
            }
            if (!xx_ext_runs_append(runs, logical, start, length)) return false;
        } else {
            /* Interior: an index pointing at the next level down. */
            uint8_t *child;
            uint64_t leaf = xx_data_get_u32(node, node_size, at + 4U, false);
            bool ok;
            leaf |= (uint64_t)xx_data_get_u16(node, node_size, at + 8U, false)
                    << 32U;
            (void)logical;
            child = (uint8_t *)xx_mem_alloc(parsed->super.block_size);
            if (!child) return false;
            if (!xx_ext_read_block(parsed, leaf, child)) {
                xx_mem_free(child);
                return false;
            }
            /* The child must claim exactly one level less than its parent;
             * anything else is a tree that could be made to cycle. */
            if (xx_data_get_u16(child, parsed->super.block_size, 6U, false) !=
                (uint16_t)(depth - 1U)) {
                xx_mem_free(child);
                return false;
            }
            ok = xx_ext_collect_extents(parsed, child, parsed->super.block_size,
                                        budget - 1U, block_total, runs, pd);
            xx_mem_free(child);
            if (!ok) return false;
        }
    }
    return true;
}

/* ------------------------------------------------------- indirect maps --- */

/* Enumerate one indirect block at `level` (1 single, 2 double, 3 triple),
 * appending every logical block it covers until *logical reaches total. A
 * zero block number is a hole covering the whole subtree, which is skipped by
 * advancing the logical cursor - extraction fills gaps with zeros. */
static bool xx_ext_collect_indirect(const xx_ext_private *parsed,
                                    uint64_t block, unsigned level,
                                    uint64_t *logical, uint64_t total,
                                    xx_ext_runs *runs, xx_pd_struct *pd) {
    uint8_t *buffer;
    uint32_t ppb = parsed->super.pointers_per_block;
    uint32_t index;
    bool ok = true;

    if (!parsed || !logical || !runs || level == 0U || level > 3U) return false;
    if (*logical >= total) return true;
    if (pd && xx_pd_is_stopped(pd)) return false;

    if (block == 0U) {
        /* Skip the subtree this pointer would have covered. */
        uint64_t span = 1U;
        unsigned step;
        for (step = 0U; step < level; ++step) {
            if (span > UINT64_MAX / ppb) {
                *logical = total;
                return true;
            }
            span *= ppb;
        }
        if (span > total - *logical) {
            *logical = total;
        } else {
            *logical += span;
        }
        return true;
    }

    buffer = (uint8_t *)xx_mem_alloc(parsed->super.block_size);
    if (!buffer) return false;
    if (!xx_ext_read_block(parsed, block, buffer)) {
        xx_mem_free(buffer);
        return false;
    }
    for (index = 0U; index < ppb && *logical < total; ++index) {
        uint32_t pointer = xx_data_get_u32(buffer, parsed->super.block_size,
                                           (size_t)index * 4U, false);
        if (level == 1U) {
            if (pointer != 0U && pointer >= parsed->super.block_count) {
                ok = false;
                break;
            }
            if (!xx_ext_runs_append(runs, *logical, pointer, 1U)) {
                ok = false;
                break;
            }
            ++(*logical);
        } else if (!xx_ext_collect_indirect(parsed, pointer, level - 1U,
                                            logical, total, runs, pd)) {
            ok = false;
            break;
        }
    }
    xx_mem_free(buffer);
    return ok;
}

/* Build the run list for one inode's first `block_total` logical blocks,
 * taking whichever of the two i_block layouts the flags select. */
static bool xx_ext_collect_runs(const xx_ext_private *parsed,
                                const uint8_t *inode, uint64_t block_total,
                                xx_ext_runs *runs, xx_pd_struct *pd) {
    uint32_t flags;
    const uint8_t *i_block;
    if (!parsed || !inode || !runs) return false;
    if (block_total == 0U) return true;
    flags = xx_data_get_u32(inode, XX_EXT_INODE_CORE_SIZE, 0x20U, false);
    i_block = inode + 0x28;

    if ((flags & XX_EXT_FL_EXTENTS) != 0U) {
        return xx_ext_collect_extents(parsed, i_block, 60U,
                                      XX_EXT_MAX_EXTENT_DEPTH + 1U,
                                      block_total, runs, pd);
    }
    {
        uint64_t logical = 0U;
        uint32_t index;
        for (index = 0U; index < 12U && logical < block_total; ++index) {
            uint32_t pointer = xx_data_get_u32(i_block, 60U,
                                               (size_t)index * 4U, false);
            if (pointer != 0U && pointer >= parsed->super.block_count) {
                return false;
            }
            if (!xx_ext_runs_append(runs, logical, pointer, 1U)) return false;
            ++logical;
        }
        for (index = 0U; index < 3U; ++index) {
            uint32_t pointer =
                xx_data_get_u32(i_block, 60U, (size_t)(12U + index) * 4U, false);
            if (!xx_ext_collect_indirect(parsed, pointer, index + 1U, &logical,
                                         block_total, runs, pd)) {
                return false;
            }
        }
    }
    return true;
}

/* ---------------------------------------------------------- superblock --- */

static bool xx_ext_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool xx_ext_parse_superblock(xx_ext_private *parsed,
                                    const uint8_t *raw) {
    xx_ext_super *super = &parsed->super;
    uint32_t log_block_size;
    uint64_t span;
    uint64_t groups;

    if (xx_data_get_u16(raw, XX_EXT_SUPERBLOCK_SIZE, 0x38U, false) !=
        XX_EXT_MAGIC) {
        return false;
    }
    super->inode_count = xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x00U, false);
    super->block_count = xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x04U, false);
    super->first_data_block =
        xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x14U, false);
    log_block_size = xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x18U, false);
    super->blocks_per_group =
        xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x20U, false);
    super->inodes_per_group =
        xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x28U, false);
    super->rev_level = xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x4CU, false);

    /* Everything from +0x54 exists only in the dynamic revision; revision 0
     * has a fixed 128-byte inode and no feature words at all. */
    if (super->rev_level == 0U) {
        super->inode_size = XX_EXT_INODE_CORE_SIZE;
        super->feature_compat = 0U;
        super->feature_incompat = 0U;
        super->feature_ro_compat = 0U;
    } else if (super->rev_level == 1U) {
        super->inode_size =
            xx_data_get_u16(raw, XX_EXT_SUPERBLOCK_SIZE, 0x58U, false);
        super->feature_compat =
            xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x5CU, false);
        super->feature_incompat =
            xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x60U, false);
        super->feature_ro_compat =
            xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x64U, false);
    } else {
        return false;
    }

    /* 1024 << 6 is the 64 KiB ceiling the kernel itself imposes. */
    if (log_block_size > 6U) return false;
    super->block_size = XX_EXT_MIN_BLOCK_SIZE << log_block_size;
    super->pointers_per_block = super->block_size / 4U;

    /* first_data_block is 1 on a 1 KiB image and 0 on every larger one -
     * the superblock always lies in block 1 of a 1 KiB filesystem and in the
     * tail of block 0 otherwise. */
    if (super->first_data_block !=
        (super->block_size == XX_EXT_MIN_BLOCK_SIZE ? 1U : 0U)) {
        return false;
    }

    if ((super->feature_incompat & XX_EXT_INCOMPAT_64BIT) != 0U) {
        super->block_count |=
            (uint64_t)xx_data_get_u32(raw, XX_EXT_SUPERBLOCK_SIZE, 0x150U,
                                      false)
            << 32U;
        super->desc_size =
            xx_data_get_u16(raw, XX_EXT_SUPERBLOCK_SIZE, 0xFEU, false);
        if (super->desc_size < 64U || super->desc_size > super->block_size ||
            !xx_ext_is_power_of_two(super->desc_size)) {
            return false;
        }
    } else {
        super->desc_size = XX_EXT_MIN_DESC_SIZE;
    }

    if (super->inode_size < XX_EXT_INODE_CORE_SIZE ||
        super->inode_size > super->block_size ||
        !xx_ext_is_power_of_two(super->inode_size)) {
        return false;
    }
    if (super->blocks_per_group == 0U || super->inodes_per_group == 0U ||
        super->inode_count == 0U || super->block_count == 0U) {
        return false;
    }
    /* A group's block bitmap is one block, so a group can hold at most
     * block_size * 8 blocks, and its inode table cannot exceed the group. */
    if (super->blocks_per_group > super->block_size * 8U) return false;
    if (super->inodes_per_group > super->block_size * 8U) return false;

    /* Features that change the on-disk meaning of what is read below. The
     * reader refuses them rather than producing plausible-looking rubbish. */
    if ((super->feature_incompat &
         (XX_EXT_INCOMPAT_META_BG | XX_EXT_INCOMPAT_INLINE_DATA |
          XX_EXT_INCOMPAT_ENCRYPT | XX_EXT_INCOMPAT_JOURNAL_DEV)) != 0U) {
        return false;
    }

    if (super->block_count <= super->first_data_block) return false;
    groups = ((super->block_count - super->first_data_block) +
              super->blocks_per_group - 1U) /
             super->blocks_per_group;
    if (groups == 0U || groups > XX_EXT_MAX_GROUPS) return false;
    super->group_count = (uint32_t)groups;
    if ((uint64_t)super->group_count * super->inodes_per_group <
        super->inode_count) {
        return false;
    }

    /* The whole filesystem must be present: every bound below - file sizes,
     * block numbers, run counts - is ultimately derived from this. */
    if (super->block_count > (uint64_t)INT64_MAX / super->block_size) {
        return false;
    }
    span = super->block_count * super->block_size;
    if (!xx_ext_range_within(parsed->input_size, parsed->base_address,
                             (int64_t)span)) {
        return false;
    }
    parsed->archive_end = parsed->base_address + (int64_t)span;
    return true;
}

/* --------------------------------------------------------------- inodes -- */

/** Physical block of a group's inode table, or 0 when unreadable. */
static uint64_t xx_ext_inode_table(const xx_ext_private *parsed,
                                   uint32_t group) {
    uint8_t descriptor[64];
    uint64_t gdt_block;
    int64_t offset;
    uint64_t table;
    size_t size = parsed->super.desc_size < sizeof(descriptor)
                      ? parsed->super.desc_size
                      : sizeof(descriptor);
    if (group >= parsed->super.group_count) return 0U;
    /* The descriptor table starts in the block following the superblock's. */
    gdt_block = (uint64_t)parsed->super.first_data_block + 1U;
    if (gdt_block >= parsed->super.block_count) return 0U;
    offset = xx_ext_block_offset(parsed, gdt_block);
    if (offset < 0) return 0U;
    if ((uint64_t)group > (uint64_t)(INT64_MAX - offset) /
                              parsed->super.desc_size) {
        return 0U;
    }
    offset += (int64_t)((uint64_t)group * parsed->super.desc_size);
    if (!xx_ext_range_within(parsed->input_size, offset, (int64_t)size) ||
        !xx_ext_read_at(parsed->device, offset, descriptor, size)) {
        return 0U;
    }
    table = xx_data_get_u32(descriptor, size, 0x08U, false);
    if (parsed->super.desc_size >= 64U) {
        table |= (uint64_t)xx_data_get_u32(descriptor, size, 0x28U, false)
                 << 32U;
    }
    return table;
}

/** Read the 128-byte core of one inode by number. */
static bool xx_ext_read_inode(const xx_ext_private *parsed, uint32_t inode,
                              uint8_t *out) {
    uint32_t group;
    uint32_t index;
    uint64_t table;
    int64_t offset;
    if (!parsed || !out || inode == 0U || inode > parsed->super.inode_count) {
        return false;
    }
    group = (inode - 1U) / parsed->super.inodes_per_group;
    index = (inode - 1U) % parsed->super.inodes_per_group;
    table = xx_ext_inode_table(parsed, group);
    offset = xx_ext_block_offset(parsed, table);
    if (offset < 0) return false;
    if ((uint64_t)index > (uint64_t)(INT64_MAX - offset) /
                              parsed->super.inode_size) {
        return false;
    }
    offset += (int64_t)((uint64_t)index * parsed->super.inode_size);
    if (!xx_ext_range_within(parsed->input_size, offset,
                             XX_EXT_INODE_CORE_SIZE)) {
        return false;
    }
    return xx_ext_read_at(parsed->device, offset, out, XX_EXT_INODE_CORE_SIZE);
}

/** The size an inode declares, combining the low and high halves. A directory
 * reuses the high word for something else on old revisions, so it is only
 * read for regular files. */
static uint64_t xx_ext_inode_size(const xx_ext_private *parsed,
                                  const uint8_t *inode, uint16_t mode) {
    uint64_t size = xx_data_get_u32(inode, XX_EXT_INODE_CORE_SIZE, 0x04U, false);
    if ((mode & XX_EXT_S_IFMT) == XX_EXT_S_IFREG &&
        (parsed->super.feature_ro_compat & 0x0002U) != 0U) {
        size |= (uint64_t)xx_data_get_u32(inode, XX_EXT_INODE_CORE_SIZE, 0x6CU,
                                          false)
                << 32U;
    }
    return size;
}

/** Number of logical blocks a file of `size` bytes occupies. */
static uint64_t xx_ext_block_total(const xx_ext_private *parsed,
                                   uint64_t size) {
    return (size + parsed->super.block_size - 1U) / parsed->super.block_size;
}

/* ---------------------------------------------------------------- names -- */

/* Parse-time check. A directory entry name is a single path component, so an
 * embedded separator or a control byte makes it implausible. */
static bool xx_ext_plausible_name(const uint8_t *name, size_t length) {
    size_t index;
    if (!name || length == 0U || length > XX_EXT_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

static bool xx_ext_is_dot_name(const uint8_t *name, size_t length) {
    if (length == 1U && name[0] == '.') return true;
    return length == 2U && name[0] == '.' && name[1] == '.';
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though an ext image may legally carry it. */
static bool xx_ext_safe_name(const char *name) {
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

static char *xx_ext_join_name(const char *prefix, const uint8_t *name,
                              size_t name_size) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    char *combined;
    size_t at = 0U;
    if (name_size == 0U || prefix_size >= XX_EXT_MAX_PATH ||
        name_size > XX_EXT_MAX_PATH - prefix_size - 1U) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size + 2U);
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        at = prefix_size;
        combined[at++] = '/';
    }
    xx_rt_memcpy(combined + at, name, name_size);
    combined[at + name_size] = '\0';
    return combined;
}

/* --------------------------------------------------------------- entries - */

static void xx_ext_entry_cleanup(xx_ext_entry *entry) {
    if (!entry) return;
    if (entry->name) xx_str_free(entry->name);
    if (entry->link_target) xx_str_free(entry->link_target);
    xx_mem_zero(entry, sizeof(*entry));
}

static bool xx_ext_append_entry(xx_ext_private *parsed, xx_ext_entry *entry) {
    xx_ext_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_EXT_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_ext_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

static void xx_ext_private_cleanup(xx_ext_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        xx_ext_entry_cleanup(&parsed->entries[index]);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->block) xx_mem_free(parsed->block);
    xx_ext_visited_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* ------------------------------------------------------------- symlinks -- */

/* A symlink shorter than 60 bytes is a "fast" symlink: the target sits in
 * i_block instead of in a data block, and i_blocks is zero because none is
 * allocated. Longer targets live in the first data block. */
static char *xx_ext_read_symlink(const xx_ext_private *parsed,
                                 const uint8_t *inode, uint64_t size,
                                 xx_pd_struct *pd) {
    char *target;
    uint32_t blocks_lo =
        xx_data_get_u32(inode, XX_EXT_INODE_CORE_SIZE, 0x1CU, false);
    if (size == 0U || size >= XX_EXT_MAX_PATH) return NULL;
    target = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!target) return NULL;
    if (size < 60U && blocks_lo == 0U) {
        xx_rt_memcpy(target, inode + 0x28, (size_t)size);
        target[size] = '\0';
        return target;
    }
    {
        /* Slow symlink: exactly one block holds the whole target. */
        xx_ext_runs runs;
        bool ok = false;
        xx_mem_zero(&runs, sizeof(runs));
        if (size <= parsed->super.block_size &&
            xx_ext_collect_runs(parsed, inode, 1U, &runs, pd) &&
            runs.count != 0U && runs.items[0].physical != 0U) {
            int64_t offset =
                xx_ext_block_offset(parsed, runs.items[0].physical);
            ok = offset >= 0 &&
                 xx_ext_read_at(parsed->device, offset, target, (size_t)size);
        }
        xx_ext_runs_cleanup(&runs);
        if (!ok) {
            xx_mem_free(target);
            return NULL;
        }
        target[size] = '\0';
    }
    return target;
}

/* ------------------------------------------------------- directory walk -- */

/* Parse one directory data block. A malformed record ends this block only:
 * the rest of the directory is still worth reading, and refusing the whole
 * image over one bad record would lose far more than it protects. */
static bool xx_ext_parse_dir_block(xx_ext_private *parsed, const uint8_t *data,
                                   const char *prefix, unsigned depth,
                                   xx_pd_struct *pd) {
    uint32_t block_size = parsed->super.block_size;
    uint32_t at = 0U;
    bool filetype =
        (parsed->super.feature_incompat & XX_EXT_INCOMPAT_FILETYPE) != 0U;

    while (at + 8U <= block_size) {
        uint32_t child_inode;
        uint32_t rec_len;
        uint32_t name_len;
        uint8_t child[XX_EXT_INODE_CORE_SIZE];
        uint16_t mode;
        uint16_t type;
        uint32_t flags;
        uint64_t size;
        const uint8_t *name;
        char *full_name;
        xx_ext_entry entry;

        if (pd && xx_pd_is_stopped(pd)) return false;
        child_inode = xx_data_get_u32(data, block_size, at, false);
        rec_len = xx_data_get_u16(data, block_size, at + 4U, false);
        if (filetype) {
            name_len = xx_data_get_u8(data, block_size, at + 6U);
        } else {
            name_len = xx_data_get_u16(data, block_size, at + 6U, false);
        }
        /* A zero or unaligned rec_len would step nowhere or step off the
         * record grid; either one ends the block rather than spinning. */
        if (rec_len < 8U || (rec_len & 3U) != 0U || rec_len > block_size - at) {
            return true;
        }
        if (name_len > rec_len - 8U) return true;
        name = data + at + 8U;
        at += rec_len;

        /* A zero inode is an unused slot - and is also how a hashed
         * directory's interior nodes hide from a linear read. */
        if (child_inode == 0U || name_len == 0U) continue;
        if (xx_ext_is_dot_name(name, name_len)) continue;
        if (!xx_ext_plausible_name(name, name_len)) continue;
        if (parsed->count >= XX_EXT_MAX_ENTRIES) return true;
        if (!xx_ext_read_inode(parsed, child_inode, child)) continue;

        mode = xx_data_get_u16(child, sizeof(child), 0x00U, false);
        type = (uint16_t)(mode & XX_EXT_S_IFMT);
        flags = xx_data_get_u32(child, sizeof(child), 0x20U, false);
        if (type != XX_EXT_S_IFREG && type != XX_EXT_S_IFDIR &&
            type != XX_EXT_S_IFLNK) {
            /* Devices, fifos and sockets carry no extractable payload. */
            continue;
        }
        if ((flags & (XX_EXT_FL_INLINE_DATA | XX_EXT_FL_ENCRYPT)) != 0U) {
            continue;
        }
        size = xx_ext_inode_size(parsed, child, mode);
        /* No file can be larger than the filesystem that holds it; this is
         * what keeps a forged i_size from driving a runaway extraction. */
        if (size > parsed->super.block_count * (uint64_t)block_size) continue;

        full_name = xx_ext_join_name(prefix, name, name_len);
        if (!full_name) continue;

        xx_mem_zero(&entry, sizeof(entry));
        entry.name = full_name;
        entry.inode = child_inode;
        entry.data_offset = -1;

        if (type == XX_EXT_S_IFLNK) {
            entry.link_target = xx_ext_read_symlink(parsed, child, size, pd);
            if (!entry.link_target) {
                xx_ext_entry_cleanup(&entry);
                continue;
            }
            entry.is_link = true;
            entry.size = size;
        } else if (type == XX_EXT_S_IFDIR) {
            entry.is_folder = true;
            entry.size = 0U;
        } else {
            xx_ext_runs runs;
            xx_mem_zero(&runs, sizeof(runs));
            entry.size = size;
            /* The listing reports where the payload starts; extraction
             * rebuilds the full map from the inode. */
            if (xx_ext_collect_runs(parsed, child,
                                    xx_ext_block_total(parsed, size), &runs,
                                    pd)) {
                size_t run;
                for (run = 0U; run < runs.count; ++run) {
                    if (runs.items[run].physical != 0U) {
                        entry.data_offset =
                            xx_ext_block_offset(parsed, runs.items[run].physical);
                        break;
                    }
                }
            }
            xx_ext_runs_cleanup(&runs);
        }

        if (!xx_ext_append_entry(parsed, &entry)) {
            xx_ext_entry_cleanup(&entry);
            return false;
        }
        if (type == XX_EXT_S_IFDIR &&
            !xx_ext_visited_mark(&parsed->visited, child_inode)) {
            /* The recursion appends entries, which can reallocate the entry
             * array out from under a pointer into it - so the prefix handed
             * down is a copy, not the stored name. */
            char *child_prefix =
                xx_str_create(parsed->entries[parsed->count - 1U].name);
            bool ok;
            if (!child_prefix) return false;
            ok = xx_ext_walk_directory(parsed, child_inode, child_prefix,
                                       depth + 1U, pd);
            xx_str_free(child_prefix);
            if (!ok) return false;
        }
    }
    return true;
}

static bool xx_ext_walk_directory(xx_ext_private *parsed, uint32_t inode,
                                  const char *prefix, unsigned depth,
                                  xx_pd_struct *pd) {
    uint8_t node[XX_EXT_INODE_CORE_SIZE];
    xx_ext_runs runs;
    uint8_t *block;
    uint64_t size;
    uint64_t total;
    size_t run;
    bool ok = true;

    if (!parsed || depth > XX_EXT_MAX_DEPTH) return true;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_ext_read_inode(parsed, inode, node)) return false;
    if ((xx_data_get_u16(node, sizeof(node), 0x00U, false) & XX_EXT_S_IFMT) !=
        XX_EXT_S_IFDIR) {
        return false;
    }
    /* A directory's size is always the low word: the high word is reused. */
    size = xx_data_get_u32(node, sizeof(node), 0x04U, false);
    total = xx_ext_block_total(parsed, size);
    if (total > parsed->super.block_count) return false;
    xx_mem_zero(&runs, sizeof(runs));
    if (!xx_ext_collect_runs(parsed, node, total, &runs, pd)) {
        xx_ext_runs_cleanup(&runs);
        return false;
    }
    /* The buffer is per invocation, not shared: xx_ext_parse_dir_block()
     * recurses into the subdirectories it finds while it is still walking
     * the records in this block, and a single scratch buffer would be
     * overwritten underneath the parent's loop. */
    block = (uint8_t *)xx_mem_alloc(parsed->super.block_size);
    if (!block) {
        xx_ext_runs_cleanup(&runs);
        return false;
    }
    for (run = 0U; ok && run < runs.count; ++run) {
        uint64_t index;
        if (runs.items[run].physical == 0U) continue; /* Hole: no records. */
        for (index = 0U; index < runs.items[run].count; ++index) {
            if (pd && xx_pd_is_stopped(pd)) {
                ok = false;
                break;
            }
            if (!xx_ext_read_block(parsed, runs.items[run].physical + index,
                                   block)) {
                ok = false;
                break;
            }
            if (!xx_ext_parse_dir_block(parsed, block, prefix, depth, pd)) {
                ok = false;
                break;
            }
        }
    }
    xx_mem_free(block);
    xx_ext_runs_cleanup(&runs);
    return ok;
}

/* ---------------------------------------------------------------- parse -- */

/* `deep` selects between the superblock-only check detection needs and the
 * full directory walk the listing needs. */
static bool xx_ext_parse(Abstractformat *self, xx_ext_private *parsed,
                         bool deep, xx_pd_struct *pd) {
    uint8_t *raw = NULL;
    int64_t super_offset;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->device = self->device;
    parsed->base_address = self->base_address;
    parsed->input_size = xx_io_total_size(self->device);
    if (self->base_address > INT64_MAX - XX_EXT_SUPERBLOCK_OFFSET) goto fail;
    super_offset = self->base_address + XX_EXT_SUPERBLOCK_OFFSET;
    if (!xx_ext_range_within(parsed->input_size, super_offset,
                             XX_EXT_SUPERBLOCK_SIZE)) {
        goto fail;
    }
    raw = (uint8_t *)xx_mem_alloc(XX_EXT_SUPERBLOCK_SIZE);
    if (!raw) goto fail;
    if (!xx_ext_read_at(self->device, super_offset, raw,
                        XX_EXT_SUPERBLOCK_SIZE)) {
        goto fail;
    }
    if (!xx_ext_parse_superblock(parsed, raw)) goto fail;
    xx_mem_free(raw);
    raw = NULL;
    if (!deep) return true;

    parsed->block = (uint8_t *)xx_mem_alloc(parsed->super.block_size);
    if (!parsed->block) goto fail;
    if (xx_ext_visited_mark(&parsed->visited, XX_EXT_ROOT_INODE)) goto fail;
    if (!xx_ext_walk_directory(parsed, XX_EXT_ROOT_INODE, "", 0U, pd)) {
        goto fail;
    }
    return true;
fail:
    if (raw) xx_mem_free(raw);
    xx_ext_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------ extraction - */

/* Write one member's bytes to `destination`, assembling the runs in logical
 * order and filling holes - and the gaps an extent tree leaves between its
 * extents - with zeros. */
static bool xx_ext_extract_entry(xx_ext_private *parsed,
                                 const xx_ext_entry *entry,
                                 xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t node[XX_EXT_INODE_CORE_SIZE];
    xx_ext_runs runs;
    uint8_t *zeros = NULL;
    uint64_t written = 0U;
    uint64_t total;
    size_t run;
    bool ok = true;
    uint32_t block_size;

    if (!parsed || !entry || !destination) return false;
    block_size = parsed->super.block_size;
    if (!xx_ext_read_inode(parsed, entry->inode, node)) return false;
    total = xx_ext_block_total(parsed, entry->size);
    xx_mem_zero(&runs, sizeof(runs));
    if (!xx_ext_collect_runs(parsed, node, total, &runs, pd)) {
        xx_ext_runs_cleanup(&runs);
        return false;
    }
    zeros = (uint8_t *)xx_mem_calloc(1U, block_size);
    if (!zeros) {
        xx_ext_runs_cleanup(&runs);
        return false;
    }

    for (run = 0U; ok && run < runs.count; ++run) {
        uint64_t start = runs.items[run].logical * (uint64_t)block_size;
        uint64_t index;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        /* Extents need not be adjacent; the space between two of them is a
         * hole that the file still contains as zeros. */
        while (ok && written < start && written < entry->size) {
            uint64_t left = start - written;
            uint64_t take = entry->size - written;
            if (take > left) take = left;
            if (take > block_size) take = block_size;
            ok = xx_io_write(destination, zeros, (size_t)take) ==
                 (ssize_t)take;
            written += take;
        }
        for (index = 0U; ok && index < runs.items[run].count; ++index) {
            uint64_t take = entry->size > written ? entry->size - written : 0U;
            if (take == 0U) break;
            if (take > block_size) take = block_size;
            if (runs.items[run].physical == 0U) {
                ok = xx_io_write(destination, zeros, (size_t)take) ==
                     (ssize_t)take;
            } else {
                int64_t offset = xx_ext_block_offset(
                    parsed, runs.items[run].physical + index);
                ok = offset >= 0 &&
                     xx_ext_read_at(parsed->device, offset, parsed->block,
                                    (size_t)take) &&
                     xx_io_write(destination, parsed->block, (size_t)take) ==
                         (ssize_t)take;
            }
            written += take;
        }
    }
    /* Trailing holes past the last run. */
    while (ok && written < entry->size) {
        uint64_t take = entry->size - written;
        if (take > block_size) take = block_size;
        ok = xx_io_write(destination, zeros, (size_t)take) == (ssize_t)take;
        written += take;
    }
    xx_mem_free(zeros);
    xx_ext_runs_cleanup(&runs);
    return ok && written == entry->size;
}

/* -------------------------------------------------------------- records -- */

static bool xx_ext_copy_options(xx_list_s *destination,
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

static const xx_var *xx_ext_find_option(const xx_list_s *options,
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

static bool xx_ext_populate_record(xx_archive_record *record,
                                   const xx_ext_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = entry->data_offset;
    record->compressed_size = (int64_t)entry->size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           (!entry->is_link ||
            xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                           entry->link_target));
}

static void xx_ext_archive_stream_free(void *pointer) {
    xx_ext_archive_stream *stream = (xx_ext_archive_stream *)pointer;
    if (!stream) return;
    xx_ext_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_ext_init(xx_ext *ext, xx_io_device *dev, int64_t base_address) {
    if (!ext) return;
    xx_mem_zero(ext, sizeof(*ext));
    xx_format_init(&ext->format, dev, base_address);
    ext->format.endian = XX_ENDIAN_LITTLE;
    ext->format.file_type = XX_EXT_FILE_TYPE;
    ext->format.format_type = XX_TYPE_ARCHIVE;
    ext->format.is_archive = true;
    xx_format_set_mime_type(&ext->format, "application/x-ext");
    xx_format_set_extension(&ext->format, "ext");
    ext->format.check_is_valid = xx_ext_check_is_valid;
    ext->format.handle_base_info = xx_ext_handle_base_info;
    ext->format.get_format_size = xx_ext_get_format_size;
    ext->format.get_number_of_archive_records =
        xx_ext_get_number_of_archive_records;
    ext->format.create_archive_records_reading =
        xx_ext_create_archive_records_reading;
    ext->format.get_current_archive_record = xx_ext_get_current_archive_record;
    ext->format.unpack_current_archive_record =
        xx_ext_unpack_current_archive_record;
    ext->format.archive_record_move_to_next = xx_ext_archive_record_move_to_next;
    ext->format.free_archive_records_reading =
        xx_ext_free_archive_records_reading;
    ext->format.destroy = xx_ext_vtable_destroy;
    ext->archive_end = -1;
}

xx_ext *xx_ext_create(xx_io_device *dev, int64_t base_address) {
    xx_ext *ext = (xx_ext *)xx_mem_alloc(sizeof(*ext));
    if (ext) xx_ext_init(ext, dev, base_address);
    return ext;
}

void xx_ext_destroy(xx_ext *ext) {
    if (!ext) return;
    if (ext->internal) {
        xx_ext_private_cleanup((xx_ext_private *)ext->internal);
        xx_mem_free(ext->internal);
        ext->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ext->format);
}

static void xx_ext_vtable_destroy(Abstractformat *self) {
    xx_ext_destroy((xx_ext *)self);
}

void xx_ext_free(xx_ext *ext) {
    if (!ext) return;
    xx_ext_destroy(ext);
    xx_mem_free(ext);
}

/* --------------------------------------------------------------- vtable -- */

bool xx_ext_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ext_private parsed;
    /* Detection stops at the superblock. The magic sits 1080 bytes into the
     * volume, well past any magic prefilter, so this has to touch the
     * device - but it only reads one kilobyte to do it. */
    bool result = xx_ext_parse(self, &parsed, false, pd);
    xx_ext_private_cleanup(&parsed);
    return result;
}

bool xx_ext_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ext_private *parsed;
    xx_ext *ext = (xx_ext *)self;
    int64_t total_size;
    if (!self || !ext) return false;
    parsed = (xx_ext_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_ext_parse(self, parsed, true, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ext->internal) {
        xx_ext_private_cleanup((xx_ext_private *)ext->internal);
        xx_mem_free(ext->internal);
    }
    ext->internal = parsed;
    ext->number_of_records = parsed->count;
    ext->number_of_members = parsed->count;
    ext->block_size = parsed->super.block_size;
    ext->block_count = parsed->super.block_count;
    ext->inode_count = parsed->super.inode_count;
    ext->inode_size = parsed->super.inode_size;
    ext->group_count = parsed->super.group_count;
    ext->feature_compat = parsed->super.feature_compat;
    ext->feature_incompat = parsed->super.feature_incompat;
    ext->feature_ro_compat = parsed->super.feature_ro_compat;
    ext->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ext_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ext_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ext *)self)->number_of_records;
}

xx_archive_record_state *xx_ext_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ext_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ext_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_ext_copy_options(&state->options, options) ||
        !xx_ext_parse(self, &stream->parsed, true, pd)) {
        xx_ext_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ext_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_ext_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ext_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ext_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_ext_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ext_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ext_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_ext_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ext_archive_stream *stream;
    const xx_ext_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ext_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_ext_safe_name(entry->name)) return false;

    option = xx_ext_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member is addressable at all.
         * A symlink answers no here for the same reason it does below. */
        return !entry->is_link &&
               (entry->is_folder ||
                entry->size <=
                    stream->parsed.super.block_count *
                        (uint64_t)stream->parsed.super.block_size);
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
        destination_path = xx_str_concat3(base, "/", entry->name);
    } else {
        destination_path = xx_str_concat(base, entry->name);
    }
    if (!destination_path) goto cleanup;

    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination_path, true);
        goto cleanup;
    }
    /* A symlink is not a byte stream. The target is on the record as
     * XX_META_ID_LINK_TARGET and creating the link is the caller's decision -
     * it needs a privilege this library does not ask for on Windows. Writing
     * the target into a regular file would leave the caller believing a file
     * exists whose contents are a path. */
    if (entry->is_link) goto cleanup;

    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    result = xx_ext_extract_entry(&stream->parsed, entry, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result) xx_rt_remove(destination_path);

cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_ext_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* -------------------------------------------------------------- getters -- */

uint64_t xx_ext_get_number_of_records(const xx_ext *ext) {
    return ext ? ext->number_of_records : 0U;
}
uint64_t xx_ext_get_number_of_members(const xx_ext *ext) {
    return ext ? ext->number_of_members : 0U;
}
uint32_t xx_ext_get_block_size(const xx_ext *ext) {
    return ext ? ext->block_size : 0U;
}
uint64_t xx_ext_get_block_count(const xx_ext *ext) {
    return ext ? ext->block_count : 0U;
}
uint32_t xx_ext_get_inode_count(const xx_ext *ext) {
    return ext ? ext->inode_count : 0U;
}
int64_t xx_ext_get_archive_end(const xx_ext *ext) {
    return ext ? ext->archive_end : -1;
}

const char *xx_ext_get_generation(const xx_ext *ext) {
    if (!ext) return "ext";
    /* There is no version field; the feature words are the only evidence.
     * Extents, 64-bit or flex_bg mean ext4; a journal alone means ext3. */
    if ((ext->feature_incompat &
         (XX_EXT_INCOMPAT_EXTENTS | XX_EXT_INCOMPAT_64BIT |
          XX_EXT_INCOMPAT_FLEX_BG)) != 0U) {
        return "ext4";
    }
    if ((ext->feature_compat & XX_EXT_COMPAT_HAS_JOURNAL) != 0U) return "ext3";
    return "ext2";
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QNX6, the QNX Neutrino power-safe filesystem. The structure layout followed
 * here is the one documented by the Linux kernel's fs/qnx6 (qnx6_fs.h, the
 * superblock, root node, inode and directory entry records, and the 16-byte
 * long-filename indirection) together with the QNX Neutrino filesystem
 * documentation; the per-field notes live in xx_qnx6.h.
 *
 * SCOPE. This is the QNX6 FILESYSTEM (superblock magic 0x68191122). It is not
 * the QNX IFS startup image container, which is a separate format and is not
 * implemented, and it is unrelated to xxfclib's "qnxbase" reader, which
 * handles the QNX archive format; neither is touched by this file.
 *
 * Everything an image says about itself is treated as hostile: block pointer
 * arrays are followed through a bounded number of indirection levels, every
 * physical block is re-checked against the block count and the device length
 * before it is read, the directory walk carries a path stack that refuses an
 * inode already open above it, and a global node budget stops a directed
 * graph of directories from fanning out.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qnx6/xx_qnx6.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as QNX6 is registered there. */
#ifdef QNX6
#define XX_QNX6_FILE_TYPE XX_FILE_TYPE_QNX6
#else
#define XX_QNX6_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_QNX6_MAGIC UINT32_C(0x68191122)
#define XX_QNX6_BOOTBLOCK_SIZE 0x2000
#define XX_QNX6_SUPERBLOCK_AREA 0x1000
#define XX_QNX6_SUPERBLOCK_SIZE 392U
#define XX_QNX6_ROOT_NODE_SIZE 80U
#define XX_QNX6_DIRECT_POINTERS 16U
#define XX_QNX6_INODE_SIZE 128U
#define XX_QNX6_DIR_ENTRY_SIZE 32U
#define XX_QNX6_SHORT_NAME_MAX 27U
#define XX_QNX6_LONG_NAME_MAX 510U
#define XX_QNX6_ROOT_INO 1U
#define XX_QNX6_LONG_MARKER 0xffU

/* Root node offsets inside the superblock. */
#define XX_QNX6_ROOT_INODE_AT 72U
#define XX_QNX6_ROOT_LONGFILE_AT 232U

/* Blocks smaller than 512 bytes leave no room for a pointer block worth
 * having, and QNX never writes more than 4 KB; a little headroom is allowed
 * so an unusual but self-consistent image still reads. */
#define XX_QNX6_MIN_BLOCKSIZE 512U
#define XX_QNX6_MAX_BLOCKSIZE 65536U

/* Each indirection level costs one block read per mapped block, so the depth
 * is what turns a mapping into work. Three levels already address more than
 * 2^40 bytes at a 4 KB block size. */
#define XX_QNX6_MAX_LEVELS 4U

#define XX_QNX6_MAX_ENTRIES 100000U
#define XX_QNX6_MAX_NODES 200000U
#define XX_QNX6_MAX_DEPTH 64U
#define XX_QNX6_MAX_PATH 4096U
/* One directory is not allowed to be larger than this many blocks; the size
 * comes off the inode and would otherwise drive an unbounded scan. */
#define XX_QNX6_MAX_DIR_BLOCKS 65536U

#define XX_QNX6_MODE_FMT 0xf000U
#define XX_QNX6_MODE_DIR 0x4000U
#define XX_QNX6_MODE_REG 0x8000U
#define XX_QNX6_MODE_LNK 0xa000U

typedef struct xx_qnx6_node_s {
    uint32_t pointers[XX_QNX6_DIRECT_POINTERS];
    uint64_t size;
    uint8_t levels;
} xx_qnx6_node;

typedef struct xx_qnx6_entry_s {
    char *name;
    uint64_t size;
    int64_t inode_offset; /**< Device offset of the 128-byte inode entry. */
    int64_t first_block;  /**< Device offset of the first data block, or -1. */
    uint32_t inode;
    uint32_t mtime;
    uint16_t mode;
    bool is_folder;
} xx_qnx6_entry;

typedef struct xx_qnx6_private_s {
    xx_qnx6_entry *entries;
    size_t count;
    size_t capacity;
    size_t nodes;              /**< Inodes examined, capped. */
    uint32_t path_stack[XX_QNX6_MAX_DEPTH + 1U];
    xx_qnx6_node inode_file;   /**< The Inode root node. */
    xx_qnx6_node longfile;     /**< The Longfile root node. */
    int64_t input_size;
    int64_t base_address;
    int64_t superblock_offset;
    int64_t data_start;        /**< Device offset of physical block zero. */
    int64_t archive_end;
    uint64_t serial;
    uint32_t blocksize;
    uint32_t pointers_per_block;
    uint32_t num_inodes;
    uint32_t free_inodes;
    uint32_t num_blocks;
    uint32_t free_blocks;
    uint32_t checksum;
    uint16_t version1;
    uint16_t version2;
    bool big_endian;
} xx_qnx6_private;

typedef struct xx_qnx6_archive_stream_s {
    xx_qnx6_private parsed;
    size_t index;
} xx_qnx6_archive_stream;

static void xx_qnx6_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* xx_io_seek64 throughout: a QNX6 volume is a disk image and long is 32 bits
 * on Win64, so xx_io_seek() would silently truncate past 2 GB. */
static bool xx_qnx6_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_qnx6_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static uint32_t xx_qnx6_u32(const xx_qnx6_private *parsed, const void *data,
                            size_t data_size, size_t offset) {
    return xx_data_get_u32(data, data_size, offset, parsed->big_endian);
}

static uint64_t xx_qnx6_u64(const xx_qnx6_private *parsed, const void *data,
                            size_t data_size, size_t offset) {
    return xx_data_get_u64(data, data_size, offset, parsed->big_endian);
}

static uint16_t xx_qnx6_u16(const xx_qnx6_private *parsed, const void *data,
                            size_t data_size, size_t offset) {
    return xx_data_get_u16(data, data_size, offset, parsed->big_endian);
}

/* Device offset of physical block, or -1 when the block is outside the
 * filesystem or outside the device. Block zero is a legal data block, but a
 * pointer of zero means "not mapped", which callers check before asking. */
static int64_t xx_qnx6_block_offset(const xx_qnx6_private *parsed,
                                    uint32_t block) {
    int64_t offset;

    if (block >= parsed->num_blocks) return -1;
    offset = parsed->data_start + (int64_t)block * (int64_t)parsed->blocksize;
    if (!xx_qnx6_range_within(parsed->input_size, offset,
                              (int64_t)parsed->blocksize)) {
        return -1;
    }
    return offset;
}

/* Map a logical block of a file - or of one of the superblock's root nodes -
 * to a physical block, following `levels` of indirection. Returns 0 for an
 * unmapped block, which every caller treats as a hole. Each level is one
 * block read and the level count is capped, so the work is bounded even when
 * an indirect block points at itself: it would simply be read again at the
 * next level and the loop still ends. */
static uint32_t xx_qnx6_map_block(xx_io_device *device,
                                  const xx_qnx6_private *parsed,
                                  const xx_qnx6_node *node, uint64_t logical) {
    uint8_t *buffer;
    uint32_t block;
    uint32_t ptrbits = 0U;
    uint32_t mask;
    uint32_t value = parsed->pointers_per_block;
    int32_t depth;
    int64_t bitdelta;
    uint64_t index;

    if (!node || node->levels > XX_QNX6_MAX_LEVELS) return 0U;
    while (value > 1U) {
        value >>= 1;
        ++ptrbits;
    }
    mask = parsed->pointers_per_block - 1U;
    depth = (int32_t)node->levels;
    bitdelta = (int64_t)ptrbits * depth;
    /* A logical block beyond what the direct pointers can address at this
     * level count is simply not part of the file. */
    if (bitdelta >= 64) return 0U;
    index = logical >> bitdelta;
    if (index >= XX_QNX6_DIRECT_POINTERS) return 0U;
    block = node->pointers[index];
    if (depth == 0) return block;

    buffer = (uint8_t *)xx_mem_alloc(parsed->blocksize);
    if (!buffer) return 0U;
    while (--depth >= 0) {
        int64_t at;
        if (block == 0U) break;
        at = xx_qnx6_block_offset(parsed, block);
        if (at < 0 || !xx_qnx6_read_at(device, at, buffer, parsed->blocksize)) {
            block = 0U;
            break;
        }
        bitdelta -= (int64_t)ptrbits;
        index = (logical >> bitdelta) & mask;
        block = xx_qnx6_u32(parsed, buffer, parsed->blocksize,
                            (size_t)index * 4U);
    }
    xx_mem_free(buffer);
    return block;
}

/* Read one logical block of a file into buffer, which is blocksize long.
 * A hole reads as zeros, which is what the filesystem reports. */
static bool xx_qnx6_read_file_block(xx_io_device *device,
                                    const xx_qnx6_private *parsed,
                                    const xx_qnx6_node *node, uint64_t logical,
                                    uint8_t *buffer) {
    uint32_t block = xx_qnx6_map_block(device, parsed, node, logical);
    int64_t at;

    if (block == 0U) {
        xx_mem_zero(buffer, parsed->blocksize);
        return true;
    }
    at = xx_qnx6_block_offset(parsed, block);
    if (at < 0) {
        xx_mem_zero(buffer, parsed->blocksize);
        return true;
    }
    return xx_qnx6_read_at(device, at, buffer, parsed->blocksize);
}

/* Read a 128-byte inode entry out of the inode file. Inode numbers are
 * 1-based; inode 1 is the root directory. */
static bool xx_qnx6_read_inode(xx_io_device *device,
                               const xx_qnx6_private *parsed, uint32_t inode,
                               uint8_t *entry, int64_t *out_offset) {
    uint64_t byte_offset;
    uint64_t logical;
    uint32_t block;
    uint32_t within;
    int64_t at;

    if (inode == 0U || inode > parsed->num_inodes) return false;
    byte_offset = (uint64_t)(inode - 1U) * XX_QNX6_INODE_SIZE;
    if (byte_offset + XX_QNX6_INODE_SIZE > parsed->inode_file.size) {
        return false;
    }
    logical = byte_offset / parsed->blocksize;
    within = (uint32_t)(byte_offset % parsed->blocksize);
    /* An inode entry never straddles a block: 128 divides every legal
     * block size. Checked rather than assumed. */
    if (within + XX_QNX6_INODE_SIZE > parsed->blocksize) return false;
    block = xx_qnx6_map_block(device, parsed, &parsed->inode_file, logical);
    if (block == 0U) return false;
    at = xx_qnx6_block_offset(parsed, block);
    if (at < 0) return false;
    if (!xx_qnx6_read_at(device, at + within, entry, XX_QNX6_INODE_SIZE)) {
        return false;
    }
    if (out_offset) *out_offset = at + within;
    return true;
}

static void xx_qnx6_load_node(const xx_qnx6_private *parsed,
                              const uint8_t *data, size_t data_size,
                              size_t offset, xx_qnx6_node *node) {
    uint32_t index;

    node->size = xx_qnx6_u64(parsed, data, data_size, offset);
    for (index = 0U; index < XX_QNX6_DIRECT_POINTERS; ++index) {
        node->pointers[index] =
            xx_qnx6_u32(parsed, data, data_size, offset + 8U + index * 4U);
    }
    node->levels = ((const uint8_t *)data)[offset + 72U];
}

/* ------------------------------------------------------------ the names -- */

/* Resolve the long-name form. The directory slot holds a block index into
 * the longfile file; that block starts with a u16 length and the name. */
static char *xx_qnx6_read_long_name(xx_io_device *device,
                                    const xx_qnx6_private *parsed,
                                    uint32_t long_block) {
    uint8_t *buffer;
    char *name = NULL;
    uint32_t length;
    uint32_t index;

    buffer = (uint8_t *)xx_mem_alloc(parsed->blocksize);
    if (!buffer) return NULL;
    if (!xx_qnx6_read_file_block(device, parsed, &parsed->longfile,
                                 (uint64_t)long_block, buffer)) {
        xx_mem_free(buffer);
        return NULL;
    }
    length = xx_qnx6_u16(parsed, buffer, parsed->blocksize, 0U);
    if (length == 0U || length > XX_QNX6_LONG_NAME_MAX ||
        length + 2U > parsed->blocksize) {
        xx_mem_free(buffer);
        return NULL;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t character = buffer[2U + index];
        if (character < 32U || character == '/' || character == '\\') {
            xx_mem_free(buffer);
            return NULL;
        }
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (name) {
        xx_mem_copy(name, buffer + 2U, length);
        name[length] = '\0';
    }
    xx_mem_free(buffer);
    return name;
}

/* A QNX6 directory entry name is one path component, so a separator or a
 * control byte makes it implausible. */
static bool xx_qnx6_plausible_name(const char *name, size_t length) {
    size_t index;

    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character < 32U || character == '/' || character == '\\') {
            return false;
        }
    }
    return true;
}

static bool xx_qnx6_is_dot_name(const char *name) {
    if (!name || name[0] != '.') return false;
    return name[1] == '\0' || (name[1] == '.' && name[2] == '\0');
}

static char *xx_qnx6_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;

    if (!name || name_size == 0U || prefix_size >= XX_QNX6_MAX_PATH ||
        name_size > XX_QNX6_MAX_PATH - prefix_size -
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

/* --------------------------------------------------------------- state -- */

static void xx_qnx6_private_cleanup(xx_qnx6_private *parsed) {
    size_t index;

    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_qnx6_append_entry(xx_qnx6_private *parsed,
                                 xx_qnx6_entry *entry) {
    xx_qnx6_entry *grown;
    size_t capacity;

    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_QNX6_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_qnx6_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* True when inode is already open somewhere above depth on the current path.
 * This is what stops a directory that contains itself, directly or through a
 * chain, from being walked forever; the node budget handles the wider case of
 * a directed graph that is not a cycle but still fans out. */
static bool xx_qnx6_on_path(const xx_qnx6_private *parsed, unsigned depth,
                            uint32_t inode) {
    unsigned index;

    for (index = 0U; index <= depth && index <= XX_QNX6_MAX_DEPTH; ++index) {
        if (parsed->path_stack[index] == inode) return true;
    }
    return false;
}

/* ---------------------------------------------------------------- walk -- */

static bool xx_qnx6_walk_dir(Abstractformat *self, xx_qnx6_private *parsed,
                             uint32_t inode, const char *prefix, unsigned depth,
                             xx_pd_struct *pd);

/* Record one directory entry and, when it names a directory, descend. */
static bool xx_qnx6_visit(Abstractformat *self, xx_qnx6_private *parsed,
                          uint32_t inode, char *full_name, unsigned depth,
                          xx_pd_struct *pd) {
    uint8_t raw[XX_QNX6_INODE_SIZE];
    xx_qnx6_entry entry;
    xx_qnx6_node node;
    int64_t inode_offset = -1;
    uint32_t first_block;
    uint16_t mode;

    if (!xx_qnx6_read_inode(self->device, parsed, inode, raw, &inode_offset)) {
        xx_str_free(full_name);
        return true; /* A dangling entry ends this entry, not the walk. */
    }
    ++parsed->nodes;
    mode = xx_qnx6_u16(parsed, raw, sizeof(raw), 32U);
    xx_mem_zero(&node, sizeof(node));
    node.size = xx_qnx6_u64(parsed, raw, sizeof(raw), 0U);
    {
        uint32_t index;
        for (index = 0U; index < XX_QNX6_DIRECT_POINTERS; ++index) {
            node.pointers[index] =
                xx_qnx6_u32(parsed, raw, sizeof(raw), 36U + index * 4U);
        }
    }
    node.levels = raw[100];

    xx_mem_zero(&entry, sizeof(entry));
    entry.name = full_name;
    entry.inode = inode;
    entry.inode_offset = inode_offset;
    entry.mode = mode;
    entry.mtime = xx_qnx6_u32(parsed, raw, sizeof(raw), 20U);
    entry.size = node.size;
    entry.is_folder = (mode & XX_QNX6_MODE_FMT) == XX_QNX6_MODE_DIR;
    first_block = xx_qnx6_map_block(self->device, parsed, &node, 0U);
    entry.first_block =
        first_block != 0U ? xx_qnx6_block_offset(parsed, first_block) : -1;
    if (entry.is_folder) entry.size = 0U;

    if ((mode & XX_QNX6_MODE_FMT) != XX_QNX6_MODE_DIR &&
        (mode & XX_QNX6_MODE_FMT) != XX_QNX6_MODE_REG &&
        (mode & XX_QNX6_MODE_FMT) != XX_QNX6_MODE_LNK) {
        /* Devices, fifos and sockets carry no extractable payload. */
        xx_str_free(full_name);
        return true;
    }
    if (!xx_qnx6_append_entry(parsed, &entry)) {
        xx_str_free(full_name);
        return false;
    }
    /* The entry owns the name now; the local pointer is only borrowed for the
     * recursion below and is released by xx_qnx6_private_cleanup(). */
    full_name = parsed->entries[parsed->count - 1U].name;
    if ((mode & XX_QNX6_MODE_FMT) == XX_QNX6_MODE_DIR) {
        return xx_qnx6_walk_dir(self, parsed, inode, full_name, depth + 1U, pd);
    }
    return true;
}

static bool xx_qnx6_walk_dir(Abstractformat *self, xx_qnx6_private *parsed,
                             uint32_t inode, const char *prefix, unsigned depth,
                             xx_pd_struct *pd) {
    uint8_t raw[XX_QNX6_INODE_SIZE];
    uint8_t *block;
    xx_qnx6_node node;
    uint64_t size;
    uint64_t blocks;
    uint64_t logical;
    uint32_t index;

    if (depth > XX_QNX6_MAX_DEPTH) return true;
    if (parsed->nodes >= XX_QNX6_MAX_NODES) return true;
    if (parsed->count >= XX_QNX6_MAX_ENTRIES) return true;
    if (xx_qnx6_on_path(parsed, depth == 0U ? 0U : depth - 1U, inode) &&
        depth != 0U) {
        return true;
    }
    parsed->path_stack[depth] = inode;
    if (!xx_qnx6_read_inode(self->device, parsed, inode, raw, NULL)) {
        return false;
    }
    if ((xx_qnx6_u16(parsed, raw, sizeof(raw), 32U) & XX_QNX6_MODE_FMT) !=
        XX_QNX6_MODE_DIR) {
        return false;
    }
    xx_mem_zero(&node, sizeof(node));
    node.size = xx_qnx6_u64(parsed, raw, sizeof(raw), 0U);
    for (index = 0U; index < XX_QNX6_DIRECT_POINTERS; ++index) {
        node.pointers[index] =
            xx_qnx6_u32(parsed, raw, sizeof(raw), 36U + index * 4U);
    }
    node.levels = raw[100];

    size = node.size;
    blocks = (size + parsed->blocksize - 1U) / parsed->blocksize;
    if (blocks > XX_QNX6_MAX_DIR_BLOCKS) blocks = XX_QNX6_MAX_DIR_BLOCKS;
    block = (uint8_t *)xx_mem_alloc(parsed->blocksize);
    if (!block) return false;

    for (logical = 0U; logical < blocks; ++logical) {
        uint32_t offset;

        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(block);
            return false;
        }
        if (!xx_qnx6_read_file_block(self->device, parsed, &node, logical,
                                     block)) {
            xx_mem_free(block);
            return false;
        }
        for (offset = 0U; offset + XX_QNX6_DIR_ENTRY_SIZE <= parsed->blocksize;
             offset += XX_QNX6_DIR_ENTRY_SIZE) {
            uint32_t child = xx_qnx6_u32(parsed, block, parsed->blocksize,
                                         offset);
            uint8_t name_size = block[offset + 4U];
            char *name = NULL;
            char *full_name;

            if (parsed->count >= XX_QNX6_MAX_ENTRIES ||
                parsed->nodes >= XX_QNX6_MAX_NODES) {
                xx_mem_free(block);
                return true;
            }
            if (child == 0U || name_size == 0U) continue;
            if (name_size == XX_QNX6_LONG_MARKER) {
                uint32_t long_block = xx_qnx6_u32(parsed, block,
                                                  parsed->blocksize,
                                                  offset + 8U);
                name = xx_qnx6_read_long_name(self->device, parsed, long_block);
            } else {
                if (name_size > XX_QNX6_SHORT_NAME_MAX) continue;
                if (!xx_qnx6_plausible_name((const char *)block + offset + 5U,
                                            name_size)) {
                    continue;
                }
                name = (char *)xx_mem_alloc((size_t)name_size + 1U);
                if (name) {
                    xx_mem_copy(name, block + offset + 5U, name_size);
                    name[name_size] = '\0';
                }
            }
            if (!name) continue;
            if (xx_qnx6_is_dot_name(name)) {
                xx_str_free(name);
                continue;
            }
            full_name = xx_qnx6_join_name(prefix, name);
            xx_str_free(name);
            if (!full_name) continue;
            if (!xx_qnx6_visit(self, parsed, child, full_name, depth, pd)) {
                xx_mem_free(block);
                return false;
            }
        }
    }
    xx_mem_free(block);
    return true;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_qnx6_parse_at(Abstractformat *self, xx_qnx6_private *parsed,
                             int64_t bootblock_offset, xx_pd_struct *pd) {
    uint8_t superblock[XX_QNX6_SUPERBLOCK_SIZE];
    int64_t superblock_at;
    int64_t total_size;
    uint32_t magic_le;
    uint32_t magic_be;
    uint32_t value;

    total_size = xx_io_total_size(self->device);
    if (self->base_address > INT64_MAX - bootblock_offset) return false;
    superblock_at = self->base_address + bootblock_offset;
    if (!xx_qnx6_range_within(total_size, superblock_at,
                              XX_QNX6_SUPERBLOCK_SIZE) ||
        !xx_qnx6_read_at(self->device, superblock_at, superblock,
                         sizeof(superblock))) {
        return false;
    }
    magic_le = xx_data_get_u32(superblock, sizeof(superblock), 0U, false);
    magic_be = xx_data_get_u32(superblock, sizeof(superblock), 0U, true);
    /* Both byte orders exist in the wild and the magic is not a palindrome,
     * so it picks the variant by itself. */
    if (magic_le == XX_QNX6_MAGIC) {
        parsed->big_endian = false;
    } else if (magic_be == XX_QNX6_MAGIC) {
        parsed->big_endian = true;
    } else {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    parsed->superblock_offset = superblock_at;
    parsed->checksum = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 4U);
    parsed->serial = xx_qnx6_u64(parsed, superblock, sizeof(superblock), 8U);
    parsed->version1 = xx_qnx6_u16(parsed, superblock, sizeof(superblock), 28U);
    parsed->version2 = xx_qnx6_u16(parsed, superblock, sizeof(superblock), 30U);
    parsed->blocksize = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 48U);
    parsed->num_inodes = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 52U);
    parsed->free_inodes = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 56U);
    parsed->num_blocks = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 60U);
    parsed->free_blocks = xx_qnx6_u32(parsed, superblock, sizeof(superblock), 64U);

    /* The block size has to be a power of two in range, and it has to divide
     * the superblock area so that the block grid starts on a block boundary. */
    value = parsed->blocksize;
    if (value < XX_QNX6_MIN_BLOCKSIZE || value > XX_QNX6_MAX_BLOCKSIZE ||
        (value & (value - 1U)) != 0U) {
        return false;
    }
    if (((uint64_t)(bootblock_offset + XX_QNX6_SUPERBLOCK_AREA) %
         (uint64_t)value) != 0U) {
        return false;
    }
    parsed->pointers_per_block = value / 4U;
    parsed->data_start =
        self->base_address + bootblock_offset + XX_QNX6_SUPERBLOCK_AREA;
    if (parsed->num_inodes == 0U || parsed->num_blocks == 0U) return false;
    /* The block area must fit the device; a claimed block count larger than
     * the image is the cheapest tell of a crafted or truncated superblock. */
    if (!xx_qnx6_range_within(total_size, parsed->data_start,
                              (int64_t)parsed->num_blocks *
                                  (int64_t)parsed->blocksize)) {
        return false;
    }
    parsed->archive_end =
        parsed->data_start +
        (int64_t)parsed->num_blocks * (int64_t)parsed->blocksize;

    xx_qnx6_load_node(parsed, superblock, sizeof(superblock),
                      XX_QNX6_ROOT_INODE_AT, &parsed->inode_file);
    xx_qnx6_load_node(parsed, superblock, sizeof(superblock),
                      XX_QNX6_ROOT_LONGFILE_AT, &parsed->longfile);
    if (parsed->inode_file.levels > XX_QNX6_MAX_LEVELS ||
        parsed->longfile.levels > XX_QNX6_MAX_LEVELS) {
        return false;
    }
    if (parsed->inode_file.size < XX_QNX6_INODE_SIZE) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The root directory is inode 1. A volume whose root will not read is not
     * a QNX6 filesystem as far as this reader is concerned. */
    return xx_qnx6_walk_dir(self, parsed, XX_QNX6_ROOT_INO, "", 0U, pd);
}

static bool xx_qnx6_parse(Abstractformat *self, xx_qnx6_private *parsed,
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
    /* The usual layout puts the superblock after a 0x2000 boot block. Images
     * dumped without the boot block put it at the very start, so that is
     * tried second rather than being treated as the norm. */
    if (xx_qnx6_parse_at(self, parsed, XX_QNX6_BOOTBLOCK_SIZE, pd)) return true;
    xx_qnx6_private_cleanup(parsed);
    if (xx_qnx6_parse_at(self, parsed, 0, pd)) return true;
    xx_qnx6_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------ extraction -- */

/* Stream one regular file to output, or just decode it when output is NULL. */
static bool xx_qnx6_write_file(Abstractformat *self,
                               const xx_qnx6_private *parsed, uint32_t inode,
                               uint64_t size, xx_io_device *output,
                               xx_pd_struct *pd) {
    uint8_t raw[XX_QNX6_INODE_SIZE];
    uint8_t *block;
    xx_qnx6_node node;
    uint64_t remaining = size;
    uint64_t logical = 0U;
    uint32_t index;
    bool result = true;

    if (!xx_qnx6_read_inode(self->device, parsed, inode, raw, NULL)) {
        return false;
    }
    xx_mem_zero(&node, sizeof(node));
    node.size = xx_qnx6_u64(parsed, raw, sizeof(raw), 0U);
    for (index = 0U; index < XX_QNX6_DIRECT_POINTERS; ++index) {
        node.pointers[index] =
            xx_qnx6_u32(parsed, raw, sizeof(raw), 36U + index * 4U);
    }
    node.levels = raw[100];
    /* The size the record published is the size the inode declared; if the
     * inode has since been re-read with a different one, trust the smaller. */
    if (node.size < remaining) remaining = node.size;

    block = (uint8_t *)xx_mem_alloc(parsed->blocksize);
    if (!block) return false;
    while (remaining != 0U) {
        size_t chunk = remaining < (uint64_t)parsed->blocksize
                           ? (size_t)remaining
                           : (size_t)parsed->blocksize;
        size_t done = 0U;

        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!xx_qnx6_read_file_block(self->device, parsed, &node, logical,
                                     block)) {
            result = false;
            break;
        }
        while (output && done < chunk) {
            ssize_t sent = xx_io_write(output, block + done, chunk - done);
            if (sent <= 0 || (size_t)sent > chunk - done) {
                result = false;
                break;
            }
            done += (size_t)sent;
        }
        if (!result) break;
        remaining -= (uint64_t)chunk;
        ++logical;
    }
    xx_mem_free(block);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_qnx6_init(xx_qnx6 *qnx6, xx_io_device *dev, int64_t base_address) {
    if (!qnx6) return;
    xx_mem_zero(qnx6, sizeof(*qnx6));
    xx_format_init(&qnx6->format, dev, base_address);
    /* Overwritten by handle_base_info once the superblock variant is known. */
    qnx6->format.endian = XX_ENDIAN_LITTLE;
    qnx6->format.file_type = XX_QNX6_FILE_TYPE;
    qnx6->format.format_type = XX_TYPE_ARCHIVE;
    qnx6->format.is_archive = true;
    xx_format_set_mime_type(&qnx6->format, "application/x-qnx6-filesystem");
    xx_format_set_extension(&qnx6->format, "qnx6");
    qnx6->format.check_is_valid = xx_qnx6_check_is_valid;
    qnx6->format.handle_base_info = xx_qnx6_handle_base_info;
    qnx6->format.get_format_size = xx_qnx6_get_format_size;
    qnx6->format.get_number_of_archive_records =
        xx_qnx6_get_number_of_archive_records;
    qnx6->format.create_archive_records_reading =
        xx_qnx6_create_archive_records_reading;
    qnx6->format.get_current_archive_record = xx_qnx6_get_current_archive_record;
    qnx6->format.unpack_current_archive_record =
        xx_qnx6_unpack_current_archive_record;
    qnx6->format.archive_record_move_to_next = xx_qnx6_archive_record_move_to_next;
    qnx6->format.free_archive_records_reading =
        xx_qnx6_free_archive_records_reading;
    qnx6->format.destroy = xx_qnx6_vtable_destroy;
    qnx6->archive_end = -1;
    qnx6->superblock_offset = -1;
}

xx_qnx6 *xx_qnx6_create(xx_io_device *dev, int64_t base_address) {
    xx_qnx6 *qnx6 = (xx_qnx6 *)xx_mem_alloc(sizeof(*qnx6));

    if (qnx6) xx_qnx6_init(qnx6, dev, base_address);
    return qnx6;
}

void xx_qnx6_destroy(xx_qnx6 *qnx6) {
    if (!qnx6) return;
    if (qnx6->internal) {
        xx_qnx6_private_cleanup((xx_qnx6_private *)qnx6->internal);
        xx_mem_free(qnx6->internal);
        qnx6->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&qnx6->format);
}

static void xx_qnx6_vtable_destroy(Abstractformat *self) {
    xx_qnx6_destroy((xx_qnx6 *)self);
}

void xx_qnx6_free(xx_qnx6 *qnx6) {
    if (!qnx6) return;
    xx_qnx6_destroy(qnx6);
    xx_mem_free(qnx6);
}

/* --------------------------------------------------------------- format -- */

bool xx_qnx6_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qnx6_private parsed;
    bool result = xx_qnx6_parse(self, &parsed, pd);

    xx_qnx6_private_cleanup(&parsed);
    return result;
}

bool xx_qnx6_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qnx6 *qnx6 = (xx_qnx6 *)self;
    xx_qnx6_private *parsed;
    int64_t total_size;

    if (!self || !qnx6) return false;
    parsed = (xx_qnx6_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_qnx6_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (qnx6->internal) {
        xx_qnx6_private_cleanup((xx_qnx6_private *)qnx6->internal);
        xx_mem_free(qnx6->internal);
    }
    qnx6->internal = parsed;
    qnx6->number_of_records = parsed->count;
    qnx6->number_of_members = parsed->count;
    qnx6->serial = parsed->serial;
    qnx6->blocksize = parsed->blocksize;
    qnx6->num_inodes = parsed->num_inodes;
    qnx6->free_inodes = parsed->free_inodes;
    qnx6->num_blocks = parsed->num_blocks;
    qnx6->free_blocks = parsed->free_blocks;
    qnx6->checksum = parsed->checksum;
    qnx6->version1 = parsed->version1;
    qnx6->version2 = parsed->version2;
    qnx6->superblock_offset = parsed->superblock_offset;
    qnx6->archive_end = parsed->archive_end;
    qnx6->big_endian = parsed->big_endian;
    self->endian = parsed->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = parsed->input_size;
    if (total_size > parsed->archive_end) {
        /* The second superblock and any padding sit past the block area. */
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

int64_t xx_qnx6_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_qnx6_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_qnx6 *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_qnx6_copy_options(xx_list_s *destination,
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

static const xx_var *xx_qnx6_find_option(const xx_list_s *options,
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

static bool xx_qnx6_populate_record(xx_archive_record *record,
                                    const xx_qnx6_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->inode_offset;
    record->header_size = XX_QNX6_INODE_SIZE;
    /* A QNX6 file is a list of blocks, not one extent; data_offset points at
     * the first of them so that a caller has somewhere to look, and the real
     * read goes through the block map. */
    record->data_offset = entry->first_block;
    record->compressed_size = (int64_t)entry->size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          entry->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          entry->mtime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_qnx6_archive_stream_free(void *pointer) {
    xx_qnx6_archive_stream *stream = (xx_qnx6_archive_stream *)pointer;

    if (!stream) return;
    xx_qnx6_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

xx_archive_record_state *xx_qnx6_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_qnx6_archive_stream *stream;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_qnx6_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_qnx6_copy_options(&state->options, options) ||
        !xx_qnx6_parse(self, &stream->parsed, pd)) {
        xx_qnx6_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_qnx6_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_qnx6_populate_record(&state->current_record,
                                &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_qnx6_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qnx6_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_qnx6_archive_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qnx6_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_qnx6_populate_record(&state->current_record,
                                 &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

/* Extraction-time check: the name has to stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though a QNX6 image may legally carry it. */
static bool xx_qnx6_safe_name(const char *name) {
    const char *component;
    const char *cursor;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (character == ':' || character == '<' || character == '>' ||
            character == '"' || character == '|' || character == '?' ||
            character == '*' || (character != 0U && character < 32U)) {
            return false;
        }
        if (character == '/' || character == '\\' || character == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (character == 0U) return true;
            component = cursor + 1;
        }
    }
}

bool xx_qnx6_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qnx6_archive_stream *stream;
    const xx_qnx6_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qnx6_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_qnx6_safe_name(entry->name)) return false;

    option = xx_qnx6_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: read the member's blocks and discard them, which
         * still proves the block map resolves. */
        if (entry->is_folder) return true;
        return xx_qnx6_write_file(self, &stream->parsed, entry->inode,
                                  entry->size, NULL, pd);
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination, true);
        xx_str_free(destination);
        return result;
    }
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    result = output != NULL && xx_qnx6_write_file(self, &stream->parsed,
                                                  entry->inode, entry->size,
                                                  output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_qnx6_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint64_t xx_qnx6_get_number_of_records(const xx_qnx6 *qnx6) {
    return qnx6 ? qnx6->number_of_records : 0U;
}
uint32_t xx_qnx6_get_blocksize(const xx_qnx6 *qnx6) {
    return qnx6 ? qnx6->blocksize : 0U;
}
uint32_t xx_qnx6_get_num_blocks(const xx_qnx6 *qnx6) {
    return qnx6 ? qnx6->num_blocks : 0U;
}
bool xx_qnx6_is_big_endian(const xx_qnx6 *qnx6) {
    return qnx6 ? qnx6->big_endian : false;
}

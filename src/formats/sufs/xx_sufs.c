/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Solaris UFS1 reader; xx_sufs.h carries the layout.
 *
 * Written from the documented on-disk structures (superblock, cylinder
 * group placement, 128-byte inode, old-style directory entry). The accepted
 * signature (fs_nrpos 8 just before fs_magic 0x011954) and the superblock
 * sanity rules (fs_frag == fs_bsize / fs_fsize, fs_bsize <= 64 KiB, a
 * non-zero group count) match what unblob's solaris_ufs1 handler requires
 * (unblob/handlers/filesystem/ufs.py, MIT licence); no code was taken from
 * it. The derived fields (shifts, fs_nindir, fs_inopb) are checked as well
 * so that garbage is rejected before any inode is read.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sufs/xx_sufs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as SUFS is registered there. */
#ifdef SUFS
#define XX_SUFS_FILE_TYPE XX_FILE_TYPE_SUFS
#else
#define XX_SUFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SUFS_SUPER_OFFSET 8192
#define SUFS_SUPER_SIZE 0x560U
#define SUFS_MAGIC 0x00011954U
#define SUFS_NRPOS 8U
#define SUFS_BSD44_INODEFMT 2U
#define SUFS_INODE_SIZE 128U
#define SUFS_NDADDR 12U
#define SUFS_NIADDR 3U
#define SUFS_ROOT_INODE 2U
#define SUFS_DIRBLKSIZ 512U
#define SUFS_DIRENT_HEADER 8U
#define SUFS_MAX_NAME 255U
#define SUFS_MAX_DEPTH 64U
#define SUFS_MAX_RECORDS 100000U
/* Directory bytes examined over one whole parse. Several hostile
 * directories may share the same blocks, so the scan is budgeted. */
#define SUFS_MAX_DIR_BYTES (256U * 1024U * 1024U)
#define SUFS_MAX_PATH 4096U
/* A regular file may be sparse and larger than the filesystem, but not by
 * more than this factor: a small hostile image cannot claim terabytes. */
#define SUFS_SPARSE_FACTOR 8U

#define SUFS_S_IFMT 0170000U
#define SUFS_S_IFDIR 0040000U
#define SUFS_S_IFREG 0100000U

typedef struct sufs_geometry_s {
    int64_t base;
    uint64_t fs_bytes;      /**< fs_size * fs_fsize. */
    uint64_t size;          /**< fs_size, in fragments. */
    uint32_t bsize;
    uint32_t fsize;
    uint32_t frag;
    uint32_t ncg;
    uint32_t ipg;
    uint32_t fpg;
    uint32_t iblkno;
    uint32_t cgoffset;
    uint32_t cgmask;
    uint32_t nindir;
    bool big;
    bool legacy;
} sufs_geometry;

typedef struct sufs_inode_s {
    uint32_t mode;
    uint64_t size;
    uint32_t db[SUFS_NDADDR];
    uint32_t ib[SUFS_NIADDR];
} sufs_inode;

typedef struct sufs_entry_s {
    char *name;
    int64_t data_offset;  /**< First mapped data block, or -1. */
    uint32_t inode;
    uint64_t size;
    bool is_folder;
} sufs_entry;

typedef struct sufs_parsed_s {
    sufs_geometry geo;
    sufs_entry *entries;
    size_t count;
    size_t capacity;
    uint32_t *name_slots;   /**< Case-folded path set: entry index + 1. */
    size_t name_capacity;
    uint32_t *dir_slots;    /**< Visited directory inodes (+0 = empty). */
    size_t dir_capacity;
    size_t dir_count;
    uint64_t dir_bytes_left;
} sufs_parsed;

typedef struct sufs_stream_s {
    sufs_parsed parsed;
    size_t index;
} sufs_stream;

static void sufs_vtable_destroy(Abstractformat *self);

static bool sufs_read_at(xx_io_device *device, int64_t offset, void *data,
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

static uint32_t sufs_u16(bool big, const uint8_t *p) {
    return big ? ((uint32_t)p[0] << 8) | p[1] : ((uint32_t)p[1] << 8) | p[0];
}

static bool sufs_pow2(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static uint32_t sufs_log2(uint32_t value) {
    uint32_t shift = 0U;
    while (shift < 31U && (1U << shift) < value) ++shift;
    return shift;
}

/* First fragment of cylinder group `group` (with the old rotation). */
static uint64_t sufs_cgstart(const sufs_geometry *geo, uint32_t group) {
    return (uint64_t)group * geo->fpg +
           (uint64_t)geo->cgoffset * (group & ~geo->cgmask);
}

/* Decode and validate the superblock. Everything later relies on these
 * bounds: power-of-two sizes that agree with their derived fields, groups
 * that cover exactly fs_size fragments, and a filesystem that fits the
 * device. */
static bool sufs_read_geometry(Abstractformat *self, sufs_geometry *geo) {
    uint8_t sb[SUFS_SUPER_SIZE];
    uint8_t *magic = sb + 0x55C;
    int64_t total;
    bool big;
    uint64_t inode_frags;
    xx_mem_zero(geo, sizeof(*geo));
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < 0 || total < self->base_address ||
        total - self->base_address < SUFS_SUPER_OFFSET + (int64_t)SUFS_SUPER_SIZE)
        return false;
    /* Cheap early exit: the magic alone, before the full superblock. */
    if (!sufs_read_at(self->device,
                      self->base_address + SUFS_SUPER_OFFSET + 0x55C, magic,
                      4U))
        return false;
    if (xx_data_get_u32(magic, 4, 0, false) == SUFS_MAGIC) big = false;
    else if (xx_data_get_u32(magic, 4, 0, true) == SUFS_MAGIC) big = true;
    else return false;
    if (!sufs_read_at(self->device, self->base_address + SUFS_SUPER_OFFSET,
                      sb, SUFS_SUPER_SIZE))
        return false;
    if (((xx_sufs *)self)->nextstep_legacy) {
        /* NeXTSTEP's 4.3BSD FFS records -1 for the old inode and
         * rotational-table formats. Its cached fshift field does not
         * describe fs_fsize, so geometry below derives it instead. */
        if (xx_data_get_u32(sb + 0x550, 4, 0, big) != UINT32_MAX ||
            xx_data_get_u32(sb + 0x52C, 4, 0, big) != UINT32_MAX || !big)
            return false;
    } else if (xx_data_get_u32(sb + 0x550, 4, 0, big) != SUFS_NRPOS ||
               xx_data_get_u32(sb + 0x52C, 4, 0, big) == SUFS_BSD44_INODEFMT)
        return false;
    geo->big = big;
    geo->legacy = ((xx_sufs *)self)->nextstep_legacy;
    geo->base = self->base_address;
    geo->iblkno = xx_data_get_u32(sb + 0x10, 4, 0, big);
    geo->cgoffset = xx_data_get_u32(sb + 0x18, 4, 0, big);
    geo->cgmask = xx_data_get_u32(sb + 0x1C, 4, 0, big);
    geo->size = xx_data_get_u32(sb + 0x24, 4, 0, big);
    geo->ncg = xx_data_get_u32(sb + 0x2C, 4, 0, big);
    geo->bsize = xx_data_get_u32(sb + 0x30, 4, 0, big);
    geo->fsize = xx_data_get_u32(sb + 0x34, 4, 0, big);
    geo->frag = xx_data_get_u32(sb + 0x38, 4, 0, big);
    geo->nindir = xx_data_get_u32(sb + 0x74, 4, 0, big);
    geo->ipg = xx_data_get_u32(sb + 0xB8, 4, 0, big);
    geo->fpg = xx_data_get_u32(sb + 0xBC, 4, 0, big);
    if (!sufs_pow2(geo->bsize) || geo->bsize < 4096U || geo->bsize > 65536U ||
        !sufs_pow2(geo->fsize) || geo->fsize < 512U ||
        geo->fsize > geo->bsize || geo->frag != geo->bsize / geo->fsize ||
        geo->frag > 8U ||
        xx_data_get_u32(sb + 0x50, 4, 0, big) != sufs_log2(geo->bsize) ||
        (!((xx_sufs *)self)->nextstep_legacy &&
         xx_data_get_u32(sb + 0x54, 4, 0, big) != sufs_log2(geo->fsize)) ||
        geo->nindir != geo->bsize / 4U ||
        xx_data_get_u32(sb + 0x78, 4, 0, big) != geo->bsize / SUFS_INODE_SIZE)
        return false;
    if (geo->ncg == 0U || geo->ipg == 0U || geo->fpg == 0U ||
        geo->size == 0U || geo->fpg % geo->frag != 0U ||
        geo->ipg % (geo->bsize / SUFS_INODE_SIZE) != 0U)
        return false;
    /* ncg groups of fpg fragments cover fs_size, the last one partially. */
    if ((uint64_t)geo->ncg * geo->fpg < geo->size ||
        (uint64_t)(geo->ncg - 1U) * geo->fpg >= geo->size)
        return false;
    /* The inode table fits inside one group. */
    inode_frags = ((uint64_t)geo->ipg * SUFS_INODE_SIZE + geo->fsize - 1U) /
                  geo->fsize;
    if ((uint64_t)geo->iblkno + inode_frags > geo->fpg) return false;
    geo->fs_bytes = geo->size * geo->fsize;
    if (geo->fs_bytes > (uint64_t)(total - self->base_address)) return false;
    return true;
}

static bool sufs_read_inode(xx_io_device *device, const sufs_geometry *geo,
                            uint32_t number, sufs_inode *inode) {
    uint8_t raw[SUFS_INODE_SIZE];
    uint32_t group;
    uint64_t offset;
    unsigned index;
    xx_mem_zero(inode, sizeof(*inode));
    if (number == 0U || (uint64_t)number >= (uint64_t)geo->ncg * geo->ipg)
        return false;
    group = number / geo->ipg;
    /* cgstart stays below 2^64 (group * fpg < 2^33, the rotation term is a
     * product of two 32-bit values); bound it before scaling to bytes. */
    offset = sufs_cgstart(geo, group);
    if (offset >= geo->size) return false;
    offset = (offset + geo->iblkno) * geo->fsize +
             (uint64_t)(number % geo->ipg) * SUFS_INODE_SIZE;
    if (offset > geo->fs_bytes || geo->fs_bytes - offset < SUFS_INODE_SIZE ||
        !sufs_read_at(device, geo->base + (int64_t)offset, raw, sizeof(raw)))
        return false;
    inode->mode = sufs_u16(geo->big, raw);
    inode->size = xx_data_get_u64(raw + 8, 8, 0, geo->big);
    for (index = 0U; index < SUFS_NDADDR; ++index)
        inode->db[index] = xx_data_get_u32(raw + 0x28 + index * 4U, 4, 0, geo->big);
    for (index = 0U; index < SUFS_NIADDR; ++index)
        inode->ib[index] = xx_data_get_u32(raw + 0x58 + index * 4U, 4, 0, geo->big);
    return true;
}

/* True when `bytes` bytes starting at fragment `address` lie inside the
 * filesystem. */
static bool sufs_extent_ok(const sufs_geometry *geo, uint32_t address,
                           uint64_t bytes) {
    uint64_t start = (uint64_t)address * geo->fsize;
    return address != 0U && start < geo->fs_bytes &&
           geo->fs_bytes - start >= bytes;
}

/* Entry `index` of the pointer block at fragment `address`; 0 stays a hole. */
static bool sufs_pointer(xx_io_device *device, const sufs_geometry *geo,
                         uint32_t address, uint64_t index, uint32_t *out) {
    uint8_t raw[4];
    *out = 0U;
    if (address == 0U) return true;
    if (index >= geo->nindir || !sufs_extent_ok(geo, address, geo->bsize) ||
        !sufs_read_at(device,
                      geo->base + (int64_t)((uint64_t)address * geo->fsize +
                                            index * 4U),
                      raw, 4U))
        return false;
    *out = xx_data_get_u32(raw, 4, 0, geo->big);
    return true;
}

/* Fragment address of logical block `index` of a file; 0 means a hole. */
static bool sufs_map(xx_io_device *device, const sufs_geometry *geo,
                     const sufs_inode *inode, uint64_t index, uint32_t *out) {
    uint64_t per = geo->nindir;
    uint32_t address;
    *out = 0U;
    if (index < SUFS_NDADDR) {
        address = inode->db[index];
    } else if ((index -= SUFS_NDADDR) < per) {
        if (!sufs_pointer(device, geo, inode->ib[0], index, &address))
            return false;
    } else if ((index -= per) < per * per) {
        if (!sufs_pointer(device, geo, inode->ib[1], index / per, &address) ||
            !sufs_pointer(device, geo, address, index % per, &address))
            return false;
    } else if ((index -= per * per) < per * per * per) {
        if (!sufs_pointer(device, geo, inode->ib[2], index / (per * per),
                          &address) ||
            !sufs_pointer(device, geo, address, (index / per) % per,
                          &address) ||
            !sufs_pointer(device, geo, address, index % per, &address))
            return false;
    } else {
        return false;
    }
    *out = address;
    return true;
}

/* Largest byte count a file of this filesystem may claim. */
static uint64_t sufs_max_file(const sufs_geometry *geo) {
    uint64_t per = geo->nindir;
    uint64_t blocks = SUFS_NDADDR + per + per * per + per * per * per;
    uint64_t addressable = blocks * geo->bsize;
    uint64_t sparse = geo->fs_bytes * SUFS_SPARSE_FACTOR;
    return addressable < sparse ? addressable : sparse;
}

static uint32_t sufs_hash_u32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

/* Remember a directory inode; false when it was seen before or the set can
 * no longer grow (the walk then stops descending). */
static bool sufs_mark_dir(sufs_parsed *parsed, uint32_t inode) {
    size_t slot;
    if (parsed->dir_count + 1U > parsed->dir_capacity / 2U) {
        size_t capacity = parsed->dir_capacity ? parsed->dir_capacity * 2U : 64U;
        uint32_t *slots;
        size_t index;
        if (capacity > ((size_t)SUFS_MAX_RECORDS + 1U) * 4U) return false;
        slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        if (!slots) return false;
        for (index = 0U; index < parsed->dir_capacity; ++index) {
            uint32_t v = parsed->dir_slots[index];
            if (v == 0U) continue;
            slot = sufs_hash_u32(v) & (capacity - 1U);
            while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
            slots[slot] = v;
        }
        if (parsed->dir_slots) xx_mem_free(parsed->dir_slots);
        parsed->dir_slots = slots;
        parsed->dir_capacity = capacity;
    }
    slot = sufs_hash_u32(inode) & (parsed->dir_capacity - 1U);
    while (parsed->dir_slots[slot] != 0U) {
        if (parsed->dir_slots[slot] == inode) return false;
        slot = (slot + 1U) & (parsed->dir_capacity - 1U);
    }
    parsed->dir_slots[slot] = inode;
    ++parsed->dir_count;
    return true;
}

static char sufs_fold(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static uint32_t sufs_hash_name(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)sufs_fold(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool sufs_same_name(const char *left, const char *right) {
    for (; *left && *right; ++left, ++right)
        if (sufs_fold(*left) != sufs_fold(*right)) return false;
    return *left == *right;
}

static bool sufs_name_taken(const sufs_parsed *parsed, const char *name) {
    size_t slot;
    if (!parsed->name_slots) return false;
    slot = sufs_hash_name(name) & (parsed->name_capacity - 1U);
    while (parsed->name_slots[slot] != 0U) {
        if (sufs_same_name(parsed->entries[parsed->name_slots[slot] - 1U].name,
                           name))
            return true;
        slot = (slot + 1U) & (parsed->name_capacity - 1U);
    }
    return false;
}

static bool sufs_name_insert(sufs_parsed *parsed, size_t index) {
    size_t slot;
    if ((parsed->count + 1U) * 2U > parsed->name_capacity) {
        size_t capacity = parsed->name_capacity ? parsed->name_capacity * 2U : 64U;
        uint32_t *slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        size_t old;
        if (!slots) return false;
        for (old = 0U; old < parsed->name_capacity; ++old) {
            uint32_t v = parsed->name_slots[old];
            if (v == 0U) continue;
            slot = sufs_hash_name(parsed->entries[v - 1U].name) & (capacity - 1U);
            while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
            slots[slot] = v;
        }
        if (parsed->name_slots) xx_mem_free(parsed->name_slots);
        parsed->name_slots = slots;
        parsed->name_capacity = capacity;
    }
    slot = sufs_hash_name(parsed->entries[index].name) &
           (parsed->name_capacity - 1U);
    while (parsed->name_slots[slot] != 0U)
        slot = (slot + 1U) & (parsed->name_capacity - 1U);
    parsed->name_slots[slot] = (uint32_t)index + 1U;
    return true;
}

static void sufs_parsed_cleanup(sufs_parsed *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index)
        if (parsed->entries[index].name) xx_mem_free(parsed->entries[index].name);
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->name_slots) xx_mem_free(parsed->name_slots);
    if (parsed->dir_slots) xx_mem_free(parsed->dir_slots);
    xx_mem_zero(parsed, sizeof(*parsed));
}

/* Join prefix and name into a fresh path. */
static char *sufs_join(const char *prefix, const char *name, size_t name_size,
                       const char *suffix) {
    size_t prefix_size = xx_str_len(prefix);
    size_t suffix_size = suffix ? xx_str_len(suffix) : 0U;
    size_t total = prefix_size + (prefix_size ? 1U : 0U) + name_size +
                   suffix_size;
    char *path;
    if (total >= SUFS_MAX_PATH) return NULL;
    path = (char *)xx_mem_alloc(total + 1U);
    if (!path) return NULL;
    if (prefix_size) {
        xx_mem_copy(path, prefix, prefix_size);
        path[prefix_size] = '/';
        xx_mem_copy(path + prefix_size + 1U, name, name_size);
    } else {
        xx_mem_copy(path, name, name_size);
    }
    if (suffix_size)
        xx_mem_copy(path + total - suffix_size, suffix, suffix_size);
    path[total] = '\0';
    return path;
}

/* Append a record, renaming a later duplicate (compared case-insensitively
 * so that nothing overwrites an earlier member on a case-folding host) to
 * "name~2", "name~3", ... Returns the stored path or NULL when skipped. */
static const char *sufs_add(sufs_parsed *parsed, const char *prefix,
                            const char *name, size_t name_size,
                            uint32_t inode, uint64_t size, bool folder,
                            int64_t data_offset) {
    char *path = sufs_join(prefix, name, name_size, NULL);
    unsigned attempt;
    sufs_entry *entry;
    if (!path) return NULL;
    for (attempt = 2U; sufs_name_taken(parsed, path); ++attempt) {
        char suffix[4];
        size_t used = 0U;
        xx_mem_free(path);
        if (attempt > 99U) return NULL;
        suffix[used++] = '~';
        if (attempt >= 10U) suffix[used++] = (char)('0' + attempt / 10U);
        suffix[used++] = (char)('0' + attempt % 10U);
        suffix[used] = '\0';
        path = sufs_join(prefix, name, name_size, suffix);
        if (!path) return NULL;
    }
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        sufs_entry *grown = (sufs_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) {
            xx_mem_free(path);
            return NULL;
        }
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    entry = &parsed->entries[parsed->count];
    entry->name = path;
    entry->inode = inode;
    entry->size = folder ? 0U : size;
    entry->is_folder = folder;
    entry->data_offset = data_offset;
    if (!sufs_name_insert(parsed, parsed->count)) {
        xx_mem_free(path);
        return NULL;
    }
    ++parsed->count;
    return path;
}

/* A directory entry name is one path component: separators and control
 * bytes make it implausible, and such entries are dropped. */
static bool sufs_plausible_name(const uint8_t *name, size_t size) {
    size_t index;
    if (size == 0U) return false;
    if (name[0] == '.' && (size == 1U || (size == 2U && name[1] == '.')))
        return false;
    for (index = 0U; index < size; ++index)
        if (name[index] < 0x20U || name[index] == 0x7FU || name[index] == '/')
            return false;
    return true;
}

static void sufs_walk(xx_io_device *device, sufs_parsed *parsed,
                      const sufs_inode *dir, const char *prefix,
                      unsigned depth, xx_pd_struct *pd);

/* Handle one live directory entry of the directory at `prefix`. */
static void sufs_visit(xx_io_device *device, sufs_parsed *parsed,
                       uint32_t number, const uint8_t *name, size_t name_size,
                       const char *prefix, unsigned depth, xx_pd_struct *pd) {
    const sufs_geometry *geo = &parsed->geo;
    sufs_inode child;
    uint32_t type;
    if (!sufs_plausible_name(name, name_size) ||
        !sufs_read_inode(device, geo, number, &child))
        return;
    type = child.mode & SUFS_S_IFMT;
    if (type == SUFS_S_IFREG) {
        uint32_t first = 0U;
        int64_t offset = -1;
        if (child.size > sufs_max_file(geo)) return;
        if (child.size != 0U && sufs_map(device, geo, &child, 0U, &first) &&
            first != 0U && sufs_extent_ok(geo, first, 1U))
            offset = geo->base + (int64_t)((uint64_t)first * geo->fsize);
        (void)sufs_add(parsed, prefix, (const char *)name, name_size, number,
                       child.size, false, offset);
    } else if (type == SUFS_S_IFDIR) {
        const char *path;
        if (child.size > geo->fs_bytes || !sufs_mark_dir(parsed, number))
            return;
        /* The path string is owned by its entry and outlives the walk even
         * when the entries array itself is reallocated. */
        path = sufs_add(parsed, prefix, (const char *)name, name_size, number,
                        0U, true, -1);
        if (path) sufs_walk(device, parsed, &child, path, depth + 1U, pd);
    }
    /* Symbolic links, devices, fifos, sockets, shadow and attribute
     * inodes are skipped. */
}

/* Walk one directory: logical block by logical block, each block split
 * into 512-byte chunks whose entries must not cross the chunk. A malformed
 * entry abandons the rest of its chunk, like the kernel does. */
static void sufs_walk(xx_io_device *device, sufs_parsed *parsed,
                      const sufs_inode *dir, const char *prefix,
                      unsigned depth, xx_pd_struct *pd) {
    const sufs_geometry *geo = &parsed->geo;
    uint64_t blocks, logical;
    uint8_t *block;
    if (depth > SUFS_MAX_DEPTH || dir->size > geo->fs_bytes) return;
    /* Each level owns its block buffer: the walk recurses while the
     * block is still being scanned. */
    block = (uint8_t *)xx_mem_alloc(geo->bsize);
    if (!block) return;
    blocks = (dir->size + geo->bsize - 1U) / geo->bsize;
    for (logical = 0U; logical < blocks; ++logical) {
        uint32_t address;
        uint64_t length = dir->size - logical * geo->bsize;
        uint64_t chunk;
        if (length > geo->bsize) length = geo->bsize;
        if (parsed->count >= SUFS_MAX_RECORDS || (pd && xx_pd_is_stopped(pd)) ||
            parsed->dir_bytes_left < length)
            break;
        parsed->dir_bytes_left -= length;
        if (!sufs_map(device, geo, dir, logical, &address)) break;
        if (address == 0U) continue;
        if (!sufs_extent_ok(geo, address, length) ||
            !sufs_read_at(device,
                          geo->base + (int64_t)((uint64_t)address * geo->fsize),
                          block, (size_t)length))
            break;
        /* NeXT's directory chunks are 1024 bytes; its final live entry may
         * span byte 512 and must not be mistaken for corrupt directory data. */
        uint64_t dir_chunk = geo->legacy ? 1024U : SUFS_DIRBLKSIZ;
        for (chunk = 0U; chunk < length; chunk += dir_chunk) {
            uint64_t end = chunk + dir_chunk;
            uint64_t pos = chunk;
            if (end > length) end = length;
            while (end - pos >= SUFS_DIRENT_HEADER) {
                const uint8_t *entry = block + pos;
                uint32_t number = xx_data_get_u32(entry, 4, 0, geo->big);
                uint32_t reclen = sufs_u16(geo->big, entry + 4);
                uint32_t namlen = sufs_u16(geo->big, entry + 6);
                if (reclen < SUFS_DIRENT_HEADER || (reclen & 3U) != 0U ||
                    reclen > end - pos || namlen > SUFS_MAX_NAME ||
                    namlen > reclen - SUFS_DIRENT_HEADER)
                    break;
                if (number != 0U) {
                    size_t size = 0U;
                    /* The name ends at namlen or at an embedded NUL. */
                    while (size < namlen && entry[SUFS_DIRENT_HEADER + size])
                        ++size;
                    if (size == namlen)
                        sufs_visit(device, parsed, number,
                                   entry + SUFS_DIRENT_HEADER, size, prefix,
                                   depth, pd);
                    else if (geo->legacy) {
                        uint8_t safe_name[SUFS_MAX_NAME];
                        size_t i;
                        /* NeXT's old directory may count trailing NUL padding
                         * in namlen. Preserve the claimed width by replacing
                         * those bytes with portable underscores, as U3 does. */
                        for (i = 0U; i < namlen; ++i) {
                            uint8_t c = entry[SUFS_DIRENT_HEADER + i];
                            safe_name[i] = c ? c : (uint8_t)'_';
                        }
                        sufs_visit(device, parsed, number, safe_name, namlen,
                                   prefix, depth, pd);
                    }
                    if (parsed->count >= SUFS_MAX_RECORDS) break;
                }
                pos += reclen;
            }
        }
    }
    xx_mem_free(block);
}

/* Quick structural check used by the probe: superblock plus a root
 * directory whose first entries are "." and ".." naming inode 2. */
static bool sufs_check(Abstractformat *self, sufs_geometry *geo,
                       sufs_inode *root) {
    uint8_t raw[24];
    if (!sufs_read_geometry(self, geo) ||
        !sufs_read_inode(self->device, geo, SUFS_ROOT_INODE, root) ||
        (root->mode & SUFS_S_IFMT) != SUFS_S_IFDIR ||
        root->size < sizeof(raw) || root->size > geo->fs_bytes ||
        !sufs_extent_ok(geo, root->db[0], sizeof(raw)) ||
        !sufs_read_at(self->device,
                      geo->base + (int64_t)((uint64_t)root->db[0] * geo->fsize),
                      raw, sizeof(raw)))
        return false;
    /* "."  : ino 2, reclen 12, namlen 1, ".\0"  */
    /* ".." : ino 2, namlen 2, "..\0"            */
    return xx_data_get_u32(raw, 4, 0, geo->big) == SUFS_ROOT_INODE &&
           sufs_u16(geo->big, raw + 4) == 12U &&
           sufs_u16(geo->big, raw + 6) == 1U && raw[8] == '.' &&
           raw[9] == 0U && xx_data_get_u32(raw + 12, 4, 0, geo->big) == SUFS_ROOT_INODE &&
           sufs_u16(geo->big, raw + 18) == 2U && raw[20] == '.' &&
           raw[21] == '.' && raw[22] == 0U;
}

static bool sufs_parse(Abstractformat *self, sufs_parsed *parsed,
                       xx_pd_struct *pd) {
    sufs_inode root;
    xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || (pd && xx_pd_is_stopped(pd)) ||
        !sufs_check(self, &parsed->geo, &root)) {
        return false;
    }
    parsed->dir_bytes_left = SUFS_MAX_DIR_BYTES;
    if (!sufs_mark_dir(parsed, SUFS_ROOT_INODE)) {
        sufs_parsed_cleanup(parsed);
        return false;
    }
    sufs_walk(self->device, parsed, &root, "", 0U, pd);
    if (pd && xx_pd_is_stopped(pd)) {
        sufs_parsed_cleanup(parsed);
        return false;
    }
    return true;
}

/* Extraction writes <base>/<path>. Every component must stay inside the
 * destination on every host: no absolute or drive paths, no "." or "..",
 * nothing Windows would resolve to them (only dots and spaces, a trailing
 * dot or space), no reserved punctuation or control bytes, and no device
 * names such as CON, LPT1.TXT or CONIN$ in any case. */
static bool sufs_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index, k;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        for (k = 0U; k < stem; ++k) {
            char c = name[k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (!word[k] || c != word[k]) break;
        }
        if (k == stem && word[k] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = sufs_fold(name[0]), b = sufs_fold(name[1]),
             c = sufs_fold(name[2]);
        if ((a == 'c' && b == 'o' && c == 'm') ||
            (a == 'l' && b == 'p' && c == 't'))
            return true;
    }
    return false;
}

static bool sufs_safe_component(const char *name, size_t length) {
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    return !sufs_is_device_stem(name, stem);
}

static bool sufs_safe_path(const char *path) {
    const char *start = path;
    const char *cursor;
    if (!path || !path[0] || path[0] == '/') return false;
    for (cursor = path;; ++cursor) {
        if (*cursor == '/' || *cursor == 0) {
            if (!sufs_safe_component(start, (size_t)(cursor - start)))
                return false;
            if (*cursor == 0) return true;
            start = cursor + 1;
        }
    }
}

static bool sufs_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *sufs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sufs_set_record(xx_archive_record *record,
                            const sufs_entry *entry) {
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
                                           entry->is_folder);
}

/* Stream one regular file (holes as zeros) to destination, or only verify
 * that it is readable when destination is NULL. */
static bool sufs_copy_file(xx_io_device *device, const sufs_geometry *geo,
                           uint32_t number, xx_io_device *destination,
                           xx_pd_struct *pd) {
    sufs_inode inode;
    uint8_t *buffer;
    uint64_t remaining, logical = 0U;
    bool ok = true;
    if (!sufs_read_inode(device, geo, number, &inode) ||
        (inode.mode & SUFS_S_IFMT) != SUFS_S_IFREG ||
        inode.size > sufs_max_file(geo)) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(geo->bsize);
    if (!buffer) return false;
    remaining = inode.size;
    while (remaining != 0U) {
        uint32_t address;
        size_t part = (size_t)(remaining < geo->bsize ? remaining : geo->bsize);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !sufs_map(device, geo, &inode, logical, &address)) {
            ok = false;
            break;
        }
        if (address == 0U) {
            xx_mem_zero(buffer, part);
        } else if (!sufs_extent_ok(geo, address, part) ||
                   !sufs_read_at(device,
                                 geo->base + (int64_t)((uint64_t)address *
                                                       geo->fsize),
                                 buffer, part)) {
            ok = false;
            break;
        }
        if (destination &&
            xx_io_write(destination, buffer, part) != (ssize_t)part) {
            ok = false;
            break;
        }
        remaining -= part;
        ++logical;
    }
    xx_mem_free(buffer);
    return ok;
}

void xx_sufs_init(xx_sufs *sufs, xx_io_device *dev, int64_t base_address) {
    if (!sufs) return;
    xx_mem_zero(sufs, sizeof(*sufs));
    xx_format_init(&sufs->format, dev, base_address);
    sufs->format.endian = XX_ENDIAN_LITTLE;
    sufs->format.file_type = XX_SUFS_FILE_TYPE;
    sufs->format.format_type = XX_TYPE_ARCHIVE;
    sufs->format.is_archive = true;
    xx_format_set_mime_type(&sufs->format, "application/x-ufs");
    xx_format_set_extension(&sufs->format, "img");
    sufs->format.check_is_valid = xx_sufs_check_is_valid;
    sufs->format.handle_base_info = xx_sufs_handle_base_info;
    sufs->format.get_format_size = xx_sufs_get_format_size;
    sufs->format.get_number_of_archive_records =
        xx_sufs_get_number_of_archive_records;
    sufs->format.create_archive_records_reading =
        xx_sufs_create_archive_records_reading;
    sufs->format.get_current_archive_record =
        xx_sufs_get_current_archive_record;
    sufs->format.unpack_current_archive_record =
        xx_sufs_unpack_current_archive_record;
    sufs->format.archive_record_move_to_next =
        xx_sufs_archive_record_move_to_next;
    sufs->format.free_archive_records_reading =
        xx_sufs_free_archive_records_reading;
    sufs->format.destroy = sufs_vtable_destroy;
}

xx_sufs *xx_sufs_create(xx_io_device *dev, int64_t base_address) {
    xx_sufs *sufs = (xx_sufs *)xx_mem_alloc(sizeof(*sufs));
    if (sufs) xx_sufs_init(sufs, dev, base_address);
    return sufs;
}

void xx_sufs_destroy(xx_sufs *sufs) {
    if (!sufs) return;
    if (sufs->internal) {
        sufs_parsed_cleanup((sufs_parsed *)sufs->internal);
        xx_mem_free(sufs->internal);
        sufs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&sufs->format);
}

static void sufs_vtable_destroy(Abstractformat *self) {
    xx_sufs_destroy((xx_sufs *)self);
}

void xx_sufs_free(xx_sufs *sufs) {
    if (!sufs) return;
    xx_sufs_destroy(sufs);
    xx_mem_free(sufs);
}
void xx_sufs_enable_nextstep_legacy(xx_sufs *sufs) {
    if (sufs && !sufs->format.base_info_handled)
        sufs->nextstep_legacy = true;
}

bool xx_sufs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    sufs_geometry geo;
    sufs_inode root;
    (void)pd;
    return self && sufs_check(self, &geo, &root);
}

bool xx_sufs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sufs *sufs = (xx_sufs *)self;
    sufs_parsed *parsed;
    int64_t total, end;
    if (!self) return false;
    parsed = (sufs_parsed *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed) return false;
    if (!sufs_parse(self, parsed, pd)) {
        xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (sufs->internal) {
        sufs_parsed_cleanup((sufs_parsed *)sufs->internal);
        xx_mem_free(sufs->internal);
    }
    sufs->internal = parsed;
    sufs->number_of_records = parsed->count;
    sufs->block_size = parsed->geo.bsize;
    sufs->fragment_size = parsed->geo.fsize;
    sufs->group_count = parsed->geo.ncg;
    sufs->fragment_count = parsed->geo.size;
    sufs->big_endian = parsed->geo.big;
    self->endian = parsed->geo.big ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = (int64_t)parsed->geo.fs_bytes;
    end = self->base_address + (int64_t)parsed->geo.fs_bytes;
    total = xx_io_total_size(self->device);
    if (total > end) {
        self->overlay_offset = end;
        self->overlay_size = total - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_sufs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_sufs_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_sufs *)self)->number_of_records;
}

static void sufs_stream_free(void *pointer) {
    sufs_stream *stream = (sufs_stream *)pointer;
    if (!stream) return;
    sufs_parsed_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

xx_archive_record_state *xx_sufs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    sufs_stream *stream;
    if (!self || !self->device) return NULL;
    stream = (sufs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!sufs_parse(self, &stream->parsed, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sufs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = sufs_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (!sufs_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->parsed.count != 0U) {
        if (!sufs_set_record(&state->current_record,
                             &stream->parsed.entries[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_sufs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sufs_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    sufs_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (sufs_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (++stream->index >= stream->parsed.count ||
        !sufs_set_record(&state->current_record,
                         &stream->parsed.entries[stream->index])) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_sufs_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    sufs_stream *stream;
    const sufs_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record ||
        !(stream = (sufs_stream *)state->internal_state) ||
        stream->index >= stream->parsed.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->parsed.entries[stream->index];
    option = sufs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        return entry->is_folder ||
               sufs_copy_file(self->device, &stream->parsed.geo, entry->inode,
                              NULL, pd);
    }
    if (!sufs_safe_path(entry->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", entry->name)
               : xx_str_concat(base, entry->name);
    if (!path) goto done;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = sufs_copy_file(self->device, &stream->parsed.geo,
                                entry->inode, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sufs_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_sufs_get_number_of_records(const xx_sufs *sufs) {
    return sufs ? sufs->number_of_records : 0U;
}
uint32_t xx_sufs_get_block_size(const xx_sufs *sufs) {
    return sufs ? sufs->block_size : 0U;
}
uint32_t xx_sufs_get_fragment_size(const xx_sufs *sufs) {
    return sufs ? sufs->fragment_size : 0U;
}
bool xx_sufs_is_big_endian(const xx_sufs *sufs) {
    return sufs ? sufs->big_endian : false;
}

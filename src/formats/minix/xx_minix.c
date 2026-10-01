/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MINIX filesystem v1/v2/v3 reader; xx_minix.h carries the layout.
 *
 * The superblock checks follow the ones unblob applies before it accepts a
 * MINIX image (unblob/handlers/filesystem/minixfs.py, MIT licence): the
 * counts must be non-zero, the first data zone must be exactly where the
 * bitmaps and the inode table end, and the zone count must fit in the file.
 * The traversal itself is written from the on-disk layout. Unlike that
 * reference, a zero zone pointer inside a file is read as a hole (zeros)
 * rather than as the end of the file, which is what MINIX and Linux do.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/minix/xx_minix.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as MINIX is registered there. */
#ifdef MINIX
#define XX_MINIX_FILE_TYPE XX_FILE_TYPE_MINIX
#else
#define XX_MINIX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MINIX_SUPER_OFFSET 1024
#define MINIX_SUPER_SIZE 32
#define MINIX_ROOT_INODE 1U
#define MINIX_MAX_DEPTH 64U
#define MINIX_MAX_RECORDS 100000U
/* Directory slots examined over one whole parse. Several hostile
 * directories may share the same zones, so the slots are budgeted. */
#define MINIX_MAX_SLOTS 2000000U
#define MINIX_MAX_PATH 4096U
#define MINIX_MAX_LOG_ZONE 4U
#define MINIX_COPY_CHUNK 65536U

#define MINIX_S_IFMT 0170000U
#define MINIX_S_IFDIR 0040000U
#define MINIX_S_IFREG 0100000U

typedef struct minix_geometry_s {
    int64_t base;
    int64_t fs_size;       /**< zones * zone_size. */
    int64_t inode_table;   /**< Absolute offset of inode 1. */
    uint32_t version;
    uint32_t name_length;
    uint32_t entry_size;   /**< Directory slot: inode field + name. */
    uint32_t block_size;
    uint32_t zone_size;
    uint32_t inode_size;
    uint32_t pointer_size;
    uint32_t pointers_per_block;
    uint32_t ninodes;
    uint32_t zones;
    uint32_t first_data_zone;
    bool big;
} minix_geometry;

typedef struct minix_inode_s {
    uint32_t mode;
    uint32_t size;
    uint32_t zone[10];
} minix_inode;

typedef struct minix_entry_s {
    char *name;
    int64_t data_offset;  /**< First mapped data zone, or -1. */
    uint32_t inode;
    uint32_t size;
    bool is_folder;
} minix_entry;

typedef struct minix_parsed_s {
    minix_geometry geo;
    minix_entry *entries;
    size_t count;
    size_t capacity;
    uint32_t *name_slots;   /**< Case-folded path set: entry index + 1. */
    size_t name_capacity;
    uint32_t *dir_slots;    /**< Visited directory inodes (+0 = empty). */
    size_t dir_capacity;
    size_t dir_count;
    size_t slots_left;
} minix_parsed;

typedef struct minix_stream_s {
    minix_parsed parsed;
    size_t index;
} minix_stream;

static void minix_vtable_destroy(Abstractformat *self);

static bool minix_read_at(xx_io_device *device, int64_t offset, void *data,
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

static uint32_t minix_u16(const minix_geometry *geo, const uint8_t *p) {
    return geo->big ? ((uint32_t)p[0] << 8) | p[1]
                    : ((uint32_t)p[1] << 8) | p[0];
}

static uint32_t minix_u32(const minix_geometry *geo, const uint8_t *p) {
    return geo->big ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                          ((uint32_t)p[2] << 8) | p[3]
                    : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[1] << 8) | p[0];
}

static uint16_t minix_raw16(const uint8_t *p, bool big) {
    return big ? (uint16_t)(((unsigned)p[0] << 8) | p[1])
               : (uint16_t)(((unsigned)p[1] << 8) | p[0]);
}

/* Decode and validate the superblock. Everything later relies on these
 * bounds: ninodes and zones fit the file, the inode table lies before the
 * first data zone and every block size is a sane power of two. */
static bool minix_read_geometry(Abstractformat *self, minix_geometry *geo) {
    uint8_t sb[MINIX_SUPER_SIZE];
    int64_t total;
    uint32_t imap, zmap, log_zone, inodes_per_block;
    uint64_t inode_blocks, first_block, blocks_per_zone, first_zone;
    uint64_t fs_size;
    unsigned order;
    xx_mem_zero(geo, sizeof(*geo));
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < 0 || total < self->base_address ||
        total - self->base_address < MINIX_SUPER_OFFSET + MINIX_SUPER_SIZE ||
        !minix_read_at(self->device, self->base_address + MINIX_SUPER_OFFSET,
                       sb, sizeof(sb))) {
        return false;
    }
    geo->base = self->base_address;
    for (order = 0U; order < 2U && geo->version == 0U; ++order) {
        bool big = order == 1U;
        uint16_t magic = minix_raw16(sb + 16, big);
        uint16_t state = minix_raw16(sb + 18, big);
        if (magic == 0x137FU || magic == 0x138FU || magic == 0x2468U ||
            magic == 0x2478U) {
            if (state > 3U) return false;
            geo->version = (magic == 0x137FU || magic == 0x138FU) ? 1U : 2U;
            geo->name_length =
                (magic == 0x137FU || magic == 0x2468U) ? 14U : 30U;
            geo->big = big;
        } else if (minix_raw16(sb + 24, big) == 0x4D5AU) {
            geo->version = 3U;
            geo->name_length = 60U;
            geo->big = big;
        }
    }
    if (geo->version == 0U) return false;
    if (geo->version < 3U) {
        geo->ninodes = minix_u16(geo, sb + 0);
        imap = minix_u16(geo, sb + 4);
        zmap = minix_u16(geo, sb + 6);
        geo->first_data_zone = minix_u16(geo, sb + 8);
        log_zone = minix_u16(geo, sb + 10);
        if (minix_u32(geo, sb + 12) == 0U) return false;
        geo->zones = geo->version == 1U ? minix_u16(geo, sb + 2)
                                        : minix_u32(geo, sb + 20);
        geo->block_size = 1024U;
    } else {
        geo->ninodes = minix_u32(geo, sb + 0);
        imap = minix_u16(geo, sb + 6);
        zmap = minix_u16(geo, sb + 8);
        geo->first_data_zone = minix_u16(geo, sb + 10);
        log_zone = minix_u16(geo, sb + 12);
        if (minix_u32(geo, sb + 16) == 0U) return false;
        geo->zones = minix_u32(geo, sb + 20);
        geo->block_size = minix_u16(geo, sb + 28);
        if (geo->block_size < 1024U || geo->block_size > 32768U ||
            (geo->block_size & (geo->block_size - 1U)) != 0U) {
            return false;
        }
    }
    if (geo->ninodes == 0U || imap == 0U || zmap == 0U || geo->zones == 0U ||
        log_zone > MINIX_MAX_LOG_ZONE) {
        return false;
    }
    geo->inode_size = geo->version == 1U ? 32U : 64U;
    geo->pointer_size = geo->version == 1U ? 2U : 4U;
    geo->entry_size = geo->name_length + (geo->version == 3U ? 4U : 2U);
    geo->pointers_per_block = geo->block_size / geo->pointer_size;
    geo->zone_size = geo->block_size << log_zone;
    inodes_per_block = geo->block_size / geo->inode_size;
    inode_blocks = ((uint64_t)geo->ninodes + inodes_per_block - 1U) /
                   inodes_per_block;
    first_block = 2U + (uint64_t)imap + zmap + inode_blocks;
    blocks_per_zone = (uint64_t)1U << log_zone;
    first_zone = (first_block + blocks_per_zone - 1U) / blocks_per_zone;
    if (geo->first_data_zone != first_zone ||
        geo->first_data_zone >= geo->zones) {
        return false;
    }
    /* Both bitmaps must be large enough for what they describe (bit 0 of
     * each is reserved). */
    if ((uint64_t)imap * geo->block_size * 8U < (uint64_t)geo->ninodes + 1U ||
        (uint64_t)zmap * geo->block_size * 8U <
            (uint64_t)geo->zones - geo->first_data_zone + 1U) {
        return false;
    }
    fs_size = (uint64_t)geo->zones * geo->zone_size;
    if (fs_size > (uint64_t)(total - self->base_address)) return false;
    geo->fs_size = (int64_t)fs_size;
    geo->inode_table =
        geo->base + (int64_t)((2U + (uint64_t)imap + zmap) * geo->block_size);
    return true;
}

static bool minix_read_inode(xx_io_device *device, const minix_geometry *geo,
                             uint32_t number, minix_inode *inode) {
    uint8_t raw[64];
    unsigned index;
    xx_mem_zero(inode, sizeof(*inode));
    if (number == 0U || number > geo->ninodes ||
        !minix_read_at(device,
                       geo->inode_table +
                           (int64_t)(number - 1U) * geo->inode_size,
                       raw, geo->inode_size)) {
        return false;
    }
    inode->mode = minix_u16(geo, raw);
    if (geo->version == 1U) {
        inode->size = minix_u32(geo, raw + 4);
        for (index = 0U; index < 9U; ++index)
            inode->zone[index] = minix_u16(geo, raw + 14U + index * 2U);
    } else {
        inode->size = minix_u32(geo, raw + 8);
        for (index = 0U; index < 10U; ++index)
            inode->zone[index] = minix_u32(geo, raw + 24U + index * 4U);
    }
    return true;
}

static bool minix_zone_ok(const minix_geometry *geo, uint32_t zone) {
    return zone >= geo->first_data_zone && zone < geo->zones;
}

static int64_t minix_zone_offset(const minix_geometry *geo, uint32_t zone) {
    return geo->base + (int64_t)zone * geo->zone_size;
}

/* Entry `index` of the pointer block in `zone`; 0 stays a hole. */
static bool minix_pointer(xx_io_device *device, const minix_geometry *geo,
                          uint32_t zone, uint64_t index, uint32_t *out) {
    uint8_t raw[4];
    *out = 0U;
    if (zone == 0U) return true;
    if (!minix_zone_ok(geo, zone) || index >= geo->pointers_per_block ||
        !minix_read_at(device,
                       minix_zone_offset(geo, zone) +
                           (int64_t)(index * geo->pointer_size),
                       raw, geo->pointer_size)) {
        return false;
    }
    *out = geo->pointer_size == 2U ? minix_u16(geo, raw) : minix_u32(geo, raw);
    return true;
}

/* Physical zone for logical zone `index` of a file; 0 means a hole. */
static bool minix_map(xx_io_device *device, const minix_geometry *geo,
                      const minix_inode *inode, uint64_t index,
                      uint32_t *out) {
    uint64_t per = geo->pointers_per_block;
    uint32_t zone;
    *out = 0U;
    if (index < 7U) {
        zone = inode->zone[index];
    } else if ((index -= 7U) < per) {
        if (!minix_pointer(device, geo, inode->zone[7], index, &zone))
            return false;
    } else if ((index -= per) < per * per) {
        if (!minix_pointer(device, geo, inode->zone[8], index / per, &zone) ||
            !minix_pointer(device, geo, zone, index % per, &zone))
            return false;
    } else if (geo->version >= 2U && (index -= per * per) < per * per * per) {
        if (!minix_pointer(device, geo, inode->zone[9], index / (per * per),
                           &zone) ||
            !minix_pointer(device, geo, zone, (index / per) % per, &zone) ||
            !minix_pointer(device, geo, zone, index % per, &zone))
            return false;
    } else {
        return false;
    }
    if (zone != 0U && !minix_zone_ok(geo, zone)) return false;
    *out = zone;
    return true;
}

static uint32_t minix_hash_u32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

/* Remember a directory inode; false when it was seen before or the set can
 * no longer grow (the walk then stops descending). */
static bool minix_mark_dir(minix_parsed *parsed, uint32_t inode) {
    size_t slot;
    if (parsed->dir_count + 1U > parsed->dir_capacity / 2U) {
        size_t capacity = parsed->dir_capacity ? parsed->dir_capacity * 2U : 64U;
        uint32_t *slots;
        size_t index;
        if (capacity > ((size_t)MINIX_MAX_RECORDS + 1U) * 4U) return false;
        slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        if (!slots) return false;
        for (index = 0U; index < parsed->dir_capacity; ++index) {
            uint32_t v = parsed->dir_slots[index];
            if (v == 0U) continue;
            slot = minix_hash_u32(v) & (capacity - 1U);
            while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
            slots[slot] = v;
        }
        if (parsed->dir_slots) xx_mem_free(parsed->dir_slots);
        parsed->dir_slots = slots;
        parsed->dir_capacity = capacity;
    }
    slot = minix_hash_u32(inode) & (parsed->dir_capacity - 1U);
    while (parsed->dir_slots[slot] != 0U) {
        if (parsed->dir_slots[slot] == inode) return false;
        slot = (slot + 1U) & (parsed->dir_capacity - 1U);
    }
    parsed->dir_slots[slot] = inode;
    ++parsed->dir_count;
    return true;
}

static char minix_fold(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static uint32_t minix_hash_name(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)minix_fold(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool minix_same_name(const char *left, const char *right) {
    for (; *left && *right; ++left, ++right)
        if (minix_fold(*left) != minix_fold(*right)) return false;
    return *left == *right;
}

static bool minix_name_taken(const minix_parsed *parsed, const char *name) {
    size_t slot;
    if (!parsed->name_slots) return false;
    slot = minix_hash_name(name) & (parsed->name_capacity - 1U);
    while (parsed->name_slots[slot] != 0U) {
        if (minix_same_name(parsed->entries[parsed->name_slots[slot] - 1U].name,
                            name))
            return true;
        slot = (slot + 1U) & (parsed->name_capacity - 1U);
    }
    return false;
}

static bool minix_name_insert(minix_parsed *parsed, size_t index) {
    size_t slot;
    if ((parsed->count + 1U) * 2U > parsed->name_capacity) {
        size_t capacity = parsed->name_capacity ? parsed->name_capacity * 2U : 64U;
        uint32_t *slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        size_t old;
        if (!slots) return false;
        for (old = 0U; old < parsed->name_capacity; ++old) {
            uint32_t v = parsed->name_slots[old];
            if (v == 0U) continue;
            slot = minix_hash_name(parsed->entries[v - 1U].name) & (capacity - 1U);
            while (slots[slot] != 0U) slot = (slot + 1U) & (capacity - 1U);
            slots[slot] = v;
        }
        if (parsed->name_slots) xx_mem_free(parsed->name_slots);
        parsed->name_slots = slots;
        parsed->name_capacity = capacity;
    }
    slot = minix_hash_name(parsed->entries[index].name) &
           (parsed->name_capacity - 1U);
    while (parsed->name_slots[slot] != 0U)
        slot = (slot + 1U) & (parsed->name_capacity - 1U);
    parsed->name_slots[slot] = (uint32_t)index + 1U;
    return true;
}

static void minix_parsed_cleanup(minix_parsed *parsed) {
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
static char *minix_join(const char *prefix, const char *name, size_t name_size,
                        const char *suffix) {
    size_t prefix_size = xx_str_len(prefix);
    size_t suffix_size = suffix ? xx_str_len(suffix) : 0U;
    size_t total = prefix_size + (prefix_size ? 1U : 0U) + name_size +
                   suffix_size;
    char *path;
    if (total >= MINIX_MAX_PATH) return NULL;
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
static const char *minix_add(minix_parsed *parsed, const char *prefix,
                             const char *name, size_t name_size,
                             uint32_t inode, const minix_inode *node,
                             bool folder, int64_t data_offset) {
    char *path = minix_join(prefix, name, name_size, NULL);
    unsigned attempt;
    minix_entry *entry;
    if (!path) return NULL;
    for (attempt = 2U; minix_name_taken(parsed, path); ++attempt) {
        char suffix[4];
        size_t used = 0U;
        xx_mem_free(path);
        if (attempt > 99U) return NULL;
        suffix[used++] = '~';
        if (attempt >= 10U) suffix[used++] = (char)('0' + attempt / 10U);
        suffix[used++] = (char)('0' + attempt % 10U);
        suffix[used] = '\0';
        path = minix_join(prefix, name, name_size, suffix);
        if (!path) return NULL;
    }
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        minix_entry *grown = (minix_entry *)xx_mem_realloc(
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
    entry->size = folder ? 0U : node->size;
    entry->is_folder = folder;
    entry->data_offset = data_offset;
    if (!minix_name_insert(parsed, parsed->count)) {
        xx_mem_free(path);
        return NULL;
    }
    ++parsed->count;
    return path;
}

/* A directory entry name is one path component: separators and control
 * bytes make it implausible, and such entries are dropped. */
static bool minix_plausible_name(const uint8_t *name, size_t size) {
    size_t index;
    if (size == 0U) return false;
    if (name[0] == '.' && (size == 1U || (size == 2U && name[1] == '.')))
        return false;
    for (index = 0U; index < size; ++index)
        if (name[index] < 0x20U || name[index] == 0x7FU || name[index] == '/')
            return false;
    return true;
}

static void minix_walk(xx_io_device *device, minix_parsed *parsed,
                       const minix_inode *dir, const char *prefix,
                       unsigned depth, xx_pd_struct *pd) {
    const minix_geometry *geo = &parsed->geo;
    uint64_t slots, slot, per_zone;
    uint32_t zone = 0U;
    uint64_t mapped = UINT64_MAX;
    uint8_t raw[64];
    if (depth > MINIX_MAX_DEPTH) return;
    if ((int64_t)dir->size > geo->fs_size) return;
    slots = dir->size / geo->entry_size;
    per_zone = geo->zone_size / geo->entry_size;
    for (slot = 0U; slot < slots; ++slot) {
        uint64_t logical = slot / per_zone;
        uint32_t child_number, name_size = 0U;
        const uint8_t *name;
        minix_inode child;
        uint32_t type;
        if (parsed->slots_left == 0U || parsed->count >= MINIX_MAX_RECORDS ||
            (pd && xx_pd_is_stopped(pd)))
            return;
        --parsed->slots_left;
        if (logical != mapped) {
            if (!minix_map(device, geo, dir, logical, &zone)) return;
            mapped = logical;
        }
        if (zone == 0U) continue;
        if (!minix_read_at(device,
                           minix_zone_offset(geo, zone) +
                               (int64_t)((slot % per_zone) * geo->entry_size),
                           raw, geo->entry_size))
            return;
        if (geo->version == 3U) {
            child_number = minix_u32(geo, raw);
            name = raw + 4;
        } else {
            child_number = minix_u16(geo, raw);
            name = raw + 2;
        }
        if (child_number == 0U) continue;
        while (name_size < geo->name_length && name[name_size] != 0U)
            ++name_size;
        if (!minix_plausible_name(name, name_size)) continue;
        if (!minix_read_inode(device, geo, child_number, &child)) continue;
        type = child.mode & MINIX_S_IFMT;
        if (type == MINIX_S_IFREG) {
            uint32_t first = 0U;
            int64_t offset = -1;
            if (child.size != 0U && minix_map(device, geo, &child, 0U, &first) &&
                first != 0U)
                offset = minix_zone_offset(geo, first);
            (void)minix_add(parsed, prefix, (const char *)name, name_size,
                            child_number, &child, false, offset);
        } else if (type == MINIX_S_IFDIR) {
            const char *path;
            if (!minix_mark_dir(parsed, child_number)) continue;
            /* The path string is owned by its entry and outlives the walk
             * even when the entries array itself is reallocated. */
            path = minix_add(parsed, prefix, (const char *)name, name_size,
                             child_number, &child, true, -1);
            if (path) minix_walk(device, parsed, &child, path, depth + 1U, pd);
        }
        /* Symbolic links, devices, fifos and sockets are skipped. */
    }
}

/* Quick structural check used by the probe: superblock plus a root
 * directory whose first slot is "." naming itself. */
static bool minix_check(Abstractformat *self, minix_geometry *geo,
                        minix_inode *root) {
    uint8_t raw[64];
    uint32_t zone, self_number;
    const uint8_t *name;
    if (!minix_read_geometry(self, geo) ||
        !minix_read_inode(self->device, geo, MINIX_ROOT_INODE, root) ||
        (root->mode & MINIX_S_IFMT) != MINIX_S_IFDIR ||
        root->size < 2U * geo->entry_size ||
        (int64_t)root->size > geo->fs_size) {
        return false;
    }
    zone = root->zone[0];
    if (!minix_zone_ok(geo, zone) ||
        !minix_read_at(self->device, minix_zone_offset(geo, zone), raw,
                       geo->entry_size)) {
        return false;
    }
    if (geo->version == 3U) {
        self_number = minix_u32(geo, raw);
        name = raw + 4;
    } else {
        self_number = minix_u16(geo, raw);
        name = raw + 2;
    }
    return self_number == MINIX_ROOT_INODE && name[0] == '.' && name[1] == 0U;
}

static bool minix_parse(Abstractformat *self, minix_parsed *parsed,
                        xx_pd_struct *pd) {
    minix_inode root;
    xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || (pd && xx_pd_is_stopped(pd)) ||
        !minix_check(self, &parsed->geo, &root)) {
        return false;
    }
    parsed->slots_left = MINIX_MAX_SLOTS;
    if (!minix_mark_dir(parsed, MINIX_ROOT_INODE)) {
        minix_parsed_cleanup(parsed);
        return false;
    }
    minix_walk(self->device, parsed, &root, "", 0U, pd);
    if (pd && xx_pd_is_stopped(pd)) {
        minix_parsed_cleanup(parsed);
        return false;
    }
    return true;
}

/* Extraction writes <base>/<path>. Every component must stay inside the
 * destination on every host: no absolute or drive paths, no "." or "..",
 * nothing Windows would resolve to them (only dots and spaces, a trailing
 * dot or space), no reserved punctuation or control bytes, and no device
 * names such as CON, LPT1.TXT or CONIN$ in any case. */
static bool minix_is_device_stem(const char *name, size_t stem) {
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
        char a = minix_fold(name[0]), b = minix_fold(name[1]),
             c = minix_fold(name[2]);
        if ((a == 'c' && b == 'o' && c == 'm') ||
            (a == 'l' && b == 'p' && c == 't'))
            return true;
    }
    return false;
}

static bool minix_safe_component(const char *name, size_t length) {
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
    return !minix_is_device_stem(name, stem);
}

static bool minix_safe_path(const char *path) {
    const char *start = path;
    const char *cursor;
    if (!path || !path[0] || path[0] == '/') return false;
    for (cursor = path;; ++cursor) {
        if (*cursor == '/' || *cursor == 0) {
            if (!minix_safe_component(start, (size_t)(cursor - start)))
                return false;
            if (*cursor == 0) return true;
            start = cursor + 1;
        }
    }
}

static bool minix_copy_options(xx_list_s *destination,
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

static const xx_var *minix_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool minix_set_record(xx_archive_record *record,
                             const minix_entry *entry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->size;
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
static bool minix_copy_file(xx_io_device *device, const minix_geometry *geo,
                            uint32_t number, xx_io_device *destination,
                            xx_pd_struct *pd) {
    minix_inode inode;
    uint8_t *buffer;
    uint64_t remaining, logical = 0U;
    bool ok = true;
    if (!minix_read_inode(device, geo, number, &inode) ||
        (inode.mode & MINIX_S_IFMT) != MINIX_S_IFREG ||
        (int64_t)inode.size > geo->fs_size) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(MINIX_COPY_CHUNK);
    if (!buffer) return false;
    remaining = inode.size;
    while (ok && remaining != 0U) {
        uint32_t zone;
        uint64_t in_zone = remaining < geo->zone_size ? remaining : geo->zone_size;
        uint64_t done = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !minix_map(device, geo, &inode, logical, &zone)) {
            ok = false;
            break;
        }
        while (done < in_zone) {
            size_t part = (size_t)(in_zone - done < MINIX_COPY_CHUNK
                                       ? in_zone - done : MINIX_COPY_CHUNK);
            if (zone == 0U) {
                xx_mem_zero(buffer, part);
            } else if (!minix_read_at(device,
                                      minix_zone_offset(geo, zone) +
                                          (int64_t)done,
                                      buffer, part)) {
                ok = false;
                break;
            }
            if (destination &&
                xx_io_write(destination, buffer, part) != (ssize_t)part) {
                ok = false;
                break;
            }
            done += part;
        }
        remaining -= in_zone;
        ++logical;
    }
    xx_mem_free(buffer);
    return ok;
}

void xx_minix_init(xx_minix *minix, xx_io_device *dev, int64_t base_address) {
    if (!minix) return;
    xx_mem_zero(minix, sizeof(*minix));
    xx_format_init(&minix->format, dev, base_address);
    minix->format.endian = XX_ENDIAN_LITTLE;
    minix->format.file_type = XX_MINIX_FILE_TYPE;
    minix->format.format_type = XX_TYPE_ARCHIVE;
    minix->format.is_archive = true;
    xx_format_set_mime_type(&minix->format, "application/x-minix-fs");
    xx_format_set_extension(&minix->format, "img");
    minix->format.check_is_valid = xx_minix_check_is_valid;
    minix->format.handle_base_info = xx_minix_handle_base_info;
    minix->format.get_format_size = xx_minix_get_format_size;
    minix->format.get_number_of_archive_records =
        xx_minix_get_number_of_archive_records;
    minix->format.create_archive_records_reading =
        xx_minix_create_archive_records_reading;
    minix->format.get_current_archive_record =
        xx_minix_get_current_archive_record;
    minix->format.unpack_current_archive_record =
        xx_minix_unpack_current_archive_record;
    minix->format.archive_record_move_to_next =
        xx_minix_archive_record_move_to_next;
    minix->format.free_archive_records_reading =
        xx_minix_free_archive_records_reading;
    minix->format.destroy = minix_vtable_destroy;
}

xx_minix *xx_minix_create(xx_io_device *dev, int64_t base_address) {
    xx_minix *minix = (xx_minix *)xx_mem_alloc(sizeof(*minix));
    if (minix) xx_minix_init(minix, dev, base_address);
    return minix;
}

void xx_minix_destroy(xx_minix *minix) {
    if (!minix) return;
    if (minix->internal) {
        minix_parsed_cleanup((minix_parsed *)minix->internal);
        xx_mem_free(minix->internal);
        minix->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&minix->format);
}

static void minix_vtable_destroy(Abstractformat *self) {
    xx_minix_destroy((xx_minix *)self);
}

void xx_minix_free(xx_minix *minix) {
    if (!minix) return;
    xx_minix_destroy(minix);
    xx_mem_free(minix);
}

bool xx_minix_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    minix_geometry geo;
    minix_inode root;
    (void)pd;
    return self && minix_check(self, &geo, &root);
}

bool xx_minix_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_minix *minix = (xx_minix *)self;
    minix_parsed *parsed;
    int64_t total, end;
    if (!self) return false;
    parsed = (minix_parsed *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed) return false;
    if (!minix_parse(self, parsed, pd)) {
        xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (minix->internal) {
        minix_parsed_cleanup((minix_parsed *)minix->internal);
        xx_mem_free(minix->internal);
    }
    minix->internal = parsed;
    minix->number_of_records = parsed->count;
    minix->version = parsed->geo.version;
    minix->name_length = parsed->geo.name_length;
    minix->block_size = parsed->geo.block_size;
    minix->zone_size = parsed->geo.zone_size;
    minix->inode_count = parsed->geo.ninodes;
    minix->zone_count = parsed->geo.zones;
    minix->big_endian = parsed->geo.big;
    self->endian = parsed->geo.big ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->geo.fs_size;
    end = self->base_address + parsed->geo.fs_size;
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

int64_t xx_minix_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_minix_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_minix *)self)->number_of_records;
}

static void minix_stream_free(void *pointer) {
    minix_stream *stream = (minix_stream *)pointer;
    if (!stream) return;
    minix_parsed_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

xx_archive_record_state *xx_minix_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    minix_stream *stream;
    if (!self || !self->device) return NULL;
    stream = (minix_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!minix_parse(self, &stream->parsed, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        minix_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = minix_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (!minix_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->parsed.count != 0U) {
        if (!minix_set_record(&state->current_record,
                              &stream->parsed.entries[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_minix_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_minix_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    minix_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (minix_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (++stream->index >= stream->parsed.count ||
        !minix_set_record(&state->current_record,
                          &stream->parsed.entries[stream->index])) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_minix_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    minix_stream *stream;
    const minix_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record ||
        !(stream = (minix_stream *)state->internal_state) ||
        stream->index >= stream->parsed.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->parsed.entries[stream->index];
    option = minix_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        return entry->is_folder ||
               minix_copy_file(self->device, &stream->parsed.geo, entry->inode,
                               NULL, pd);
    }
    if (!minix_safe_path(entry->name)) return false;
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
        result = minix_copy_file(self->device, &stream->parsed.geo,
                                 entry->inode, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_minix_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_minix_get_number_of_records(const xx_minix *minix) {
    return minix ? minix->number_of_records : 0U;
}
uint32_t xx_minix_get_version(const xx_minix *minix) {
    return minix ? minix->version : 0U;
}
uint32_t xx_minix_get_name_length(const xx_minix *minix) {
    return minix ? minix->name_length : 0U;
}
uint32_t xx_minix_get_block_size(const xx_minix *minix) {
    return minix ? minix->block_size : 0U;
}

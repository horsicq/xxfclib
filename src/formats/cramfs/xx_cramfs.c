/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cramfs/xx_cramfs.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is taken from the enumerator when it exists
 * and falls back to "unknown" until it lands. Delete this block once
 * XX_FILE_TYPE_CRAMFS is unconditionally available. */
#ifdef CRAMFS
#define XX_CRAMFS_FILE_TYPE XX_FILE_TYPE_CRAMFS
#else
#define XX_CRAMFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_CRAMFS_MAGIC UINT32_C(0x28cd3d45)
#define XX_CRAMFS_SUPERBLOCK_SIZE 76
#define XX_CRAMFS_INODE_SIZE 12
#define XX_CRAMFS_SIGNATURE_OFFSET 16U
#define XX_CRAMFS_SIGNATURE "Compressed ROMFS"
#define XX_CRAMFS_SIGNATURE_SIZE 16U

/* The block size is not stored anywhere in the image: the kernel decompresses
 * into one page at a time, so a cramfs block is PAGE_SIZE bytes and every
 * image in the wild is built for a 4 KiB page. */
#define XX_CRAMFS_BLOCK_SIZE 4096U

/* A block pointer's two flag bits, meaningful only when the superblock sets
 * CRAMFS_FLAG_EXT_BLOCK_POINTERS. */
#define XX_CRAMFS_BLK_FLAG_UNCOMPRESSED UINT32_C(0x80000000)
#define XX_CRAMFS_BLK_FLAG_DIRECT_PTR UINT32_C(0x40000000)
#define XX_CRAMFS_BLK_FLAGS \
    (XX_CRAMFS_BLK_FLAG_UNCOMPRESSED | XX_CRAMFS_BLK_FLAG_DIRECT_PTR)

/* The bits of a mode word that name the file type, and the two types that
 * carry something this reader can list or extract. */
#define XX_CRAMFS_S_IFMT 0xF000U
#define XX_CRAMFS_S_IFDIR 0x4000U
#define XX_CRAMFS_S_IFREG 0x8000U

/* An offset field is 26 bits counting 4-byte units, so no legal image reaches
 * past 256 MiB; a size field is 24 bits. Both are enforced rather than
 * trusted, since nothing in the image stops a smaller bound being exceeded. */
#define XX_CRAMFS_MAX_IMAGE_SIZE INT64_C(0x10000000)
#define XX_CRAMFS_MAX_ENTRIES 200000U
#define XX_CRAMFS_MAX_DEPTH 64U
#define XX_CRAMFS_MAX_NAME 256U
#define XX_CRAMFS_MAX_PATH 4096U
/* namelen is 6 bits of 4-byte units, so a name is at most 252 bytes. */
#define XX_CRAMFS_MAX_NAMELEN_UNITS 63U
/* A directory payload is read into memory whole; 24 bits caps it at 16 MiB,
 * which is small enough to allocate but large enough to be worth refusing
 * when it cannot possibly be backed by the file. */
#define XX_CRAMFS_MAX_DIR_SIZE (16U * 1024U * 1024U)

typedef struct xx_cramfs_inode_s {
    uint32_t mode;
    uint32_t uid;
    uint32_t size;
    uint32_t gid;
    uint32_t namelen; /**< In 4-byte units, as stored. */
    uint32_t offset;  /**< In 4-byte units, as stored. */
} xx_cramfs_inode;

typedef struct xx_cramfs_entry_s {
    char *name;
    int64_t header_offset; /**< Where the entry's inode sits in the image. */
    int64_t header_size;   /**< The inode plus its padded name. */
    int64_t data_offset;   /**< inode.offset * 4, absolute, or -1. */
    uint32_t size;         /**< Payload bytes: the file length, or 0. */
    uint32_t mode;
    bool is_folder;
} xx_cramfs_entry;

/* Open-addressing set of directory payload offsets already walked. The child
 * pointer of a directory entry is an unchecked 26-bit number, so an image can
 * point a subdirectory back at one of its own ancestors; a linear scan over
 * the entry list would be quadratic in the entry cap, so the set is a
 * power-of-two hash table holding offset + 1 (slot value 0 means empty). */
typedef struct xx_cramfs_visited_s {
    int64_t *slots;
    size_t capacity;
    size_t count;
} xx_cramfs_visited;

typedef struct xx_cramfs_private_s {
    xx_cramfs_entry *entries;
    size_t count;
    size_t capacity;
    xx_cramfs_visited visited;
    int64_t input_size;  /**< Total device size. */
    int64_t image_end;   /**< base_address + superblock size field. */
    int64_t base;        /**< The format's base address. */
    uint32_t image_size;
    uint32_t flags;
    uint32_t crc;
    uint32_t edition;
    uint32_t block_count;
    uint32_t file_count;
    bool big_endian;
} xx_cramfs_private;

typedef struct xx_cramfs_archive_stream_s {
    xx_cramfs_private parsed;
    size_t index;
} xx_cramfs_archive_stream;

static void xx_cramfs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------ plumbing --- */

static bool xx_cramfs_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_cramfs_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_cramfs_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Turn a stored 26-bit offset, which counts 4-byte units relative to the start
 * of the image, into an absolute device offset. Zero is the image's own
 * "no payload" marker and is reported as such through out_present. */
static bool xx_cramfs_resolve_offset(const xx_cramfs_private *parsed,
                                     uint32_t stored, int64_t *out_offset,
                                     bool *out_present) {
    int64_t absolute;
    if (!parsed || !out_offset || !out_present) return false;
    *out_offset = -1;
    *out_present = false;
    if (stored == 0U) return true;
    if (!xx_cramfs_add(parsed->base, (uint64_t)stored * 4U, &absolute)) {
        return false;
    }
    /* Everything an image addresses has to be inside the image, not merely
     * inside the file: trailing bytes past the superblock's size field are
     * overlay and are not part of the filesystem. */
    if (absolute < parsed->base || absolute > parsed->image_end) return false;
    *out_offset = absolute;
    *out_present = true;
    return true;
}

/* -------------------------------------------------------- visited set --- */

static void xx_cramfs_visited_cleanup(xx_cramfs_visited *visited) {
    if (!visited) return;
    if (visited->slots) xx_mem_free(visited->slots);
    xx_mem_zero(visited, sizeof(*visited));
}

static size_t xx_cramfs_visited_slot(const xx_cramfs_visited *visited,
                                     int64_t offset) {
    /* Payload offsets are 4-byte aligned, so the low two bits carry no
     * entropy; fold the rest of the value down with a 64-bit odd multiplier. */
    uint64_t key = (uint64_t)offset >> 2U;
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (visited->capacity - 1U);
}

static bool xx_cramfs_visited_grow(xx_cramfs_visited *visited) {
    int64_t *slots;
    size_t capacity = visited->capacity ? visited->capacity * 2U : 256U;
    size_t index;
    xx_cramfs_visited grown;
    if (capacity < visited->capacity ||
        capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (int64_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    grown.slots = slots;
    grown.capacity = capacity;
    grown.count = visited->count;
    for (index = 0U; index < visited->capacity; ++index) {
        int64_t stored = visited->slots[index];
        size_t slot;
        if (stored == 0) continue;
        slot = xx_cramfs_visited_slot(&grown, stored - 1);
        while (slots[slot] != 0) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = stored;
    }
    if (visited->slots) xx_mem_free(visited->slots);
    *visited = grown;
    return true;
}

/* Record offset and report whether it had already been seen. Allocation
 * failure is reported as "seen" so the traversal stops rather than looping
 * with a set that can no longer remember anything. */
static bool xx_cramfs_visited_mark(xx_cramfs_visited *visited, int64_t offset) {
    size_t slot;
    if (!visited || offset < 0) return true;
    if ((visited->count + 1U) * 4U >= visited->capacity * 3U) {
        if (!xx_cramfs_visited_grow(visited)) return true;
    }
    slot = xx_cramfs_visited_slot(visited, offset);
    while (visited->slots[slot] != 0) {
        if (visited->slots[slot] == offset + 1) return true;
        slot = (slot + 1U) & (visited->capacity - 1U);
    }
    visited->slots[slot] = offset + 1;
    ++visited->count;
    return false;
}

/* ------------------------------------------------------------- entries --- */

static void xx_cramfs_private_cleanup(xx_cramfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_cramfs_visited_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->image_end = -1;
}

static bool xx_cramfs_append_entry(xx_cramfs_private *parsed,
                                   xx_cramfs_entry *entry) {
    xx_cramfs_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_CRAMFS_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_cramfs_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though a cramfs image may legally carry it. */
static bool xx_cramfs_safe_name(const char *name) {
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

/* Parse-time check, deliberately looser than xx_cramfs_safe_name: a directory
 * entry's name is a single path component, so only control bytes, an embedded
 * separator and the two dot names make it implausible. mkcramfs does not emit
 * "." or ".." entries at all, so seeing one means the image is lying. */
static bool xx_cramfs_plausible_name(const char *name, size_t length) {
    size_t index;
    if (!name || length == 0U || length > XX_CRAMFS_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    if (name[0] == '.' &&
        (length == 1U || (length == 2U && name[1] == '.'))) {
        return false;
    }
    return true;
}

static char *xx_cramfs_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_CRAMFS_MAX_PATH ||
        name_size > XX_CRAMFS_MAX_PATH - prefix_size -
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

/* --------------------------------------------------------------- inode --- */

/* Unpack the three packed words of an inode. The two byte orders do not just
 * swap the words: a big-endian compiler allocates bitfields from the most
 * significant end, so every field also moves to the other side of its word.
 * Both halves of the difference are undone here, in one place. */
static void xx_cramfs_decode_inode(const uint8_t *data, size_t data_size,
                                   size_t at, bool big_endian,
                                   xx_cramfs_inode *out) {
    uint32_t w0 = xx_data_get_u32(data, data_size, at, big_endian);
    uint32_t w1 = xx_data_get_u32(data, data_size, at + 4U, big_endian);
    uint32_t w2 = xx_data_get_u32(data, data_size, at + 8U, big_endian);
    if (!out) return;
    if (big_endian) {
        out->mode = w0 >> 16;
        out->uid = w0 & 0xFFFFU;
        out->size = w1 >> 8;
        out->gid = w1 & 0xFFU;
        out->namelen = w2 >> 26;
        out->offset = w2 & UINT32_C(0x03FFFFFF);
    } else {
        out->mode = w0 & 0xFFFFU;
        out->uid = w0 >> 16;
        out->size = w1 & UINT32_C(0x00FFFFFF);
        out->gid = w1 >> 24;
        out->namelen = w2 & 0x3FU;
        out->offset = w2 >> 6;
    }
}

/* ---------------------------------------------------------------- walk --- */

static bool xx_cramfs_walk(Abstractformat *self, xx_cramfs_private *parsed,
                           int64_t directory_offset, uint32_t directory_size,
                           const char *prefix, unsigned depth,
                           xx_pd_struct *pd);

/* Read one directory's payload and append an entry for every member, then
 * recurse into the members that are themselves directories.
 *
 * A malformed record ends the whole parse. A cycle, an exhausted depth budget
 * or an exhausted entry budget only ends this branch, so that the records
 * gathered before the anomaly stay usable. */
static bool xx_cramfs_walk(Abstractformat *self, xx_cramfs_private *parsed,
                           int64_t directory_offset, uint32_t directory_size,
                           const char *prefix, unsigned depth,
                           xx_pd_struct *pd) {
    uint8_t *payload = NULL;
    size_t cursor = 0U;
    bool result = false;

    if (!self || !parsed) return false;
    if (depth > XX_CRAMFS_MAX_DEPTH) return true;
    if (directory_size == 0U) return true;
    if (directory_size > XX_CRAMFS_MAX_DIR_SIZE) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* An empty-looking directory that is too small to hold even one inode is
     * not an error the way a cycle is, but nothing can be read out of it. */
    if (directory_size < (uint32_t)XX_CRAMFS_INODE_SIZE) return true;
    if (!xx_cramfs_range_within(parsed->image_end, directory_offset,
                                (int64_t)directory_size)) {
        return false;
    }
    if (xx_cramfs_visited_mark(&parsed->visited, directory_offset)) return true;

    payload = (uint8_t *)xx_mem_alloc(directory_size);
    if (!payload) return false;
    if (!xx_cramfs_read_at(self->device, directory_offset, payload,
                           directory_size)) {
        xx_mem_free(payload);
        return false;
    }

    while (cursor + (size_t)XX_CRAMFS_INODE_SIZE <= (size_t)directory_size) {
        xx_cramfs_inode inode;
        size_t name_bytes;
        size_t name_length;
        size_t entry_size;
        char name[XX_CRAMFS_MAX_NAME + 1U];
        char *full_name;
        int64_t child_offset = -1;
        bool child_present = false;
        xx_cramfs_entry entry;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (parsed->count >= XX_CRAMFS_MAX_ENTRIES) {
            result = true;
            goto done;
        }
        xx_cramfs_decode_inode(payload, directory_size, cursor,
                               parsed->big_endian, &inode);
        /* namelen is the only field that bounds the record, so a zero here
         * would make the cursor stand still and spin this loop forever. */
        if (inode.namelen == 0U ||
            inode.namelen > XX_CRAMFS_MAX_NAMELEN_UNITS) {
            goto done;
        }
        name_bytes = (size_t)inode.namelen * 4U;
        entry_size = (size_t)XX_CRAMFS_INODE_SIZE + name_bytes;
        if (entry_size > (size_t)directory_size - cursor) goto done;
        /* The name is NUL padded up to its 4-byte unit, never NUL terminated
         * when it fills the unit exactly. */
        for (name_length = 0U; name_length < name_bytes; ++name_length) {
            if (payload[cursor + (size_t)XX_CRAMFS_INODE_SIZE + name_length] ==
                0U) {
                break;
            }
        }
        if (name_length > XX_CRAMFS_MAX_NAME) goto done;
        if (name_length != 0U) {
            xx_mem_copy(name,
                        payload + cursor + (size_t)XX_CRAMFS_INODE_SIZE,
                        name_length);
        }
        name[name_length] = '\0';
        if (!xx_cramfs_plausible_name(name, name_length)) goto done;
        if (!xx_cramfs_resolve_offset(parsed, inode.offset, &child_offset,
                                      &child_present)) {
            goto done;
        }

        full_name = xx_cramfs_join_name(prefix, name);
        if (!full_name) goto done;

        xx_mem_zero(&entry, sizeof(entry));
        entry.name = full_name;
        entry.header_offset = directory_offset + (int64_t)cursor;
        entry.header_size = (int64_t)entry_size;
        entry.data_offset = child_present ? child_offset : -1;
        entry.mode = inode.mode;

        if ((inode.mode & XX_CRAMFS_S_IFMT) == XX_CRAMFS_S_IFDIR) {
            entry.size = 0U;
            entry.is_folder = true;
            if (!xx_cramfs_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                goto done;
            }
            /* The entry list owns full_name from here on; it is only borrowed
             * for the recursion and is released by the cleanup path. */
            if (child_present &&
                !xx_cramfs_walk(self, parsed, child_offset, inode.size,
                                full_name, depth + 1U, pd)) {
                goto done;
            }
        } else if ((inode.mode & XX_CRAMFS_S_IFMT) == XX_CRAMFS_S_IFREG) {
            /* A zero-length file has no block pointer array at all, and its
             * offset field is legitimately zero. */
            if (inode.size != 0U && !child_present) {
                xx_str_free(full_name);
                goto done;
            }
            entry.size = inode.size;
            entry.is_folder = false;
            if (!xx_cramfs_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                goto done;
            }
        } else {
            /* Symlinks, devices, sockets and fifos have no payload this
             * reader can hand back as a file, and are skipped. A symlink's
             * target is a compressed block like a file's, but writing it out
             * as a regular file would silently change what the image says. */
            xx_str_free(full_name);
        }
        cursor += entry_size;
    }
    result = true;

done:
    xx_mem_free(payload);
    return result;
}

/* -------------------------------------------------------------- parse --- */

/* Read the superblock and, when full is true, walk the whole tree.
 * check_is_valid only needs the superblock, so detection stays cheap. */
static bool xx_cramfs_parse(Abstractformat *self, xx_cramfs_private *parsed,
                            bool full, xx_pd_struct *pd) {
    uint8_t superblock[XX_CRAMFS_SUPERBLOCK_SIZE];
    xx_cramfs_inode root;
    int64_t total_size;
    int64_t root_offset = -1;
    uint32_t magic;
    uint32_t image_size;
    bool big_endian;
    bool root_present = false;

    /* Initialise before the guard clauses: callers such as
     * xx_cramfs_check_is_valid() run xx_cramfs_private_cleanup() on their
     * stack copy whatever this returns, and cleaning up an uninitialised one
     * would free indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->image_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_cramfs_range_within(total_size, self->base_address,
                                XX_CRAMFS_SUPERBLOCK_SIZE) ||
        !xx_cramfs_read_at(self->device, self->base_address, superblock,
                           sizeof(superblock))) {
        goto fail;
    }
    /* The magic is the only thing that says which byte order the rest of the
     * image is in: read it one way, and if it comes back as the reversed
     * constant, read everything the other way. */
    magic = xx_data_get_u32(superblock, sizeof(superblock), 0U, false);
    if (magic == XX_CRAMFS_MAGIC) {
        big_endian = false;
    } else if (xx_data_get_u32(superblock, sizeof(superblock), 0U, true) ==
               XX_CRAMFS_MAGIC) {
        big_endian = true;
    } else {
        goto fail;
    }
    parsed->big_endian = big_endian;
    parsed->base = self->base_address;
    parsed->input_size = total_size;
    parsed->flags = xx_data_get_u32(superblock, sizeof(superblock), 8U,
                                    big_endian);
    parsed->crc = xx_data_get_u32(superblock, sizeof(superblock), 32U,
                                  big_endian);
    parsed->edition = xx_data_get_u32(superblock, sizeof(superblock), 36U,
                                      big_endian);
    parsed->block_count = xx_data_get_u32(superblock, sizeof(superblock), 40U,
                                          big_endian);
    parsed->file_count = xx_data_get_u32(superblock, sizeof(superblock), 44U,
                                         big_endian);
    /* The signature is what separates a real superblock from four bytes that
     * happen to match. An image built by a tool that got it wrong says so in
     * the flags, and is accepted without it. */
    if (!(parsed->flags & XX_CRAMFS_FLAG_WRONG_SIGNATURE) &&
        xx_rt_memcmp(superblock + XX_CRAMFS_SIGNATURE_OFFSET,
                     XX_CRAMFS_SIGNATURE, XX_CRAMFS_SIGNATURE_SIZE) != 0) {
        goto fail;
    }
    image_size = xx_data_get_u32(superblock, sizeof(superblock), 4U,
                                 big_endian);
    parsed->image_size = image_size;
    if (image_size < (uint32_t)XX_CRAMFS_SUPERBLOCK_SIZE ||
        (int64_t)image_size > XX_CRAMFS_MAX_IMAGE_SIZE ||
        !xx_cramfs_add(self->base_address, image_size, &parsed->image_end) ||
        parsed->image_end > total_size) {
        goto fail;
    }

    xx_cramfs_decode_inode(superblock, sizeof(superblock), 64U, big_endian,
                           &root);
    if ((root.mode & XX_CRAMFS_S_IFMT) != XX_CRAMFS_S_IFDIR) goto fail;
    if (!xx_cramfs_resolve_offset(parsed, root.offset, &root_offset,
                                  &root_present)) {
        goto fail;
    }
    /* The root directory normally begins immediately after the superblock, or
     * after a 512-byte pad in the images that carry a boot sector. Anything
     * else is only allowed when the image says its root offset is shifted. */
    if (root_present &&
        !(parsed->flags & XX_CRAMFS_FLAG_SHIFTED_ROOT_OFFSET) &&
        root_offset != self->base_address + XX_CRAMFS_SUPERBLOCK_SIZE &&
        root_offset != self->base_address + 512 + XX_CRAMFS_SUPERBLOCK_SIZE) {
        goto fail;
    }
    if (!full) return true;
    /* An image with no root payload is an empty but legal filesystem; the
     * walk does nothing and the entry list stays empty. */
    if (root_present &&
        !xx_cramfs_walk(self, parsed, root_offset, root.size, "", 0U, pd)) {
        goto fail;
    }
    return true;

fail:
    xx_cramfs_private_cleanup(parsed);
    return false;
}

/* ----------------------------------------------------------- extraction --- */

/* Decode one block into output. The kernel refuses anything that expands past
 * a page, so the caller always passes a buffer of exactly the expected size
 * and a short result is an error, not a partial success. */
static bool xx_cramfs_decode_block(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t expected) {
    size_t produced = 0U;
    if (!input || input_size == 0U || !output || expected == 0U) return false;
    if (xx_zlib_stream_decode_memory(input, input_size, output, expected,
                                     &produced) &&
        produced == expected) {
        return true;
    }
    /* A widely deployed vendor patch stores cramfs blocks as LZMA instead of
     * zlib, in the .lzma "alone" layout: five property bytes, an eight-byte
     * uncompressed length, then the stream. It is tried only after zlib has
     * been ruled out, so a normal image never reaches this path. */
    if (input_size > 13U) {
        produced = 0U;
        if (xx_lzma_decompress_memory(input + 13U, input_size - 13U, input, 5U,
                                      (int64_t)expected, output, expected,
                                      &produced) &&
            produced == expected) {
            return true;
        }
    }
    return false;
}

/* Write one regular file's decoded contents to destination. */
static bool xx_cramfs_extract_entry(Abstractformat *self,
                                    const xx_cramfs_private *parsed,
                                    const xx_cramfs_entry *entry,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    uint8_t *pointers = NULL;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t block_count;
    size_t pointer_bytes;
    size_t index;
    int64_t previous_end;
    uint32_t remaining;
    bool masks_flags;
    bool result = false;

    if (!self || !self->device || !parsed || !entry || !destination) {
        return false;
    }
    if (entry->size == 0U) return true;
    if (entry->data_offset < 0) return false;

    block_count = ((size_t)entry->size + (size_t)XX_CRAMFS_BLOCK_SIZE - 1U) /
                  (size_t)XX_CRAMFS_BLOCK_SIZE;
    pointer_bytes = block_count * 4U;
    if (!xx_cramfs_range_within(parsed->image_end, entry->data_offset,
                                (int64_t)pointer_bytes)) {
        return false;
    }
    pointers = (uint8_t *)xx_mem_alloc(pointer_bytes);
    input = (uint8_t *)xx_mem_alloc((size_t)XX_CRAMFS_BLOCK_SIZE * 2U);
    output = (uint8_t *)xx_mem_alloc((size_t)XX_CRAMFS_BLOCK_SIZE);
    if (!pointers || !input || !output) goto done;
    if (!xx_cramfs_read_at(self->device, entry->data_offset, pointers,
                           pointer_bytes)) {
        goto done;
    }

    /* The two high bits of a pointer only mean anything when the superblock
     * opts into extended pointers; in every other image they are part of the
     * offset and masking them would corrupt it. */
    masks_flags = (parsed->flags & XX_CRAMFS_FLAG_EXT_BLOCK_POINTERS) != 0U;
    previous_end = entry->data_offset + (int64_t)pointer_bytes;
    remaining = entry->size;

    for (index = 0U; index < block_count; ++index) {
        uint32_t raw = xx_data_get_u32(pointers, pointer_bytes, index * 4U,
                                       parsed->big_endian);
        uint32_t flags = masks_flags ? (raw & XX_CRAMFS_BLK_FLAGS) : 0U;
        uint32_t stored = masks_flags ? (raw & ~XX_CRAMFS_BLK_FLAGS) : raw;
        size_t expected = remaining < XX_CRAMFS_BLOCK_SIZE
                              ? (size_t)remaining
                              : (size_t)XX_CRAMFS_BLOCK_SIZE;
        int64_t block_end;
        int64_t block_length;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        /* A direct pointer relocates the block somewhere else entirely and
         * carries its length out of band; it is produced only by the XIP
         * writer and is not decoded here. */
        if (flags & XX_CRAMFS_BLK_FLAG_DIRECT_PTR) goto done;
        if (!xx_cramfs_add(parsed->base, stored, &block_end) ||
            block_end > parsed->image_end || block_end < previous_end) {
            goto done;
        }
        block_length = block_end - previous_end;
        /* The kernel's own cap: a block can grow slightly under compression
         * but never past two pages. */
        if (block_length > (int64_t)XX_CRAMFS_BLOCK_SIZE * 2) goto done;

        if (block_length == 0) {
            /* A hole. The image stores nothing and the block reads as zeros. */
            xx_mem_zero(output, expected);
        } else if (flags & XX_CRAMFS_BLK_FLAG_UNCOMPRESSED) {
            if ((size_t)block_length != expected) goto done;
            if (!xx_cramfs_read_at(self->device, previous_end, output,
                                   expected)) {
                goto done;
            }
        } else {
            if (!xx_cramfs_read_at(self->device, previous_end, input,
                                   (size_t)block_length)) {
                goto done;
            }
            if (!xx_cramfs_decode_block(input, (size_t)block_length, output,
                                        expected)) {
                goto done;
            }
        }
        if (xx_io_write(destination, output, expected) != (ssize_t)expected) {
            goto done;
        }
        remaining -= (uint32_t)expected;
        previous_end = block_end;
    }
    result = (remaining == 0U);

done:
    if (pointers) xx_mem_free(pointers);
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    return result;
}

/* --------------------------------------------------------------- state --- */

static bool xx_cramfs_copy_options(xx_list_s *destination,
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

static const xx_var *xx_cramfs_find_option(const xx_list_s *options,
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

static bool xx_cramfs_populate_record(xx_archive_record *record,
                                      const xx_cramfs_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    /* The compressed span of a cramfs file is not contiguous with anything
     * the caller could copy out, so only the decoded length is reported as a
     * size; compressed_size stays zero rather than claiming a raw extent. */
    record->compressed_size = 0;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          entry->is_folder ? 0U : 8U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_cramfs_archive_stream_free(void *pointer) {
    xx_cramfs_archive_stream *stream = (xx_cramfs_archive_stream *)pointer;
    if (!stream) return;
    xx_cramfs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------ lifetime --- */

void xx_cramfs_init(xx_cramfs *cramfs, xx_io_device *dev,
                    int64_t base_address) {
    if (!cramfs) return;
    xx_mem_zero(cramfs, sizeof(*cramfs));
    xx_format_init(&cramfs->format, dev, base_address);
    cramfs->format.endian = XX_ENDIAN_LITTLE;
    cramfs->format.file_type = XX_CRAMFS_FILE_TYPE;
    cramfs->format.format_type = XX_TYPE_ARCHIVE;
    cramfs->format.is_archive = true;
    xx_format_set_mime_type(&cramfs->format, "application/x-cramfs");
    xx_format_set_extension(&cramfs->format, "cramfs");
    cramfs->format.check_is_valid = xx_cramfs_check_is_valid;
    cramfs->format.handle_base_info = xx_cramfs_handle_base_info;
    cramfs->format.get_format_size = xx_cramfs_get_format_size;
    cramfs->format.get_number_of_archive_records =
        xx_cramfs_get_number_of_archive_records;
    cramfs->format.create_archive_records_reading =
        xx_cramfs_create_archive_records_reading;
    cramfs->format.get_current_archive_record =
        xx_cramfs_get_current_archive_record;
    cramfs->format.unpack_current_archive_record =
        xx_cramfs_unpack_current_archive_record;
    cramfs->format.archive_record_move_to_next =
        xx_cramfs_archive_record_move_to_next;
    cramfs->format.free_archive_records_reading =
        xx_cramfs_free_archive_records_reading;
    cramfs->format.destroy = xx_cramfs_vtable_destroy;
    cramfs->archive_end = -1;
}

xx_cramfs *xx_cramfs_create(xx_io_device *dev, int64_t base_address) {
    xx_cramfs *cramfs = (xx_cramfs *)xx_mem_alloc(sizeof(*cramfs));
    if (cramfs) xx_cramfs_init(cramfs, dev, base_address);
    return cramfs;
}

void xx_cramfs_destroy(xx_cramfs *cramfs) {
    if (!cramfs) return;
    if (cramfs->internal) {
        xx_cramfs_private_cleanup((xx_cramfs_private *)cramfs->internal);
        xx_mem_free(cramfs->internal);
        cramfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&cramfs->format);
}

static void xx_cramfs_vtable_destroy(Abstractformat *self) {
    xx_cramfs_destroy((xx_cramfs *)self);
}

void xx_cramfs_free(xx_cramfs *cramfs) {
    if (!cramfs) return;
    xx_cramfs_destroy(cramfs);
    xx_mem_free(cramfs);
}

/* -------------------------------------------------------------- vtable --- */

bool xx_cramfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cramfs_private parsed;
    /* Detection stops at the superblock: the tree walk only happens on the
     * full parse, so validity stays cheap. */
    bool result = xx_cramfs_parse(self, &parsed, false, pd);
    xx_cramfs_private_cleanup(&parsed);
    return result;
}

bool xx_cramfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cramfs_private *parsed;
    xx_cramfs *cramfs = (xx_cramfs *)self;
    int64_t total_size;
    if (!self || !cramfs) return false;
    parsed = (xx_cramfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_cramfs_parse(self, parsed, true, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (cramfs->internal) {
        xx_cramfs_private_cleanup((xx_cramfs_private *)cramfs->internal);
        xx_mem_free(cramfs->internal);
    }
    cramfs->internal = parsed;
    cramfs->number_of_records = parsed->count;
    cramfs->number_of_members = parsed->count;
    cramfs->image_size = parsed->image_size;
    cramfs->flags = parsed->flags;
    cramfs->crc = parsed->crc;
    cramfs->edition = parsed->edition;
    cramfs->block_count = parsed->block_count;
    cramfs->file_count = parsed->file_count;
    cramfs->archive_end = parsed->image_end;
    cramfs->big_endian = parsed->big_endian;
    self->endian = parsed->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->image_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->image_end) {
        self->overlay_offset = parsed->image_end;
        self->overlay_size = total_size - parsed->image_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_cramfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_cramfs_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_cramfs *)self)->number_of_records;
}

xx_archive_record_state *xx_cramfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_cramfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_cramfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_cramfs_copy_options(&state->options, options) ||
        !xx_cramfs_parse(self, &stream->parsed, true, pd)) {
        xx_cramfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_cramfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_cramfs_populate_record(&state->current_record,
                                  &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_cramfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cramfs_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_cramfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_cramfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_cramfs_populate_record(&state->current_record,
                                   &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_cramfs_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_cramfs_archive_stream *stream;
    const xx_cramfs_entry *entry;
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
    stream = (xx_cramfs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_cramfs_safe_name(entry->name)) return false;

    option = xx_cramfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member's payload is addressable
         * at all, without writing anything. */
        if (entry->is_folder || entry->size == 0U) return true;
        return entry->data_offset >= 0 &&
               entry->data_offset <= stream->parsed.image_end;
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
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    result = xx_cramfs_extract_entry(self, &stream->parsed, entry, destination,
                                     pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_cramfs_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors --- */

uint64_t xx_cramfs_get_number_of_records(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->number_of_records : 0U;
}
uint64_t xx_cramfs_get_number_of_members(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->number_of_members : 0U;
}
uint32_t xx_cramfs_get_image_size(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->image_size : 0U;
}
uint32_t xx_cramfs_get_flags(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->flags : 0U;
}
uint32_t xx_cramfs_get_crc(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->crc : 0U;
}
uint32_t xx_cramfs_get_edition(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->edition : 0U;
}
uint32_t xx_cramfs_get_block_count(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->block_count : 0U;
}
uint32_t xx_cramfs_get_file_count(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->file_count : 0U;
}
int64_t xx_cramfs_get_archive_end(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->archive_end : -1;
}
bool xx_cramfs_is_big_endian(const xx_cramfs *cramfs) {
    return cramfs ? cramfs->big_endian : false;
}

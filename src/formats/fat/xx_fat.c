/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fat/xx_fat.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from here,
 * so the file-type constant is taken from the alias macro when it exists and
 * falls back to UNKNOWN until the enumerator lands. Delete this block once
 * XX_FILE_TYPE_FAT is in the enum. */
#ifdef FAT
#define XX_FAT_FILE_TYPE XX_FILE_TYPE_FAT
#else
#define XX_FAT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ------------------------------------------------------------- limits --- */

/* Every bound below caps attacker-controlled arithmetic; none of them is a
 * statement about what FAT permits. A crafted BPB can claim any cluster count
 * and a crafted FAT can describe a chain of any length, so the parse refuses
 * rather than allocating or seeking on those numbers. */
#define XX_FAT_BOOT_SIZE 512U
#define XX_FAT_DIR_ENTRY_SIZE 32U
#define XX_FAT_MAX_CLUSTER_SIZE (1024U * 1024U)
/* 16M clusters is a 2 MiB visited bitmap and, at the 512-byte minimum cluster,
 * an 8 GiB volume; beyond that the image is refused rather than parsed. */
#define XX_FAT_MAX_CLUSTERS 16777216U
#define XX_FAT_MAX_VOLUME (UINT64_C(64) * 1024 * 1024 * 1024)
#define XX_FAT_MAX_ENTRIES 200000U
/* Total cluster-chain links followed across one whole parse. A self-looping
 * chain is cut on the first revisit, but a fan of many short loops still costs
 * I/O, so the work of an entire parse is capped too. */
#define XX_FAT_MAX_CHAIN_STEPS 4000000U
#define XX_FAT_MAX_DIR_ENTRIES 2000000U
#define XX_FAT_MAX_DEPTH 32U
#define XX_FAT_MAX_PATH 4096U
/* Every record stores its full '/'-joined path, and a crafted image can give
 * each of XX_FAT_MAX_ENTRIES records a ~4 KiB inherited prefix. The bytes of
 * all stored paths together are therefore capped; once the budget is spent
 * the walk stops and the records gathered so far are what the volume lists. */
#define XX_FAT_MAX_NAME_BYTES (UINT64_C(64) * 1024 * 1024)
/* Probe steps the duplicate-name set may spend over a whole parse. A seeded
 * hash keeps precomputed collisions out; this cap bounds the cost even if a
 * run of names still clusters. Past it, further members are dropped. */
#define XX_FAT_MAX_NAME_PROBES 16000000U
/* Bytes of path text the duplicate-name guard may touch over a whole parse:
 * joins, hashes and compares. A member that is dropped as a duplicate costs
 * this work without adding to XX_FAT_MAX_NAME_BYTES, so without a second cap a
 * crafted tree could spend minutes on members it never lists. Its path bytes
 * are also charged to XX_FAT_MAX_NAME_BYTES, as if it had been kept. */
#define XX_FAT_MAX_NAME_WORK (UINT64_C(256) * 1024 * 1024)
#define XX_FAT_CHUNK 65536U
/* Root entries inspected when a boot sector without the IBM PC markers has to
 * be corroborated by its root directory (see xx_fat_root_plausible). */
#define XX_FAT_PROBE_ENTRIES 128U
/* Bytes of FAT #0 (and of FAT #1) compared by the corroboration checks. The
 * smallest legal sector is this size, so it never exceeds one FAT. */
#define XX_FAT_PROBE_FAT_BYTES 512U
/* Lowest media descriptor accepted. 0xE5 is the 8-inch single-density disk,
 * 0xED the Tandy 2000 720K disk, 0xF0 and 0xF8..0xFF the IBM set. */
#define XX_FAT_MIN_MEDIA 0xE5U

/* A VFAT sequence is at most 20 entries of 13 UTF-16 units each. The ordinal
 * is masked to 6 bits and then range-checked against this, so a crafted
 * ordinal can never index past the assembly buffer. */
#define XX_FAT_LFN_MAX_ORDER 20U
#define XX_FAT_LFN_CHARS_PER_ENTRY 13U
#define XX_FAT_LFN_MAX_CHARS (XX_FAT_LFN_MAX_ORDER * XX_FAT_LFN_CHARS_PER_ENTRY)

/* Directory entry attributes. */
#define XX_FAT_ATTR_READ_ONLY 0x01U
#define XX_FAT_ATTR_HIDDEN 0x02U
#define XX_FAT_ATTR_SYSTEM 0x04U
#define XX_FAT_ATTR_VOLUME_ID 0x08U
#define XX_FAT_ATTR_DIRECTORY 0x10U
#define XX_FAT_ATTR_ARCHIVE 0x20U
/* The long-name attribute is exactly RO|HID|SYS|VOL_ID; no other combination
 * is a long-name entry, and the test must be for equality, not a mask. */
#define XX_FAT_ATTR_LONG_NAME 0x0FU
#define XX_FAT_ATTR_LONG_MASK 0x3FU

#define XX_FAT_LAST_LONG_ENTRY 0x40U
#define XX_FAT_ENTRY_FREE 0xE5U
#define XX_FAT_ENTRY_END 0x00U
/* 0x05 in name[0] stands in for a real 0xE5 lead byte, which would otherwise
 * be mistaken for the deleted marker. */
#define XX_FAT_ENTRY_KANJI_E5 0x05U

/* ---------------------------------------------------------- structures --- */

typedef struct xx_fat_entry_s {
    char *name;            /**< Full '/'-joined path inside the volume. */
    uint32_t name_hash;    /**< Key hash of `name` (xx_fat_member_hash). */
    int64_t header_offset; /**< Offset of the 8.3 directory entry. */
    int64_t data_offset;   /**< Offset of the first cluster, or -1. */
    uint32_t first_cluster;
    uint32_t size;
    uint16_t dos_date;
    uint16_t dos_time;
    uint8_t attributes;
    bool is_folder;
} xx_fat_entry;

/* Geometry, all of it derived from the boot sector and validated before use. */
typedef struct xx_fat_geometry_s {
    int64_t base;
    int64_t total_size;  /**< Size of the backing device. */
    int64_t volume_end;  /**< base + volume_size. */
    int64_t fat_offset;  /**< Byte offset of FAT #0. */
    int64_t fat_bytes;   /**< Byte length of one FAT. */
    int64_t root_offset; /**< FAT12/16 fixed root directory, else -1. */
    int64_t root_bytes;  /**< FAT12/16 fixed root directory length, else 0. */
    int64_t data_offset; /**< Byte offset of cluster 2. */
    uint64_t volume_size;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t root_entry_count;
    uint32_t fat_size_sectors;
    uint32_t total_sectors;
    uint32_t cluster_count; /**< Data clusters; valid numbers are 2..count+1. */
    uint32_t root_cluster;  /**< FAT32 only. */
    uint32_t kind;          /**< xx_fat_kind. */
    uint32_t boot_kind;     /**< xx_fat_boot_kind. */
    bool truncated;         /**< The image ends before volume_end. */
    uint8_t media;
} xx_fat_geometry;

/* One-sector window over FAT #0. A FAT12 entry can straddle the sector
 * boundary, so the FAT is addressed a byte at a time through this cache rather
 * than by reading two or three bytes at a computed offset. */
typedef struct xx_fat_fatcache_s {
    uint8_t *data;
    int64_t offset; /**< Byte offset of the cached window, or -1. */
    size_t size;
} xx_fat_fatcache;

/* A subdirectory queued for later. Directories are NOT walked recursively:
 * the cycle guard below is one shared visited set, and a nested walk would
 * clobber the set belonging to the chain that spawned it - which is exactly
 * how a two-cluster loop would slip past it. Breadth-first over a queue keeps
 * one chain active at a time. `prefix` borrows the owning entry's name, which
 * is a separate allocation and therefore stable across entry-array growth. */
typedef struct xx_fat_pending_s {
    const char *prefix;
    uint32_t cluster;
    unsigned depth;
} xx_fat_pending;

typedef struct xx_fat_private_s {
    xx_fat_entry *entries;
    size_t count;
    size_t capacity;
    xx_fat_geometry geometry;
    xx_fat_fatcache fat_cache;
    /* One bit per cluster. `visited` is cleared between chains via the
     * touched list; `dir_seen` is never cleared, so a directory cluster is
     * entered at most once for the whole parse and a directory cycle - or a
     * directory aliased into two places - cannot expand the tree. */
    uint8_t *visited;
    uint8_t *dir_seen;
    size_t bitmap_bytes;
    uint32_t *touched;
    size_t touched_count;
    size_t touched_capacity;
    xx_fat_pending *queue;
    size_t queue_count;
    size_t queue_head;
    size_t queue_capacity;
    uint64_t chain_steps;
    uint64_t dir_entries;
    char *volume_label;
    /* Open-addressed set of entry indices + 1, keyed on the case-folded full
     * path, so a crafted directory cannot make two members share one output
     * path (see xx_fat_unique_name). */
    uint32_t *name_slots;
    size_t name_slot_capacity;
    uint32_t hash_seed;
    uint64_t name_probes;
    uint64_t name_bytes;
    uint64_t name_work; /**< See XX_FAT_MAX_NAME_WORK. */
    size_t dropped;     /**< Members dropped as unresolvable duplicates. */
    /* Owners of a heap copy: the xx_fat that parsed it in handle_base_info
     * and every record stream reading from it (see xx_fat_private_release). */
    unsigned refs;
} xx_fat_private;

typedef struct xx_fat_archive_stream_s {
    xx_fat_private *parsed; /**< Shared with xx_fat::internal; one reference. */
    size_t index;
    /* Bytes this stream may still write. Directory entries can all point at
     * one cluster chain, so without a cap the output would be members x data
     * region; a sound volume never writes more than its data region. */
    uint64_t output_budget;
} xx_fat_archive_stream;

/* State machine for one directory's in-flight VFAT sequence. */
typedef struct xx_fat_lfn_s {
    uint16_t chars[XX_FAT_LFN_MAX_CHARS];
    uint32_t present; /**< Bit i set when slot i+1 has been filled. */
    uint8_t expected; /**< Ordinal the next entry on disk must carry. */
    uint8_t order;    /**< Ordinal of the last (highest) entry. */
    uint8_t checksum; /**< Checksum the 8.3 entry must reproduce. */
    bool active;
} xx_fat_lfn;

static void xx_fat_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers --- */

static bool xx_fat_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    /* xx_io_seek64 rather than xx_io_seek: `long` is 32 bits on Win64 and a
     * FAT32 volume can run well past 2 GiB. */
    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
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
static bool xx_fat_range_within(int64_t total_size, int64_t offset, int64_t size)
{
    return (total_size >= 0) && (offset >= 0) && (size >= 0) && (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_fat_add(int64_t left, uint64_t right, int64_t *result)
{
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_fat_is_power_of_two(uint32_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

/* ---------------------------------------------------------- bit vector --- */

static bool xx_fat_bit_test(const uint8_t *bits, uint32_t index)
{
    return (bits[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void xx_fat_bit_set(uint8_t *bits, uint32_t index)
{
    bits[index >> 3U] |= (uint8_t)(1U << (index & 7U));
}

static void xx_fat_bit_clear(uint8_t *bits, uint32_t index)
{
    bits[index >> 3U] &= (uint8_t)~(1U << (index & 7U));
}

/* Record a cluster in the per-chain visited set. Returns false when the
 * cluster had already been seen on this chain - the loop signal - or when the
 * touched list cannot grow, in which case the chain is abandoned rather than
 * walked with a set that can no longer be undone. */
static bool xx_fat_visit(xx_fat_private *parsed, uint32_t cluster)
{
    if (!parsed->visited || cluster >= (uint32_t)(parsed->bitmap_bytes * 8U)) {
        return false;
    }
    if (xx_fat_bit_test(parsed->visited, cluster)) return false;
    if (parsed->touched_count == parsed->touched_capacity) {
        size_t capacity = parsed->touched_capacity ? parsed->touched_capacity * 2U : 256U;
        uint32_t *grown;
        if (capacity < parsed->touched_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
            return false;
        }
        grown = (uint32_t *)xx_mem_realloc(parsed->touched, capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->touched = grown;
        parsed->touched_capacity = capacity;
    }
    xx_fat_bit_set(parsed->visited, cluster);
    parsed->touched[parsed->touched_count++] = cluster;
    return true;
}

/* Undo exactly the bits this chain set, so the next chain starts clean without
 * paying for a full bitmap wipe per file. */
static void xx_fat_visit_reset(xx_fat_private *parsed)
{
    size_t index;
    if (parsed->visited) {
        for (index = 0U; index < parsed->touched_count; ++index) {
            xx_fat_bit_clear(parsed->visited, parsed->touched[index]);
        }
    }
    parsed->touched_count = 0U;
}

/* ------------------------------------------------------- FAT accessors --- */

static void xx_fat_cache_cleanup(xx_fat_fatcache *cache)
{
    if (!cache) return;
    if (cache->data) xx_mem_free(cache->data);
    xx_mem_zero(cache, sizeof(*cache));
    cache->offset = -1;
}

/* Fetch one byte of FAT #0 through the sector window. Out-of-range reads fail
 * rather than wrap, so a chain that walks off the end of the FAT simply ends. */
static bool xx_fat_fat_byte(Abstractformat *self, xx_fat_private *parsed, int64_t byte_offset, uint8_t *out)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_fat_fatcache *cache = &parsed->fat_cache;
    int64_t window;
    if (byte_offset < 0 || byte_offset >= geometry->fat_bytes || !cache->data) {
        return false;
    }
    window = geometry->fat_offset + (byte_offset - (byte_offset % (int64_t)cache->size));
    if (cache->offset != window) {
        size_t want = cache->size;
        int64_t available = geometry->total_size - window;
        if (available <= 0) return false;
        if ((int64_t)want > available) want = (size_t)available;
        xx_mem_zero(cache->data, cache->size);
        if (!xx_fat_read_at(self->device, window, cache->data, want)) {
            cache->offset = -1;
            return false;
        }
        cache->offset = window;
    }
    *out = cache->data[byte_offset % (int64_t)cache->size];
    return true;
}

/* Read one FAT entry. FAT12 packs one-and-a-half bytes per entry, so it is
 * assembled from two independent byte fetches; that also makes the sector
 * straddle a non-issue. */
static bool xx_fat_fat_entry(Abstractformat *self, xx_fat_private *parsed, uint32_t cluster, uint32_t *out)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    uint8_t bytes[4];
    int64_t offset;
    size_t index;
    size_t width;
    if (!out) return false;
    if (geometry->kind == XX_FAT_KIND_FAT12) {
        offset = (int64_t)cluster + ((int64_t)cluster / 2);
        width = 2U;
    } else if (geometry->kind == XX_FAT_KIND_FAT16) {
        offset = (int64_t)cluster * 2;
        width = 2U;
    } else {
        offset = (int64_t)cluster * 4;
        width = 4U;
    }
    for (index = 0U; index < width; ++index) {
        if (!xx_fat_fat_byte(self, parsed, offset + (int64_t)index, &bytes[index])) {
            return false;
        }
    }
    if (geometry->kind == XX_FAT_KIND_FAT12) {
        uint32_t packed = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
        *out = (cluster & 1U) ? (packed >> 4U) : (packed & 0x0FFFU);
    } else if (geometry->kind == XX_FAT_KIND_FAT16) {
        *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
    } else {
        /* The top four bits of a FAT32 entry are reserved and must be ignored
         * on read; a volume that leaves them set is still valid. */
        *out = ((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U)) & 0x0FFFFFFFU;
    }
    return true;
}

/* A cluster number is usable only inside 2..cluster_count+1. Everything else -
 * 0 (free), 1 (reserved), the bad-cluster mark and every end-of-chain value -
 * terminates a chain. */
static bool xx_fat_cluster_is_data(const xx_fat_geometry *geometry, uint32_t cluster)
{
    return cluster >= 2U && cluster <= geometry->cluster_count + 1U;
}

static bool xx_fat_cluster_offset(const xx_fat_geometry *geometry, uint32_t cluster, int64_t *out)
{
    if (!xx_fat_cluster_is_data(geometry, cluster)) return false;
    return xx_fat_add(geometry->data_offset, (uint64_t)(cluster - 2U) * (uint64_t)geometry->bytes_per_cluster, out) &&
           xx_fat_range_within(geometry->total_size, *out, (int64_t)geometry->bytes_per_cluster);
}

/* Step to the successor of `cluster`, refusing a revisit. `*out` is set to 0
 * when the chain ends for any reason; false means the whole walk should stop
 * because a global budget is exhausted. */
static bool xx_fat_chain_next(Abstractformat *self, xx_fat_private *parsed, uint32_t cluster, uint32_t *out)
{
    uint32_t next = 0U;
    *out = 0U;
    if (parsed->chain_steps >= XX_FAT_MAX_CHAIN_STEPS) return false;
    ++parsed->chain_steps;
    if (!xx_fat_fat_entry(self, parsed, cluster, &next)) return true;
    if (!xx_fat_cluster_is_data(&parsed->geometry, next)) return true;
    /* The cycle guard. A chain that revisits a cluster is truncated here; the
     * bytes gathered before the revisit stay usable. */
    if (!xx_fat_visit(parsed, next)) return true;
    *out = next;
    return true;
}

/* ------------------------------------------------------ name handling --- */

static size_t xx_fat_utf8_length(uint32_t code_point)
{
    if (code_point < 0x80U) return 1U;
    if (code_point < 0x800U) return 2U;
    if (code_point < 0x10000U) return 3U;
    return 4U;
}

static size_t xx_fat_utf8_encode(uint32_t code_point, char *out)
{
    if (code_point < 0x80U) {
        out[0] = (char)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        out[0] = (char)(0xC0U | (code_point >> 6U));
        out[1] = (char)(0x80U | (code_point & 0x3FU));
        return 2U;
    }
    if (code_point < 0x10000U) {
        out[0] = (char)(0xE0U | (code_point >> 12U));
        out[1] = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
        out[2] = (char)(0x80U | (code_point & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (code_point >> 18U));
    out[1] = (char)(0x80U | ((code_point >> 12U) & 0x3FU));
    out[2] = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
    out[3] = (char)(0x80U | (code_point & 0x3FU));
    return 4U;
}

/* Convert `length` UTF-16 units to a fresh UTF-8 string. Lone surrogates are
 * replaced rather than rejected: a name is not worth failing a whole volume
 * over, and U+FFFD keeps the result well formed. */
static char *xx_fat_utf16_to_utf8(const uint16_t *units, size_t length)
{
    size_t needed = 0U;
    size_t index;
    size_t written = 0U;
    char *name;
    for (index = 0U; index < length; ++index) {
        uint32_t code_point = units[index];
        if (code_point >= 0xD800U && code_point <= 0xDBFFU && index + 1U < length && units[index + 1U] >= 0xDC00U && units[index + 1U] <= 0xDFFFU) {
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (units[index + 1U] - 0xDC00U);
            ++index;
        } else if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        needed += xx_fat_utf8_length(code_point);
        if (needed > XX_FAT_MAX_PATH) return NULL;
    }
    name = (char *)xx_mem_alloc(needed + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t code_point = units[index];
        if (code_point >= 0xD800U && code_point <= 0xDBFFU && index + 1U < length && units[index + 1U] >= 0xDC00U && units[index + 1U] <= 0xDFFFU) {
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (units[index + 1U] - 0xDC00U);
            ++index;
        } else if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        written += xx_fat_utf8_encode(code_point, name + written);
    }
    name[written] = '\0';
    return name;
}

/* The checksum that ties a VFAT sequence to its 8.3 entry: an 8-bit rotate of
 * the running value plus each of the eleven raw name bytes. Any mismatch means
 * the long name does not belong to this entry. */
static uint8_t xx_fat_short_checksum(const uint8_t *short_name)
{
    uint8_t sum = 0U;
    size_t index;
    for (index = 0U; index < 11U; ++index) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0x00U) + (sum >> 1U) + short_name[index]);
    }
    return sum;
}

/* Render the 8.3 name. The stored form is space padded and upper cased; the
 * two NT case flags at +12 record that the base and/or extension were really
 * lower case, and are honoured because ignoring them silently mangles names
 * written by Windows. */
/* Code page 437, bytes 0x80..0xFF, as Unicode. 8.3 names hold OEM bytes, not
 * UTF-8; passing them through raw would hand invalid UTF-8 to the file layer,
 * which maps every such byte to U+FFFD, so "M\x9aLLER" and "M\x8eLLER" would
 * become one output file. The mapping is one-to-one, so distinct raw names
 * stay distinct. CP437 is the IBM PC default and what Deark and 7-Zip use on
 * a US system; disks written under another code page get stable but
 * non-native names. */
static const uint16_t xx_fat_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6,
    0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1,
    0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555,
    0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566,
    0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590,
    0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1,
    0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static size_t xx_fat_oem_put(unsigned char ch, char *out)
{
    if (ch < 0x80U) {
        out[0] = (char)ch;
        return 1U;
    }
    return xx_fat_utf8_encode(xx_fat_cp437[ch - 0x80U], out);
}

static char *xx_fat_short_name(const uint8_t *entry)
{
    /* 11 name bytes, each at most 3 UTF-8 bytes, plus the dot. */
    char buffer[40];
    size_t used = 0U;
    size_t index;
    size_t limit;
    uint8_t flags = entry[12];
    char *name;
    limit = 8U;
    while (limit > 0U && entry[limit - 1U] == ' ') --limit;
    for (index = 0U; index < limit; ++index) {
        unsigned char ch = entry[index];
        if (index == 0U && ch == XX_FAT_ENTRY_KANJI_E5) ch = XX_FAT_ENTRY_FREE;
        if (ch < 0x20U || ch == '/' || ch == '\\') return NULL;
        if ((flags & 0x08U) && ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + 32);
        used += xx_fat_oem_put(ch, buffer + used);
    }
    if (used == 0U) return NULL;
    limit = 3U;
    while (limit > 0U && entry[8U + limit - 1U] == ' ') --limit;
    if (limit != 0U) {
        buffer[used++] = '.';
        for (index = 0U; index < limit; ++index) {
            unsigned char ch = entry[8U + index];
            if (ch < 0x20U || ch == '/' || ch == '\\') return NULL;
            if ((flags & 0x10U) && ch >= 'A' && ch <= 'Z') {
                ch = (unsigned char)(ch + 32);
            }
            used += xx_fat_oem_put(ch, buffer + used);
        }
    }
    name = (char *)xx_mem_alloc(used + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, buffer, used);
    name[used] = '\0';
    return name;
}

/* Join `prefix` (prefix_size bytes, "" for the root) and `name` with '/'. The
 * sizes are passed in so a deep parent costs nothing when the result would be
 * too long anyway. */
static char *xx_fat_join_name(const char *prefix, size_t prefix_size, const char *name, size_t *joined_size)
{
    size_t name_size = name ? xx_str_len(name) : 0U;
    size_t total;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_FAT_MAX_PATH || name_size > XX_FAT_MAX_PATH - prefix_size - 1U) {
        return NULL;
    }
    total = prefix_size + name_size + (prefix_size != 0U ? 1U : 0U);
    combined = (char *)xx_mem_alloc(total + 1U);
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_rt_memcpy(combined + prefix_size + 1U, name, name_size);
    } else {
        xx_rt_memcpy(combined, name, name_size);
    }
    combined[total] = '\0';
    *joined_size = total;
    return combined;
}

static bool xx_fat_is_dot_name(const char *name)
{
    if (!name || name[0] != '.') return false;
    return name[1] == '\0' || (name[1] == '.' && name[2] == '\0');
}

static char xx_fat_upper(char ch)
{
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
}

/* True when the component [text, text + length) names a Windows device - CON,
 * PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ or CLOCK$ - with or without
 * an extension and trailing spaces, in any case. Opening such a path writes to
 * the device instead of creating a file. */
static bool xx_fat_is_device_name(const char *text, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *word = devices[index];
        size_t at = 0U;
        while (at < stem && word[at] && xx_fat_upper(text[at]) == word[at]) ++at;
        if (at == stem && word[at] == '\0') return true;
    }
    /* COM0-9 / LPT0-9, and the superscript forms COM¹²³ / LPT¹²³ (UTF-8
     * C2 B9, C2 B2, C2 B3) that Windows also reserves. */
    if ((stem == 4U && text[3] >= '0' && text[3] <= '9') ||
        (stem == 5U && (unsigned char)text[3] == 0xC2U && ((unsigned char)text[4] == 0xB9U || (unsigned char)text[4] == 0xB2U || (unsigned char)text[4] == 0xB3U))) {
        char a = xx_fat_upper(text[0]);
        char b = xx_fat_upper(text[1]);
        char c = xx_fat_upper(text[2]);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) {
            return true;
        }
    }
    return false;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation and
 * the device names are rejected here even though a FAT long name may legally
 * carry some of them. */
static bool xx_fat_safe_name(const char *name)
{
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') || (length == 2U && component[0] == '.' && component[1] == '.') || component[length - 1U] == ' ' ||
                component[length - 1U] == '.' || xx_fat_is_device_name(component, length)) {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* ------------------------------------------------ long-name assembly --- */

static void xx_fat_lfn_reset(xx_fat_lfn *lfn)
{
    lfn->active = false;
    lfn->present = 0U;
    lfn->expected = 0U;
    lfn->order = 0U;
    lfn->checksum = 0U;
}

/* Feed one 0x0F entry into the sequence. VFAT writes the pieces in reverse:
 * the entry with LAST_LONG_ENTRY set carries the highest ordinal and comes
 * first on disk, counting down to ordinal 1 immediately before the 8.3 entry.
 * Any break in that order, a checksum change mid-sequence, or an ordinal
 * outside 1..20 discards what was gathered - the buffer is indexed only after
 * the ordinal has been range checked. */
static void xx_fat_lfn_push(xx_fat_lfn *lfn, const uint8_t *entry)
{
    uint8_t raw = entry[0];
    uint8_t order = (uint8_t)(raw & XX_FAT_ATTR_LONG_MASK);
    uint8_t checksum = entry[13];
    size_t base;
    size_t index;
    static const size_t offsets[XX_FAT_LFN_CHARS_PER_ENTRY] = {1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};

    if (order == 0U || order > XX_FAT_LFN_MAX_ORDER) {
        xx_fat_lfn_reset(lfn);
        return;
    }
    if (raw & XX_FAT_LAST_LONG_ENTRY) {
        xx_fat_lfn_reset(lfn);
        lfn->active = true;
        lfn->order = order;
        lfn->checksum = checksum;
        lfn->expected = order;
        /* Unwritten slots must read as terminated, not as stale characters. */
        xx_mem_zero(lfn->chars, sizeof(lfn->chars));
    } else if (!lfn->active || order != lfn->expected || checksum != lfn->checksum) {
        xx_fat_lfn_reset(lfn);
        return;
    }
    base = ((size_t)order - 1U) * XX_FAT_LFN_CHARS_PER_ENTRY;
    if (base + XX_FAT_LFN_CHARS_PER_ENTRY > XX_FAT_LFN_MAX_CHARS) {
        xx_fat_lfn_reset(lfn);
        return;
    }
    for (index = 0U; index < XX_FAT_LFN_CHARS_PER_ENTRY; ++index) {
        size_t at = offsets[index];
        lfn->chars[base + index] = (uint16_t)((uint16_t)entry[at] | ((uint16_t)entry[at + 1U] << 8U));
    }
    lfn->present |= (uint32_t)1U << (order - 1U);
    lfn->expected = (uint8_t)(order - 1U);
}

/* Turn a completed sequence into a name, or return NULL when it does not
 * belong to this 8.3 entry. The sequence must be whole (every ordinal from 1
 * to `order` present), must have counted down to zero, and its checksum must
 * match the eleven short-name bytes. */
static char *xx_fat_lfn_finish(const xx_fat_lfn *lfn, const uint8_t *entry)
{
    uint32_t wanted;
    size_t length = 0U;
    if (!lfn->active || lfn->expected != 0U || lfn->order == 0U) return NULL;
    wanted = (lfn->order >= 32U) ? 0xFFFFFFFFU : (((uint32_t)1U << lfn->order) - 1U);
    if (lfn->present != wanted) return NULL;
    if (lfn->checksum != xx_fat_short_checksum(entry)) return NULL;
    while (length < (size_t)lfn->order * XX_FAT_LFN_CHARS_PER_ENTRY && lfn->chars[length] != 0x0000U && lfn->chars[length] != 0xFFFFU) {
        ++length;
    }
    if (length == 0U) return NULL;
    /* A long name may not hold a path separator or a control character. One
     * that does would change the shape of the member path - "a\b" would be
     * written as a/b and could land on another member's file - so it is
     * treated like any other broken sequence and the 8.3 name is used. */
    {
        size_t index;
        for (index = 0U; index < length; ++index) {
            uint16_t unit = lfn->chars[index];
            if (unit < 0x20U || unit == (uint16_t)'/' || unit == (uint16_t)'\\') {
                return NULL;
            }
        }
    }
    return xx_fat_utf16_to_utf8(lfn->chars, length);
}

/* ------------------------------------------------------------- parsing --- */

static void xx_fat_private_cleanup(xx_fat_private *parsed)
{
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->visited) xx_mem_free(parsed->visited);
    if (parsed->dir_seen) xx_mem_free(parsed->dir_seen);
    if (parsed->touched) xx_mem_free(parsed->touched);
    if (parsed->queue) xx_mem_free(parsed->queue);
    if (parsed->volume_label) xx_str_free(parsed->volume_label);
    if (parsed->name_slots) xx_mem_free(parsed->name_slots);
    xx_fat_cache_cleanup(&parsed->fat_cache);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->geometry.total_size = -1;
    parsed->geometry.volume_end = -1;
    parsed->geometry.root_offset = -1;
    parsed->fat_cache.offset = -1;
}

/* Drop one reference to a heap copy of the parse, freeing it with the last. */
static void xx_fat_private_release(xx_fat_private *parsed)
{
    if (!parsed) return;
    if (parsed->refs > 1U) {
        --parsed->refs;
        return;
    }
    xx_fat_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* ------------------------------------------------- unique member names --- */

/* The duplicate-name key. Two members must never reach one output file, and
 * the file systems that receive them compare names loosely: NTFS through its
 * $UpCase table (every cased script, fullwidth forms included), APFS and HFS+
 * also through canonical decomposition, so "é" (U+00E9) and "e" + U+0301 are
 * one name there. The key of a path is therefore its NFD with every code
 * point folded to one representative of its case class. The class joins the
 * single-code-point upper, lower, title and case-fold mappings of Unicode
 * 15.1, which makes the key coarser than any of those comparisons: two names
 * one of them treats as equal always share a key, and a pair that only this
 * key merges costs a harmless rename. The tables were derived from the
 * Unicode Character Database (Unicode license) with an original script;
 * Hangul syllables decompose arithmetically and are not listed. */
typedef struct xx_fat_fold_range_s {
    uint32_t first;
    uint32_t last;
    int32_t stride; /**< 1, or 2 for alternating upper/lower pairs. */
    int32_t delta;  /**< Added to a code point in the range to fold it. */
} xx_fat_fold_range;

static const xx_fat_fold_range xx_fat_fold_ranges[211] = {
    {0x00061U, 0x0007AU, 1, -32},    {0x000E0U, 0x000F6U, 1, -32},    {0x000F8U, 0x000FEU, 1, -32},    {0x00101U, 0x0012FU, 2, -1},     {0x00131U, 0x00131U, 1, -232},
    {0x00133U, 0x00137U, 2, -1},     {0x0013AU, 0x00148U, 2, -1},     {0x0014BU, 0x00177U, 2, -1},     {0x00178U, 0x00178U, 1, -121},   {0x0017AU, 0x0017EU, 2, -1},
    {0x0017FU, 0x0017FU, 1, -300},   {0x00183U, 0x00185U, 2, -1},     {0x00188U, 0x00188U, 1, -1},     {0x0018CU, 0x0018CU, 1, -1},     {0x00192U, 0x00192U, 1, -1},
    {0x00199U, 0x00199U, 1, -1},     {0x001A1U, 0x001A5U, 2, -1},     {0x001A8U, 0x001A8U, 1, -1},     {0x001ADU, 0x001ADU, 1, -1},     {0x001B0U, 0x001B0U, 1, -1},
    {0x001B4U, 0x001B6U, 2, -1},     {0x001B9U, 0x001B9U, 1, -1},     {0x001BDU, 0x001BDU, 1, -1},     {0x001C5U, 0x001C5U, 1, -1},     {0x001C6U, 0x001C6U, 1, -2},
    {0x001C8U, 0x001C8U, 1, -1},     {0x001C9U, 0x001C9U, 1, -2},     {0x001CBU, 0x001CBU, 1, -1},     {0x001CCU, 0x001CCU, 1, -2},     {0x001CEU, 0x001DCU, 2, -1},
    {0x001DDU, 0x001DDU, 1, -79},    {0x001DFU, 0x001EFU, 2, -1},     {0x001F2U, 0x001F2U, 1, -1},     {0x001F3U, 0x001F3U, 1, -2},     {0x001F5U, 0x001F5U, 1, -1},
    {0x001F6U, 0x001F6U, 1, -97},    {0x001F7U, 0x001F7U, 1, -56},    {0x001F9U, 0x0021FU, 2, -1},     {0x00220U, 0x00220U, 1, -130},   {0x00223U, 0x00233U, 2, -1},
    {0x0023CU, 0x0023CU, 1, -1},     {0x0023DU, 0x0023DU, 1, -163},   {0x00242U, 0x00242U, 1, -1},     {0x00243U, 0x00243U, 1, -195},   {0x00247U, 0x0024FU, 2, -1},
    {0x00253U, 0x00253U, 1, -210},   {0x00254U, 0x00254U, 1, -206},   {0x00256U, 0x00257U, 1, -205},   {0x00259U, 0x00259U, 1, -202},   {0x0025BU, 0x0025BU, 1, -203},
    {0x00260U, 0x00260U, 1, -205},   {0x00263U, 0x00263U, 1, -207},   {0x00268U, 0x00268U, 1, -209},   {0x00269U, 0x00269U, 1, -211},   {0x0026FU, 0x0026FU, 1, -211},
    {0x00272U, 0x00272U, 1, -213},   {0x00275U, 0x00275U, 1, -214},   {0x00280U, 0x00280U, 1, -218},   {0x00283U, 0x00283U, 1, -218},   {0x00288U, 0x00288U, 1, -218},
    {0x00289U, 0x00289U, 1, -69},    {0x0028AU, 0x0028BU, 1, -217},   {0x0028CU, 0x0028CU, 1, -71},    {0x00292U, 0x00292U, 1, -219},   {0x00371U, 0x00373U, 2, -1},
    {0x00377U, 0x00377U, 1, -1},     {0x00399U, 0x00399U, 1, -84},    {0x0039CU, 0x0039CU, 1, -743},   {0x003ACU, 0x003ACU, 1, -38},    {0x003ADU, 0x003AFU, 1, -37},
    {0x003B1U, 0x003B8U, 1, -32},    {0x003B9U, 0x003B9U, 1, -116},   {0x003BAU, 0x003BBU, 1, -32},    {0x003BCU, 0x003BCU, 1, -775},   {0x003BDU, 0x003C1U, 1, -32},
    {0x003C2U, 0x003C2U, 1, -31},    {0x003C3U, 0x003CBU, 1, -32},    {0x003CCU, 0x003CCU, 1, -64},    {0x003CDU, 0x003CEU, 1, -63},    {0x003D0U, 0x003D0U, 1, -62},
    {0x003D1U, 0x003D1U, 1, -57},    {0x003D5U, 0x003D5U, 1, -47},    {0x003D6U, 0x003D6U, 1, -54},    {0x003D7U, 0x003D7U, 1, -8},     {0x003D9U, 0x003EFU, 2, -1},
    {0x003F0U, 0x003F0U, 1, -86},    {0x003F1U, 0x003F1U, 1, -80},    {0x003F3U, 0x003F3U, 1, -116},   {0x003F4U, 0x003F4U, 1, -92},    {0x003F5U, 0x003F5U, 1, -96},
    {0x003F8U, 0x003F8U, 1, -1},     {0x003F9U, 0x003F9U, 1, -7},     {0x003FBU, 0x003FBU, 1, -1},     {0x003FDU, 0x003FFU, 1, -130},   {0x00430U, 0x0044FU, 1, -32},
    {0x00450U, 0x0045FU, 1, -80},    {0x00461U, 0x00481U, 2, -1},     {0x0048BU, 0x004BFU, 2, -1},     {0x004C2U, 0x004CEU, 2, -1},     {0x004CFU, 0x004CFU, 1, -15},
    {0x004D1U, 0x0052FU, 2, -1},     {0x00561U, 0x00586U, 1, -48},    {0x013F8U, 0x013FDU, 1, -8},     {0x01C80U, 0x01C80U, 1, -6254},  {0x01C81U, 0x01C81U, 1, -6253},
    {0x01C82U, 0x01C82U, 1, -6244},  {0x01C83U, 0x01C84U, 1, -6242},  {0x01C85U, 0x01C85U, 1, -6243},  {0x01C86U, 0x01C86U, 1, -6236},  {0x01C87U, 0x01C87U, 1, -6181},
    {0x01C90U, 0x01CBAU, 1, -3008},  {0x01CBDU, 0x01CBFU, 1, -3008},  {0x01E01U, 0x01E95U, 2, -1},     {0x01E9BU, 0x01E9BU, 1, -59},    {0x01E9EU, 0x01E9EU, 1, -7615},
    {0x01EA1U, 0x01EFFU, 2, -1},     {0x01F08U, 0x01F0FU, 1, -8},     {0x01F18U, 0x01F1DU, 1, -8},     {0x01F28U, 0x01F2FU, 1, -8},     {0x01F38U, 0x01F3FU, 1, -8},
    {0x01F48U, 0x01F4DU, 1, -8},     {0x01F59U, 0x01F5FU, 2, -8},     {0x01F68U, 0x01F6FU, 1, -8},     {0x01F88U, 0x01F8FU, 1, -8},     {0x01F98U, 0x01F9FU, 1, -8},
    {0x01FA8U, 0x01FAFU, 1, -8},     {0x01FB8U, 0x01FB9U, 1, -8},     {0x01FBAU, 0x01FBBU, 1, -74},    {0x01FBCU, 0x01FBCU, 1, -9},     {0x01FBEU, 0x01FBEU, 1, -7289},
    {0x01FC8U, 0x01FCBU, 1, -86},    {0x01FCCU, 0x01FCCU, 1, -9},     {0x01FD8U, 0x01FD9U, 1, -8},     {0x01FDAU, 0x01FDBU, 1, -100},   {0x01FE8U, 0x01FE9U, 1, -8},
    {0x01FEAU, 0x01FEBU, 1, -112},   {0x01FECU, 0x01FECU, 1, -7},     {0x01FF8U, 0x01FF9U, 1, -128},   {0x01FFAU, 0x01FFBU, 1, -126},   {0x01FFCU, 0x01FFCU, 1, -9},
    {0x02126U, 0x02126U, 1, -7549},  {0x0212AU, 0x0212AU, 1, -8415},  {0x0212BU, 0x0212BU, 1, -8294},  {0x0214EU, 0x0214EU, 1, -28},    {0x02170U, 0x0217FU, 1, -16},
    {0x02184U, 0x02184U, 1, -1},     {0x024D0U, 0x024E9U, 1, -26},    {0x02C30U, 0x02C5FU, 1, -48},    {0x02C61U, 0x02C61U, 1, -1},     {0x02C62U, 0x02C62U, 1, -10743},
    {0x02C63U, 0x02C63U, 1, -3814},  {0x02C64U, 0x02C64U, 1, -10727}, {0x02C65U, 0x02C65U, 1, -10795}, {0x02C66U, 0x02C66U, 1, -10792}, {0x02C68U, 0x02C6CU, 2, -1},
    {0x02C6DU, 0x02C6DU, 1, -10780}, {0x02C6EU, 0x02C6EU, 1, -10749}, {0x02C6FU, 0x02C6FU, 1, -10783}, {0x02C70U, 0x02C70U, 1, -10782}, {0x02C73U, 0x02C73U, 1, -1},
    {0x02C76U, 0x02C76U, 1, -1},     {0x02C7EU, 0x02C7FU, 1, -10815}, {0x02C81U, 0x02CE3U, 2, -1},     {0x02CECU, 0x02CEEU, 2, -1},     {0x02CF3U, 0x02CF3U, 1, -1},
    {0x02D00U, 0x02D25U, 1, -7264},  {0x02D27U, 0x02D27U, 1, -7264},  {0x02D2DU, 0x02D2DU, 1, -7264},  {0x0A641U, 0x0A649U, 2, -1},     {0x0A64AU, 0x0A64AU, 1, -35266},
    {0x0A64BU, 0x0A64BU, 1, -35267}, {0x0A64DU, 0x0A66DU, 2, -1},     {0x0A681U, 0x0A69BU, 2, -1},     {0x0A723U, 0x0A72FU, 2, -1},     {0x0A733U, 0x0A76FU, 2, -1},
    {0x0A77AU, 0x0A77CU, 2, -1},     {0x0A77DU, 0x0A77DU, 1, -35332}, {0x0A77FU, 0x0A787U, 2, -1},     {0x0A78CU, 0x0A78CU, 1, -1},     {0x0A78DU, 0x0A78DU, 1, -42280},
    {0x0A791U, 0x0A793U, 2, -1},     {0x0A797U, 0x0A7A9U, 2, -1},     {0x0A7AAU, 0x0A7AAU, 1, -42308}, {0x0A7ABU, 0x0A7ABU, 1, -42319}, {0x0A7ACU, 0x0A7ACU, 1, -42315},
    {0x0A7ADU, 0x0A7ADU, 1, -42305}, {0x0A7AEU, 0x0A7AEU, 1, -42308}, {0x0A7B0U, 0x0A7B0U, 1, -42258}, {0x0A7B1U, 0x0A7B1U, 1, -42282}, {0x0A7B2U, 0x0A7B2U, 1, -42261},
    {0x0A7B5U, 0x0A7C3U, 2, -1},     {0x0A7C4U, 0x0A7C4U, 1, -48},    {0x0A7C5U, 0x0A7C5U, 1, -42307}, {0x0A7C6U, 0x0A7C6U, 1, -35384}, {0x0A7C8U, 0x0A7CAU, 2, -1},
    {0x0A7D1U, 0x0A7D1U, 1, -1},     {0x0A7D7U, 0x0A7D9U, 2, -1},     {0x0A7F6U, 0x0A7F6U, 1, -1},     {0x0AB53U, 0x0AB53U, 1, -928},   {0x0AB70U, 0x0ABBFU, 1, -38864},
    {0x0FF41U, 0x0FF5AU, 1, -32},    {0x10428U, 0x1044FU, 1, -40},    {0x104D8U, 0x104FBU, 1, -40},    {0x10597U, 0x105A1U, 1, -39},    {0x105A3U, 0x105B1U, 1, -39},
    {0x105B3U, 0x105B9U, 1, -39},    {0x105BBU, 0x105BCU, 1, -39},    {0x10CC0U, 0x10CF2U, 1, -64},    {0x118C0U, 0x118DFU, 1, -32},    {0x16E60U, 0x16E7FU, 1, -32},
    {0x1E922U, 0x1E943U, 1, -34}};

static const uint16_t xx_fat_decomp_keys[1486] = {
    0x00C0U, 0x00C1U, 0x00C2U, 0x00C3U, 0x00C4U, 0x00C5U, 0x00C7U, 0x00C8U, 0x00C9U, 0x00CAU, 0x00CBU, 0x00CCU, 0x00CDU, 0x00CEU, 0x00CFU, 0x00D1U, 0x00D2U, 0x00D3U,
    0x00D4U, 0x00D5U, 0x00D6U, 0x00D9U, 0x00DAU, 0x00DBU, 0x00DCU, 0x00DDU, 0x00E0U, 0x00E1U, 0x00E2U, 0x00E3U, 0x00E4U, 0x00E5U, 0x00E7U, 0x00E8U, 0x00E9U, 0x00EAU,
    0x00EBU, 0x00ECU, 0x00EDU, 0x00EEU, 0x00EFU, 0x00F1U, 0x00F2U, 0x00F3U, 0x00F4U, 0x00F5U, 0x00F6U, 0x00F9U, 0x00FAU, 0x00FBU, 0x00FCU, 0x00FDU, 0x00FFU, 0x0100U,
    0x0101U, 0x0102U, 0x0103U, 0x0104U, 0x0105U, 0x0106U, 0x0107U, 0x0108U, 0x0109U, 0x010AU, 0x010BU, 0x010CU, 0x010DU, 0x010EU, 0x010FU, 0x0112U, 0x0113U, 0x0114U,
    0x0115U, 0x0116U, 0x0117U, 0x0118U, 0x0119U, 0x011AU, 0x011BU, 0x011CU, 0x011DU, 0x011EU, 0x011FU, 0x0120U, 0x0121U, 0x0122U, 0x0123U, 0x0124U, 0x0125U, 0x0128U,
    0x0129U, 0x012AU, 0x012BU, 0x012CU, 0x012DU, 0x012EU, 0x012FU, 0x0130U, 0x0134U, 0x0135U, 0x0136U, 0x0137U, 0x0139U, 0x013AU, 0x013BU, 0x013CU, 0x013DU, 0x013EU,
    0x0143U, 0x0144U, 0x0145U, 0x0146U, 0x0147U, 0x0148U, 0x014CU, 0x014DU, 0x014EU, 0x014FU, 0x0150U, 0x0151U, 0x0154U, 0x0155U, 0x0156U, 0x0157U, 0x0158U, 0x0159U,
    0x015AU, 0x015BU, 0x015CU, 0x015DU, 0x015EU, 0x015FU, 0x0160U, 0x0161U, 0x0162U, 0x0163U, 0x0164U, 0x0165U, 0x0168U, 0x0169U, 0x016AU, 0x016BU, 0x016CU, 0x016DU,
    0x016EU, 0x016FU, 0x0170U, 0x0171U, 0x0172U, 0x0173U, 0x0174U, 0x0175U, 0x0176U, 0x0177U, 0x0178U, 0x0179U, 0x017AU, 0x017BU, 0x017CU, 0x017DU, 0x017EU, 0x01A0U,
    0x01A1U, 0x01AFU, 0x01B0U, 0x01CDU, 0x01CEU, 0x01CFU, 0x01D0U, 0x01D1U, 0x01D2U, 0x01D3U, 0x01D4U, 0x01D5U, 0x01D6U, 0x01D7U, 0x01D8U, 0x01D9U, 0x01DAU, 0x01DBU,
    0x01DCU, 0x01DEU, 0x01DFU, 0x01E0U, 0x01E1U, 0x01E2U, 0x01E3U, 0x01E6U, 0x01E7U, 0x01E8U, 0x01E9U, 0x01EAU, 0x01EBU, 0x01ECU, 0x01EDU, 0x01EEU, 0x01EFU, 0x01F0U,
    0x01F4U, 0x01F5U, 0x01F8U, 0x01F9U, 0x01FAU, 0x01FBU, 0x01FCU, 0x01FDU, 0x01FEU, 0x01FFU, 0x0200U, 0x0201U, 0x0202U, 0x0203U, 0x0204U, 0x0205U, 0x0206U, 0x0207U,
    0x0208U, 0x0209U, 0x020AU, 0x020BU, 0x020CU, 0x020DU, 0x020EU, 0x020FU, 0x0210U, 0x0211U, 0x0212U, 0x0213U, 0x0214U, 0x0215U, 0x0216U, 0x0217U, 0x0218U, 0x0219U,
    0x021AU, 0x021BU, 0x021EU, 0x021FU, 0x0226U, 0x0227U, 0x0228U, 0x0229U, 0x022AU, 0x022BU, 0x022CU, 0x022DU, 0x022EU, 0x022FU, 0x0230U, 0x0231U, 0x0232U, 0x0233U,
    0x0340U, 0x0341U, 0x0343U, 0x0344U, 0x0374U, 0x037EU, 0x0385U, 0x0386U, 0x0387U, 0x0388U, 0x0389U, 0x038AU, 0x038CU, 0x038EU, 0x038FU, 0x0390U, 0x03AAU, 0x03ABU,
    0x03ACU, 0x03ADU, 0x03AEU, 0x03AFU, 0x03B0U, 0x03CAU, 0x03CBU, 0x03CCU, 0x03CDU, 0x03CEU, 0x03D3U, 0x03D4U, 0x0400U, 0x0401U, 0x0403U, 0x0407U, 0x040CU, 0x040DU,
    0x040EU, 0x0419U, 0x0439U, 0x0450U, 0x0451U, 0x0453U, 0x0457U, 0x045CU, 0x045DU, 0x045EU, 0x0476U, 0x0477U, 0x04C1U, 0x04C2U, 0x04D0U, 0x04D1U, 0x04D2U, 0x04D3U,
    0x04D6U, 0x04D7U, 0x04DAU, 0x04DBU, 0x04DCU, 0x04DDU, 0x04DEU, 0x04DFU, 0x04E2U, 0x04E3U, 0x04E4U, 0x04E5U, 0x04E6U, 0x04E7U, 0x04EAU, 0x04EBU, 0x04ECU, 0x04EDU,
    0x04EEU, 0x04EFU, 0x04F0U, 0x04F1U, 0x04F2U, 0x04F3U, 0x04F4U, 0x04F5U, 0x04F8U, 0x04F9U, 0x0622U, 0x0623U, 0x0624U, 0x0625U, 0x0626U, 0x06C0U, 0x06C2U, 0x06D3U,
    0x0929U, 0x0931U, 0x0934U, 0x0958U, 0x0959U, 0x095AU, 0x095BU, 0x095CU, 0x095DU, 0x095EU, 0x095FU, 0x09CBU, 0x09CCU, 0x09DCU, 0x09DDU, 0x09DFU, 0x0A33U, 0x0A36U,
    0x0A59U, 0x0A5AU, 0x0A5BU, 0x0A5EU, 0x0B48U, 0x0B4BU, 0x0B4CU, 0x0B5CU, 0x0B5DU, 0x0B94U, 0x0BCAU, 0x0BCBU, 0x0BCCU, 0x0C48U, 0x0CC0U, 0x0CC7U, 0x0CC8U, 0x0CCAU,
    0x0CCBU, 0x0D4AU, 0x0D4BU, 0x0D4CU, 0x0DDAU, 0x0DDCU, 0x0DDDU, 0x0DDEU, 0x0F43U, 0x0F4DU, 0x0F52U, 0x0F57U, 0x0F5CU, 0x0F69U, 0x0F73U, 0x0F75U, 0x0F76U, 0x0F78U,
    0x0F81U, 0x0F93U, 0x0F9DU, 0x0FA2U, 0x0FA7U, 0x0FACU, 0x0FB9U, 0x1026U, 0x1B06U, 0x1B08U, 0x1B0AU, 0x1B0CU, 0x1B0EU, 0x1B12U, 0x1B3BU, 0x1B3DU, 0x1B40U, 0x1B41U,
    0x1B43U, 0x1E00U, 0x1E01U, 0x1E02U, 0x1E03U, 0x1E04U, 0x1E05U, 0x1E06U, 0x1E07U, 0x1E08U, 0x1E09U, 0x1E0AU, 0x1E0BU, 0x1E0CU, 0x1E0DU, 0x1E0EU, 0x1E0FU, 0x1E10U,
    0x1E11U, 0x1E12U, 0x1E13U, 0x1E14U, 0x1E15U, 0x1E16U, 0x1E17U, 0x1E18U, 0x1E19U, 0x1E1AU, 0x1E1BU, 0x1E1CU, 0x1E1DU, 0x1E1EU, 0x1E1FU, 0x1E20U, 0x1E21U, 0x1E22U,
    0x1E23U, 0x1E24U, 0x1E25U, 0x1E26U, 0x1E27U, 0x1E28U, 0x1E29U, 0x1E2AU, 0x1E2BU, 0x1E2CU, 0x1E2DU, 0x1E2EU, 0x1E2FU, 0x1E30U, 0x1E31U, 0x1E32U, 0x1E33U, 0x1E34U,
    0x1E35U, 0x1E36U, 0x1E37U, 0x1E38U, 0x1E39U, 0x1E3AU, 0x1E3BU, 0x1E3CU, 0x1E3DU, 0x1E3EU, 0x1E3FU, 0x1E40U, 0x1E41U, 0x1E42U, 0x1E43U, 0x1E44U, 0x1E45U, 0x1E46U,
    0x1E47U, 0x1E48U, 0x1E49U, 0x1E4AU, 0x1E4BU, 0x1E4CU, 0x1E4DU, 0x1E4EU, 0x1E4FU, 0x1E50U, 0x1E51U, 0x1E52U, 0x1E53U, 0x1E54U, 0x1E55U, 0x1E56U, 0x1E57U, 0x1E58U,
    0x1E59U, 0x1E5AU, 0x1E5BU, 0x1E5CU, 0x1E5DU, 0x1E5EU, 0x1E5FU, 0x1E60U, 0x1E61U, 0x1E62U, 0x1E63U, 0x1E64U, 0x1E65U, 0x1E66U, 0x1E67U, 0x1E68U, 0x1E69U, 0x1E6AU,
    0x1E6BU, 0x1E6CU, 0x1E6DU, 0x1E6EU, 0x1E6FU, 0x1E70U, 0x1E71U, 0x1E72U, 0x1E73U, 0x1E74U, 0x1E75U, 0x1E76U, 0x1E77U, 0x1E78U, 0x1E79U, 0x1E7AU, 0x1E7BU, 0x1E7CU,
    0x1E7DU, 0x1E7EU, 0x1E7FU, 0x1E80U, 0x1E81U, 0x1E82U, 0x1E83U, 0x1E84U, 0x1E85U, 0x1E86U, 0x1E87U, 0x1E88U, 0x1E89U, 0x1E8AU, 0x1E8BU, 0x1E8CU, 0x1E8DU, 0x1E8EU,
    0x1E8FU, 0x1E90U, 0x1E91U, 0x1E92U, 0x1E93U, 0x1E94U, 0x1E95U, 0x1E96U, 0x1E97U, 0x1E98U, 0x1E99U, 0x1E9BU, 0x1EA0U, 0x1EA1U, 0x1EA2U, 0x1EA3U, 0x1EA4U, 0x1EA5U,
    0x1EA6U, 0x1EA7U, 0x1EA8U, 0x1EA9U, 0x1EAAU, 0x1EABU, 0x1EACU, 0x1EADU, 0x1EAEU, 0x1EAFU, 0x1EB0U, 0x1EB1U, 0x1EB2U, 0x1EB3U, 0x1EB4U, 0x1EB5U, 0x1EB6U, 0x1EB7U,
    0x1EB8U, 0x1EB9U, 0x1EBAU, 0x1EBBU, 0x1EBCU, 0x1EBDU, 0x1EBEU, 0x1EBFU, 0x1EC0U, 0x1EC1U, 0x1EC2U, 0x1EC3U, 0x1EC4U, 0x1EC5U, 0x1EC6U, 0x1EC7U, 0x1EC8U, 0x1EC9U,
    0x1ECAU, 0x1ECBU, 0x1ECCU, 0x1ECDU, 0x1ECEU, 0x1ECFU, 0x1ED0U, 0x1ED1U, 0x1ED2U, 0x1ED3U, 0x1ED4U, 0x1ED5U, 0x1ED6U, 0x1ED7U, 0x1ED8U, 0x1ED9U, 0x1EDAU, 0x1EDBU,
    0x1EDCU, 0x1EDDU, 0x1EDEU, 0x1EDFU, 0x1EE0U, 0x1EE1U, 0x1EE2U, 0x1EE3U, 0x1EE4U, 0x1EE5U, 0x1EE6U, 0x1EE7U, 0x1EE8U, 0x1EE9U, 0x1EEAU, 0x1EEBU, 0x1EECU, 0x1EEDU,
    0x1EEEU, 0x1EEFU, 0x1EF0U, 0x1EF1U, 0x1EF2U, 0x1EF3U, 0x1EF4U, 0x1EF5U, 0x1EF6U, 0x1EF7U, 0x1EF8U, 0x1EF9U, 0x1F00U, 0x1F01U, 0x1F02U, 0x1F03U, 0x1F04U, 0x1F05U,
    0x1F06U, 0x1F07U, 0x1F08U, 0x1F09U, 0x1F0AU, 0x1F0BU, 0x1F0CU, 0x1F0DU, 0x1F0EU, 0x1F0FU, 0x1F10U, 0x1F11U, 0x1F12U, 0x1F13U, 0x1F14U, 0x1F15U, 0x1F18U, 0x1F19U,
    0x1F1AU, 0x1F1BU, 0x1F1CU, 0x1F1DU, 0x1F20U, 0x1F21U, 0x1F22U, 0x1F23U, 0x1F24U, 0x1F25U, 0x1F26U, 0x1F27U, 0x1F28U, 0x1F29U, 0x1F2AU, 0x1F2BU, 0x1F2CU, 0x1F2DU,
    0x1F2EU, 0x1F2FU, 0x1F30U, 0x1F31U, 0x1F32U, 0x1F33U, 0x1F34U, 0x1F35U, 0x1F36U, 0x1F37U, 0x1F38U, 0x1F39U, 0x1F3AU, 0x1F3BU, 0x1F3CU, 0x1F3DU, 0x1F3EU, 0x1F3FU,
    0x1F40U, 0x1F41U, 0x1F42U, 0x1F43U, 0x1F44U, 0x1F45U, 0x1F48U, 0x1F49U, 0x1F4AU, 0x1F4BU, 0x1F4CU, 0x1F4DU, 0x1F50U, 0x1F51U, 0x1F52U, 0x1F53U, 0x1F54U, 0x1F55U,
    0x1F56U, 0x1F57U, 0x1F59U, 0x1F5BU, 0x1F5DU, 0x1F5FU, 0x1F60U, 0x1F61U, 0x1F62U, 0x1F63U, 0x1F64U, 0x1F65U, 0x1F66U, 0x1F67U, 0x1F68U, 0x1F69U, 0x1F6AU, 0x1F6BU,
    0x1F6CU, 0x1F6DU, 0x1F6EU, 0x1F6FU, 0x1F70U, 0x1F71U, 0x1F72U, 0x1F73U, 0x1F74U, 0x1F75U, 0x1F76U, 0x1F77U, 0x1F78U, 0x1F79U, 0x1F7AU, 0x1F7BU, 0x1F7CU, 0x1F7DU,
    0x1F80U, 0x1F81U, 0x1F82U, 0x1F83U, 0x1F84U, 0x1F85U, 0x1F86U, 0x1F87U, 0x1F88U, 0x1F89U, 0x1F8AU, 0x1F8BU, 0x1F8CU, 0x1F8DU, 0x1F8EU, 0x1F8FU, 0x1F90U, 0x1F91U,
    0x1F92U, 0x1F93U, 0x1F94U, 0x1F95U, 0x1F96U, 0x1F97U, 0x1F98U, 0x1F99U, 0x1F9AU, 0x1F9BU, 0x1F9CU, 0x1F9DU, 0x1F9EU, 0x1F9FU, 0x1FA0U, 0x1FA1U, 0x1FA2U, 0x1FA3U,
    0x1FA4U, 0x1FA5U, 0x1FA6U, 0x1FA7U, 0x1FA8U, 0x1FA9U, 0x1FAAU, 0x1FABU, 0x1FACU, 0x1FADU, 0x1FAEU, 0x1FAFU, 0x1FB0U, 0x1FB1U, 0x1FB2U, 0x1FB3U, 0x1FB4U, 0x1FB6U,
    0x1FB7U, 0x1FB8U, 0x1FB9U, 0x1FBAU, 0x1FBBU, 0x1FBCU, 0x1FBEU, 0x1FC1U, 0x1FC2U, 0x1FC3U, 0x1FC4U, 0x1FC6U, 0x1FC7U, 0x1FC8U, 0x1FC9U, 0x1FCAU, 0x1FCBU, 0x1FCCU,
    0x1FCDU, 0x1FCEU, 0x1FCFU, 0x1FD0U, 0x1FD1U, 0x1FD2U, 0x1FD3U, 0x1FD6U, 0x1FD7U, 0x1FD8U, 0x1FD9U, 0x1FDAU, 0x1FDBU, 0x1FDDU, 0x1FDEU, 0x1FDFU, 0x1FE0U, 0x1FE1U,
    0x1FE2U, 0x1FE3U, 0x1FE4U, 0x1FE5U, 0x1FE6U, 0x1FE7U, 0x1FE8U, 0x1FE9U, 0x1FEAU, 0x1FEBU, 0x1FECU, 0x1FEDU, 0x1FEEU, 0x1FEFU, 0x1FF2U, 0x1FF3U, 0x1FF4U, 0x1FF6U,
    0x1FF7U, 0x1FF8U, 0x1FF9U, 0x1FFAU, 0x1FFBU, 0x1FFCU, 0x1FFDU, 0x2000U, 0x2001U, 0x2126U, 0x212AU, 0x212BU, 0x219AU, 0x219BU, 0x21AEU, 0x21CDU, 0x21CEU, 0x21CFU,
    0x2204U, 0x2209U, 0x220CU, 0x2224U, 0x2226U, 0x2241U, 0x2244U, 0x2247U, 0x2249U, 0x2260U, 0x2262U, 0x226DU, 0x226EU, 0x226FU, 0x2270U, 0x2271U, 0x2274U, 0x2275U,
    0x2278U, 0x2279U, 0x2280U, 0x2281U, 0x2284U, 0x2285U, 0x2288U, 0x2289U, 0x22ACU, 0x22ADU, 0x22AEU, 0x22AFU, 0x22E0U, 0x22E1U, 0x22E2U, 0x22E3U, 0x22EAU, 0x22EBU,
    0x22ECU, 0x22EDU, 0x2329U, 0x232AU, 0x2ADCU, 0x304CU, 0x304EU, 0x3050U, 0x3052U, 0x3054U, 0x3056U, 0x3058U, 0x305AU, 0x305CU, 0x305EU, 0x3060U, 0x3062U, 0x3065U,
    0x3067U, 0x3069U, 0x3070U, 0x3071U, 0x3073U, 0x3074U, 0x3076U, 0x3077U, 0x3079U, 0x307AU, 0x307CU, 0x307DU, 0x3094U, 0x309EU, 0x30ACU, 0x30AEU, 0x30B0U, 0x30B2U,
    0x30B4U, 0x30B6U, 0x30B8U, 0x30BAU, 0x30BCU, 0x30BEU, 0x30C0U, 0x30C2U, 0x30C5U, 0x30C7U, 0x30C9U, 0x30D0U, 0x30D1U, 0x30D3U, 0x30D4U, 0x30D6U, 0x30D7U, 0x30D9U,
    0x30DAU, 0x30DCU, 0x30DDU, 0x30F4U, 0x30F7U, 0x30F8U, 0x30F9U, 0x30FAU, 0x30FEU, 0xF900U, 0xF901U, 0xF902U, 0xF903U, 0xF904U, 0xF905U, 0xF906U, 0xF907U, 0xF908U,
    0xF909U, 0xF90AU, 0xF90BU, 0xF90CU, 0xF90DU, 0xF90EU, 0xF90FU, 0xF910U, 0xF911U, 0xF912U, 0xF913U, 0xF914U, 0xF915U, 0xF916U, 0xF917U, 0xF918U, 0xF919U, 0xF91AU,
    0xF91BU, 0xF91CU, 0xF91DU, 0xF91EU, 0xF91FU, 0xF920U, 0xF921U, 0xF922U, 0xF923U, 0xF924U, 0xF925U, 0xF926U, 0xF927U, 0xF928U, 0xF929U, 0xF92AU, 0xF92BU, 0xF92CU,
    0xF92DU, 0xF92EU, 0xF92FU, 0xF930U, 0xF931U, 0xF932U, 0xF933U, 0xF934U, 0xF935U, 0xF936U, 0xF937U, 0xF938U, 0xF939U, 0xF93AU, 0xF93BU, 0xF93CU, 0xF93DU, 0xF93EU,
    0xF93FU, 0xF940U, 0xF941U, 0xF942U, 0xF943U, 0xF944U, 0xF945U, 0xF946U, 0xF947U, 0xF948U, 0xF949U, 0xF94AU, 0xF94BU, 0xF94CU, 0xF94DU, 0xF94EU, 0xF94FU, 0xF950U,
    0xF951U, 0xF952U, 0xF953U, 0xF954U, 0xF955U, 0xF956U, 0xF957U, 0xF958U, 0xF959U, 0xF95AU, 0xF95BU, 0xF95CU, 0xF95DU, 0xF95EU, 0xF95FU, 0xF960U, 0xF961U, 0xF962U,
    0xF963U, 0xF964U, 0xF965U, 0xF966U, 0xF967U, 0xF968U, 0xF969U, 0xF96AU, 0xF96BU, 0xF96CU, 0xF96DU, 0xF96EU, 0xF96FU, 0xF970U, 0xF971U, 0xF972U, 0xF973U, 0xF974U,
    0xF975U, 0xF976U, 0xF977U, 0xF978U, 0xF979U, 0xF97AU, 0xF97BU, 0xF97CU, 0xF97DU, 0xF97EU, 0xF97FU, 0xF980U, 0xF981U, 0xF982U, 0xF983U, 0xF984U, 0xF985U, 0xF986U,
    0xF987U, 0xF988U, 0xF989U, 0xF98AU, 0xF98BU, 0xF98CU, 0xF98DU, 0xF98EU, 0xF98FU, 0xF990U, 0xF991U, 0xF992U, 0xF993U, 0xF994U, 0xF995U, 0xF996U, 0xF997U, 0xF998U,
    0xF999U, 0xF99AU, 0xF99BU, 0xF99CU, 0xF99DU, 0xF99EU, 0xF99FU, 0xF9A0U, 0xF9A1U, 0xF9A2U, 0xF9A3U, 0xF9A4U, 0xF9A5U, 0xF9A6U, 0xF9A7U, 0xF9A8U, 0xF9A9U, 0xF9AAU,
    0xF9ABU, 0xF9ACU, 0xF9ADU, 0xF9AEU, 0xF9AFU, 0xF9B0U, 0xF9B1U, 0xF9B2U, 0xF9B3U, 0xF9B4U, 0xF9B5U, 0xF9B6U, 0xF9B7U, 0xF9B8U, 0xF9B9U, 0xF9BAU, 0xF9BBU, 0xF9BCU,
    0xF9BDU, 0xF9BEU, 0xF9BFU, 0xF9C0U, 0xF9C1U, 0xF9C2U, 0xF9C3U, 0xF9C4U, 0xF9C5U, 0xF9C6U, 0xF9C7U, 0xF9C8U, 0xF9C9U, 0xF9CAU, 0xF9CBU, 0xF9CCU, 0xF9CDU, 0xF9CEU,
    0xF9CFU, 0xF9D0U, 0xF9D1U, 0xF9D2U, 0xF9D3U, 0xF9D4U, 0xF9D5U, 0xF9D6U, 0xF9D7U, 0xF9D8U, 0xF9D9U, 0xF9DAU, 0xF9DBU, 0xF9DCU, 0xF9DDU, 0xF9DEU, 0xF9DFU, 0xF9E0U,
    0xF9E1U, 0xF9E2U, 0xF9E3U, 0xF9E4U, 0xF9E5U, 0xF9E6U, 0xF9E7U, 0xF9E8U, 0xF9E9U, 0xF9EAU, 0xF9EBU, 0xF9ECU, 0xF9EDU, 0xF9EEU, 0xF9EFU, 0xF9F0U, 0xF9F1U, 0xF9F2U,
    0xF9F3U, 0xF9F4U, 0xF9F5U, 0xF9F6U, 0xF9F7U, 0xF9F8U, 0xF9F9U, 0xF9FAU, 0xF9FBU, 0xF9FCU, 0xF9FDU, 0xF9FEU, 0xF9FFU, 0xFA00U, 0xFA01U, 0xFA02U, 0xFA03U, 0xFA04U,
    0xFA05U, 0xFA06U, 0xFA07U, 0xFA08U, 0xFA09U, 0xFA0AU, 0xFA0BU, 0xFA0CU, 0xFA0DU, 0xFA10U, 0xFA12U, 0xFA15U, 0xFA16U, 0xFA17U, 0xFA18U, 0xFA19U, 0xFA1AU, 0xFA1BU,
    0xFA1CU, 0xFA1DU, 0xFA1EU, 0xFA20U, 0xFA22U, 0xFA25U, 0xFA26U, 0xFA2AU, 0xFA2BU, 0xFA2CU, 0xFA2DU, 0xFA2EU, 0xFA2FU, 0xFA30U, 0xFA31U, 0xFA32U, 0xFA33U, 0xFA34U,
    0xFA35U, 0xFA36U, 0xFA37U, 0xFA38U, 0xFA39U, 0xFA3AU, 0xFA3BU, 0xFA3CU, 0xFA3DU, 0xFA3EU, 0xFA3FU, 0xFA40U, 0xFA41U, 0xFA42U, 0xFA43U, 0xFA44U, 0xFA45U, 0xFA46U,
    0xFA47U, 0xFA48U, 0xFA49U, 0xFA4AU, 0xFA4BU, 0xFA4CU, 0xFA4DU, 0xFA4EU, 0xFA4FU, 0xFA50U, 0xFA51U, 0xFA52U, 0xFA53U, 0xFA54U, 0xFA55U, 0xFA56U, 0xFA57U, 0xFA58U,
    0xFA59U, 0xFA5AU, 0xFA5BU, 0xFA5CU, 0xFA5DU, 0xFA5EU, 0xFA5FU, 0xFA60U, 0xFA61U, 0xFA62U, 0xFA63U, 0xFA64U, 0xFA65U, 0xFA66U, 0xFA67U, 0xFA68U, 0xFA69U, 0xFA6AU,
    0xFA6BU, 0xFA6DU, 0xFA70U, 0xFA71U, 0xFA72U, 0xFA73U, 0xFA74U, 0xFA75U, 0xFA76U, 0xFA77U, 0xFA78U, 0xFA79U, 0xFA7AU, 0xFA7BU, 0xFA7CU, 0xFA7DU, 0xFA7EU, 0xFA7FU,
    0xFA80U, 0xFA81U, 0xFA82U, 0xFA83U, 0xFA84U, 0xFA85U, 0xFA86U, 0xFA87U, 0xFA88U, 0xFA89U, 0xFA8AU, 0xFA8BU, 0xFA8CU, 0xFA8DU, 0xFA8EU, 0xFA8FU, 0xFA90U, 0xFA91U,
    0xFA92U, 0xFA93U, 0xFA94U, 0xFA95U, 0xFA96U, 0xFA97U, 0xFA98U, 0xFA99U, 0xFA9AU, 0xFA9BU, 0xFA9CU, 0xFA9DU, 0xFA9EU, 0xFA9FU, 0xFAA0U, 0xFAA1U, 0xFAA2U, 0xFAA3U,
    0xFAA4U, 0xFAA5U, 0xFAA6U, 0xFAA7U, 0xFAA8U, 0xFAA9U, 0xFAAAU, 0xFAABU, 0xFAACU, 0xFAADU, 0xFAAEU, 0xFAAFU, 0xFAB0U, 0xFAB1U, 0xFAB2U, 0xFAB3U, 0xFAB4U, 0xFAB5U,
    0xFAB6U, 0xFAB7U, 0xFAB8U, 0xFAB9U, 0xFABAU, 0xFABBU, 0xFABCU, 0xFABDU, 0xFABEU, 0xFABFU, 0xFAC0U, 0xFAC1U, 0xFAC2U, 0xFAC3U, 0xFAC4U, 0xFAC5U, 0xFAC6U, 0xFAC7U,
    0xFAC8U, 0xFAC9U, 0xFACAU, 0xFACBU, 0xFACCU, 0xFACDU, 0xFACEU, 0xFAD2U, 0xFAD3U, 0xFAD4U, 0xFAD8U, 0xFAD9U, 0xFB1DU, 0xFB1FU, 0xFB2AU, 0xFB2BU, 0xFB2CU, 0xFB2DU,
    0xFB2EU, 0xFB2FU, 0xFB30U, 0xFB31U, 0xFB32U, 0xFB33U, 0xFB34U, 0xFB35U, 0xFB36U, 0xFB38U, 0xFB39U, 0xFB3AU, 0xFB3BU, 0xFB3CU, 0xFB3EU, 0xFB40U, 0xFB41U, 0xFB43U,
    0xFB44U, 0xFB46U, 0xFB47U, 0xFB48U, 0xFB49U, 0xFB4AU, 0xFB4BU, 0xFB4CU, 0xFB4DU, 0xFB4EU};

/* (pool index << 2) | (length - 1) for each key above. */
static const uint16_t xx_fat_decomp_refs[1486] = {
    0x0001U, 0x0009U, 0x0011U, 0x0019U, 0x0021U, 0x0029U, 0x0031U, 0x0039U, 0x0041U, 0x0049U, 0x0051U, 0x0059U, 0x0061U, 0x0069U, 0x0071U, 0x0079U, 0x0081U, 0x0089U,
    0x0091U, 0x0099U, 0x00A1U, 0x00A9U, 0x00B1U, 0x00B9U, 0x00C1U, 0x00C9U, 0x00D1U, 0x00D9U, 0x00E1U, 0x00E9U, 0x00F1U, 0x00F9U, 0x0101U, 0x0109U, 0x0111U, 0x0119U,
    0x0121U, 0x0129U, 0x0131U, 0x0139U, 0x0141U, 0x0149U, 0x0151U, 0x0159U, 0x0161U, 0x0169U, 0x0171U, 0x0179U, 0x0181U, 0x0189U, 0x0191U, 0x0199U, 0x01A1U, 0x01A9U,
    0x01B1U, 0x01B9U, 0x01C1U, 0x01C9U, 0x01D1U, 0x01D9U, 0x01E1U, 0x01E9U, 0x01F1U, 0x01F9U, 0x0201U, 0x0209U, 0x0211U, 0x0219U, 0x0221U, 0x0229U, 0x0231U, 0x0239U,
    0x0241U, 0x0249U, 0x0251U, 0x0259U, 0x0261U, 0x0269U, 0x0271U, 0x0279U, 0x0281U, 0x0289U, 0x0291U, 0x0299U, 0x02A1U, 0x02A9U, 0x02B1U, 0x02B9U, 0x02C1U, 0x02C9U,
    0x02D1U, 0x02D9U, 0x02E1U, 0x02E9U, 0x02F1U, 0x02F9U, 0x0301U, 0x0309U, 0x0311U, 0x0319U, 0x0321U, 0x0329U, 0x0331U, 0x0339U, 0x0341U, 0x0349U, 0x0351U, 0x0359U,
    0x0361U, 0x0369U, 0x0371U, 0x0379U, 0x0381U, 0x0389U, 0x0391U, 0x0399U, 0x03A1U, 0x03A9U, 0x03B1U, 0x03B9U, 0x03C1U, 0x03C9U, 0x03D1U, 0x03D9U, 0x03E1U, 0x03E9U,
    0x03F1U, 0x03F9U, 0x0401U, 0x0409U, 0x0411U, 0x0419U, 0x0421U, 0x0429U, 0x0431U, 0x0439U, 0x0441U, 0x0449U, 0x0451U, 0x0459U, 0x0461U, 0x0469U, 0x0471U, 0x0479U,
    0x0481U, 0x0489U, 0x0491U, 0x0499U, 0x04A1U, 0x04A9U, 0x04B1U, 0x04B9U, 0x04C1U, 0x04C9U, 0x04D1U, 0x04D9U, 0x04E1U, 0x04E9U, 0x04F1U, 0x04F9U, 0x0501U, 0x0509U,
    0x0511U, 0x0519U, 0x0521U, 0x0529U, 0x0531U, 0x0539U, 0x0541U, 0x0549U, 0x0551U, 0x0559U, 0x0561U, 0x056AU, 0x0576U, 0x0582U, 0x058EU, 0x059AU, 0x05A6U, 0x05B2U,
    0x05BEU, 0x05CAU, 0x05D6U, 0x05E2U, 0x05EEU, 0x05F9U, 0x0601U, 0x0609U, 0x0611U, 0x0619U, 0x0621U, 0x0629U, 0x0631U, 0x063AU, 0x0646U, 0x0651U, 0x0659U, 0x0661U,
    0x0669U, 0x0671U, 0x0679U, 0x0681U, 0x068AU, 0x0696U, 0x06A1U, 0x06A9U, 0x06B1U, 0x06B9U, 0x06C1U, 0x06C9U, 0x06D1U, 0x06D9U, 0x06E1U, 0x06E9U, 0x06F1U, 0x06F9U,
    0x0701U, 0x0709U, 0x0711U, 0x0719U, 0x0721U, 0x0729U, 0x0731U, 0x0739U, 0x0741U, 0x0749U, 0x0751U, 0x0759U, 0x0761U, 0x0769U, 0x0771U, 0x0779U, 0x0781U, 0x0789U,
    0x0791U, 0x0799U, 0x07A1U, 0x07A9U, 0x07B1U, 0x07B9U, 0x07C1U, 0x07C9U, 0x07D2U, 0x07DEU, 0x07EAU, 0x07F6U, 0x0801U, 0x0809U, 0x0812U, 0x081EU, 0x0829U, 0x0831U,
    0x0838U, 0x083CU, 0x0840U, 0x0845U, 0x084CU, 0x0850U, 0x0855U, 0x085DU, 0x0864U, 0x0869U, 0x0871U, 0x0879U, 0x0881U, 0x0889U, 0x0891U, 0x089AU, 0x08A5U, 0x08ADU,
    0x08B5U, 0x08BDU, 0x08C5U, 0x08CDU, 0x08D6U, 0x08E1U, 0x08E9U, 0x08F1U, 0x08F9U, 0x0901U, 0x0909U, 0x0911U, 0x0919U, 0x0921U, 0x0929U, 0x0931U, 0x0939U, 0x0941U,
    0x0949U, 0x0951U, 0x0959U, 0x0961U, 0x0969U, 0x0971U, 0x0979U, 0x0981U, 0x0989U, 0x0991U, 0x0999U, 0x09A1U, 0x09A9U, 0x09B1U, 0x09B9U, 0x09C1U, 0x09C9U, 0x09D1U,
    0x09D9U, 0x09E1U, 0x09E9U, 0x09F1U, 0x09F9U, 0x0A01U, 0x0A09U, 0x0A11U, 0x0A19U, 0x0A21U, 0x0A29U, 0x0A31U, 0x0A39U, 0x0A41U, 0x0A49U, 0x0A51U, 0x0A59U, 0x0A61U,
    0x0A69U, 0x0A71U, 0x0A79U, 0x0A81U, 0x0A89U, 0x0A91U, 0x0A99U, 0x0AA1U, 0x0AA9U, 0x0AB1U, 0x0AB9U, 0x0AC1U, 0x0AC9U, 0x0AD1U, 0x0AD9U, 0x0AE1U, 0x0AE9U, 0x0AF1U,
    0x0AF9U, 0x0B01U, 0x0B09U, 0x0B11U, 0x0B19U, 0x0B21U, 0x0B29U, 0x0B31U, 0x0B39U, 0x0B41U, 0x0B49U, 0x0B51U, 0x0B59U, 0x0B61U, 0x0B69U, 0x0B71U, 0x0B79U, 0x0B81U,
    0x0B89U, 0x0B91U, 0x0B99U, 0x0BA1U, 0x0BA9U, 0x0BB1U, 0x0BB9U, 0x0BC1U, 0x0BC9U, 0x0BD1U, 0x0BD9U, 0x0BE1U, 0x0BE9U, 0x0BF1U, 0x0BF9U, 0x0C01U, 0x0C09U, 0x0C11U,
    0x0C1AU, 0x0C25U, 0x0C2DU, 0x0C35U, 0x0C3DU, 0x0C45U, 0x0C4EU, 0x0C59U, 0x0C61U, 0x0C69U, 0x0C71U, 0x0C79U, 0x0C81U, 0x0C89U, 0x0C91U, 0x0C99U, 0x0CA1U, 0x0CA9U,
    0x0CB1U, 0x0CB9U, 0x0CC1U, 0x0CC9U, 0x0CD1U, 0x0CD9U, 0x0CE1U, 0x0CE9U, 0x0CF1U, 0x0CF9U, 0x0D01U, 0x0D09U, 0x0D11U, 0x0D19U, 0x0D21U, 0x0D29U, 0x0D31U, 0x0D39U,
    0x0D41U, 0x0D49U, 0x0D51U, 0x0D59U, 0x0D61U, 0x0D69U, 0x0D71U, 0x0D79U, 0x0D81U, 0x0D8AU, 0x0D96U, 0x0DA1U, 0x0DA9U, 0x0DB1U, 0x0DB9U, 0x0DC1U, 0x0DC9U, 0x0DD1U,
    0x0DD9U, 0x0DE1U, 0x0DE9U, 0x0DF2U, 0x0DFEU, 0x0E0AU, 0x0E16U, 0x0E21U, 0x0E29U, 0x0E31U, 0x0E39U, 0x0E42U, 0x0E4EU, 0x0E59U, 0x0E61U, 0x0E69U, 0x0E71U, 0x0E79U,
    0x0E81U, 0x0E89U, 0x0E91U, 0x0E99U, 0x0EA1U, 0x0EA9U, 0x0EB1U, 0x0EB9U, 0x0EC1U, 0x0EC9U, 0x0ED1U, 0x0EDAU, 0x0EE6U, 0x0EF1U, 0x0EF9U, 0x0F01U, 0x0F09U, 0x0F11U,
    0x0F19U, 0x0F21U, 0x0F29U, 0x0F32U, 0x0F3EU, 0x0F49U, 0x0F51U, 0x0F59U, 0x0F61U, 0x0F69U, 0x0F71U, 0x0F79U, 0x0F81U, 0x0F89U, 0x0F91U, 0x0F99U, 0x0FA1U, 0x0FA9U,
    0x0FB1U, 0x0FB9U, 0x0FC1U, 0x0FC9U, 0x0FD1U, 0x0FDAU, 0x0FE6U, 0x0FF2U, 0x0FFEU, 0x100AU, 0x1016U, 0x1022U, 0x102EU, 0x1039U, 0x1041U, 0x1049U, 0x1051U, 0x1059U,
    0x1061U, 0x1069U, 0x1071U, 0x107AU, 0x1086U, 0x1091U, 0x1099U, 0x10A1U, 0x10A9U, 0x10B1U, 0x10B9U, 0x10C2U, 0x10CEU, 0x10DAU, 0x10E6U, 0x10F2U, 0x10FEU, 0x1109U,
    0x1111U, 0x1119U, 0x1121U, 0x1129U, 0x1131U, 0x1139U, 0x1141U, 0x1149U, 0x1151U, 0x1159U, 0x1161U, 0x1169U, 0x1171U, 0x117AU, 0x1186U, 0x1192U, 0x119EU, 0x11A9U,
    0x11B1U, 0x11B9U, 0x11C1U, 0x11C9U, 0x11D1U, 0x11D9U, 0x11E1U, 0x11E9U, 0x11F1U, 0x11F9U, 0x1201U, 0x1209U, 0x1211U, 0x1219U, 0x1221U, 0x1229U, 0x1231U, 0x1239U,
    0x1241U, 0x1249U, 0x1251U, 0x1259U, 0x1261U, 0x1269U, 0x1271U, 0x1279U, 0x1281U, 0x1289U, 0x1291U, 0x1299U, 0x12A1U, 0x12A9U, 0x12B1U, 0x12B9U, 0x12C2U, 0x12CEU,
    0x12DAU, 0x12E6U, 0x12F2U, 0x12FEU, 0x130AU, 0x1316U, 0x1322U, 0x132EU, 0x133AU, 0x1346U, 0x1352U, 0x135EU, 0x136AU, 0x1376U, 0x1382U, 0x138EU, 0x139AU, 0x13A6U,
    0x13B1U, 0x13B9U, 0x13C1U, 0x13C9U, 0x13D1U, 0x13D9U, 0x13E2U, 0x13EEU, 0x13FAU, 0x1406U, 0x1412U, 0x141EU, 0x142AU, 0x1436U, 0x1442U, 0x144EU, 0x1459U, 0x1461U,
    0x1469U, 0x1471U, 0x1479U, 0x1481U, 0x1489U, 0x1491U, 0x149AU, 0x14A6U, 0x14B2U, 0x14BEU, 0x14CAU, 0x14D6U, 0x14E2U, 0x14EEU, 0x14FAU, 0x1506U, 0x1512U, 0x151EU,
    0x152AU, 0x1536U, 0x1542U, 0x154EU, 0x155AU, 0x1566U, 0x1572U, 0x157EU, 0x1589U, 0x1591U, 0x1599U, 0x15A1U, 0x15AAU, 0x15B6U, 0x15C2U, 0x15CEU, 0x15DAU, 0x15E6U,
    0x15F2U, 0x15FEU, 0x160AU, 0x1616U, 0x1621U, 0x1629U, 0x1631U, 0x1639U, 0x1641U, 0x1649U, 0x1651U, 0x1659U, 0x1661U, 0x1669U, 0x1672U, 0x167EU, 0x168AU, 0x1696U,
    0x16A2U, 0x16AEU, 0x16B9U, 0x16C1U, 0x16CAU, 0x16D6U, 0x16E2U, 0x16EEU, 0x16FAU, 0x1706U, 0x1711U, 0x1719U, 0x1722U, 0x172EU, 0x173AU, 0x1746U, 0x1751U, 0x1759U,
    0x1762U, 0x176EU, 0x177AU, 0x1786U, 0x1791U, 0x1799U, 0x17A2U, 0x17AEU, 0x17BAU, 0x17C6U, 0x17D2U, 0x17DEU, 0x17E9U, 0x17F1U, 0x17FAU, 0x1806U, 0x1812U, 0x181EU,
    0x182AU, 0x1836U, 0x1841U, 0x1849U, 0x1852U, 0x185EU, 0x186AU, 0x1876U, 0x1882U, 0x188EU, 0x1899U, 0x18A1U, 0x18AAU, 0x18B6U, 0x18C2U, 0x18CEU, 0x18DAU, 0x18E6U,
    0x18F1U, 0x18F9U, 0x1902U, 0x190EU, 0x191AU, 0x1926U, 0x1931U, 0x1939U, 0x1942U, 0x194EU, 0x195AU, 0x1966U, 0x1971U, 0x1979U, 0x1982U, 0x198EU, 0x199AU, 0x19A6U,
    0x19B2U, 0x19BEU, 0x19C9U, 0x19D2U, 0x19DEU, 0x19EAU, 0x19F5U, 0x19FDU, 0x1A06U, 0x1A12U, 0x1A1EU, 0x1A2AU, 0x1A36U, 0x1A42U, 0x1A4DU, 0x1A55U, 0x1A5EU, 0x1A6AU,
    0x1A76U, 0x1A82U, 0x1A8EU, 0x1A9AU, 0x1AA5U, 0x1AADU, 0x1AB5U, 0x1ABDU, 0x1AC5U, 0x1ACDU, 0x1AD5U, 0x1ADDU, 0x1AE5U, 0x1AEDU, 0x1AF5U, 0x1AFDU, 0x1B05U, 0x1B0DU,
    0x1B16U, 0x1B22U, 0x1B2FU, 0x1B3FU, 0x1B4FU, 0x1B5FU, 0x1B6FU, 0x1B7FU, 0x1B8EU, 0x1B9AU, 0x1BA7U, 0x1BB7U, 0x1BC7U, 0x1BD7U, 0x1BE7U, 0x1BF7U, 0x1C06U, 0x1C12U,
    0x1C1FU, 0x1C2FU, 0x1C3FU, 0x1C4FU, 0x1C5FU, 0x1C6FU, 0x1C7EU, 0x1C8AU, 0x1C97U, 0x1CA7U, 0x1CB7U, 0x1CC7U, 0x1CD7U, 0x1CE7U, 0x1CF6U, 0x1D02U, 0x1D0FU, 0x1D1FU,
    0x1D2FU, 0x1D3FU, 0x1D4FU, 0x1D5FU, 0x1D6EU, 0x1D7AU, 0x1D87U, 0x1D97U, 0x1DA7U, 0x1DB7U, 0x1DC7U, 0x1DD7U, 0x1DE5U, 0x1DEDU, 0x1DF6U, 0x1E01U, 0x1E0AU, 0x1E15U,
    0x1E1EU, 0x1E29U, 0x1E31U, 0x1E39U, 0x1E41U, 0x1E49U, 0x1E50U, 0x1E55U, 0x1E5EU, 0x1E69U, 0x1E72U, 0x1E7DU, 0x1E86U, 0x1E91U, 0x1E99U, 0x1EA1U, 0x1EA9U, 0x1EB1U,
    0x1EB9U, 0x1EC1U, 0x1EC9U, 0x1ED1U, 0x1ED9U, 0x1EE2U, 0x1EEEU, 0x1EF9U, 0x1F02U, 0x1F0DU, 0x1F15U, 0x1F1DU, 0x1F25U, 0x1F2DU, 0x1F35U, 0x1F3DU, 0x1F45U, 0x1F4DU,
    0x1F56U, 0x1F62U, 0x1F6DU, 0x1F75U, 0x1F7DU, 0x1F86U, 0x1F91U, 0x1F99U, 0x1FA1U, 0x1FA9U, 0x1FB1U, 0x1FB9U, 0x1FC1U, 0x1FC8U, 0x1FCEU, 0x1FD9U, 0x1FE2U, 0x1FEDU,
    0x1FF6U, 0x2001U, 0x2009U, 0x2011U, 0x2019U, 0x2021U, 0x2028U, 0x202CU, 0x2030U, 0x2034U, 0x2038U, 0x203DU, 0x2045U, 0x204DU, 0x2055U, 0x205DU, 0x2065U, 0x206DU,
    0x2075U, 0x207DU, 0x2085U, 0x208DU, 0x2095U, 0x209DU, 0x20A5U, 0x20ADU, 0x20B5U, 0x20BDU, 0x20C5U, 0x20CDU, 0x20D5U, 0x20DDU, 0x20E5U, 0x20EDU, 0x20F5U, 0x20FDU,
    0x2105U, 0x210DU, 0x2115U, 0x211DU, 0x2125U, 0x212DU, 0x2135U, 0x213DU, 0x2145U, 0x214DU, 0x2155U, 0x215DU, 0x2165U, 0x216DU, 0x2175U, 0x217DU, 0x2185U, 0x218DU,
    0x2195U, 0x219DU, 0x21A4U, 0x21A8U, 0x21ADU, 0x21B5U, 0x21BDU, 0x21C5U, 0x21CDU, 0x21D5U, 0x21DDU, 0x21E5U, 0x21EDU, 0x21F5U, 0x21FDU, 0x2205U, 0x220DU, 0x2215U,
    0x221DU, 0x2225U, 0x222DU, 0x2235U, 0x223DU, 0x2245U, 0x224DU, 0x2255U, 0x225DU, 0x2265U, 0x226DU, 0x2275U, 0x227DU, 0x2285U, 0x228DU, 0x2295U, 0x229DU, 0x22A5U,
    0x22ADU, 0x22B5U, 0x22BDU, 0x22C5U, 0x22CDU, 0x22D5U, 0x22DDU, 0x22E5U, 0x22EDU, 0x22F5U, 0x22FDU, 0x2305U, 0x230DU, 0x2315U, 0x231DU, 0x2325U, 0x232DU, 0x2335U,
    0x233DU, 0x2345U, 0x234DU, 0x2355U, 0x235DU, 0x2365U, 0x236DU, 0x2375U, 0x237DU, 0x2384U, 0x2388U, 0x238CU, 0x2390U, 0x2394U, 0x2398U, 0x239CU, 0x23A0U, 0x23A4U,
    0x23A8U, 0x23ACU, 0x23B0U, 0x23B4U, 0x23B8U, 0x23BCU, 0x23C0U, 0x23C4U, 0x23C8U, 0x23CCU, 0x23D0U, 0x23D4U, 0x23D8U, 0x23DCU, 0x23E0U, 0x23E4U, 0x23E8U, 0x23ECU,
    0x23F0U, 0x23F4U, 0x23F8U, 0x23FCU, 0x2400U, 0x2404U, 0x2408U, 0x240CU, 0x2410U, 0x2414U, 0x2418U, 0x241CU, 0x2420U, 0x2424U, 0x2428U, 0x242CU, 0x2430U, 0x2434U,
    0x2438U, 0x243CU, 0x2440U, 0x2444U, 0x2448U, 0x244CU, 0x2450U, 0x2454U, 0x2458U, 0x245CU, 0x2460U, 0x2464U, 0x2468U, 0x246CU, 0x2470U, 0x2474U, 0x2478U, 0x247CU,
    0x2480U, 0x2484U, 0x2488U, 0x248CU, 0x2490U, 0x2494U, 0x2498U, 0x249CU, 0x24A0U, 0x24A4U, 0x24A8U, 0x24ACU, 0x24B0U, 0x24B4U, 0x24B8U, 0x24BCU, 0x24C0U, 0x24C4U,
    0x24C8U, 0x24CCU, 0x24D0U, 0x24D4U, 0x24D8U, 0x24DCU, 0x24E0U, 0x24E4U, 0x24E8U, 0x24ECU, 0x24F0U, 0x24F4U, 0x24F8U, 0x24FCU, 0x2500U, 0x2504U, 0x2508U, 0x250CU,
    0x2510U, 0x2514U, 0x2518U, 0x251CU, 0x2520U, 0x2524U, 0x2528U, 0x252CU, 0x2530U, 0x2534U, 0x2538U, 0x253CU, 0x2540U, 0x2544U, 0x2548U, 0x254CU, 0x2550U, 0x2554U,
    0x2558U, 0x255CU, 0x2560U, 0x2564U, 0x2568U, 0x256CU, 0x2570U, 0x2574U, 0x2578U, 0x257CU, 0x2580U, 0x2584U, 0x2588U, 0x258CU, 0x2590U, 0x2594U, 0x2598U, 0x259CU,
    0x25A0U, 0x25A4U, 0x25A8U, 0x25ACU, 0x25B0U, 0x25B4U, 0x25B8U, 0x25BCU, 0x25C0U, 0x25C4U, 0x25C8U, 0x25CCU, 0x25D0U, 0x25D4U, 0x25D8U, 0x25DCU, 0x25E0U, 0x25E4U,
    0x25E8U, 0x25ECU, 0x25F0U, 0x25F4U, 0x25F8U, 0x25FCU, 0x2600U, 0x2604U, 0x2608U, 0x260CU, 0x2610U, 0x2614U, 0x2618U, 0x261CU, 0x2620U, 0x2624U, 0x2628U, 0x262CU,
    0x2630U, 0x2634U, 0x2638U, 0x263CU, 0x2640U, 0x2644U, 0x2648U, 0x264CU, 0x2650U, 0x2654U, 0x2658U, 0x265CU, 0x2660U, 0x2664U, 0x2668U, 0x266CU, 0x2670U, 0x2674U,
    0x2678U, 0x267CU, 0x2680U, 0x2684U, 0x2688U, 0x268CU, 0x2690U, 0x2694U, 0x2698U, 0x269CU, 0x26A0U, 0x26A4U, 0x26A8U, 0x26ACU, 0x26B0U, 0x26B4U, 0x26B8U, 0x26BCU,
    0x26C0U, 0x26C4U, 0x26C8U, 0x26CCU, 0x26D0U, 0x26D4U, 0x26D8U, 0x26DCU, 0x26E0U, 0x26E4U, 0x26E8U, 0x26ECU, 0x26F0U, 0x26F4U, 0x26F8U, 0x26FCU, 0x2700U, 0x2704U,
    0x2708U, 0x270CU, 0x2710U, 0x2714U, 0x2718U, 0x271CU, 0x2720U, 0x2724U, 0x2728U, 0x272CU, 0x2730U, 0x2734U, 0x2738U, 0x273CU, 0x2740U, 0x2744U, 0x2748U, 0x274CU,
    0x2750U, 0x2754U, 0x2758U, 0x275CU, 0x2760U, 0x2764U, 0x2768U, 0x276CU, 0x2770U, 0x2774U, 0x2778U, 0x277CU, 0x2780U, 0x2784U, 0x2788U, 0x278CU, 0x2790U, 0x2794U,
    0x2798U, 0x279CU, 0x27A0U, 0x27A4U, 0x27A8U, 0x27ACU, 0x27B0U, 0x27B4U, 0x27B8U, 0x27BCU, 0x27C0U, 0x27C4U, 0x27C8U, 0x27CCU, 0x27D0U, 0x27D4U, 0x27D8U, 0x27DCU,
    0x27E0U, 0x27E4U, 0x27E8U, 0x27ECU, 0x27F0U, 0x27F4U, 0x27F8U, 0x27FCU, 0x2800U, 0x2804U, 0x2808U, 0x280CU, 0x2810U, 0x2814U, 0x2818U, 0x281CU, 0x2820U, 0x2824U,
    0x2828U, 0x282CU, 0x2830U, 0x2834U, 0x2838U, 0x283CU, 0x2840U, 0x2844U, 0x2848U, 0x284CU, 0x2850U, 0x2854U, 0x2858U, 0x285CU, 0x2860U, 0x2864U, 0x2868U, 0x286CU,
    0x2870U, 0x2874U, 0x2878U, 0x287CU, 0x2880U, 0x2884U, 0x2888U, 0x288CU, 0x2890U, 0x2894U, 0x2898U, 0x289CU, 0x28A0U, 0x28A4U, 0x28A8U, 0x28ACU, 0x28B0U, 0x28B4U,
    0x28B8U, 0x28BCU, 0x28C0U, 0x28C4U, 0x28C8U, 0x28CCU, 0x28D0U, 0x28D4U, 0x28D8U, 0x28DCU, 0x28E0U, 0x28E4U, 0x28E8U, 0x28ECU, 0x28F0U, 0x28F4U, 0x28F8U, 0x28FCU,
    0x2900U, 0x2904U, 0x2908U, 0x290CU, 0x2910U, 0x2914U, 0x2918U, 0x291CU, 0x2920U, 0x2924U, 0x2928U, 0x292CU, 0x2930U, 0x2934U, 0x2938U, 0x293CU, 0x2940U, 0x2944U,
    0x2948U, 0x294CU, 0x2950U, 0x2954U, 0x2958U, 0x295CU, 0x2960U, 0x2964U, 0x2968U, 0x296CU, 0x2970U, 0x2974U, 0x2978U, 0x297CU, 0x2980U, 0x2984U, 0x2988U, 0x298CU,
    0x2990U, 0x2994U, 0x2998U, 0x299CU, 0x29A0U, 0x29A4U, 0x29A8U, 0x29ACU, 0x29B0U, 0x29B4U, 0x29B8U, 0x29BCU, 0x29C0U, 0x29C4U, 0x29C8U, 0x29CCU, 0x29D0U, 0x29D4U,
    0x29D8U, 0x29DCU, 0x29E0U, 0x29E4U, 0x29E8U, 0x29ECU, 0x29F0U, 0x29F4U, 0x29F8U, 0x29FCU, 0x2A00U, 0x2A04U, 0x2A08U, 0x2A0CU, 0x2A10U, 0x2A14U, 0x2A18U, 0x2A1CU,
    0x2A20U, 0x2A24U, 0x2A28U, 0x2A2CU, 0x2A30U, 0x2A34U, 0x2A38U, 0x2A3CU, 0x2A40U, 0x2A44U, 0x2A48U, 0x2A4CU, 0x2A50U, 0x2A54U, 0x2A58U, 0x2A5CU, 0x2A60U, 0x2A64U,
    0x2A68U, 0x2A6CU, 0x2A70U, 0x2A74U, 0x2A78U, 0x2A7CU, 0x2A80U, 0x2A84U, 0x2A88U, 0x2A8CU, 0x2A90U, 0x2A94U, 0x2A99U, 0x2AA1U, 0x2AA9U, 0x2AB1U, 0x2ABAU, 0x2AC6U,
    0x2AD1U, 0x2AD9U, 0x2AE1U, 0x2AE9U, 0x2AF1U, 0x2AF9U, 0x2B01U, 0x2B09U, 0x2B11U, 0x2B19U, 0x2B21U, 0x2B29U, 0x2B31U, 0x2B39U, 0x2B41U, 0x2B49U, 0x2B51U, 0x2B59U,
    0x2B61U, 0x2B69U, 0x2B71U, 0x2B79U, 0x2B81U, 0x2B89U, 0x2B91U, 0x2B99U, 0x2BA1U, 0x2BA9U};

static const uint16_t xx_fat_decomp_pool[2796] = {
    0x0041U, 0x0300U, 0x0041U, 0x0301U, 0x0041U, 0x0302U, 0x0041U, 0x0303U, 0x0041U, 0x0308U, 0x0041U, 0x030AU, 0x0043U, 0x0327U, 0x0045U, 0x0300U, 0x0045U, 0x0301U,
    0x0045U, 0x0302U, 0x0045U, 0x0308U, 0x0049U, 0x0300U, 0x0049U, 0x0301U, 0x0049U, 0x0302U, 0x0049U, 0x0308U, 0x004EU, 0x0303U, 0x004FU, 0x0300U, 0x004FU, 0x0301U,
    0x004FU, 0x0302U, 0x004FU, 0x0303U, 0x004FU, 0x0308U, 0x0055U, 0x0300U, 0x0055U, 0x0301U, 0x0055U, 0x0302U, 0x0055U, 0x0308U, 0x0059U, 0x0301U, 0x0041U, 0x0300U,
    0x0041U, 0x0301U, 0x0041U, 0x0302U, 0x0041U, 0x0303U, 0x0041U, 0x0308U, 0x0041U, 0x030AU, 0x0043U, 0x0327U, 0x0045U, 0x0300U, 0x0045U, 0x0301U, 0x0045U, 0x0302U,
    0x0045U, 0x0308U, 0x0049U, 0x0300U, 0x0049U, 0x0301U, 0x0049U, 0x0302U, 0x0049U, 0x0308U, 0x004EU, 0x0303U, 0x004FU, 0x0300U, 0x004FU, 0x0301U, 0x004FU, 0x0302U,
    0x004FU, 0x0303U, 0x004FU, 0x0308U, 0x0055U, 0x0300U, 0x0055U, 0x0301U, 0x0055U, 0x0302U, 0x0055U, 0x0308U, 0x0059U, 0x0301U, 0x0059U, 0x0308U, 0x0041U, 0x0304U,
    0x0041U, 0x0304U, 0x0041U, 0x0306U, 0x0041U, 0x0306U, 0x0041U, 0x0328U, 0x0041U, 0x0328U, 0x0043U, 0x0301U, 0x0043U, 0x0301U, 0x0043U, 0x0302U, 0x0043U, 0x0302U,
    0x0043U, 0x0307U, 0x0043U, 0x0307U, 0x0043U, 0x030CU, 0x0043U, 0x030CU, 0x0044U, 0x030CU, 0x0044U, 0x030CU, 0x0045U, 0x0304U, 0x0045U, 0x0304U, 0x0045U, 0x0306U,
    0x0045U, 0x0306U, 0x0045U, 0x0307U, 0x0045U, 0x0307U, 0x0045U, 0x0328U, 0x0045U, 0x0328U, 0x0045U, 0x030CU, 0x0045U, 0x030CU, 0x0047U, 0x0302U, 0x0047U, 0x0302U,
    0x0047U, 0x0306U, 0x0047U, 0x0306U, 0x0047U, 0x0307U, 0x0047U, 0x0307U, 0x0047U, 0x0327U, 0x0047U, 0x0327U, 0x0048U, 0x0302U, 0x0048U, 0x0302U, 0x0049U, 0x0303U,
    0x0049U, 0x0303U, 0x0049U, 0x0304U, 0x0049U, 0x0304U, 0x0049U, 0x0306U, 0x0049U, 0x0306U, 0x0049U, 0x0328U, 0x0049U, 0x0328U, 0x0049U, 0x0307U, 0x004AU, 0x0302U,
    0x004AU, 0x0302U, 0x004BU, 0x0327U, 0x004BU, 0x0327U, 0x004CU, 0x0301U, 0x004CU, 0x0301U, 0x004CU, 0x0327U, 0x004CU, 0x0327U, 0x004CU, 0x030CU, 0x004CU, 0x030CU,
    0x004EU, 0x0301U, 0x004EU, 0x0301U, 0x004EU, 0x0327U, 0x004EU, 0x0327U, 0x004EU, 0x030CU, 0x004EU, 0x030CU, 0x004FU, 0x0304U, 0x004FU, 0x0304U, 0x004FU, 0x0306U,
    0x004FU, 0x0306U, 0x004FU, 0x030BU, 0x004FU, 0x030BU, 0x0052U, 0x0301U, 0x0052U, 0x0301U, 0x0052U, 0x0327U, 0x0052U, 0x0327U, 0x0052U, 0x030CU, 0x0052U, 0x030CU,
    0x0053U, 0x0301U, 0x0053U, 0x0301U, 0x0053U, 0x0302U, 0x0053U, 0x0302U, 0x0053U, 0x0327U, 0x0053U, 0x0327U, 0x0053U, 0x030CU, 0x0053U, 0x030CU, 0x0054U, 0x0327U,
    0x0054U, 0x0327U, 0x0054U, 0x030CU, 0x0054U, 0x030CU, 0x0055U, 0x0303U, 0x0055U, 0x0303U, 0x0055U, 0x0304U, 0x0055U, 0x0304U, 0x0055U, 0x0306U, 0x0055U, 0x0306U,
    0x0055U, 0x030AU, 0x0055U, 0x030AU, 0x0055U, 0x030BU, 0x0055U, 0x030BU, 0x0055U, 0x0328U, 0x0055U, 0x0328U, 0x0057U, 0x0302U, 0x0057U, 0x0302U, 0x0059U, 0x0302U,
    0x0059U, 0x0302U, 0x0059U, 0x0308U, 0x005AU, 0x0301U, 0x005AU, 0x0301U, 0x005AU, 0x0307U, 0x005AU, 0x0307U, 0x005AU, 0x030CU, 0x005AU, 0x030CU, 0x004FU, 0x031BU,
    0x004FU, 0x031BU, 0x0055U, 0x031BU, 0x0055U, 0x031BU, 0x0041U, 0x030CU, 0x0041U, 0x030CU, 0x0049U, 0x030CU, 0x0049U, 0x030CU, 0x004FU, 0x030CU, 0x004FU, 0x030CU,
    0x0055U, 0x030CU, 0x0055U, 0x030CU, 0x0055U, 0x0308U, 0x0304U, 0x0055U, 0x0308U, 0x0304U, 0x0055U, 0x0308U, 0x0301U, 0x0055U, 0x0308U, 0x0301U, 0x0055U, 0x0308U,
    0x030CU, 0x0055U, 0x0308U, 0x030CU, 0x0055U, 0x0308U, 0x0300U, 0x0055U, 0x0308U, 0x0300U, 0x0041U, 0x0308U, 0x0304U, 0x0041U, 0x0308U, 0x0304U, 0x0041U, 0x0307U,
    0x0304U, 0x0041U, 0x0307U, 0x0304U, 0x00C6U, 0x0304U, 0x00C6U, 0x0304U, 0x0047U, 0x030CU, 0x0047U, 0x030CU, 0x004BU, 0x030CU, 0x004BU, 0x030CU, 0x004FU, 0x0328U,
    0x004FU, 0x0328U, 0x004FU, 0x0328U, 0x0304U, 0x004FU, 0x0328U, 0x0304U, 0x01B7U, 0x030CU, 0x01B7U, 0x030CU, 0x004AU, 0x030CU, 0x0047U, 0x0301U, 0x0047U, 0x0301U,
    0x004EU, 0x0300U, 0x004EU, 0x0300U, 0x0041U, 0x030AU, 0x0301U, 0x0041U, 0x030AU, 0x0301U, 0x00C6U, 0x0301U, 0x00C6U, 0x0301U, 0x00D8U, 0x0301U, 0x00D8U, 0x0301U,
    0x0041U, 0x030FU, 0x0041U, 0x030FU, 0x0041U, 0x0311U, 0x0041U, 0x0311U, 0x0045U, 0x030FU, 0x0045U, 0x030FU, 0x0045U, 0x0311U, 0x0045U, 0x0311U, 0x0049U, 0x030FU,
    0x0049U, 0x030FU, 0x0049U, 0x0311U, 0x0049U, 0x0311U, 0x004FU, 0x030FU, 0x004FU, 0x030FU, 0x004FU, 0x0311U, 0x004FU, 0x0311U, 0x0052U, 0x030FU, 0x0052U, 0x030FU,
    0x0052U, 0x0311U, 0x0052U, 0x0311U, 0x0055U, 0x030FU, 0x0055U, 0x030FU, 0x0055U, 0x0311U, 0x0055U, 0x0311U, 0x0053U, 0x0326U, 0x0053U, 0x0326U, 0x0054U, 0x0326U,
    0x0054U, 0x0326U, 0x0048U, 0x030CU, 0x0048U, 0x030CU, 0x0041U, 0x0307U, 0x0041U, 0x0307U, 0x0045U, 0x0327U, 0x0045U, 0x0327U, 0x004FU, 0x0308U, 0x0304U, 0x004FU,
    0x0308U, 0x0304U, 0x004FU, 0x0303U, 0x0304U, 0x004FU, 0x0303U, 0x0304U, 0x004FU, 0x0307U, 0x004FU, 0x0307U, 0x004FU, 0x0307U, 0x0304U, 0x004FU, 0x0307U, 0x0304U,
    0x0059U, 0x0304U, 0x0059U, 0x0304U, 0x0300U, 0x0301U, 0x0313U, 0x0308U, 0x0301U, 0x02B9U, 0x003BU, 0x00A8U, 0x0301U, 0x0391U, 0x0301U, 0x00B7U, 0x0395U, 0x0301U,
    0x0397U, 0x0301U, 0x0345U, 0x0301U, 0x039FU, 0x0301U, 0x03A5U, 0x0301U, 0x03A9U, 0x0301U, 0x0345U, 0x0308U, 0x0301U, 0x0345U, 0x0308U, 0x03A5U, 0x0308U, 0x0391U,
    0x0301U, 0x0395U, 0x0301U, 0x0397U, 0x0301U, 0x0345U, 0x0301U, 0x03A5U, 0x0308U, 0x0301U, 0x0345U, 0x0308U, 0x03A5U, 0x0308U, 0x039FU, 0x0301U, 0x03A5U, 0x0301U,
    0x03A9U, 0x0301U, 0x03D2U, 0x0301U, 0x03D2U, 0x0308U, 0x0415U, 0x0300U, 0x0415U, 0x0308U, 0x0413U, 0x0301U, 0x0406U, 0x0308U, 0x041AU, 0x0301U, 0x0418U, 0x0300U,
    0x0423U, 0x0306U, 0x0418U, 0x0306U, 0x0418U, 0x0306U, 0x0415U, 0x0300U, 0x0415U, 0x0308U, 0x0413U, 0x0301U, 0x0406U, 0x0308U, 0x041AU, 0x0301U, 0x0418U, 0x0300U,
    0x0423U, 0x0306U, 0x0474U, 0x030FU, 0x0474U, 0x030FU, 0x0416U, 0x0306U, 0x0416U, 0x0306U, 0x0410U, 0x0306U, 0x0410U, 0x0306U, 0x0410U, 0x0308U, 0x0410U, 0x0308U,
    0x0415U, 0x0306U, 0x0415U, 0x0306U, 0x04D8U, 0x0308U, 0x04D8U, 0x0308U, 0x0416U, 0x0308U, 0x0416U, 0x0308U, 0x0417U, 0x0308U, 0x0417U, 0x0308U, 0x0418U, 0x0304U,
    0x0418U, 0x0304U, 0x0418U, 0x0308U, 0x0418U, 0x0308U, 0x041EU, 0x0308U, 0x041EU, 0x0308U, 0x04E8U, 0x0308U, 0x04E8U, 0x0308U, 0x042DU, 0x0308U, 0x042DU, 0x0308U,
    0x0423U, 0x0304U, 0x0423U, 0x0304U, 0x0423U, 0x0308U, 0x0423U, 0x0308U, 0x0423U, 0x030BU, 0x0423U, 0x030BU, 0x0427U, 0x0308U, 0x0427U, 0x0308U, 0x042BU, 0x0308U,
    0x042BU, 0x0308U, 0x0627U, 0x0653U, 0x0627U, 0x0654U, 0x0648U, 0x0654U, 0x0627U, 0x0655U, 0x064AU, 0x0654U, 0x06D5U, 0x0654U, 0x06C1U, 0x0654U, 0x06D2U, 0x0654U,
    0x0928U, 0x093CU, 0x0930U, 0x093CU, 0x0933U, 0x093CU, 0x0915U, 0x093CU, 0x0916U, 0x093CU, 0x0917U, 0x093CU, 0x091CU, 0x093CU, 0x0921U, 0x093CU, 0x0922U, 0x093CU,
    0x092BU, 0x093CU, 0x092FU, 0x093CU, 0x09C7U, 0x09BEU, 0x09C7U, 0x09D7U, 0x09A1U, 0x09BCU, 0x09A2U, 0x09BCU, 0x09AFU, 0x09BCU, 0x0A32U, 0x0A3CU, 0x0A38U, 0x0A3CU,
    0x0A16U, 0x0A3CU, 0x0A17U, 0x0A3CU, 0x0A1CU, 0x0A3CU, 0x0A2BU, 0x0A3CU, 0x0B47U, 0x0B56U, 0x0B47U, 0x0B3EU, 0x0B47U, 0x0B57U, 0x0B21U, 0x0B3CU, 0x0B22U, 0x0B3CU,
    0x0B92U, 0x0BD7U, 0x0BC6U, 0x0BBEU, 0x0BC7U, 0x0BBEU, 0x0BC6U, 0x0BD7U, 0x0C46U, 0x0C56U, 0x0CBFU, 0x0CD5U, 0x0CC6U, 0x0CD5U, 0x0CC6U, 0x0CD6U, 0x0CC6U, 0x0CC2U,
    0x0CC6U, 0x0CC2U, 0x0CD5U, 0x0D46U, 0x0D3EU, 0x0D47U, 0x0D3EU, 0x0D46U, 0x0D57U, 0x0DD9U, 0x0DCAU, 0x0DD9U, 0x0DCFU, 0x0DD9U, 0x0DCFU, 0x0DCAU, 0x0DD9U, 0x0DDFU,
    0x0F42U, 0x0FB7U, 0x0F4CU, 0x0FB7U, 0x0F51U, 0x0FB7U, 0x0F56U, 0x0FB7U, 0x0F5BU, 0x0FB7U, 0x0F40U, 0x0FB5U, 0x0F71U, 0x0F72U, 0x0F71U, 0x0F74U, 0x0FB2U, 0x0F80U,
    0x0FB3U, 0x0F80U, 0x0F71U, 0x0F80U, 0x0F92U, 0x0FB7U, 0x0F9CU, 0x0FB7U, 0x0FA1U, 0x0FB7U, 0x0FA6U, 0x0FB7U, 0x0FABU, 0x0FB7U, 0x0F90U, 0x0FB5U, 0x1025U, 0x102EU,
    0x1B05U, 0x1B35U, 0x1B07U, 0x1B35U, 0x1B09U, 0x1B35U, 0x1B0BU, 0x1B35U, 0x1B0DU, 0x1B35U, 0x1B11U, 0x1B35U, 0x1B3AU, 0x1B35U, 0x1B3CU, 0x1B35U, 0x1B3EU, 0x1B35U,
    0x1B3FU, 0x1B35U, 0x1B42U, 0x1B35U, 0x0041U, 0x0325U, 0x0041U, 0x0325U, 0x0042U, 0x0307U, 0x0042U, 0x0307U, 0x0042U, 0x0323U, 0x0042U, 0x0323U, 0x0042U, 0x0331U,
    0x0042U, 0x0331U, 0x0043U, 0x0327U, 0x0301U, 0x0043U, 0x0327U, 0x0301U, 0x0044U, 0x0307U, 0x0044U, 0x0307U, 0x0044U, 0x0323U, 0x0044U, 0x0323U, 0x0044U, 0x0331U,
    0x0044U, 0x0331U, 0x0044U, 0x0327U, 0x0044U, 0x0327U, 0x0044U, 0x032DU, 0x0044U, 0x032DU, 0x0045U, 0x0304U, 0x0300U, 0x0045U, 0x0304U, 0x0300U, 0x0045U, 0x0304U,
    0x0301U, 0x0045U, 0x0304U, 0x0301U, 0x0045U, 0x032DU, 0x0045U, 0x032DU, 0x0045U, 0x0330U, 0x0045U, 0x0330U, 0x0045U, 0x0327U, 0x0306U, 0x0045U, 0x0327U, 0x0306U,
    0x0046U, 0x0307U, 0x0046U, 0x0307U, 0x0047U, 0x0304U, 0x0047U, 0x0304U, 0x0048U, 0x0307U, 0x0048U, 0x0307U, 0x0048U, 0x0323U, 0x0048U, 0x0323U, 0x0048U, 0x0308U,
    0x0048U, 0x0308U, 0x0048U, 0x0327U, 0x0048U, 0x0327U, 0x0048U, 0x032EU, 0x0048U, 0x032EU, 0x0049U, 0x0330U, 0x0049U, 0x0330U, 0x0049U, 0x0308U, 0x0301U, 0x0049U,
    0x0308U, 0x0301U, 0x004BU, 0x0301U, 0x004BU, 0x0301U, 0x004BU, 0x0323U, 0x004BU, 0x0323U, 0x004BU, 0x0331U, 0x004BU, 0x0331U, 0x004CU, 0x0323U, 0x004CU, 0x0323U,
    0x004CU, 0x0323U, 0x0304U, 0x004CU, 0x0323U, 0x0304U, 0x004CU, 0x0331U, 0x004CU, 0x0331U, 0x004CU, 0x032DU, 0x004CU, 0x032DU, 0x004DU, 0x0301U, 0x004DU, 0x0301U,
    0x004DU, 0x0307U, 0x004DU, 0x0307U, 0x004DU, 0x0323U, 0x004DU, 0x0323U, 0x004EU, 0x0307U, 0x004EU, 0x0307U, 0x004EU, 0x0323U, 0x004EU, 0x0323U, 0x004EU, 0x0331U,
    0x004EU, 0x0331U, 0x004EU, 0x032DU, 0x004EU, 0x032DU, 0x004FU, 0x0303U, 0x0301U, 0x004FU, 0x0303U, 0x0301U, 0x004FU, 0x0303U, 0x0308U, 0x004FU, 0x0303U, 0x0308U,
    0x004FU, 0x0304U, 0x0300U, 0x004FU, 0x0304U, 0x0300U, 0x004FU, 0x0304U, 0x0301U, 0x004FU, 0x0304U, 0x0301U, 0x0050U, 0x0301U, 0x0050U, 0x0301U, 0x0050U, 0x0307U,
    0x0050U, 0x0307U, 0x0052U, 0x0307U, 0x0052U, 0x0307U, 0x0052U, 0x0323U, 0x0052U, 0x0323U, 0x0052U, 0x0323U, 0x0304U, 0x0052U, 0x0323U, 0x0304U, 0x0052U, 0x0331U,
    0x0052U, 0x0331U, 0x0053U, 0x0307U, 0x0053U, 0x0307U, 0x0053U, 0x0323U, 0x0053U, 0x0323U, 0x0053U, 0x0301U, 0x0307U, 0x0053U, 0x0301U, 0x0307U, 0x0053U, 0x030CU,
    0x0307U, 0x0053U, 0x030CU, 0x0307U, 0x0053U, 0x0323U, 0x0307U, 0x0053U, 0x0323U, 0x0307U, 0x0054U, 0x0307U, 0x0054U, 0x0307U, 0x0054U, 0x0323U, 0x0054U, 0x0323U,
    0x0054U, 0x0331U, 0x0054U, 0x0331U, 0x0054U, 0x032DU, 0x0054U, 0x032DU, 0x0055U, 0x0324U, 0x0055U, 0x0324U, 0x0055U, 0x0330U, 0x0055U, 0x0330U, 0x0055U, 0x032DU,
    0x0055U, 0x032DU, 0x0055U, 0x0303U, 0x0301U, 0x0055U, 0x0303U, 0x0301U, 0x0055U, 0x0304U, 0x0308U, 0x0055U, 0x0304U, 0x0308U, 0x0056U, 0x0303U, 0x0056U, 0x0303U,
    0x0056U, 0x0323U, 0x0056U, 0x0323U, 0x0057U, 0x0300U, 0x0057U, 0x0300U, 0x0057U, 0x0301U, 0x0057U, 0x0301U, 0x0057U, 0x0308U, 0x0057U, 0x0308U, 0x0057U, 0x0307U,
    0x0057U, 0x0307U, 0x0057U, 0x0323U, 0x0057U, 0x0323U, 0x0058U, 0x0307U, 0x0058U, 0x0307U, 0x0058U, 0x0308U, 0x0058U, 0x0308U, 0x0059U, 0x0307U, 0x0059U, 0x0307U,
    0x005AU, 0x0302U, 0x005AU, 0x0302U, 0x005AU, 0x0323U, 0x005AU, 0x0323U, 0x005AU, 0x0331U, 0x005AU, 0x0331U, 0x0048U, 0x0331U, 0x0054U, 0x0308U, 0x0057U, 0x030AU,
    0x0059U, 0x030AU, 0x0053U, 0x0307U, 0x0041U, 0x0323U, 0x0041U, 0x0323U, 0x0041U, 0x0309U, 0x0041U, 0x0309U, 0x0041U, 0x0302U, 0x0301U, 0x0041U, 0x0302U, 0x0301U,
    0x0041U, 0x0302U, 0x0300U, 0x0041U, 0x0302U, 0x0300U, 0x0041U, 0x0302U, 0x0309U, 0x0041U, 0x0302U, 0x0309U, 0x0041U, 0x0302U, 0x0303U, 0x0041U, 0x0302U, 0x0303U,
    0x0041U, 0x0323U, 0x0302U, 0x0041U, 0x0323U, 0x0302U, 0x0041U, 0x0306U, 0x0301U, 0x0041U, 0x0306U, 0x0301U, 0x0041U, 0x0306U, 0x0300U, 0x0041U, 0x0306U, 0x0300U,
    0x0041U, 0x0306U, 0x0309U, 0x0041U, 0x0306U, 0x0309U, 0x0041U, 0x0306U, 0x0303U, 0x0041U, 0x0306U, 0x0303U, 0x0041U, 0x0323U, 0x0306U, 0x0041U, 0x0323U, 0x0306U,
    0x0045U, 0x0323U, 0x0045U, 0x0323U, 0x0045U, 0x0309U, 0x0045U, 0x0309U, 0x0045U, 0x0303U, 0x0045U, 0x0303U, 0x0045U, 0x0302U, 0x0301U, 0x0045U, 0x0302U, 0x0301U,
    0x0045U, 0x0302U, 0x0300U, 0x0045U, 0x0302U, 0x0300U, 0x0045U, 0x0302U, 0x0309U, 0x0045U, 0x0302U, 0x0309U, 0x0045U, 0x0302U, 0x0303U, 0x0045U, 0x0302U, 0x0303U,
    0x0045U, 0x0323U, 0x0302U, 0x0045U, 0x0323U, 0x0302U, 0x0049U, 0x0309U, 0x0049U, 0x0309U, 0x0049U, 0x0323U, 0x0049U, 0x0323U, 0x004FU, 0x0323U, 0x004FU, 0x0323U,
    0x004FU, 0x0309U, 0x004FU, 0x0309U, 0x004FU, 0x0302U, 0x0301U, 0x004FU, 0x0302U, 0x0301U, 0x004FU, 0x0302U, 0x0300U, 0x004FU, 0x0302U, 0x0300U, 0x004FU, 0x0302U,
    0x0309U, 0x004FU, 0x0302U, 0x0309U, 0x004FU, 0x0302U, 0x0303U, 0x004FU, 0x0302U, 0x0303U, 0x004FU, 0x0323U, 0x0302U, 0x004FU, 0x0323U, 0x0302U, 0x004FU, 0x031BU,
    0x0301U, 0x004FU, 0x031BU, 0x0301U, 0x004FU, 0x031BU, 0x0300U, 0x004FU, 0x031BU, 0x0300U, 0x004FU, 0x031BU, 0x0309U, 0x004FU, 0x031BU, 0x0309U, 0x004FU, 0x031BU,
    0x0303U, 0x004FU, 0x031BU, 0x0303U, 0x004FU, 0x031BU, 0x0323U, 0x004FU, 0x031BU, 0x0323U, 0x0055U, 0x0323U, 0x0055U, 0x0323U, 0x0055U, 0x0309U, 0x0055U, 0x0309U,
    0x0055U, 0x031BU, 0x0301U, 0x0055U, 0x031BU, 0x0301U, 0x0055U, 0x031BU, 0x0300U, 0x0055U, 0x031BU, 0x0300U, 0x0055U, 0x031BU, 0x0309U, 0x0055U, 0x031BU, 0x0309U,
    0x0055U, 0x031BU, 0x0303U, 0x0055U, 0x031BU, 0x0303U, 0x0055U, 0x031BU, 0x0323U, 0x0055U, 0x031BU, 0x0323U, 0x0059U, 0x0300U, 0x0059U, 0x0300U, 0x0059U, 0x0323U,
    0x0059U, 0x0323U, 0x0059U, 0x0309U, 0x0059U, 0x0309U, 0x0059U, 0x0303U, 0x0059U, 0x0303U, 0x0391U, 0x0313U, 0x0391U, 0x0314U, 0x0391U, 0x0313U, 0x0300U, 0x0391U,
    0x0314U, 0x0300U, 0x0391U, 0x0313U, 0x0301U, 0x0391U, 0x0314U, 0x0301U, 0x0391U, 0x0313U, 0x0342U, 0x0391U, 0x0314U, 0x0342U, 0x0391U, 0x0313U, 0x0391U, 0x0314U,
    0x0391U, 0x0313U, 0x0300U, 0x0391U, 0x0314U, 0x0300U, 0x0391U, 0x0313U, 0x0301U, 0x0391U, 0x0314U, 0x0301U, 0x0391U, 0x0313U, 0x0342U, 0x0391U, 0x0314U, 0x0342U,
    0x0395U, 0x0313U, 0x0395U, 0x0314U, 0x0395U, 0x0313U, 0x0300U, 0x0395U, 0x0314U, 0x0300U, 0x0395U, 0x0313U, 0x0301U, 0x0395U, 0x0314U, 0x0301U, 0x0395U, 0x0313U,
    0x0395U, 0x0314U, 0x0395U, 0x0313U, 0x0300U, 0x0395U, 0x0314U, 0x0300U, 0x0395U, 0x0313U, 0x0301U, 0x0395U, 0x0314U, 0x0301U, 0x0397U, 0x0313U, 0x0397U, 0x0314U,
    0x0397U, 0x0313U, 0x0300U, 0x0397U, 0x0314U, 0x0300U, 0x0397U, 0x0313U, 0x0301U, 0x0397U, 0x0314U, 0x0301U, 0x0397U, 0x0313U, 0x0342U, 0x0397U, 0x0314U, 0x0342U,
    0x0397U, 0x0313U, 0x0397U, 0x0314U, 0x0397U, 0x0313U, 0x0300U, 0x0397U, 0x0314U, 0x0300U, 0x0397U, 0x0313U, 0x0301U, 0x0397U, 0x0314U, 0x0301U, 0x0397U, 0x0313U,
    0x0342U, 0x0397U, 0x0314U, 0x0342U, 0x0345U, 0x0313U, 0x0345U, 0x0314U, 0x0345U, 0x0313U, 0x0300U, 0x0345U, 0x0314U, 0x0300U, 0x0345U, 0x0313U, 0x0301U, 0x0345U,
    0x0314U, 0x0301U, 0x0345U, 0x0313U, 0x0342U, 0x0345U, 0x0314U, 0x0342U, 0x0345U, 0x0313U, 0x0345U, 0x0314U, 0x0345U, 0x0313U, 0x0300U, 0x0345U, 0x0314U, 0x0300U,
    0x0345U, 0x0313U, 0x0301U, 0x0345U, 0x0314U, 0x0301U, 0x0345U, 0x0313U, 0x0342U, 0x0345U, 0x0314U, 0x0342U, 0x039FU, 0x0313U, 0x039FU, 0x0314U, 0x039FU, 0x0313U,
    0x0300U, 0x039FU, 0x0314U, 0x0300U, 0x039FU, 0x0313U, 0x0301U, 0x039FU, 0x0314U, 0x0301U, 0x039FU, 0x0313U, 0x039FU, 0x0314U, 0x039FU, 0x0313U, 0x0300U, 0x039FU,
    0x0314U, 0x0300U, 0x039FU, 0x0313U, 0x0301U, 0x039FU, 0x0314U, 0x0301U, 0x03A5U, 0x0313U, 0x03A5U, 0x0314U, 0x03A5U, 0x0313U, 0x0300U, 0x03A5U, 0x0314U, 0x0300U,
    0x03A5U, 0x0313U, 0x0301U, 0x03A5U, 0x0314U, 0x0301U, 0x03A5U, 0x0313U, 0x0342U, 0x03A5U, 0x0314U, 0x0342U, 0x03A5U, 0x0314U, 0x03A5U, 0x0314U, 0x0300U, 0x03A5U,
    0x0314U, 0x0301U, 0x03A5U, 0x0314U, 0x0342U, 0x03A9U, 0x0313U, 0x03A9U, 0x0314U, 0x03A9U, 0x0313U, 0x0300U, 0x03A9U, 0x0314U, 0x0300U, 0x03A9U, 0x0313U, 0x0301U,
    0x03A9U, 0x0314U, 0x0301U, 0x03A9U, 0x0313U, 0x0342U, 0x03A9U, 0x0314U, 0x0342U, 0x03A9U, 0x0313U, 0x03A9U, 0x0314U, 0x03A9U, 0x0313U, 0x0300U, 0x03A9U, 0x0314U,
    0x0300U, 0x03A9U, 0x0313U, 0x0301U, 0x03A9U, 0x0314U, 0x0301U, 0x03A9U, 0x0313U, 0x0342U, 0x03A9U, 0x0314U, 0x0342U, 0x0391U, 0x0300U, 0x0391U, 0x0301U, 0x0395U,
    0x0300U, 0x0395U, 0x0301U, 0x0397U, 0x0300U, 0x0397U, 0x0301U, 0x0345U, 0x0300U, 0x0345U, 0x0301U, 0x039FU, 0x0300U, 0x039FU, 0x0301U, 0x03A5U, 0x0300U, 0x03A5U,
    0x0301U, 0x03A9U, 0x0300U, 0x03A9U, 0x0301U, 0x0391U, 0x0313U, 0x0345U, 0x0391U, 0x0314U, 0x0345U, 0x0391U, 0x0313U, 0x0300U, 0x0345U, 0x0391U, 0x0314U, 0x0300U,
    0x0345U, 0x0391U, 0x0313U, 0x0301U, 0x0345U, 0x0391U, 0x0314U, 0x0301U, 0x0345U, 0x0391U, 0x0313U, 0x0342U, 0x0345U, 0x0391U, 0x0314U, 0x0342U, 0x0345U, 0x0391U,
    0x0313U, 0x0345U, 0x0391U, 0x0314U, 0x0345U, 0x0391U, 0x0313U, 0x0300U, 0x0345U, 0x0391U, 0x0314U, 0x0300U, 0x0345U, 0x0391U, 0x0313U, 0x0301U, 0x0345U, 0x0391U,
    0x0314U, 0x0301U, 0x0345U, 0x0391U, 0x0313U, 0x0342U, 0x0345U, 0x0391U, 0x0314U, 0x0342U, 0x0345U, 0x0397U, 0x0313U, 0x0345U, 0x0397U, 0x0314U, 0x0345U, 0x0397U,
    0x0313U, 0x0300U, 0x0345U, 0x0397U, 0x0314U, 0x0300U, 0x0345U, 0x0397U, 0x0313U, 0x0301U, 0x0345U, 0x0397U, 0x0314U, 0x0301U, 0x0345U, 0x0397U, 0x0313U, 0x0342U,
    0x0345U, 0x0397U, 0x0314U, 0x0342U, 0x0345U, 0x0397U, 0x0313U, 0x0345U, 0x0397U, 0x0314U, 0x0345U, 0x0397U, 0x0313U, 0x0300U, 0x0345U, 0x0397U, 0x0314U, 0x0300U,
    0x0345U, 0x0397U, 0x0313U, 0x0301U, 0x0345U, 0x0397U, 0x0314U, 0x0301U, 0x0345U, 0x0397U, 0x0313U, 0x0342U, 0x0345U, 0x0397U, 0x0314U, 0x0342U, 0x0345U, 0x03A9U,
    0x0313U, 0x0345U, 0x03A9U, 0x0314U, 0x0345U, 0x03A9U, 0x0313U, 0x0300U, 0x0345U, 0x03A9U, 0x0314U, 0x0300U, 0x0345U, 0x03A9U, 0x0313U, 0x0301U, 0x0345U, 0x03A9U,
    0x0314U, 0x0301U, 0x0345U, 0x03A9U, 0x0313U, 0x0342U, 0x0345U, 0x03A9U, 0x0314U, 0x0342U, 0x0345U, 0x03A9U, 0x0313U, 0x0345U, 0x03A9U, 0x0314U, 0x0345U, 0x03A9U,
    0x0313U, 0x0300U, 0x0345U, 0x03A9U, 0x0314U, 0x0300U, 0x0345U, 0x03A9U, 0x0313U, 0x0301U, 0x0345U, 0x03A9U, 0x0314U, 0x0301U, 0x0345U, 0x03A9U, 0x0313U, 0x0342U,
    0x0345U, 0x03A9U, 0x0314U, 0x0342U, 0x0345U, 0x0391U, 0x0306U, 0x0391U, 0x0304U, 0x0391U, 0x0300U, 0x0345U, 0x0391U, 0x0345U, 0x0391U, 0x0301U, 0x0345U, 0x0391U,
    0x0342U, 0x0391U, 0x0342U, 0x0345U, 0x0391U, 0x0306U, 0x0391U, 0x0304U, 0x0391U, 0x0300U, 0x0391U, 0x0301U, 0x0391U, 0x0345U, 0x0345U, 0x00A8U, 0x0342U, 0x0397U,
    0x0300U, 0x0345U, 0x0397U, 0x0345U, 0x0397U, 0x0301U, 0x0345U, 0x0397U, 0x0342U, 0x0397U, 0x0342U, 0x0345U, 0x0395U, 0x0300U, 0x0395U, 0x0301U, 0x0397U, 0x0300U,
    0x0397U, 0x0301U, 0x0397U, 0x0345U, 0x1FBFU, 0x0300U, 0x1FBFU, 0x0301U, 0x1FBFU, 0x0342U, 0x0345U, 0x0306U, 0x0345U, 0x0304U, 0x0345U, 0x0308U, 0x0300U, 0x0345U,
    0x0308U, 0x0301U, 0x0345U, 0x0342U, 0x0345U, 0x0308U, 0x0342U, 0x0345U, 0x0306U, 0x0345U, 0x0304U, 0x0345U, 0x0300U, 0x0345U, 0x0301U, 0x1FFEU, 0x0300U, 0x1FFEU,
    0x0301U, 0x1FFEU, 0x0342U, 0x03A5U, 0x0306U, 0x03A5U, 0x0304U, 0x03A5U, 0x0308U, 0x0300U, 0x03A5U, 0x0308U, 0x0301U, 0x03A1U, 0x0313U, 0x03A1U, 0x0314U, 0x03A5U,
    0x0342U, 0x03A5U, 0x0308U, 0x0342U, 0x03A5U, 0x0306U, 0x03A5U, 0x0304U, 0x03A5U, 0x0300U, 0x03A5U, 0x0301U, 0x03A1U, 0x0314U, 0x00A8U, 0x0300U, 0x00A8U, 0x0301U,
    0x0060U, 0x03A9U, 0x0300U, 0x0345U, 0x03A9U, 0x0345U, 0x03A9U, 0x0301U, 0x0345U, 0x03A9U, 0x0342U, 0x03A9U, 0x0342U, 0x0345U, 0x039FU, 0x0300U, 0x039FU, 0x0301U,
    0x03A9U, 0x0300U, 0x03A9U, 0x0301U, 0x03A9U, 0x0345U, 0x00B4U, 0x2002U, 0x2003U, 0x03A9U, 0x004BU, 0x0041U, 0x030AU, 0x2190U, 0x0338U, 0x2192U, 0x0338U, 0x2194U,
    0x0338U, 0x21D0U, 0x0338U, 0x21D4U, 0x0338U, 0x21D2U, 0x0338U, 0x2203U, 0x0338U, 0x2208U, 0x0338U, 0x220BU, 0x0338U, 0x2223U, 0x0338U, 0x2225U, 0x0338U, 0x223CU,
    0x0338U, 0x2243U, 0x0338U, 0x2245U, 0x0338U, 0x2248U, 0x0338U, 0x003DU, 0x0338U, 0x2261U, 0x0338U, 0x224DU, 0x0338U, 0x003CU, 0x0338U, 0x003EU, 0x0338U, 0x2264U,
    0x0338U, 0x2265U, 0x0338U, 0x2272U, 0x0338U, 0x2273U, 0x0338U, 0x2276U, 0x0338U, 0x2277U, 0x0338U, 0x227AU, 0x0338U, 0x227BU, 0x0338U, 0x2282U, 0x0338U, 0x2283U,
    0x0338U, 0x2286U, 0x0338U, 0x2287U, 0x0338U, 0x22A2U, 0x0338U, 0x22A8U, 0x0338U, 0x22A9U, 0x0338U, 0x22ABU, 0x0338U, 0x227CU, 0x0338U, 0x227DU, 0x0338U, 0x2291U,
    0x0338U, 0x2292U, 0x0338U, 0x22B2U, 0x0338U, 0x22B3U, 0x0338U, 0x22B4U, 0x0338U, 0x22B5U, 0x0338U, 0x3008U, 0x3009U, 0x2ADDU, 0x0338U, 0x304BU, 0x3099U, 0x304DU,
    0x3099U, 0x304FU, 0x3099U, 0x3051U, 0x3099U, 0x3053U, 0x3099U, 0x3055U, 0x3099U, 0x3057U, 0x3099U, 0x3059U, 0x3099U, 0x305BU, 0x3099U, 0x305DU, 0x3099U, 0x305FU,
    0x3099U, 0x3061U, 0x3099U, 0x3064U, 0x3099U, 0x3066U, 0x3099U, 0x3068U, 0x3099U, 0x306FU, 0x3099U, 0x306FU, 0x309AU, 0x3072U, 0x3099U, 0x3072U, 0x309AU, 0x3075U,
    0x3099U, 0x3075U, 0x309AU, 0x3078U, 0x3099U, 0x3078U, 0x309AU, 0x307BU, 0x3099U, 0x307BU, 0x309AU, 0x3046U, 0x3099U, 0x309DU, 0x3099U, 0x30ABU, 0x3099U, 0x30ADU,
    0x3099U, 0x30AFU, 0x3099U, 0x30B1U, 0x3099U, 0x30B3U, 0x3099U, 0x30B5U, 0x3099U, 0x30B7U, 0x3099U, 0x30B9U, 0x3099U, 0x30BBU, 0x3099U, 0x30BDU, 0x3099U, 0x30BFU,
    0x3099U, 0x30C1U, 0x3099U, 0x30C4U, 0x3099U, 0x30C6U, 0x3099U, 0x30C8U, 0x3099U, 0x30CFU, 0x3099U, 0x30CFU, 0x309AU, 0x30D2U, 0x3099U, 0x30D2U, 0x309AU, 0x30D5U,
    0x3099U, 0x30D5U, 0x309AU, 0x30D8U, 0x3099U, 0x30D8U, 0x309AU, 0x30DBU, 0x3099U, 0x30DBU, 0x309AU, 0x30A6U, 0x3099U, 0x30EFU, 0x3099U, 0x30F0U, 0x3099U, 0x30F1U,
    0x3099U, 0x30F2U, 0x3099U, 0x30FDU, 0x3099U, 0x8C48U, 0x66F4U, 0x8ECAU, 0x8CC8U, 0x6ED1U, 0x4E32U, 0x53E5U, 0x9F9CU, 0x9F9CU, 0x5951U, 0x91D1U, 0x5587U, 0x5948U,
    0x61F6U, 0x7669U, 0x7F85U, 0x863FU, 0x87BAU, 0x88F8U, 0x908FU, 0x6A02U, 0x6D1BU, 0x70D9U, 0x73DEU, 0x843DU, 0x916AU, 0x99F1U, 0x4E82U, 0x5375U, 0x6B04U, 0x721BU,
    0x862DU, 0x9E1EU, 0x5D50U, 0x6FEBU, 0x85CDU, 0x8964U, 0x62C9U, 0x81D8U, 0x881FU, 0x5ECAU, 0x6717U, 0x6D6AU, 0x72FCU, 0x90CEU, 0x4F86U, 0x51B7U, 0x52DEU, 0x64C4U,
    0x6AD3U, 0x7210U, 0x76E7U, 0x8001U, 0x8606U, 0x865CU, 0x8DEFU, 0x9732U, 0x9B6FU, 0x9DFAU, 0x788CU, 0x797FU, 0x7DA0U, 0x83C9U, 0x9304U, 0x9E7FU, 0x8AD6U, 0x58DFU,
    0x5F04U, 0x7C60U, 0x807EU, 0x7262U, 0x78CAU, 0x8CC2U, 0x96F7U, 0x58D8U, 0x5C62U, 0x6A13U, 0x6DDAU, 0x6F0FU, 0x7D2FU, 0x7E37U, 0x964BU, 0x52D2U, 0x808BU, 0x51DCU,
    0x51CCU, 0x7A1CU, 0x7DBEU, 0x83F1U, 0x9675U, 0x8B80U, 0x62CFU, 0x6A02U, 0x8AFEU, 0x4E39U, 0x5BE7U, 0x6012U, 0x7387U, 0x7570U, 0x5317U, 0x78FBU, 0x4FBFU, 0x5FA9U,
    0x4E0DU, 0x6CCCU, 0x6578U, 0x7D22U, 0x53C3U, 0x585EU, 0x7701U, 0x8449U, 0x8AAAU, 0x6BBAU, 0x8FB0U, 0x6C88U, 0x62FEU, 0x82E5U, 0x63A0U, 0x7565U, 0x4EAEU, 0x5169U,
    0x51C9U, 0x6881U, 0x7CE7U, 0x826FU, 0x8AD2U, 0x91CFU, 0x52F5U, 0x5442U, 0x5973U, 0x5EECU, 0x65C5U, 0x6FFEU, 0x792AU, 0x95ADU, 0x9A6AU, 0x9E97U, 0x9ECEU, 0x529BU,
    0x66C6U, 0x6B77U, 0x8F62U, 0x5E74U, 0x6190U, 0x6200U, 0x649AU, 0x6F23U, 0x7149U, 0x7489U, 0x79CAU, 0x7DF4U, 0x806FU, 0x8F26U, 0x84EEU, 0x9023U, 0x934AU, 0x5217U,
    0x52A3U, 0x54BDU, 0x70C8U, 0x88C2U, 0x8AAAU, 0x5EC9U, 0x5FF5U, 0x637BU, 0x6BAEU, 0x7C3EU, 0x7375U, 0x4EE4U, 0x56F9U, 0x5BE7U, 0x5DBAU, 0x601CU, 0x73B2U, 0x7469U,
    0x7F9AU, 0x8046U, 0x9234U, 0x96F6U, 0x9748U, 0x9818U, 0x4F8BU, 0x79AEU, 0x91B4U, 0x96B8U, 0x60E1U, 0x4E86U, 0x50DAU, 0x5BEEU, 0x5C3FU, 0x6599U, 0x6A02U, 0x71CEU,
    0x7642U, 0x84FCU, 0x907CU, 0x9F8DU, 0x6688U, 0x962EU, 0x5289U, 0x677BU, 0x67F3U, 0x6D41U, 0x6E9CU, 0x7409U, 0x7559U, 0x786BU, 0x7D10U, 0x985EU, 0x516DU, 0x622EU,
    0x9678U, 0x502BU, 0x5D19U, 0x6DEAU, 0x8F2AU, 0x5F8BU, 0x6144U, 0x6817U, 0x7387U, 0x9686U, 0x5229U, 0x540FU, 0x5C65U, 0x6613U, 0x674EU, 0x68A8U, 0x6CE5U, 0x7406U,
    0x75E2U, 0x7F79U, 0x88CFU, 0x88E1U, 0x91CCU, 0x96E2U, 0x533FU, 0x6EBAU, 0x541DU, 0x71D0U, 0x7498U, 0x85FAU, 0x96A3U, 0x9C57U, 0x9E9FU, 0x6797U, 0x6DCBU, 0x81E8U,
    0x7ACBU, 0x7B20U, 0x7C92U, 0x72C0U, 0x7099U, 0x8B58U, 0x4EC0U, 0x8336U, 0x523AU, 0x5207U, 0x5EA6U, 0x62D3U, 0x7CD6U, 0x5B85U, 0x6D1EU, 0x66B4U, 0x8F3BU, 0x884CU,
    0x964DU, 0x898BU, 0x5ED3U, 0x5140U, 0x55C0U, 0x585AU, 0x6674U, 0x51DEU, 0x732AU, 0x76CAU, 0x793CU, 0x795EU, 0x7965U, 0x798FU, 0x9756U, 0x7CBEU, 0x7FBDU, 0x8612U,
    0x8AF8U, 0x9038U, 0x90FDU, 0x98EFU, 0x98FCU, 0x9928U, 0x9DB4U, 0x90DEU, 0x96B7U, 0x4FAEU, 0x50E7U, 0x514DU, 0x52C9U, 0x52E4U, 0x5351U, 0x559DU, 0x5606U, 0x5668U,
    0x5840U, 0x58A8U, 0x5C64U, 0x5C6EU, 0x6094U, 0x6168U, 0x618EU, 0x61F2U, 0x654FU, 0x65E2U, 0x6691U, 0x6885U, 0x6D77U, 0x6E1AU, 0x6F22U, 0x716EU, 0x722BU, 0x7422U,
    0x7891U, 0x793EU, 0x7949U, 0x7948U, 0x7950U, 0x7956U, 0x795DU, 0x798DU, 0x798EU, 0x7A40U, 0x7A81U, 0x7BC0U, 0x7DF4U, 0x7E09U, 0x7E41U, 0x7F72U, 0x8005U, 0x81EDU,
    0x8279U, 0x8279U, 0x8457U, 0x8910U, 0x8996U, 0x8B01U, 0x8B39U, 0x8CD3U, 0x8D08U, 0x8FB6U, 0x9038U, 0x96E3U, 0x97FFU, 0x983BU, 0x6075U, 0x8218U, 0x4E26U, 0x51B5U,
    0x5168U, 0x4F80U, 0x5145U, 0x5180U, 0x52C7U, 0x52FAU, 0x559DU, 0x5555U, 0x5599U, 0x55E2U, 0x585AU, 0x58B3U, 0x5944U, 0x5954U, 0x5A62U, 0x5B28U, 0x5ED2U, 0x5ED9U,
    0x5F69U, 0x5FADU, 0x60D8U, 0x614EU, 0x6108U, 0x618EU, 0x6160U, 0x61F2U, 0x6234U, 0x63C4U, 0x641CU, 0x6452U, 0x6556U, 0x6674U, 0x6717U, 0x671BU, 0x6756U, 0x6B79U,
    0x6BBAU, 0x6D41U, 0x6EDBU, 0x6ECBU, 0x6F22U, 0x701EU, 0x716EU, 0x77A7U, 0x7235U, 0x72AFU, 0x732AU, 0x7471U, 0x7506U, 0x753BU, 0x761DU, 0x761FU, 0x76CAU, 0x76DBU,
    0x76F4U, 0x774AU, 0x7740U, 0x78CCU, 0x7AB1U, 0x7BC0U, 0x7C7BU, 0x7D5BU, 0x7DF4U, 0x7F3EU, 0x8005U, 0x8352U, 0x83EFU, 0x8779U, 0x8941U, 0x8986U, 0x8996U, 0x8ABFU,
    0x8AF8U, 0x8ACBU, 0x8B01U, 0x8AFEU, 0x8AEDU, 0x8B39U, 0x8B8AU, 0x8D08U, 0x8F38U, 0x9072U, 0x9199U, 0x9276U, 0x967CU, 0x96E3U, 0x9756U, 0x97DBU, 0x97FFU, 0x980BU,
    0x983BU, 0x9B12U, 0x9F9CU, 0x3B9DU, 0x4018U, 0x4039U, 0x9F43U, 0x9F8EU, 0x05D9U, 0x05B4U, 0x05F2U, 0x05B7U, 0x05E9U, 0x05C1U, 0x05E9U, 0x05C2U, 0x05E9U, 0x05BCU,
    0x05C1U, 0x05E9U, 0x05BCU, 0x05C2U, 0x05D0U, 0x05B7U, 0x05D0U, 0x05B8U, 0x05D0U, 0x05BCU, 0x05D1U, 0x05BCU, 0x05D2U, 0x05BCU, 0x05D3U, 0x05BCU, 0x05D4U, 0x05BCU,
    0x05D5U, 0x05BCU, 0x05D6U, 0x05BCU, 0x05D8U, 0x05BCU, 0x05D9U, 0x05BCU, 0x05DAU, 0x05BCU, 0x05DBU, 0x05BCU, 0x05DCU, 0x05BCU, 0x05DEU, 0x05BCU, 0x05E0U, 0x05BCU,
    0x05E1U, 0x05BCU, 0x05E3U, 0x05BCU, 0x05E4U, 0x05BCU, 0x05E6U, 0x05BCU, 0x05E7U, 0x05BCU, 0x05E8U, 0x05BCU, 0x05E9U, 0x05BCU, 0x05EAU, 0x05BCU, 0x05D5U, 0x05B9U,
    0x05D1U, 0x05BFU, 0x05DBU, 0x05BFU, 0x05E4U, 0x05BFU};

#define XX_FAT_HANGUL_S 0xAC00U
#define XX_FAT_HANGUL_L 0x1100U
#define XX_FAT_HANGUL_V 0x1161U
#define XX_FAT_HANGUL_T 0x11A7U
#define XX_FAT_HANGUL_V_COUNT 21U
#define XX_FAT_HANGUL_T_COUNT 28U
#define XX_FAT_HANGUL_COUNT 11172U

/* Decode the next code point of a member path. Paths are built by this file
 * and are well-formed UTF-8; a stray byte is still consumed as itself so the
 * walk always advances. */
static uint32_t xx_fat_utf8_next(const char **cursor)
{
    const unsigned char *text = (const unsigned char *)*cursor;
    uint32_t code_point = text[0];
    size_t extra = 0U;
    size_t index;
    if (code_point >= 0xF0U && code_point < 0xF8U) {
        code_point &= 0x07U;
        extra = 3U;
    } else if (code_point >= 0xE0U && code_point < 0xF0U) {
        code_point &= 0x0FU;
        extra = 2U;
    } else if (code_point >= 0xC0U && code_point < 0xE0U) {
        code_point &= 0x1FU;
        extra = 1U;
    }
    for (index = 1U; index <= extra; ++index) {
        if ((text[index] & 0xC0U) != 0x80U) {
            *cursor += 1;
            return text[0];
        }
        code_point = (code_point << 6U) | (text[index] & 0x3FU);
    }
    *cursor += 1U + extra;
    /* An overlong NUL must not end the key early. */
    return code_point != 0U ? code_point : 0xFFFDU;
}

static uint32_t xx_fat_fold(uint32_t cp)
{
    size_t low = 0U;
    size_t high = sizeof(xx_fat_fold_ranges) / sizeof(xx_fat_fold_ranges[0]);
    if (cp < 0x80U) return (cp >= 'a' && cp <= 'z') ? cp - 32U : cp;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const xx_fat_fold_range *range = &xx_fat_fold_ranges[middle];
        if (cp < range->first) {
            high = middle;
        } else if (cp > range->last) {
            low = middle + 1U;
        } else {
            if (((cp - range->first) % (uint32_t)range->stride) != 0U) return cp;
            return (uint32_t)((int32_t)cp + range->delta);
        }
    }
    return cp;
}

/* Expand one code point into its key units (1 to 4). */
static unsigned xx_fat_key_expand(uint32_t cp, uint32_t *out)
{
    size_t low = 0U;
    size_t high = sizeof(xx_fat_decomp_keys) / sizeof(xx_fat_decomp_keys[0]);
    if (cp < 0xC0U) {
        out[0] = xx_fat_fold(cp);
        return 1U;
    }
    if (cp >= XX_FAT_HANGUL_S && cp < XX_FAT_HANGUL_S + XX_FAT_HANGUL_COUNT) {
        uint32_t s = cp - XX_FAT_HANGUL_S;
        uint32_t per_l = XX_FAT_HANGUL_V_COUNT * XX_FAT_HANGUL_T_COUNT;
        out[0] = XX_FAT_HANGUL_L + s / per_l;
        out[1] = XX_FAT_HANGUL_V + (s % per_l) / XX_FAT_HANGUL_T_COUNT;
        if (s % XX_FAT_HANGUL_T_COUNT == 0U) return 2U;
        out[2] = XX_FAT_HANGUL_T + s % XX_FAT_HANGUL_T_COUNT;
        return 3U;
    }
    if (cp <= 0xFFFFU) {
        while (low < high) {
            size_t middle = low + (high - low) / 2U;
            uint32_t key = xx_fat_decomp_keys[middle];
            if (cp < key) {
                high = middle;
            } else if (cp > key) {
                low = middle + 1U;
            } else {
                uint32_t ref = xx_fat_decomp_refs[middle];
                unsigned length = (unsigned)(ref & 3U) + 1U;
                size_t start = (size_t)(ref >> 2U);
                unsigned index;
                /* Pool entries are stored already folded. */
                for (index = 0U; index < length; ++index) {
                    out[index] = xx_fat_decomp_pool[start + index];
                }
                return length;
            }
        }
    }
    out[0] = xx_fat_fold(cp);
    return 1U;
}

/* Streams the key units of a path; 0 marks the end. */
typedef struct xx_fat_key_iter_s {
    const char *text;
    uint32_t units[4];
    unsigned count;
    unsigned at;
} xx_fat_key_iter;

static void xx_fat_key_begin(xx_fat_key_iter *iter, const char *text)
{
    iter->text = text;
    iter->count = 0U;
    iter->at = 0U;
}

static uint32_t xx_fat_key_next(xx_fat_key_iter *iter)
{
    if (iter->at < iter->count) return iter->units[iter->at++];
    if (*iter->text == '\0') return 0U;
    iter->count = xx_fat_key_expand(xx_fat_utf8_next(&iter->text), iter->units);
    iter->at = 1U;
    return iter->units[0];
}

/* Compare two paths by key. Bytes both share verbatim - typically the whole
 * parent directory - are skipped first, back to a code-point boundary, since
 * the key is built one code point at a time. */
static bool xx_fat_name_equal(const char *left, const char *right, uint64_t *work)
{
    size_t same = 0U;
    xx_fat_key_iter a;
    xx_fat_key_iter b;
    while (left[same] != '\0' && left[same] == right[same]) ++same;
    *work += (uint64_t)same + 1U;
    if (left[same] == '\0' && right[same] == '\0') return true;
    while (same > 0U && ((unsigned char)left[same] & 0xC0U) == 0x80U) --same;
    xx_fat_key_begin(&a, left + same);
    xx_fat_key_begin(&b, right + same);
    for (;;) {
        uint32_t x = xx_fat_key_next(&a);
        uint32_t y = xx_fat_key_next(&b);
        ++*work;
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

/* A seeded multiplicative hash over the key units. The seed changes per
 * parse, so names cannot be precomputed to share a bucket. The state is
 * streamed: a directory hashes its own path once and every member continues
 * from there (see xx_fat_dirctx), so the cost of a member does not grow with
 * the depth it sits at. */
static uint32_t xx_fat_hash_feed(uint32_t hash, const char *text, uint64_t *work)
{
    xx_fat_key_iter iter;
    uint32_t unit;
    xx_fat_key_begin(&iter, text);
    while ((unit = xx_fat_key_next(&iter)) != 0U) {
        hash ^= unit;
        hash *= 16777619U;
        hash ^= hash >> 13U;
        ++*work;
    }
    return hash;
}

static uint32_t xx_fat_hash_final(uint32_t seed, uint32_t hash)
{
    hash ^= seed;
    hash ^= hash >> 16U;
    hash *= 0x85EBCA6BU;
    hash ^= hash >> 13U;
    hash *= 0xC2B2AE35U;
    hash ^= hash >> 16U;
    return hash;
}

/* True when an earlier member already has this name - or when the probe
 * budget is spent, in which case the caller drops the member. Only slots
 * whose stored hash matches are compared by name. */
static bool xx_fat_name_taken(xx_fat_private *parsed, const char *name, uint32_t hash)
{
    size_t mask;
    size_t slot;
    if (!parsed->name_slots) return false;
    mask = parsed->name_slot_capacity - 1U;
    slot = (size_t)hash & mask;
    while (parsed->name_slots[slot] != 0U) {
        const xx_fat_entry *other = &parsed->entries[parsed->name_slots[slot] - 1U];
        if (++parsed->name_probes > XX_FAT_MAX_NAME_PROBES) return true;
        if (other->name && other->name_hash == hash && xx_fat_name_equal(other->name, name, &parsed->name_work)) {
            return true;
        }
        slot = (slot + 1U) & mask;
    }
    return false;
}

static void xx_fat_name_insert(uint32_t *slots, size_t capacity, uint32_t hash, uint32_t value)
{
    size_t mask = capacity - 1U;
    size_t slot = (size_t)hash & mask;
    while (slots[slot] != 0U) slot = (slot + 1U) & mask;
    slots[slot] = value;
}

/* Record entries[index] in the name set, growing it to keep the load under
 * one half. The entry count is capped at XX_FAT_MAX_ENTRIES, so the table
 * never exceeds 512K slots (2 MiB). Growth re-inserts from the stored hashes
 * and never re-reads a name. */
static bool xx_fat_name_remember(xx_fat_private *parsed, size_t index)
{
    if ((index + 1U) * 2U > parsed->name_slot_capacity) {
        size_t capacity = parsed->name_slot_capacity ? parsed->name_slot_capacity : 64U;
        uint32_t *slots;
        size_t at;
        while ((index + 1U) * 2U > capacity) capacity *= 2U;
        if (capacity > SIZE_MAX / sizeof(*slots)) return false;
        slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        if (!slots) return false;
        for (at = 0U; at < index; ++at) {
            if (parsed->entries[at].name) {
                xx_fat_name_insert(slots, capacity, parsed->entries[at].name_hash, (uint32_t)(at + 1U));
            }
        }
        if (parsed->name_slots) xx_mem_free(parsed->name_slots);
        parsed->name_slots = slots;
        parsed->name_slot_capacity = capacity;
    }
    xx_fat_name_insert(parsed->name_slots, parsed->name_slot_capacity, parsed->entries[index].name_hash, (uint32_t)(index + 1U));
    return true;
}

/* 8.3 shape of one path component, as Windows judges it when it decides
 * whether a file needs a generated short alias: a 1-8 character stem, at most
 * one dot and a 1-3 character extension, all from the short-name character
 * set (lower case is accepted; NTFS stores such a name as its own alias). */
static bool xx_fat_is_83(const char *text, size_t length, bool *alias_shaped)
{
    size_t stem = 0U;
    size_t extension = 0U;
    bool dot = false;
    bool tilde_digit = false;
    size_t index;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)text[index];
        if (ch == '.') {
            if (dot) return false;
            dot = true;
            continue;
        }
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
              ch == '\'' || ch == '(' || ch == ')' || ch == '-' || ch == '@' || ch == '^' || ch == '_' || ch == '`' || ch == '{' || ch == '}' || ch == '~')) {
            return false;
        }
        if (dot) {
            ++extension;
        } else {
            if (ch >= '0' && ch <= '9' && index > 0U && text[index - 1U] == '~') {
                tilde_digit = true;
            }
            ++stem;
        }
    }
    if (stem == 0U || stem > 8U || extension > 3U || (dot && extension == 0U)) {
        return false;
    }
    if (alias_shaped) *alias_shaped = tilde_digit;
    return true;
}

/* Per-directory state shared by every member of one directory. */
typedef struct xx_fat_dirctx_s {
    const char *prefix;  /**< Parent path, "" for the root. */
    size_t prefix_size;  /**< xx_str_len(prefix). */
    size_t name_offset;  /**< Where a member's own name starts in its path. */
    uint32_t hash_state; /**< Hash state after prefix + '/'. */
    unsigned depth;
    bool is_root;
    /* An earlier member of this directory has a name that is not 8.3, so on a
     * host that generates short names it owns an alias like LONGFI~1.TXT, and
     * a later member of that name would open the aliased file instead. */
    bool has_long;
} xx_fat_dirctx;

static void xx_fat_dirctx_init(xx_fat_private *parsed, xx_fat_dirctx *ctx, const char *prefix, unsigned depth, bool is_root)
{
    ctx->prefix = prefix ? prefix : "";
    ctx->prefix_size = xx_str_len(ctx->prefix);
    ctx->name_offset = ctx->prefix_size != 0U ? ctx->prefix_size + 1U : 0U;
    ctx->hash_state = parsed->hash_seed ^ 2166136261U;
    ctx->depth = depth;
    ctx->is_root = is_root;
    ctx->has_long = false;
    parsed->name_work += (uint64_t)ctx->prefix_size;
    if (ctx->prefix_size != 0U) {
        ctx->hash_state = xx_fat_hash_feed(ctx->hash_state, ctx->prefix, &parsed->name_work);
        ctx->hash_state = xx_fat_hash_feed(ctx->hash_state, "/", &parsed->name_work);
    }
}

static uint32_t xx_fat_member_hash(xx_fat_private *parsed, const xx_fat_dirctx *ctx, const char *full_name)
{
    return xx_fat_hash_final(parsed->hash_seed, xx_fat_hash_feed(ctx->hash_state, full_name + ctx->name_offset, &parsed->name_work));
}

/* "<stem><mark><n><.ext>" for the last component of `full_name`. */
static char *xx_fat_rename(const char *full_name, size_t length, size_t name_offset, char mark, size_t value)
{
    char digits[24];
    char reversed[20];
    char *renamed;
    size_t split = length;
    size_t count = 0U;
    size_t used = 0U;
    size_t index;
    /* Insert before the extension of the last component, if it has one. */
    for (index = length; index > name_offset; --index) {
        if (full_name[index - 1U] == '.' && index - 1U > name_offset) {
            split = index - 1U;
            break;
        }
    }
    digits[count++] = mark;
    do {
        reversed[used++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && used < sizeof(reversed));
    while (used > 0U) digits[count++] = reversed[--used];
    if (length + count > XX_FAT_MAX_PATH) return NULL;
    renamed = (char *)xx_mem_alloc(length + count + 1U);
    if (!renamed) return NULL;
    xx_rt_memcpy(renamed, full_name, split);
    xx_rt_memcpy(renamed + split, digits, count);
    xx_rt_memcpy(renamed + split + count, full_name + split, length - split);
    renamed[length + count] = '\0';
    return renamed;
}

/* Return `full_name` if no earlier member has it (by key) and it cannot be
 * taken for another member's Windows short alias; otherwise a fresh
 * "<stem>~<n><.ext>", where n counts the members kept and dropped so far, or
 * NULL when that is taken too and the member has to be dropped. A name shaped
 * like a short alias (a '~' and a digit in an 8.3 stem) in a directory that
 * already holds a long name gets "+<n>" instead, which is never 8.3 and so can
 * never be an alias. `full_name` (`length` bytes) is consumed either way.
 * A FAT directory never holds two equal names, so only a damaged or crafted
 * image reaches the rename; each member costs at most two lookups. */
static char *xx_fat_unique_name(xx_fat_private *parsed, const xx_fat_dirctx *ctx, char *full_name, size_t length, uint32_t *hash_out)
{
    char *renamed;
    bool alias = false;
    uint32_t hash;
    size_t serial = parsed->count + parsed->dropped + 1U;
    (void)xx_fat_is_83(full_name + ctx->name_offset, length - ctx->name_offset, &alias);
    alias = alias && ctx->has_long;
    hash = xx_fat_member_hash(parsed, ctx, full_name);
    if (!alias && !xx_fat_name_taken(parsed, full_name, hash)) {
        *hash_out = hash;
        return full_name;
    }
    renamed = xx_fat_rename(full_name, length, ctx->name_offset, '~', serial);
    if (renamed && ctx->has_long) {
        size_t renamed_size = xx_str_len(renamed);
        bool renamed_alias = false;
        (void)xx_fat_is_83(renamed + ctx->name_offset, renamed_size - ctx->name_offset, &renamed_alias);
        if (renamed_alias) {
            xx_mem_free(renamed);
            renamed = xx_fat_rename(full_name, length, ctx->name_offset, '+', serial);
        }
    }
    if (renamed) {
        hash = xx_fat_member_hash(parsed, ctx, renamed);
        if (xx_fat_name_taken(parsed, renamed, hash)) {
            xx_mem_free(renamed);
            renamed = NULL;
        }
    }
    xx_str_free(full_name);
    *hash_out = hash;
    return renamed;
}

static bool xx_fat_append_entry(xx_fat_private *parsed, xx_fat_entry *entry)
{
    xx_fat_entry *grown;
    size_t capacity;
    uint64_t name_size;
    if (!parsed || !entry || !entry->name || parsed->count >= XX_FAT_MAX_ENTRIES) {
        return false;
    }
    name_size = (uint64_t)xx_str_len(entry->name) + 1U;
    if (name_size > XX_FAT_MAX_NAME_BYTES - parsed->name_bytes) return false;
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count || capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_fat_entry *)xx_mem_realloc(parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count] = *entry;
    if (!xx_fat_name_remember(parsed, parsed->count)) {
        parsed->entries[parsed->count].name = NULL;
        return false;
    }
    parsed->name_bytes += name_size;
    ++parsed->count;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Pull the start cluster out of a directory entry. The high half at +20 is
 * meaningful on FAT32 only; on FAT12/16 it is a different field entirely and
 * must be masked away. */
static uint32_t xx_fat_entry_cluster(const xx_fat_geometry *geometry, const uint8_t *entry)
{
    uint32_t low = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8U);
    uint32_t high = (uint32_t)entry[20] | ((uint32_t)entry[21] << 8U);
    if (geometry->kind != XX_FAT_KIND_FAT32) return low;
    return low | (high << 16U);
}

/* The BPB fields, as stored from +11 of the boot sector, or synthesised for a
 * DOS 1.x disk that carries none. Kept raw so that one validator serves both. */
typedef struct xx_fat_bpb_s {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t root_entry_count;
    uint32_t total_sectors16;
    uint32_t total_sectors32;
    uint32_t fat_size16;
    uint32_t fat_size32;
    uint32_t root_cluster;
    uint8_t media;
} xx_fat_bpb;

static void xx_fat_bpb_decode(const uint8_t *boot, xx_fat_bpb *bpb)
{
    bpb->bytes_per_sector = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 11U, false);
    bpb->sectors_per_cluster = boot[13];
    bpb->reserved_sectors = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 14U, false);
    bpb->num_fats = boot[16];
    bpb->root_entry_count = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 17U, false);
    bpb->total_sectors16 = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 19U, false);
    bpb->media = boot[21];
    bpb->fat_size16 = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 22U, false);
    bpb->total_sectors32 = xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 32U, false);
    bpb->fat_size32 = xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 36U, false);
    bpb->root_cluster = xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 44U, false);
}

/* Validate a BPB and derive the volume layout from it. Nothing downstream
 * re-checks geometry, so every field a later computation depends on is pinned
 * here. geometry->base and geometry->total_size must already be set.
 *
 * The image may end before the volume does: disk copiers (CopyQM, SaveDskF,
 * DiskDupe, PM Diskcopy, ...) store only the cylinders in use, and dumps of
 * 82-track disks are often cut at track 80. Such an image is accepted as long
 * as the boot sector, FAT #0, the fixed root directory and cluster 2 are all
 * inside it; `truncated` is set, xx_fat_read_geometry() then asks the FAT
 * to corroborate the BPB, and any cluster past the end of the image simply
 * cannot be read. */
static bool xx_fat_layout(xx_fat_geometry *geometry, const xx_fat_bpb *bpb)
{
    uint32_t root_dir_sectors;
    uint32_t fat_size;
    uint32_t total_sectors;
    uint32_t meta_sectors;
    uint32_t data_sectors;
    uint64_t volume_size;

    geometry->bytes_per_sector = bpb->bytes_per_sector;
    geometry->sectors_per_cluster = bpb->sectors_per_cluster;
    geometry->reserved_sectors = bpb->reserved_sectors;
    geometry->num_fats = bpb->num_fats;
    geometry->root_entry_count = bpb->root_entry_count;
    geometry->media = bpb->media;
    geometry->truncated = false;

    if (geometry->bytes_per_sector != 512U && geometry->bytes_per_sector != 1024U && geometry->bytes_per_sector != 2048U && geometry->bytes_per_sector != 4096U) {
        return false;
    }
    if (!xx_fat_is_power_of_two(geometry->sectors_per_cluster) || geometry->sectors_per_cluster > 128U) {
        return false;
    }
    /* Reserved sectors is never zero on FAT - the boot sector itself lives
     * there. NTFS stores 0 in this field, so this single test is what keeps an
     * NTFS boot sector out of this reader. */
    if (geometry->reserved_sectors == 0U) return false;
    if (geometry->num_fats != 1U && geometry->num_fats != 2U) return false;
    if (geometry->media < XX_FAT_MIN_MEDIA) return false;

    geometry->bytes_per_cluster = geometry->bytes_per_sector * geometry->sectors_per_cluster;
    if (geometry->bytes_per_cluster > XX_FAT_MAX_CLUSTER_SIZE) return false;

    /* The fixed root directory must be a whole number of sectors' worth of
     * 32-byte entries, per the specification. */
    if (geometry->root_entry_count != 0U && ((geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE) % geometry->bytes_per_sector) != 0U) {
        return false;
    }
    root_dir_sectors = (geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE + geometry->bytes_per_sector - 1U) / geometry->bytes_per_sector;

    fat_size = bpb->fat_size16 != 0U ? bpb->fat_size16 : bpb->fat_size32;
    total_sectors = bpb->total_sectors16 != 0U ? bpb->total_sectors16 : bpb->total_sectors32;
    if (fat_size == 0U || total_sectors == 0U) return false;
    geometry->fat_size_sectors = fat_size;
    geometry->total_sectors = total_sectors;

    /* Reserved + FATs + fixed root must fit inside the volume with at least
     * one data sector left over. All of this is done in 64 bits because the
     * operands are attacker controlled 32-bit values. */
    {
        uint64_t meta = (uint64_t)geometry->reserved_sectors + (uint64_t)geometry->num_fats * (uint64_t)fat_size + (uint64_t)root_dir_sectors;
        if (meta >= (uint64_t)total_sectors || meta > UINT32_MAX) return false;
        meta_sectors = (uint32_t)meta;
    }
    data_sectors = total_sectors - meta_sectors;
    geometry->cluster_count = data_sectors / geometry->sectors_per_cluster;
    if (geometry->cluster_count == 0U || geometry->cluster_count > XX_FAT_MAX_CLUSTERS) {
        return false;
    }

    /* The Microsoft rule, and the only authority on the FAT type. The strings
     * at +54 / +82 are not consulted: real images leave them blank. */
    if (geometry->cluster_count < 4085U) {
        geometry->kind = XX_FAT_KIND_FAT12;
    } else if (geometry->cluster_count < 65525U) {
        geometry->kind = XX_FAT_KIND_FAT16;
    } else {
        geometry->kind = XX_FAT_KIND_FAT32;
    }

    /* Cross-check the type against the fields that are defined to be zero or
     * non-zero for it. This is what separates a real FAT32 volume from a
     * FAT12/16 boot sector whose cluster arithmetic happened to land high. */
    if (geometry->kind == XX_FAT_KIND_FAT32) {
        if (geometry->root_entry_count != 0U || bpb->fat_size16 != 0U) {
            return false;
        }
        geometry->root_cluster = bpb->root_cluster;
        if (!xx_fat_cluster_is_data(geometry, geometry->root_cluster)) {
            return false;
        }
    } else {
        if (geometry->root_entry_count == 0U || bpb->fat_size16 == 0U) {
            return false;
        }
        geometry->root_cluster = 0U;
    }

    /* The FAT must be large enough to address every cluster it claims. A FAT
     * too small for its own cluster count is malformed, and without this check
     * every chain walk would simply run off the end of the table. */
    {
        uint64_t needed;
        if (geometry->kind == XX_FAT_KIND_FAT12) {
            needed = (((uint64_t)geometry->cluster_count + 2U) * 3U + 1U) / 2U;
        } else if (geometry->kind == XX_FAT_KIND_FAT16) {
            needed = ((uint64_t)geometry->cluster_count + 2U) * 2U;
        } else {
            needed = ((uint64_t)geometry->cluster_count + 2U) * 4U;
        }
        if (needed > (uint64_t)fat_size * geometry->bytes_per_sector) {
            return false;
        }
    }

    volume_size = (uint64_t)total_sectors * geometry->bytes_per_sector;
    if (volume_size == 0U || volume_size > XX_FAT_MAX_VOLUME) return false;
    geometry->volume_size = volume_size;
    if (!xx_fat_add(geometry->base, volume_size, &geometry->volume_end)) {
        return false;
    }
    geometry->truncated = geometry->volume_end > geometry->total_size;

    if (!xx_fat_add(geometry->base, (uint64_t)geometry->reserved_sectors * geometry->bytes_per_sector, &geometry->fat_offset)) {
        return false;
    }
    geometry->fat_bytes = (int64_t)((uint64_t)fat_size * geometry->bytes_per_sector);
    if (!xx_fat_range_within(geometry->total_size, geometry->fat_offset, geometry->fat_bytes)) {
        return false;
    }
    if (!xx_fat_add(geometry->fat_offset, (uint64_t)geometry->num_fats * (uint64_t)fat_size * geometry->bytes_per_sector, &geometry->root_offset)) {
        return false;
    }
    geometry->root_bytes = (int64_t)((uint64_t)geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE);
    if (!xx_fat_add(geometry->root_offset, (uint64_t)geometry->root_bytes, &geometry->data_offset)) {
        return false;
    }
    if (geometry->kind == XX_FAT_KIND_FAT32) {
        geometry->root_offset = -1;
        geometry->root_bytes = 0;
    } else if (!xx_fat_range_within(geometry->total_size, geometry->root_offset, geometry->root_bytes)) {
        return false;
    }
    /* Cluster 2 must exist; the last cluster is bounds-checked per access. */
    return xx_fat_range_within(geometry->total_size, geometry->data_offset, (int64_t)geometry->bytes_per_cluster);
}

/* The first two FAT entries are reserved and hold fixed values: entry 0 is the
 * media descriptor padded with ones, entry 1 is end-of-chain (FAT16 and FAT32
 * may clear its top two bits as the clean-shutdown and no-error flags). A
 * formatted IBM-compatible FAT therefore opens with md FF FF. */
static bool xx_fat_fat_id_ok(const uint8_t *fat, uint32_t kind)
{
    if (fat[0] < XX_FAT_MIN_MEDIA || fat[1] != 0xFFU || fat[2] != 0xFFU) {
        return false;
    }
    if (kind == XX_FAT_KIND_FAT16) return (fat[3] & 0x3FU) == 0x3FU;
    if (kind == XX_FAT_KIND_FAT32) {
        return (fat[3] & 0x0FU) == 0x0FU && fat[4] == 0xFFU && fat[5] == 0xFFU && fat[6] == 0xFFU && (fat[7] & 0x03U) == 0x03U;
    }
    return true;
}

static bool xx_fat_is_uniform(const uint8_t *data, size_t size)
{
    size_t index;
    for (index = 1U; index < size; ++index) {
        if (data[index] != data[0]) return false;
    }
    return true;
}

/* Bytes that no DOS, TOS or MSX-DOS directory entry name can hold. Characters
 * that are merely discouraged (lower case, '+', ',', ';', '=', '[', ']') are
 * let through: non-PC systems wrote them. */
static bool xx_fat_probe_name_byte(uint8_t ch)
{
    return ch >= 0x20U && ch != 0x7FU && ch != '"' && ch != '*' && ch != '/' && ch != ':' && ch != '<' && ch != '>' && ch != '?' && ch != '\\' && ch != '|';
}

/* Classify one root entry for xx_fat_root_plausible: 1 live and sane, 0 not
 * counted (deleted or a long-name piece), -1 impossible, 2 end marker. */
static int xx_fat_probe_entry(const xx_fat_geometry *geometry, const uint8_t *entry)
{
    uint8_t attributes = entry[11];
    uint32_t cluster;
    uint32_t size;
    size_t index;
    if (entry[0] == XX_FAT_ENTRY_END) return 2;
    if (entry[0] == XX_FAT_ENTRY_FREE) return 0;
    if ((attributes & XX_FAT_ATTR_LONG_MASK) == XX_FAT_ATTR_LONG_NAME) {
        /* A long-name piece: its cluster field is defined to be zero. */
        return ((attributes & 0xC0U) == 0U && entry[26] == 0U && entry[27] == 0U) ? 0 : -1;
    }
    if ((attributes & 0xC0U) != 0U) return -1;
    if ((attributes & (XX_FAT_ATTR_DIRECTORY | XX_FAT_ATTR_VOLUME_ID)) == (XX_FAT_ATTR_DIRECTORY | XX_FAT_ATTR_VOLUME_ID)) {
        return -1;
    }
    if (entry[0] == ' ') return -1;
    for (index = 0U; index < 11U; ++index) {
        if (index == 0U && entry[0] == XX_FAT_ENTRY_KANJI_E5) continue;
        if (!xx_fat_probe_name_byte(entry[index])) return -1;
    }
    if (attributes & XX_FAT_ATTR_VOLUME_ID) return 1;
    cluster = xx_fat_entry_cluster(geometry, entry);
    size = xx_data_get_u32(entry, XX_FAT_DIR_ENTRY_SIZE, 28U, false);
    if (attributes & XX_FAT_ATTR_DIRECTORY) {
        return xx_fat_cluster_is_data(geometry, cluster) ? 1 : -1;
    }
    if (cluster == 0U) return size == 0U ? 1 : -1;
    if (!xx_fat_cluster_is_data(geometry, cluster)) return -1;
    if ((uint64_t)size > (uint64_t)geometry->cluster_count * geometry->bytes_per_cluster) {
        return -1;
    }
    return 1;
}

/* Does the root directory look like one? Used only when the boot sector lacks
 * the IBM PC markers, so an arbitrary block whose bytes 11..23 happen to form
 * a consistent BPB is not taken for a volume. At least one live entry is
 * required, and at most one impossible entry per four live ones is tolerated
 * (real disks carry the odd damaged entry). At most XX_FAT_PROBE_ENTRIES
 * entries (4 KiB) are read. */
static bool xx_fat_root_plausible(Abstractformat *self, const xx_fat_geometry *geometry)
{
    uint8_t buffer[XX_FAT_PROBE_ENTRIES * XX_FAT_DIR_ENTRY_SIZE];
    int64_t offset;
    uint64_t available;
    size_t count;
    size_t index;
    unsigned live = 0U;
    unsigned bad = 0U;
    if (geometry->kind == XX_FAT_KIND_FAT32) {
        if (!xx_fat_cluster_offset(geometry, geometry->root_cluster, &offset)) {
            return false;
        }
        available = geometry->bytes_per_cluster;
    } else {
        offset = geometry->root_offset;
        available = (uint64_t)geometry->root_bytes;
    }
    count = (size_t)(available / XX_FAT_DIR_ENTRY_SIZE);
    if (count > XX_FAT_PROBE_ENTRIES) count = XX_FAT_PROBE_ENTRIES;
    if (count == 0U || !xx_fat_range_within(geometry->total_size, offset, (int64_t)(count * XX_FAT_DIR_ENTRY_SIZE)) ||
        !xx_fat_read_at(self->device, offset, buffer, count * XX_FAT_DIR_ENTRY_SIZE)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        int verdict = xx_fat_probe_entry(geometry, buffer + index * XX_FAT_DIR_ENTRY_SIZE);
        if (verdict == 2) break;
        if (verdict == 1) ++live;
        if (verdict < 0) ++bad;
    }
    return live != 0U && bad * 4U <= live;
}

/* How much corroboration a boot sector without the IBM PC markers needs. */
#define XX_FAT_CONFIRM_X86 0     /* FAT id, or matching FATs plus a sane root */
#define XX_FAT_CONFIRM_FOREIGN 1 /* (FAT id or matching FATs) and a sane root */
#define XX_FAT_CONFIRM_STATIC 2  /* FAT id and matching FATs and a sane root */

static bool xx_fat_confirm(Abstractformat *self, const xx_fat_geometry *geometry, int mode)
{
    uint8_t first[XX_FAT_PROBE_FAT_BYTES];
    uint8_t second[XX_FAT_PROBE_FAT_BYTES];
    bool id_ok;
    bool copies_ok = false;
    if (!xx_fat_read_at(self->device, geometry->fat_offset, first, sizeof(first))) {
        return false;
    }
    id_ok = xx_fat_fat_id_ok(first, geometry->kind);
    if (geometry->num_fats >= 2U) {
        int64_t second_offset;
        /* The second copy can lie past the end of a truncated image; it then
         * simply does not count as corroboration. */
        if (xx_fat_add(geometry->fat_offset, (uint64_t)geometry->fat_bytes, &second_offset) &&
            xx_fat_range_within(geometry->total_size, second_offset, (int64_t)sizeof(second)) && xx_fat_read_at(self->device, second_offset, second, sizeof(second))) {
            /* A blank region matches itself; only a FAT with content counts. */
            copies_ok = xx_rt_memcmp(first, second, sizeof(first)) == 0 && !xx_fat_is_uniform(first, sizeof(first));
        }
    }
    if (mode == XX_FAT_CONFIRM_X86) {
        return id_ok || (copies_ok && xx_fat_root_plausible(self, geometry));
    }
    if (mode == XX_FAT_CONFIRM_FOREIGN) {
        return (id_ok || copies_ok) && xx_fat_root_plausible(self, geometry);
    }
    return id_ok && copies_ok && xx_fat_root_plausible(self, geometry);
}

/* DOS 1.x wrote no BPB: the boot sector is all code, and the geometry follows
 * from the disk size alone, confirmed by the media byte that opens the FAT.
 * These are the four IBM PC formats of that era (PC DOS 1.0 / 1.1), with the
 * layout PC DOS 2.0 later recorded in its BPB for the same media bytes. The
 * same path recovers later disks whose boot sector a boot-sector virus
 * replaced (Stoned opens with a far jump, EA xx xx C0 07, and no BPB). The
 * boot sector must open with an x86 jump (EB, E9 or the far jump EA) - IBM
 * PC boot code always does, and this keeps out disks of other machines that
 * share these sizes and FAT layouts but carry their own disk label at +0
 * (ACT Apricot: "IBM  3.3" with its BPB at +0x50). Beyond that the exact
 * image size, the FAT id, the agreeing FAT copies and the root directory
 * carry the decision. */
static bool xx_fat_static_geometry(Abstractformat *self, xx_fat_geometry *geometry, const uint8_t *boot)
{
    static const struct {
        int64_t size;
        uint8_t media;
        uint8_t sectors_per_cluster;
        uint8_t fat_sectors;
        uint16_t root_entries;
        uint16_t total_sectors;
    } k_dos1[] = {
        {163840, 0xFEU, 1U, 1U, 64U, 320U},  /* 160K: 40 x 1 x 8 */
        {184320, 0xFCU, 1U, 2U, 64U, 360U},  /* 180K: 40 x 1 x 9 */
        {327680, 0xFFU, 2U, 1U, 112U, 640U}, /* 320K: 40 x 2 x 8 */
        {368640, 0xFDU, 2U, 2U, 112U, 720U}  /* 360K: 40 x 2 x 9 */
    };
    int64_t image;
    size_t index;
    if (boot[0] != 0xEBU && boot[0] != 0xE9U && boot[0] != 0xEAU) return false;
    image = geometry->total_size - geometry->base;
    for (index = 0U; index < sizeof(k_dos1) / sizeof(k_dos1[0]); ++index) {
        xx_fat_bpb bpb;
        uint8_t id[3];
        if (image != k_dos1[index].size) continue;
        if (!xx_fat_read_at(self->device, geometry->base + XX_FAT_BOOT_SIZE, id, sizeof(id)) || id[0] != k_dos1[index].media) {
            return false;
        }
        xx_mem_zero(&bpb, sizeof(bpb));
        bpb.bytes_per_sector = XX_FAT_BOOT_SIZE;
        bpb.sectors_per_cluster = k_dos1[index].sectors_per_cluster;
        bpb.reserved_sectors = 1U;
        bpb.num_fats = 2U;
        bpb.root_entry_count = k_dos1[index].root_entries;
        bpb.total_sectors16 = k_dos1[index].total_sectors;
        bpb.fat_size16 = k_dos1[index].fat_sectors;
        bpb.media = k_dos1[index].media;
        if (!xx_fat_layout(geometry, &bpb)) return false;
        geometry->boot_kind = XX_FAT_BOOT_STATIC;
        return xx_fat_confirm(self, geometry, XX_FAT_CONFIRM_STATIC);
    }
    return false;
}

/* Read the boot sector and settle the geometry. Four kinds are accepted, from
 * the most self-describing to the least, and the weaker the boot sector's own
 * evidence the more the FAT and root directory must corroborate it:
 *
 *   PC       x86 jump (EB xx 90 / E9) and 0x55AA at +510, consistent BPB;
 *            a truncated image also needs what X86 needs.
 *   X86      x86 jump without 0x55AA (MSX-DOS, PC-98, DOS 1.1-2.x disks with a
 *            BPB): the FAT must open with the media id, or both FAT copies
 *            must agree and the root directory must be sane.
 *   FOREIGN  any other opening - Atari ST (68000 BRA.S 0x60 xx, or zeros on a
 *            non-bootable disk), FM Towns ("IPL4"), and other non-PC machines
 *            that kept the DOS BPB at +11: the FAT id or matching FAT copies,
 *            and a sane root directory.
 *   STATIC   no BPB at all (DOS 1.x): see xx_fat_static_geometry. */
static bool xx_fat_read_geometry(Abstractformat *self, xx_fat_geometry *geometry, uint8_t *boot)
{
    xx_fat_bpb bpb;
    bool x86_jump;
    bool pc_signature;

    geometry->base = self->base_address;
    geometry->total_size = xx_io_total_size(self->device);
    geometry->volume_end = -1;
    geometry->root_offset = -1;
    geometry->boot_kind = XX_FAT_BOOT_PC;
    if (!xx_fat_range_within(geometry->total_size, geometry->base, XX_FAT_BOOT_SIZE) || !xx_fat_read_at(self->device, geometry->base, boot, XX_FAT_BOOT_SIZE)) {
        return false;
    }
    x86_jump = (boot[0] == 0xEBU && boot[2] == 0x90U) || boot[0] == 0xE9U;
    pc_signature = boot[510] == 0x55U && boot[511] == 0xAAU;

    xx_fat_bpb_decode(boot, &bpb);
    if (!x86_jump && bpb.reserved_sectors == 0U) {
        /* Some Atari ST formatters leave the reserved-sector count at zero
         * although the boot sector still occupies sector 0; TOS ignores the
         * field. Read it as 1, as Deark does (modules/fat.c). Only a boot
         * sector without an x86 jump gets this reading: NTFS also stores 0
         * here, behind an x86 jump, and must stay rejected. */
        bpb.reserved_sectors = 1U;
    }
    if (xx_fat_layout(geometry, &bpb)) {
        if (x86_jump && pc_signature) {
            geometry->boot_kind = XX_FAT_BOOT_PC;
            /* A short image has lost the size check that corroborates the
             * BPB, so its FAT has to back it up, as for an X86 boot sector.
             * A complete one needs either the FAT id or a sane root: a disk
             * whose sectors were remapped by a geometry conversion keeps its
             * PC boot sector but has neither where the BPB points. */
            if (geometry->truncated) {
                return xx_fat_confirm(self, geometry, XX_FAT_CONFIRM_X86);
            }
            return xx_fat_confirm(self, geometry, XX_FAT_CONFIRM_X86) || xx_fat_root_plausible(self, geometry);
        }
        if (x86_jump) {
            geometry->boot_kind = XX_FAT_BOOT_X86;
            return xx_fat_confirm(self, geometry, XX_FAT_CONFIRM_X86);
        }
        geometry->boot_kind = XX_FAT_BOOT_FOREIGN;
        return xx_fat_confirm(self, geometry, XX_FAT_CONFIRM_FOREIGN);
    }
    /* Only the boot sector without a BPB is left. Reset what the failed
     * attempt may have filled in before trying the size table. */
    geometry->volume_end = -1;
    geometry->root_offset = -1;
    geometry->truncated = false;
    return xx_fat_static_geometry(self, geometry, boot);
}

/* Record a root volume-label entry. The label lives in the eleven raw name
 * bytes and is not an 8.3 name: no dot is inserted and the case flags do not
 * apply to it. */
static void xx_fat_take_label(xx_fat_private *parsed, const uint8_t *entry)
{
    char buffer[12];
    size_t limit = 11U;
    size_t index;
    char *label;
    if (parsed->volume_label) return;
    while (limit > 0U && entry[limit - 1U] == ' ') --limit;
    if (limit == 0U) return;
    for (index = 0U; index < limit; ++index) {
        if (entry[index] < 0x20U) return;
        buffer[index] = (char)entry[index];
    }
    buffer[limit] = '\0';
    label = (char *)xx_mem_alloc(limit + 1U);
    if (!label) return;
    xx_rt_memcpy(label, buffer, limit + 1U);
    parsed->volume_label = label;
}

/* Queue a subdirectory for a later pass. Failure here loses a subtree but is
 * never fatal: the records already gathered stay usable. */
static bool xx_fat_enqueue(xx_fat_private *parsed, uint32_t cluster, const char *prefix, unsigned depth)
{
    if (parsed->queue_count == parsed->queue_capacity) {
        size_t capacity = parsed->queue_capacity ? parsed->queue_capacity * 2U : 32U;
        xx_fat_pending *grown;
        if (capacity < parsed->queue_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
            return false;
        }
        grown = (xx_fat_pending *)xx_mem_realloc(parsed->queue, capacity * sizeof(*grown));
        if (!grown) return false;
        parsed->queue = grown;
        parsed->queue_capacity = capacity;
    }
    parsed->queue[parsed->queue_count].cluster = cluster;
    parsed->queue[parsed->queue_count].prefix = prefix;
    parsed->queue[parsed->queue_count].depth = depth;
    ++parsed->queue_count;
    return true;
}

/* Decode one 32-byte record. Returns false only to stop the whole directory
 * (end-of-directory marker or a fatal allocation failure). */
static bool xx_fat_handle_entry(Abstractformat *self, xx_fat_private *parsed, const uint8_t *raw, int64_t entry_offset, xx_fat_dirctx *ctx, xx_fat_lfn *lfn,
                                xx_pd_struct *pd)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    uint8_t attributes = raw[11];
    uint32_t cluster;
    uint32_t size;
    uint32_t name_hash = 0U;
    size_t full_size = 0U;
    char *name = NULL;
    char *full_name = NULL;
    xx_fat_entry entry;
    bool folder;

    if (raw[0] == XX_FAT_ENTRY_END) return false; /* Nothing follows. */
    /* Out of name budget: stop this directory; the walk stops on the same
     * test before the next one. */
    if (parsed->name_work > XX_FAT_MAX_NAME_WORK || parsed->count + parsed->dropped >= XX_FAT_MAX_ENTRIES) {
        return false;
    }
    if (raw[0] == XX_FAT_ENTRY_FREE) {
        /* Deleted. Its long-name entries, if any, are now orphaned, so the
         * in-flight sequence is dropped rather than attached to whatever comes
         * next. */
        xx_fat_lfn_reset(lfn);
        return true;
    }
    if ((attributes & XX_FAT_ATTR_LONG_MASK) == XX_FAT_ATTR_LONG_NAME) {
        xx_fat_lfn_push(lfn, raw);
        return true;
    }
    if (attributes & XX_FAT_ATTR_VOLUME_ID) {
        /* The volume label. Only the root copy is meaningful. */
        if (ctx->is_root) xx_fat_take_label(parsed, raw);
        xx_fat_lfn_reset(lfn);
        return true;
    }

    folder = (attributes & XX_FAT_ATTR_DIRECTORY) != 0U;
    cluster = xx_fat_entry_cluster(geometry, raw);
    size = xx_data_get_u32(raw, XX_FAT_DIR_ENTRY_SIZE, 28U, false);
    if (folder) size = 0U; /* A directory's size field is defined to be zero. */

    name = xx_fat_lfn_finish(lfn, raw);
    if (!name) name = xx_fat_short_name(raw);
    xx_fat_lfn_reset(lfn);
    if (!name) return true; /* Undecodable name: skip this record, keep going. */
    /* "." and ".." point at this directory and its parent; following either
     * would loop, and neither is a member in its own right. */
    if (xx_fat_is_dot_name(name)) {
        xx_str_free(name);
        return true;
    }
    /* A file cannot be larger than the data region that could hold it. */
    if (!folder && (uint64_t)size > (uint64_t)geometry->cluster_count * geometry->bytes_per_cluster) {
        xx_str_free(name);
        return true;
    }
    full_name = xx_fat_join_name(ctx->prefix, ctx->prefix_size, name, &full_size);
    xx_str_free(name);
    if (!full_name) return true;
    parsed->name_work += (uint64_t)full_size;
    full_name = xx_fat_unique_name(parsed, ctx, full_name, full_size, &name_hash);
    if (!full_name) {
        /* Dropped: charge its path as if it had been kept, so repeated
         * duplicates run into the same budget as listed members. */
        uint64_t charge = (uint64_t)full_size + 1U;
        ++parsed->dropped;
        parsed->name_bytes = charge > XX_FAT_MAX_NAME_BYTES - parsed->name_bytes ? XX_FAT_MAX_NAME_BYTES : parsed->name_bytes + charge;
        return parsed->name_bytes + XX_FAT_MAX_PATH + 1U <= XX_FAT_MAX_NAME_BYTES;
    }

    xx_mem_zero(&entry, sizeof(entry));
    entry.name = full_name;
    entry.name_hash = name_hash;
    entry.header_offset = entry_offset;
    entry.data_offset = -1;
    entry.first_cluster = cluster;
    entry.size = size;
    entry.dos_time = xx_data_get_u16(raw, XX_FAT_DIR_ENTRY_SIZE, 22U, false);
    entry.dos_date = xx_data_get_u16(raw, XX_FAT_DIR_ENTRY_SIZE, 24U, false);
    entry.attributes = attributes;
    entry.is_folder = folder;
    if (xx_fat_cluster_is_data(geometry, cluster)) {
        int64_t offset;
        if (xx_fat_cluster_offset(geometry, cluster, &offset)) {
            entry.data_offset = offset;
        }
    }
    if (!xx_fat_append_entry(parsed, &entry)) {
        xx_str_free(full_name);
        return false;
    }
    if (!ctx->has_long) {
        const char *own = full_name + ctx->name_offset;
        ctx->has_long = !xx_fat_is_83(own, xx_str_len(own), NULL);
    }
    /* The appended record owns full_name; the queue only borrows it, and
     * xx_fat_private_cleanup() is what releases it. */
    if (folder && xx_fat_cluster_is_data(geometry, cluster) && ctx->depth + 1U <= XX_FAT_MAX_DEPTH) {
        (void)xx_fat_enqueue(parsed, cluster, full_name, ctx->depth + 1U);
    }
    (void)self;
    (void)pd;
    return true;
}

/* The FAT12/16 root directory: one contiguous run of entries between the FATs
 * and cluster 2. It has no cluster chain and cannot grow, which is the one
 * structural difference from every other directory on the volume. */
static bool xx_fat_scan_fixed_root(Abstractformat *self, xx_fat_private *parsed, xx_pd_struct *pd)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    int64_t offset = geometry->root_offset;
    int64_t remaining = geometry->root_bytes;
    uint8_t raw[XX_FAT_DIR_ENTRY_SIZE];
    xx_fat_lfn lfn;
    xx_fat_dirctx ctx;
    xx_fat_lfn_reset(&lfn);
    if (pd && xx_pd_is_stopped(pd)) return false;
    xx_fat_dirctx_init(parsed, &ctx, "", 0U, true);
    while (remaining >= (int64_t)XX_FAT_DIR_ENTRY_SIZE) {
        if (parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES) break;
        ++parsed->dir_entries;
        if (!xx_fat_read_at(self->device, offset, raw, sizeof(raw))) break;
        if (!xx_fat_handle_entry(self, parsed, raw, offset, &ctx, &lfn, pd)) {
            break;
        }
        offset += (int64_t)XX_FAT_DIR_ENTRY_SIZE;
        remaining -= (int64_t)XX_FAT_DIR_ENTRY_SIZE;
    }
    return true;
}

/* One clustered directory, walked along its chain. Only ever called from the
 * queue driver, so the shared visited set belongs to this chain alone for the
 * duration - which is what makes the loop test sound. */
static bool xx_fat_scan_clustered(Abstractformat *self, xx_fat_private *parsed, uint32_t start_cluster, const char *prefix, unsigned depth, bool is_root,
                                  xx_pd_struct *pd)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_fat_lfn lfn;
    xx_fat_dirctx ctx;
    uint8_t *buffer;
    uint32_t cluster = start_cluster;
    bool ok = true;

    if (!xx_fat_cluster_is_data(geometry, start_cluster)) return true;
    /* Each start cluster is admitted once for the whole parse, so a directory
     * that contains itself - or one aliased into two parents - terminates
     * instead of expanding the tree without bound. */
    if (!parsed->dir_seen || start_cluster >= (uint32_t)(parsed->bitmap_bytes * 8U)) {
        return true;
    }
    if (xx_fat_bit_test(parsed->dir_seen, start_cluster)) return true;
    xx_fat_bit_set(parsed->dir_seen, start_cluster);

    buffer = (uint8_t *)xx_mem_alloc(geometry->bytes_per_cluster);
    if (!buffer) return false;
    xx_fat_dirctx_init(parsed, &ctx, prefix, depth, is_root);
    xx_fat_lfn_reset(&lfn);
    xx_fat_visit_reset(parsed);
    if (!xx_fat_visit(parsed, start_cluster)) cluster = 0U;
    while (cluster != 0U) {
        int64_t offset;
        size_t position;
        uint32_t next = 0U;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (!xx_fat_cluster_offset(geometry, cluster, &offset) || !xx_fat_read_at(self->device, offset, buffer, geometry->bytes_per_cluster)) {
            break;
        }
        for (position = 0U; position + XX_FAT_DIR_ENTRY_SIZE <= geometry->bytes_per_cluster; position += XX_FAT_DIR_ENTRY_SIZE) {
            if (parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES) {
                cluster = 0U;
                break;
            }
            ++parsed->dir_entries;
            if (!xx_fat_handle_entry(self, parsed, buffer + position, offset + (int64_t)position, &ctx, &lfn, pd)) {
                cluster = 0U;
                break;
            }
        }
        if (cluster == 0U) break;
        if (!xx_fat_chain_next(self, parsed, cluster, &next)) {
            break; /* Global step budget exhausted. */
        }
        cluster = next;
    }
    xx_fat_visit_reset(parsed);
    xx_mem_free(buffer);
    return ok;
}

/* Drive the whole tree. Breadth-first rather than recursive: it bounds stack
 * use regardless of nesting and, more importantly, keeps exactly one cluster
 * chain live at a time so the shared visited set is never clobbered mid-walk. */
static bool xx_fat_walk_tree(Abstractformat *self, xx_fat_private *parsed, xx_pd_struct *pd)
{
    if (parsed->geometry.kind == XX_FAT_KIND_FAT32) {
        /* The FAT32 root is an ordinary cluster chain starting at the cluster
         * named in the BPB. There is no fixed root region at all. */
        if (!xx_fat_scan_clustered(self, parsed, parsed->geometry.root_cluster, "", 0U, true, pd)) {
            return false;
        }
    } else if (!xx_fat_scan_fixed_root(self, parsed, pd)) {
        return false;
    }
    while (parsed->queue_head < parsed->queue_count) {
        xx_fat_pending item = parsed->queue[parsed->queue_head++];
        if (parsed->count + parsed->dropped >= XX_FAT_MAX_ENTRIES || parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES || parsed->name_work > XX_FAT_MAX_NAME_WORK ||
            parsed->name_bytes + XX_FAT_MAX_PATH + 1U > XX_FAT_MAX_NAME_BYTES) {
            break;
        }
        if (!xx_fat_scan_clustered(self, parsed, item.cluster, item.prefix, item.depth, false, pd)) {
            return false;
        }
    }
    return true;
}

static bool xx_fat_parse(Abstractformat *self, xx_fat_private *parsed, xx_pd_struct *pd)
{
    uint8_t boot[XX_FAT_BOOT_SIZE];
    xx_fat_geometry *geometry;
    size_t bitmap_bytes;

    /* Initialise before the guard clause: callers such as
     * xx_fat_check_is_valid() run xx_fat_private_cleanup() on their stack copy
     * whatever this returns, and cleaning up an uninitialised one would free
     * indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->geometry.total_size = -1;
        parsed->geometry.volume_end = -1;
        parsed->geometry.root_offset = -1;
        parsed->fat_cache.offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    geometry = &parsed->geometry;
    /* Per-parse seed for the duplicate-name hash (see xx_fat_name_hash). It
     * need not be cryptographic: XX_FAT_MAX_NAME_PROBES bounds the cost
     * regardless, the seed only keeps legitimate names off that bound. */
    {
        uint64_t mix = (uint64_t)xx_rt_clock_ms() ^ ((uint64_t)(uintptr_t)parsed << 7U) ^ (uint64_t)(uintptr_t)boot;
        mix ^= mix >> 33U;
        mix *= UINT64_C(0xFF51AFD7ED558CCD);
        mix ^= mix >> 33U;
        parsed->hash_seed = (uint32_t)mix ^ (uint32_t)(mix >> 32U);
    }
    if (!xx_fat_read_geometry(self, geometry, boot)) goto fail;

    bitmap_bytes = ((size_t)geometry->cluster_count + 2U + 7U) / 8U;
    parsed->bitmap_bytes = bitmap_bytes;
    parsed->visited = (uint8_t *)xx_mem_calloc(bitmap_bytes, 1U);
    parsed->dir_seen = (uint8_t *)xx_mem_calloc(bitmap_bytes, 1U);
    parsed->fat_cache.data = (uint8_t *)xx_mem_alloc(geometry->bytes_per_sector);
    parsed->fat_cache.size = geometry->bytes_per_sector;
    parsed->fat_cache.offset = -1;
    if (!parsed->visited || !parsed->dir_seen || !parsed->fat_cache.data) {
        goto fail;
    }

    if (!xx_fat_walk_tree(self, parsed, pd)) goto fail;
    /* An empty but structurally sound volume is still a FAT volume; the BPB
     * cross-checks above are what carry the identification, so no minimum
     * entry count is imposed here. */
    return true;
fail:
    xx_fat_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------- record glue --- */

static bool xx_fat_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_fat_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_fat_populate_record(xx_archive_record *record, const xx_fat_entry *entry)
{
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = (int64_t)XX_FAT_DIR_ENTRY_SIZE;
    /* The offset of the first cluster. A fragmented file is not contiguous on
     * disk, so this locates the data but does not bound it; extraction follows
     * the chain. */
    record->data_offset = entry->data_offset;
    record->compressed_size = (int64_t)entry->size;
    return xx_archive_record_set_original_name(record, entry->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, entry->size) &&
           /* FAT stores file data verbatim; there is no codec here. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, entry->is_folder);
}

static void xx_fat_archive_stream_free(void *pointer)
{
    xx_fat_archive_stream *stream = (xx_fat_archive_stream *)pointer;
    if (!stream) return;
    xx_fat_private_release(stream->parsed);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fat_init(xx_fat *fat, xx_io_device *dev, int64_t base_address)
{
    if (!fat) return;
    xx_mem_zero(fat, sizeof(*fat));
    xx_format_init(&fat->format, dev, base_address);
    fat->format.endian = XX_ENDIAN_LITTLE;
    fat->format.file_type = XX_FAT_FILE_TYPE;
    fat->format.format_type = XX_TYPE_ARCHIVE;
    fat->format.is_archive = true;
    xx_format_set_mime_type(&fat->format, "application/octet-stream");
    xx_format_set_extension(&fat->format, "img");
    fat->format.check_is_valid = xx_fat_check_is_valid;
    fat->format.handle_base_info = xx_fat_handle_base_info;
    fat->format.get_format_size = xx_fat_get_format_size;
    fat->format.get_number_of_archive_records = xx_fat_get_number_of_archive_records;
    fat->format.create_archive_records_reading = xx_fat_create_archive_records_reading;
    fat->format.get_current_archive_record = xx_fat_get_current_archive_record;
    fat->format.unpack_current_archive_record = xx_fat_unpack_current_archive_record;
    fat->format.archive_record_move_to_next = xx_fat_archive_record_move_to_next;
    fat->format.free_archive_records_reading = xx_fat_free_archive_records_reading;
    fat->format.destroy = xx_fat_vtable_destroy;
    fat->volume_end = -1;
}

xx_fat *xx_fat_create(xx_io_device *dev, int64_t base_address)
{
    xx_fat *fat = (xx_fat *)xx_mem_alloc(sizeof(*fat));
    if (fat) xx_fat_init(fat, dev, base_address);
    return fat;
}

void xx_fat_destroy(xx_fat *fat)
{
    if (!fat) return;
    if (fat->internal) {
        /* A record stream still reading keeps its own reference. */
        xx_fat_private_release((xx_fat_private *)fat->internal);
        fat->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&fat->format);
}

static void xx_fat_vtable_destroy(Abstractformat *self)
{
    xx_fat_destroy((xx_fat *)self);
}

void xx_fat_free(xx_fat *fat)
{
    if (!fat) return;
    xx_fat_destroy(fat);
    xx_mem_free(fat);
}

/* ---------------------------------------------------------------- API --- */

bool xx_fat_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fat_private parsed;
    bool result = xx_fat_parse(self, &parsed, pd);
    xx_fat_private_cleanup(&parsed);
    return result;
}

bool xx_fat_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fat_private *parsed;
    xx_fat *fat = (xx_fat *)self;
    int64_t total_size;
    if (!self || !fat) return false;
    parsed = (xx_fat_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_fat_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    parsed->refs = 1U;
    if (fat->internal) xx_fat_private_release((xx_fat_private *)fat->internal);
    fat->internal = parsed;
    fat->number_of_records = parsed->count;
    fat->number_of_members = parsed->count;
    fat->fat_kind = parsed->geometry.kind;
    fat->bytes_per_sector = parsed->geometry.bytes_per_sector;
    fat->bytes_per_cluster = parsed->geometry.bytes_per_cluster;
    fat->cluster_count = parsed->geometry.cluster_count;
    fat->root_cluster = parsed->geometry.root_cluster;
    fat->volume_size = parsed->geometry.volume_size;
    fat->volume_end = parsed->geometry.volume_end;
    total_size = xx_io_total_size(self->device);
    /* A truncated image is measured by what is actually there. */
    self->format_size = (parsed->geometry.volume_end > total_size ? total_size : parsed->geometry.volume_end) - self->base_address;
    if (total_size > parsed->geometry.volume_end) {
        self->overlay_offset = parsed->geometry.volume_end;
        self->overlay_size = total_size - parsed->geometry.volume_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_fat_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_fat_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_fat *)self)->number_of_records;
}

xx_archive_record_state *xx_fat_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_fat_archive_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_fat_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    /* Read from the parse handle_base_info already made instead of building
     * a second copy of the whole member list. */
    if (((xx_fat *)self)->internal) {
        stream->parsed = (xx_fat_private *)((xx_fat *)self)->internal;
        ++stream->parsed->refs;
    } else {
        stream->parsed = (xx_fat_private *)xx_mem_alloc(sizeof(*stream->parsed));
        if (stream->parsed && !xx_fat_parse(self, stream->parsed, pd)) {
            xx_mem_free(stream->parsed);
            stream->parsed = NULL;
        }
        if (stream->parsed) stream->parsed->refs = 1U;
    }
    if (!stream->parsed || !xx_fat_copy_options(&state->options, options)) {
        xx_fat_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    /* Twice the data region present in the image, plus 1 MiB of slack. */
    {
        const xx_fat_geometry *geometry = &stream->parsed->geometry;
        int64_t end = geometry->volume_end < geometry->total_size ? geometry->volume_end : geometry->total_size;
        uint64_t data = end > geometry->data_offset ? (uint64_t)(end - geometry->data_offset) : 0U;
        stream->output_budget = data * 2U + UINT64_C(1048576);
    }
    state->internal_state = stream;
    state->free_internal = xx_fat_archive_stream_free;
    state->total_records = (int64_t)stream->parsed->count;
    if (stream->parsed->count != 0U && xx_fat_populate_record(&state->current_record, &stream->parsed->entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_fat_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_fat_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_fat_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fat_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_fat_populate_record(&state->current_record, &stream->parsed->entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

/* Stream one file to disk, cluster by cluster. The data is stored verbatim, so
 * this is a copy, not a decode - but the clusters are not contiguous, so it
 * cannot be a single ranged copy either. The chain is re-walked here rather
 * than cached at parse time, and the same cycle guard applies. */
static bool xx_fat_write_entry(Abstractformat *self, xx_fat_private *parsed, const xx_fat_entry *entry, const char *destination, bool overwrite, uint64_t *budget,
                               xx_pd_struct *pd)
{
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_io_device *output;
    uint8_t *buffer;
    uint64_t remaining = entry->size;
    uint32_t cluster = entry->first_cluster;
    bool ok = true;
    bool created = false;

    if (remaining != 0U && !xx_fat_cluster_is_data(geometry, cluster)) {
        return false;
    }
    /* Cross-linked entries can make every member re-read one chain; the
     * stream's output budget refuses what a sound volume could not hold. */
    if (remaining > *budget) return false;
    *budget -= remaining;
    buffer = (uint8_t *)xx_mem_alloc(geometry->bytes_per_cluster);
    if (!buffer) return false;
    output = xx_io_file_open(destination, overwrite ? "wb" : "wbx");
    created = output != NULL;
    if (!output) {
        xx_mem_free(buffer);
        return false;
    }
    /* The per-chain visited set is shared with the parser, which is finished
     * by now; take it clean and give it back clean. */
    xx_fat_visit_reset(parsed);
    parsed->chain_steps = 0U;
    if (remaining != 0U && !xx_fat_visit(parsed, cluster)) ok = false;
    while (ok && remaining != 0U) {
        int64_t offset;
        uint64_t count = remaining;
        uint64_t written = 0U;
        uint32_t next = 0U;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (count > geometry->bytes_per_cluster) {
            count = geometry->bytes_per_cluster;
        }
        if (!xx_fat_cluster_offset(geometry, cluster, &offset) || !xx_fat_read_at(self->device, offset, buffer, (size_t)count)) {
            ok = false;
            break;
        }
        while (written < count) {
            ssize_t put = xx_io_write(output, buffer + written, (size_t)(count - written));
            if (put <= 0 || (uint64_t)put > count - written) {
                ok = false;
                break;
            }
            written += (uint64_t)put;
        }
        if (!ok) break;
        remaining -= count;
        if (remaining == 0U) break;
        if (!xx_fat_chain_next(self, parsed, cluster, &next) || next == 0U) {
            /* The chain ended - or looped - before the declared size was
             * satisfied. The file is short, which means it is wrong. */
            ok = false;
            break;
        }
        cluster = next;
    }
    xx_fat_visit_reset(parsed);
    if (xx_io_close(output) != 0) ok = false;
    xx_mem_free(buffer);
    if (!ok && created) (void)xx_rt_remove(destination);
    return ok;
}

bool xx_fat_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_fat_archive_stream *stream;
    xx_fat_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    bool overwrite = false;
    if (!self || !self->device || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fat_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed->count) return false;
    entry = &stream->parsed->entries[stream->index];
    if (!xx_fat_safe_name(entry->name)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    option = xx_fat_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the record could be extracted. */
        return entry->is_folder || entry->size == 0U || xx_fat_cluster_is_data(&stream->parsed->geometry, entry->first_cluster);
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination) goto cleanup;
    if (entry->is_folder) {
        result = xx_io_create_dirs_a(destination, true);
    } else if (xx_io_create_dirs_a(destination, false)) {
        result = xx_fat_write_entry(self, stream->parsed, entry, destination, overwrite, &stream->output_budget, pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_fat_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

/* -------------------------------------------------------- accessors --- */

uint64_t xx_fat_get_number_of_records(const xx_fat *fat)
{
    return fat ? fat->number_of_records : 0U;
}
uint64_t xx_fat_get_number_of_members(const xx_fat *fat)
{
    return fat ? fat->number_of_members : 0U;
}
uint32_t xx_fat_get_kind(const xx_fat *fat)
{
    return fat ? fat->fat_kind : 0U;
}
uint32_t xx_fat_get_bytes_per_sector(const xx_fat *fat)
{
    return fat ? fat->bytes_per_sector : 0U;
}
uint32_t xx_fat_get_bytes_per_cluster(const xx_fat *fat)
{
    return fat ? fat->bytes_per_cluster : 0U;
}
uint32_t xx_fat_get_cluster_count(const xx_fat *fat)
{
    return fat ? fat->cluster_count : 0U;
}
uint32_t xx_fat_get_root_cluster(const xx_fat *fat)
{
    return fat ? fat->root_cluster : 0U;
}
uint64_t xx_fat_get_volume_size(const xx_fat *fat)
{
    return fat ? fat->volume_size : 0U;
}
int64_t xx_fat_get_volume_end(const xx_fat *fat)
{
    return fat ? fat->volume_end : -1;
}
/* The boot kind and the truncation flag live in the private state rather
 * than in struct xx_fat: code built against the original header embeds an
 * xx_fat by value (xx_format_is_fat_device), so the public layout must not
 * grow. */
uint32_t xx_fat_get_boot_kind(const xx_fat *fat)
{
    return (fat && fat->internal) ? ((const xx_fat_private *)fat->internal)->geometry.boot_kind : 0U;
}
bool xx_fat_is_truncated(const xx_fat *fat)
{
    return (fat && fat->internal) ? ((const xx_fat_private *)fat->internal)->geometry.truncated : false;
}
const char *xx_fat_get_volume_label(const xx_fat *fat)
{
    return (fat && fat->internal) ? ((const xx_fat_private *)fat->internal)->volume_label : NULL;
}

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
#define XX_FAT_CHUNK 65536U

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
    int64_t total_size;    /**< Size of the backing device. */
    int64_t volume_end;    /**< base + volume_size. */
    int64_t fat_offset;    /**< Byte offset of FAT #0. */
    int64_t fat_bytes;     /**< Byte length of one FAT. */
    int64_t root_offset;   /**< FAT12/16 fixed root directory, else -1. */
    int64_t root_bytes;    /**< FAT12/16 fixed root directory length, else 0. */
    int64_t data_offset;   /**< Byte offset of cluster 2. */
    uint64_t volume_size;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t root_entry_count;
    uint32_t fat_size_sectors;
    uint32_t total_sectors;
    uint32_t cluster_count;  /**< Data clusters; valid numbers are 2..count+1. */
    uint32_t root_cluster;   /**< FAT32 only. */
    uint32_t kind;           /**< xx_fat_kind. */
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
} xx_fat_private;

typedef struct xx_fat_archive_stream_s {
    xx_fat_private parsed;
    size_t index;
} xx_fat_archive_stream;

/* State machine for one directory's in-flight VFAT sequence. */
typedef struct xx_fat_lfn_s {
    uint16_t chars[XX_FAT_LFN_MAX_CHARS];
    uint32_t present;   /**< Bit i set when slot i+1 has been filled. */
    uint8_t expected;   /**< Ordinal the next entry on disk must carry. */
    uint8_t order;      /**< Ordinal of the last (highest) entry. */
    uint8_t checksum;   /**< Checksum the 8.3 entry must reproduce. */
    bool active;
} xx_fat_lfn;

static void xx_fat_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers --- */

static bool xx_fat_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    /* xx_io_seek64 rather than xx_io_seek: `long` is 32 bits on Win64 and a
     * FAT32 volume can run well past 2 GiB. */
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
static bool xx_fat_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_fat_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_fat_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

/* ---------------------------------------------------------- bit vector --- */

static bool xx_fat_bit_test(const uint8_t *bits, uint32_t index) {
    return (bits[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void xx_fat_bit_set(uint8_t *bits, uint32_t index) {
    bits[index >> 3U] |= (uint8_t)(1U << (index & 7U));
}

static void xx_fat_bit_clear(uint8_t *bits, uint32_t index) {
    bits[index >> 3U] &= (uint8_t)~(1U << (index & 7U));
}

/* Record a cluster in the per-chain visited set. Returns false when the
 * cluster had already been seen on this chain - the loop signal - or when the
 * touched list cannot grow, in which case the chain is abandoned rather than
 * walked with a set that can no longer be undone. */
static bool xx_fat_visit(xx_fat_private *parsed, uint32_t cluster) {
    if (!parsed->visited || cluster >= (uint32_t)(parsed->bitmap_bytes * 8U)) {
        return false;
    }
    if (xx_fat_bit_test(parsed->visited, cluster)) return false;
    if (parsed->touched_count == parsed->touched_capacity) {
        size_t capacity =
            parsed->touched_capacity ? parsed->touched_capacity * 2U : 256U;
        uint32_t *grown;
        if (capacity < parsed->touched_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) {
            return false;
        }
        grown = (uint32_t *)xx_mem_realloc(parsed->touched,
                                           capacity * sizeof(*grown));
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
static void xx_fat_visit_reset(xx_fat_private *parsed) {
    size_t index;
    if (parsed->visited) {
        for (index = 0U; index < parsed->touched_count; ++index) {
            xx_fat_bit_clear(parsed->visited, parsed->touched[index]);
        }
    }
    parsed->touched_count = 0U;
}

/* ------------------------------------------------------- FAT accessors --- */

static void xx_fat_cache_cleanup(xx_fat_fatcache *cache) {
    if (!cache) return;
    if (cache->data) xx_mem_free(cache->data);
    xx_mem_zero(cache, sizeof(*cache));
    cache->offset = -1;
}

/* Fetch one byte of FAT #0 through the sector window. Out-of-range reads fail
 * rather than wrap, so a chain that walks off the end of the FAT simply ends. */
static bool xx_fat_fat_byte(Abstractformat *self, xx_fat_private *parsed,
                            int64_t byte_offset, uint8_t *out) {
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_fat_fatcache *cache = &parsed->fat_cache;
    int64_t window;
    if (byte_offset < 0 || byte_offset >= geometry->fat_bytes || !cache->data) {
        return false;
    }
    window = geometry->fat_offset +
             (byte_offset - (byte_offset % (int64_t)cache->size));
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
static bool xx_fat_fat_entry(Abstractformat *self, xx_fat_private *parsed,
                             uint32_t cluster, uint32_t *out) {
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
        if (!xx_fat_fat_byte(self, parsed, offset + (int64_t)index,
                             &bytes[index])) {
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
        *out = ((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
                ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U)) &
               0x0FFFFFFFU;
    }
    return true;
}

/* A cluster number is usable only inside 2..cluster_count+1. Everything else -
 * 0 (free), 1 (reserved), the bad-cluster mark and every end-of-chain value -
 * terminates a chain. */
static bool xx_fat_cluster_is_data(const xx_fat_geometry *geometry,
                                   uint32_t cluster) {
    return cluster >= 2U && cluster <= geometry->cluster_count + 1U;
}

static bool xx_fat_cluster_offset(const xx_fat_geometry *geometry,
                                  uint32_t cluster, int64_t *out) {
    if (!xx_fat_cluster_is_data(geometry, cluster)) return false;
    return xx_fat_add(geometry->data_offset,
                      (uint64_t)(cluster - 2U) *
                          (uint64_t)geometry->bytes_per_cluster,
                      out) &&
           xx_fat_range_within(geometry->total_size, *out,
                               (int64_t)geometry->bytes_per_cluster);
}

/* Step to the successor of `cluster`, refusing a revisit. `*out` is set to 0
 * when the chain ends for any reason; false means the whole walk should stop
 * because a global budget is exhausted. */
static bool xx_fat_chain_next(Abstractformat *self, xx_fat_private *parsed,
                              uint32_t cluster, uint32_t *out) {
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

static size_t xx_fat_utf8_length(uint32_t code_point) {
    if (code_point < 0x80U) return 1U;
    if (code_point < 0x800U) return 2U;
    if (code_point < 0x10000U) return 3U;
    return 4U;
}

static size_t xx_fat_utf8_encode(uint32_t code_point, char *out) {
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
static char *xx_fat_utf16_to_utf8(const uint16_t *units, size_t length) {
    size_t needed = 0U;
    size_t index;
    size_t written = 0U;
    char *name;
    for (index = 0U; index < length; ++index) {
        uint32_t code_point = units[index];
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < length && units[index + 1U] >= 0xDC00U &&
            units[index + 1U] <= 0xDFFFU) {
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                         (units[index + 1U] - 0xDC00U);
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
        if (code_point >= 0xD800U && code_point <= 0xDBFFU &&
            index + 1U < length && units[index + 1U] >= 0xDC00U &&
            units[index + 1U] <= 0xDFFFU) {
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                         (units[index + 1U] - 0xDC00U);
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
static uint8_t xx_fat_short_checksum(const uint8_t *short_name) {
    uint8_t sum = 0U;
    size_t index;
    for (index = 0U; index < 11U; ++index) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0x00U) + (sum >> 1U) +
                        short_name[index]);
    }
    return sum;
}

/* Render the 8.3 name. The stored form is space padded and upper cased; the
 * two NT case flags at +12 record that the base and/or extension were really
 * lower case, and are honoured because ignoring them silently mangles names
 * written by Windows. */
static char *xx_fat_short_name(const uint8_t *entry) {
    char buffer[13];
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
        buffer[used++] = (char)ch;
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
            buffer[used++] = (char)ch;
        }
    }
    name = (char *)xx_mem_alloc(used + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, buffer, used);
    name[used] = '\0';
    return name;
}

static char *xx_fat_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_FAT_MAX_PATH ||
        name_size > XX_FAT_MAX_PATH - prefix_size - 1U) {
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

static bool xx_fat_is_dot_name(const char *name) {
    if (!name || name[0] != '.') return false;
    return name[1] == '\0' || (name[1] == '.' && name[2] == '\0');
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though a FAT long name may legally carry some of it. */
static bool xx_fat_safe_name(const char *name) {
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
                component[length - 1U] == ' ' || component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* ------------------------------------------------ long-name assembly --- */

static void xx_fat_lfn_reset(xx_fat_lfn *lfn) {
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
static void xx_fat_lfn_push(xx_fat_lfn *lfn, const uint8_t *entry) {
    uint8_t raw = entry[0];
    uint8_t order = (uint8_t)(raw & XX_FAT_ATTR_LONG_MASK);
    uint8_t checksum = entry[13];
    size_t base;
    size_t index;
    static const size_t offsets[XX_FAT_LFN_CHARS_PER_ENTRY] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};

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
    } else if (!lfn->active || order != lfn->expected ||
               checksum != lfn->checksum) {
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
        lfn->chars[base + index] =
            (uint16_t)((uint16_t)entry[at] | ((uint16_t)entry[at + 1U] << 8U));
    }
    lfn->present |= (uint32_t)1U << (order - 1U);
    lfn->expected = (uint8_t)(order - 1U);
}

/* Turn a completed sequence into a name, or return NULL when it does not
 * belong to this 8.3 entry. The sequence must be whole (every ordinal from 1
 * to `order` present), must have counted down to zero, and its checksum must
 * match the eleven short-name bytes. */
static char *xx_fat_lfn_finish(const xx_fat_lfn *lfn, const uint8_t *entry) {
    uint32_t wanted;
    size_t length = 0U;
    if (!lfn->active || lfn->expected != 0U || lfn->order == 0U) return NULL;
    wanted = (lfn->order >= 32U) ? 0xFFFFFFFFU
                                 : (((uint32_t)1U << lfn->order) - 1U);
    if (lfn->present != wanted) return NULL;
    if (lfn->checksum != xx_fat_short_checksum(entry)) return NULL;
    while (length < (size_t)lfn->order * XX_FAT_LFN_CHARS_PER_ENTRY &&
           lfn->chars[length] != 0x0000U && lfn->chars[length] != 0xFFFFU) {
        ++length;
    }
    if (length == 0U) return NULL;
    return xx_fat_utf16_to_utf8(lfn->chars, length);
}

/* ------------------------------------------------------------- parsing --- */

static void xx_fat_private_cleanup(xx_fat_private *parsed) {
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
    xx_fat_cache_cleanup(&parsed->fat_cache);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->geometry.total_size = -1;
    parsed->geometry.volume_end = -1;
    parsed->geometry.root_offset = -1;
    parsed->fat_cache.offset = -1;
}

static bool xx_fat_append_entry(xx_fat_private *parsed, xx_fat_entry *entry) {
    xx_fat_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_FAT_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_fat_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Decode and validate the BIOS Parameter Block. Nothing downstream re-checks
 * geometry, so every field a later computation depends on is pinned here. */
static bool xx_fat_read_geometry(Abstractformat *self, xx_fat_geometry *geometry,
                                 uint8_t *boot) {
    uint32_t root_dir_sectors;
    uint32_t fat_size;
    uint32_t total_sectors;
    uint32_t meta_sectors;
    uint32_t data_sectors;
    uint64_t volume_size;

    geometry->base = self->base_address;
    geometry->total_size = xx_io_total_size(self->device);
    geometry->volume_end = -1;
    geometry->root_offset = -1;
    if (!xx_fat_range_within(geometry->total_size, geometry->base,
                             XX_FAT_BOOT_SIZE) ||
        !xx_fat_read_at(self->device, geometry->base, boot, XX_FAT_BOOT_SIZE)) {
        return false;
    }
    /* The boot signature. It is at +510, past any magic prefilter, which is
     * why FAT identification needs a device probe and not a byte pattern. */
    if (boot[510] != 0x55U || boot[511] != 0xAAU) return false;
    /* A FAT boot sector opens with a jump over the BPB. Accepting anything
     * else would let an arbitrary 512-byte block with 0x55AA through. */
    if (!((boot[0] == 0xEBU && boot[2] == 0x90U) || boot[0] == 0xE9U)) {
        return false;
    }

    geometry->bytes_per_sector =
        xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 11U, false);
    geometry->sectors_per_cluster = boot[13];
    geometry->reserved_sectors =
        xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 14U, false);
    geometry->num_fats = boot[16];
    geometry->root_entry_count =
        xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 17U, false);
    geometry->media = boot[21];

    if (geometry->bytes_per_sector != 512U &&
        geometry->bytes_per_sector != 1024U &&
        geometry->bytes_per_sector != 2048U &&
        geometry->bytes_per_sector != 4096U) {
        return false;
    }
    if (!xx_fat_is_power_of_two(geometry->sectors_per_cluster) ||
        geometry->sectors_per_cluster > 128U) {
        return false;
    }
    /* Reserved sectors is never zero on FAT - the boot sector itself lives
     * there. NTFS stores 0 in this field, so this single test is what keeps an
     * NTFS boot sector out of this reader. */
    if (geometry->reserved_sectors == 0U) return false;
    if (geometry->num_fats != 1U && geometry->num_fats != 2U) return false;
    if (geometry->media != 0xF0U && geometry->media < 0xF8U) return false;

    geometry->bytes_per_cluster =
        geometry->bytes_per_sector * geometry->sectors_per_cluster;
    if (geometry->bytes_per_cluster > XX_FAT_MAX_CLUSTER_SIZE) return false;

    /* The fixed root directory must be a whole number of sectors' worth of
     * 32-byte entries, per the specification. */
    if (geometry->root_entry_count != 0U &&
        ((geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE) %
         geometry->bytes_per_sector) != 0U) {
        return false;
    }
    root_dir_sectors = (geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE +
                        geometry->bytes_per_sector - 1U) /
                       geometry->bytes_per_sector;

    fat_size = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 22U, false);
    if (fat_size == 0U) {
        fat_size = xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 36U, false);
    }
    total_sectors = xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 19U, false);
    if (total_sectors == 0U) {
        total_sectors = xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 32U, false);
    }
    if (fat_size == 0U || total_sectors == 0U) return false;
    geometry->fat_size_sectors = fat_size;
    geometry->total_sectors = total_sectors;

    /* Reserved + FATs + fixed root must fit inside the volume with at least
     * one data sector left over. All of this is done in 64 bits because the
     * operands are attacker controlled 32-bit values. */
    {
        uint64_t meta = (uint64_t)geometry->reserved_sectors +
                        (uint64_t)geometry->num_fats * (uint64_t)fat_size +
                        (uint64_t)root_dir_sectors;
        if (meta >= (uint64_t)total_sectors || meta > UINT32_MAX) return false;
        meta_sectors = (uint32_t)meta;
    }
    data_sectors = total_sectors - meta_sectors;
    geometry->cluster_count = data_sectors / geometry->sectors_per_cluster;
    if (geometry->cluster_count == 0U ||
        geometry->cluster_count > XX_FAT_MAX_CLUSTERS) {
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
        if (geometry->root_entry_count != 0U ||
            xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 22U, false) != 0U) {
            return false;
        }
        geometry->root_cluster =
            xx_data_get_u32(boot, XX_FAT_BOOT_SIZE, 44U, false);
        if (!xx_fat_cluster_is_data(geometry, geometry->root_cluster)) {
            return false;
        }
    } else {
        if (geometry->root_entry_count == 0U ||
            xx_data_get_u16(boot, XX_FAT_BOOT_SIZE, 22U, false) == 0U) {
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
    if (!xx_fat_add(geometry->base, volume_size, &geometry->volume_end) ||
        geometry->volume_end > geometry->total_size) {
        return false;
    }

    if (!xx_fat_add(geometry->base,
                    (uint64_t)geometry->reserved_sectors *
                        geometry->bytes_per_sector,
                    &geometry->fat_offset)) {
        return false;
    }
    geometry->fat_bytes = (int64_t)((uint64_t)fat_size *
                                    geometry->bytes_per_sector);
    if (!xx_fat_range_within(geometry->total_size, geometry->fat_offset,
                             geometry->fat_bytes)) {
        return false;
    }
    if (!xx_fat_add(geometry->fat_offset,
                    (uint64_t)geometry->num_fats * (uint64_t)fat_size *
                        geometry->bytes_per_sector,
                    &geometry->root_offset)) {
        return false;
    }
    geometry->root_bytes =
        (int64_t)((uint64_t)geometry->root_entry_count * XX_FAT_DIR_ENTRY_SIZE);
    if (!xx_fat_add(geometry->root_offset, (uint64_t)geometry->root_bytes,
                    &geometry->data_offset)) {
        return false;
    }
    if (geometry->kind == XX_FAT_KIND_FAT32) {
        geometry->root_offset = -1;
        geometry->root_bytes = 0;
    } else if (!xx_fat_range_within(geometry->total_size, geometry->root_offset,
                                    geometry->root_bytes)) {
        return false;
    }
    /* Cluster 2 must exist; the last cluster is bounds-checked per access. */
    return xx_fat_range_within(geometry->total_size, geometry->data_offset,
                               (int64_t)geometry->bytes_per_cluster);
}

/* Pull the start cluster out of a directory entry. The high half at +20 is
 * meaningful on FAT32 only; on FAT12/16 it is a different field entirely and
 * must be masked away. */
static uint32_t xx_fat_entry_cluster(const xx_fat_geometry *geometry,
                                     const uint8_t *entry) {
    uint32_t low = (uint32_t)entry[26] | ((uint32_t)entry[27] << 8U);
    uint32_t high = (uint32_t)entry[20] | ((uint32_t)entry[21] << 8U);
    if (geometry->kind != XX_FAT_KIND_FAT32) return low;
    return low | (high << 16U);
}

/* Record a root volume-label entry. The label lives in the eleven raw name
 * bytes and is not an 8.3 name: no dot is inserted and the case flags do not
 * apply to it. */
static void xx_fat_take_label(xx_fat_private *parsed, const uint8_t *entry) {
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
static bool xx_fat_enqueue(xx_fat_private *parsed, uint32_t cluster,
                           const char *prefix, unsigned depth) {
    if (parsed->queue_count == parsed->queue_capacity) {
        size_t capacity =
            parsed->queue_capacity ? parsed->queue_capacity * 2U : 32U;
        xx_fat_pending *grown;
        if (capacity < parsed->queue_capacity ||
            capacity > SIZE_MAX / sizeof(*grown)) {
            return false;
        }
        grown = (xx_fat_pending *)xx_mem_realloc(parsed->queue,
                                                 capacity * sizeof(*grown));
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
static bool xx_fat_handle_entry(Abstractformat *self, xx_fat_private *parsed,
                                const uint8_t *raw, int64_t entry_offset,
                                const char *prefix, unsigned depth,
                                xx_fat_lfn *lfn, bool is_root,
                                xx_pd_struct *pd) {
    const xx_fat_geometry *geometry = &parsed->geometry;
    uint8_t attributes = raw[11];
    uint32_t cluster;
    uint32_t size;
    char *name = NULL;
    char *full_name = NULL;
    xx_fat_entry entry;
    bool folder;

    if (raw[0] == XX_FAT_ENTRY_END) return false; /* Nothing follows. */
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
        if (is_root) xx_fat_take_label(parsed, raw);
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
    if (!folder && (uint64_t)size > (uint64_t)geometry->cluster_count *
                                        geometry->bytes_per_cluster) {
        xx_str_free(name);
        return true;
    }
    full_name = xx_fat_join_name(prefix, name);
    xx_str_free(name);
    if (!full_name) return true;

    xx_mem_zero(&entry, sizeof(entry));
    entry.name = full_name;
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
    /* The appended record owns full_name; the queue only borrows it, and
     * xx_fat_private_cleanup() is what releases it. */
    if (folder && xx_fat_cluster_is_data(geometry, cluster) &&
        depth + 1U <= XX_FAT_MAX_DEPTH) {
        (void)xx_fat_enqueue(parsed, cluster, full_name, depth + 1U);
    }
    (void)self;
    (void)pd;
    return true;
}

/* The FAT12/16 root directory: one contiguous run of entries between the FATs
 * and cluster 2. It has no cluster chain and cannot grow, which is the one
 * structural difference from every other directory on the volume. */
static bool xx_fat_scan_fixed_root(Abstractformat *self, xx_fat_private *parsed,
                                   xx_pd_struct *pd) {
    const xx_fat_geometry *geometry = &parsed->geometry;
    int64_t offset = geometry->root_offset;
    int64_t remaining = geometry->root_bytes;
    uint8_t raw[XX_FAT_DIR_ENTRY_SIZE];
    xx_fat_lfn lfn;
    xx_fat_lfn_reset(&lfn);
    if (pd && xx_pd_is_stopped(pd)) return false;
    while (remaining >= (int64_t)XX_FAT_DIR_ENTRY_SIZE) {
        if (parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES) break;
        ++parsed->dir_entries;
        if (!xx_fat_read_at(self->device, offset, raw, sizeof(raw))) break;
        if (!xx_fat_handle_entry(self, parsed, raw, offset, "", 0U, &lfn, true,
                                 pd)) {
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
static bool xx_fat_scan_clustered(Abstractformat *self, xx_fat_private *parsed,
                                  uint32_t start_cluster, const char *prefix,
                                  unsigned depth, bool is_root,
                                  xx_pd_struct *pd) {
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_fat_lfn lfn;
    uint8_t *buffer;
    uint32_t cluster = start_cluster;
    bool ok = true;

    if (!xx_fat_cluster_is_data(geometry, start_cluster)) return true;
    /* Each start cluster is admitted once for the whole parse, so a directory
     * that contains itself - or one aliased into two parents - terminates
     * instead of expanding the tree without bound. */
    if (!parsed->dir_seen ||
        start_cluster >= (uint32_t)(parsed->bitmap_bytes * 8U)) {
        return true;
    }
    if (xx_fat_bit_test(parsed->dir_seen, start_cluster)) return true;
    xx_fat_bit_set(parsed->dir_seen, start_cluster);

    buffer = (uint8_t *)xx_mem_alloc(geometry->bytes_per_cluster);
    if (!buffer) return false;
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
        if (!xx_fat_cluster_offset(geometry, cluster, &offset) ||
            !xx_fat_read_at(self->device, offset, buffer,
                            geometry->bytes_per_cluster)) {
            break;
        }
        for (position = 0U;
             position + XX_FAT_DIR_ENTRY_SIZE <= geometry->bytes_per_cluster;
             position += XX_FAT_DIR_ENTRY_SIZE) {
            if (parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES) {
                cluster = 0U;
                break;
            }
            ++parsed->dir_entries;
            if (!xx_fat_handle_entry(self, parsed, buffer + position,
                                     offset + (int64_t)position, prefix, depth,
                                     &lfn, is_root, pd)) {
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
static bool xx_fat_walk_tree(Abstractformat *self, xx_fat_private *parsed,
                             xx_pd_struct *pd) {
    if (parsed->geometry.kind == XX_FAT_KIND_FAT32) {
        /* The FAT32 root is an ordinary cluster chain starting at the cluster
         * named in the BPB. There is no fixed root region at all. */
        if (!xx_fat_scan_clustered(self, parsed, parsed->geometry.root_cluster,
                                   "", 0U, true, pd)) {
            return false;
        }
    } else if (!xx_fat_scan_fixed_root(self, parsed, pd)) {
        return false;
    }
    while (parsed->queue_head < parsed->queue_count) {
        xx_fat_pending item = parsed->queue[parsed->queue_head++];
        if (parsed->count >= XX_FAT_MAX_ENTRIES ||
            parsed->dir_entries >= XX_FAT_MAX_DIR_ENTRIES) {
            break;
        }
        if (!xx_fat_scan_clustered(self, parsed, item.cluster, item.prefix,
                                   item.depth, false, pd)) {
            return false;
        }
    }
    return true;
}

static bool xx_fat_parse(Abstractformat *self, xx_fat_private *parsed,
                         xx_pd_struct *pd) {
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
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    geometry = &parsed->geometry;
    if (!xx_fat_read_geometry(self, geometry, boot)) goto fail;

    bitmap_bytes = ((size_t)geometry->cluster_count + 2U + 7U) / 8U;
    parsed->bitmap_bytes = bitmap_bytes;
    parsed->visited = (uint8_t *)xx_mem_calloc(bitmap_bytes, 1U);
    parsed->dir_seen = (uint8_t *)xx_mem_calloc(bitmap_bytes, 1U);
    parsed->fat_cache.data =
        (uint8_t *)xx_mem_alloc(geometry->bytes_per_sector);
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

static bool xx_fat_copy_options(xx_list_s *destination,
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

static const xx_var *xx_fat_find_option(const xx_list_s *options,
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

static bool xx_fat_populate_record(xx_archive_record *record,
                                   const xx_fat_entry *entry) {
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
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->size) &&
           /* FAT stores file data verbatim; there is no codec here. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_fat_archive_stream_free(void *pointer) {
    xx_fat_archive_stream *stream = (xx_fat_archive_stream *)pointer;
    if (!stream) return;
    xx_fat_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fat_init(xx_fat *fat, xx_io_device *dev, int64_t base_address) {
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
    fat->format.get_number_of_archive_records =
        xx_fat_get_number_of_archive_records;
    fat->format.create_archive_records_reading =
        xx_fat_create_archive_records_reading;
    fat->format.get_current_archive_record = xx_fat_get_current_archive_record;
    fat->format.unpack_current_archive_record =
        xx_fat_unpack_current_archive_record;
    fat->format.archive_record_move_to_next = xx_fat_archive_record_move_to_next;
    fat->format.free_archive_records_reading =
        xx_fat_free_archive_records_reading;
    fat->format.destroy = xx_fat_vtable_destroy;
    fat->volume_end = -1;
}

xx_fat *xx_fat_create(xx_io_device *dev, int64_t base_address) {
    xx_fat *fat = (xx_fat *)xx_mem_alloc(sizeof(*fat));
    if (fat) xx_fat_init(fat, dev, base_address);
    return fat;
}

void xx_fat_destroy(xx_fat *fat) {
    if (!fat) return;
    if (fat->internal) {
        xx_fat_private_cleanup((xx_fat_private *)fat->internal);
        xx_mem_free(fat->internal);
        fat->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&fat->format);
}

static void xx_fat_vtable_destroy(Abstractformat *self) {
    xx_fat_destroy((xx_fat *)self);
}

void xx_fat_free(xx_fat *fat) {
    if (!fat) return;
    xx_fat_destroy(fat);
    xx_mem_free(fat);
}

/* ---------------------------------------------------------------- API --- */

bool xx_fat_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fat_private parsed;
    bool result = xx_fat_parse(self, &parsed, pd);
    xx_fat_private_cleanup(&parsed);
    return result;
}

bool xx_fat_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
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
    if (fat->internal) {
        xx_fat_private_cleanup((xx_fat_private *)fat->internal);
        xx_mem_free(fat->internal);
    }
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
    self->format_size = parsed->geometry.volume_end - self->base_address;
    total_size = xx_io_total_size(self->device);
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

int64_t xx_fat_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_fat_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_fat *)self)->number_of_records;
}

xx_archive_record_state *xx_fat_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_fat_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
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
    if (!xx_fat_copy_options(&state->options, options) ||
        !xx_fat_parse(self, &stream->parsed, pd)) {
        xx_fat_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_fat_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_fat_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_fat_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fat_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_fat_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fat_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_fat_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
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
static bool xx_fat_write_entry(Abstractformat *self, xx_fat_private *parsed,
                               const xx_fat_entry *entry,
                               const char *destination, xx_pd_struct *pd) {
    const xx_fat_geometry *geometry = &parsed->geometry;
    xx_io_device *output;
    uint8_t *buffer;
    uint64_t remaining = entry->size;
    uint32_t cluster = entry->first_cluster;
    bool ok = true;

    if (remaining != 0U && !xx_fat_cluster_is_data(geometry, cluster)) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(geometry->bytes_per_cluster);
    if (!buffer) return false;
    output = xx_io_file_open(destination, "wb");
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
        if (!xx_fat_cluster_offset(geometry, cluster, &offset) ||
            !xx_fat_read_at(self->device, offset, buffer, (size_t)count)) {
            ok = false;
            break;
        }
        while (written < count) {
            ssize_t put = xx_io_write(output, buffer + written,
                                      (size_t)(count - written));
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
    xx_io_close(output);
    xx_mem_free(buffer);
    if (!ok) (void)xx_rt_remove(destination);
    return ok;
}

bool xx_fat_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_fat_archive_stream *stream;
    xx_fat_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fat_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_fat_safe_name(entry->name)) return false;
    option = xx_fat_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the record could be extracted. */
        return entry->is_folder || entry->size == 0U ||
               xx_fat_cluster_is_data(&stream->parsed.geometry,
                                      entry->first_cluster);
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
        destination = xx_str_concat3(base, "/", entry->name);
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination) goto cleanup;
    if (entry->is_folder) {
        result = xx_io_create_dirs_a(destination, true);
    } else if (xx_io_create_dirs_a(destination, false)) {
        result = xx_fat_write_entry(self, &stream->parsed, entry, destination,
                                    pd);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_fat_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* -------------------------------------------------------- accessors --- */

uint64_t xx_fat_get_number_of_records(const xx_fat *fat) {
    return fat ? fat->number_of_records : 0U;
}
uint64_t xx_fat_get_number_of_members(const xx_fat *fat) {
    return fat ? fat->number_of_members : 0U;
}
uint32_t xx_fat_get_kind(const xx_fat *fat) { return fat ? fat->fat_kind : 0U; }
uint32_t xx_fat_get_bytes_per_sector(const xx_fat *fat) {
    return fat ? fat->bytes_per_sector : 0U;
}
uint32_t xx_fat_get_bytes_per_cluster(const xx_fat *fat) {
    return fat ? fat->bytes_per_cluster : 0U;
}
uint32_t xx_fat_get_cluster_count(const xx_fat *fat) {
    return fat ? fat->cluster_count : 0U;
}
uint32_t xx_fat_get_root_cluster(const xx_fat *fat) {
    return fat ? fat->root_cluster : 0U;
}
uint64_t xx_fat_get_volume_size(const xx_fat *fat) {
    return fat ? fat->volume_size : 0U;
}
int64_t xx_fat_get_volume_end(const xx_fat *fat) {
    return fat ? fat->volume_end : -1;
}
const char *xx_fat_get_volume_label(const xx_fat *fat) {
    return (fat && fat->internal)
               ? ((const xx_fat_private *)fat->internal)->volume_label
               : NULL;
}

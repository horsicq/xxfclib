/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Amiga ADF / AmigaDOS OFS and FFS volumes (DOS\0 .. DOS\7).  xx_adf.h
 * carries the block layouts.
 *
 * Written from the AmigaDOS on-disk structure.  The volume is located by
 * the boot block's "DOS" tag (or, on an exact DD/HD floppy, all-zero boot
 * blocks) and a root block that must check out in full (type, key, hash
 * table size, secondary type and checksum).  The directory
 * tree is walked from the root's hash table through every hash chain; each
 * header block is claimed once, so cross-linked or looping chains cannot
 * repeat or spin.  File data is followed through the header's block table
 * and its extension chain, never through the OFS "next" links, and every
 * pointer is range-checked against the volume before it is read.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/adf/xx_adf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: picks up the real file type as soon as ADF is
 * registered in xxfc_defs.h. */
#ifdef ADF
#define XX_ADF_FILE_TYPE XX_FILE_TYPE_ADF
#else
#define XX_ADF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ADF_BSIZE 512U
#define ADF_LONGS (ADF_BSIZE / 4U)
#define ADF_HT_SIZE (ADF_LONGS - 56U) /* 72 */

/* Block types and secondary types. */
#define ADF_T_HEADER 2U
#define ADF_T_DATA 8U
#define ADF_T_LIST 16U
#define ADF_T_COMMENT 64U
#define ADF_ST_ROOT 1U
#define ADF_ST_USERDIR 2U
#define ADF_ST_SOFTLINK 3U
#define ADF_ST_LINKDIR 4U
#define ADF_ST_FILE 0xFFFFFFFDU     /* -3 */
#define ADF_ST_LINKFILE 0xFFFFFFFCU /* -4 */

/* Header block offsets. */
#define ADF_OFF_TYPE 0x000U
#define ADF_OFF_KEY 0x004U
#define ADF_OFF_HIGH_SEQ 0x008U
#define ADF_OFF_HT_SIZE 0x00CU
#define ADF_OFF_TABLE 0x018U
#define ADF_OFF_TABLE_LAST 0x134U /* first data block of a file */
#define ADF_OFF_PROTECT 0x140U
#define ADF_OFF_BYTE_SIZE 0x144U
#define ADF_OFF_COMMENT 0x148U
#define ADF_OFF_DATE 0x1A4U
#define ADF_OFF_NAME 0x1B0U
#define ADF_OFF_REAL_ENTRY 0x1D4U
#define ADF_OFF_HASH_CHAIN 0x1F0U
#define ADF_OFF_PARENT 0x1F4U
#define ADF_OFF_EXTENSION 0x1F8U
#define ADF_OFF_SEC_TYPE 0x1FCU
/* Long-name variants: name and comment share one field at 0x148. */
#define ADF_LN_FIELD 112U
#define ADF_LN_COMMENT_BLOCK 0x1B8U
#define ADF_LN_DATE 0x1C4U
#define ADF_LN_NAME_MAX 107U
#define ADF_NAME_MAX 30U
#define ADF_COMMENT_MAX 79U
/* Soft link target: a C string over what would be the hash table. */
#define ADF_LINK_PATH_MAX (ADF_OFF_TABLE_LAST + 4U - ADF_OFF_TABLE)

#define ADF_OFS_HEADER 24U
#define ADF_OFS_PAYLOAD (ADF_BSIZE - ADF_OFS_HEADER) /* 488 */

/* Standard floppy geometry: 22 blocks per cylinder (DD) or 44 (HD), 80
 * cylinders, and the 81..83-cylinder images that some tools write. */
#define ADF_DD_BLOCKS 1760U
#define ADF_HD_BLOCKS 3520U
#define ADF_DD_ROOT 880U
#define ADF_HD_ROOT 1760U
/* The two boot blocks; a standard floppy whose boot blocks are all zero
 * (wiped, or never written by the tool that made the image) still carries a
 * complete filesystem. */
#define ADF_BOOT_BYTES (2U * ADF_BSIZE)

/* Limits.  A hardfile larger than this is only examined for an embedded
 * standard floppy; the visited-block map stays at or below 4 MiB. */
#define ADF_MAX_BLOCKS UINT32_C(0x2000000)
#define ADF_MAX_DEPTH 64U
#define ADF_MAX_MEMBERS 200000U
#define ADF_MAX_COLLISIONS 1024U
/* Suffixed candidates tried over a whole volume; past it a name that
 * collides is dropped instead of probed, so crafted runs of equal names cost
 * a bounded amount of work. */
#define ADF_MAX_EXTRA_PROBES UINT32_C(0x100000)
#define ADF_COPY_BUFFER 0x8000U

/* 1978-01-01 in Unix seconds. */
#define ADF_EPOCH INT64_C(252460800)

typedef enum adf_kind_e {
    ADF_KIND_FILE = 0,
    ADF_KIND_FOLDER = 1,
    ADF_KIND_SOFTLINK = 2
} adf_kind;

typedef struct adf_volume_s {
    xx_io_device *device;
    int64_t base;
    uint32_t blocks;      /**< Filesystem span; every pointer is below it. */
    uint32_t root;
    uint8_t dos_type;
    bool ffs;
    bool long_names;
    bool blank_boot;   /**< Boot blocks all zero; OFS/FFS still to decide. */
    bool type_known;   /**< ffs is settled (always, unless blank_boot). */
    int64_t format_size;
    char volume_name[96];
} adf_volume;

typedef struct adf_member_s {
    char *name;         /**< Unique, host-safe UTF-8 path. */
    char *comment;      /**< UTF-8 or NULL. */
    char *link_target;  /**< Soft links only. */
    uint32_t header;    /**< The entry's own header block. */
    uint32_t data;      /**< File header that supplies the bytes. */
    uint32_t first;     /**< First data block of a file, 0 if none. */
    uint32_t protect;
    uint64_t size;
    int64_t timestamp;  /**< Unix seconds, or -1. */
    adf_kind kind;
} adf_member;

typedef struct adf_names_s {
    const char **slots;
    uint32_t *hashes;
    size_t capacity; /**< Power of two, or 0. */
    size_t used;
    uint32_t extra_probes; /**< Suffixed candidates tried so far. */
} adf_names;

typedef struct adf_stream_s {
    adf_volume volume;
    adf_member *items;
    size_t count;
    size_t capacity;
    size_t index;
} adf_stream;

typedef struct adf_frame_s {
    uint32_t block;       /**< Directory (or root) header block. */
    const char *path;     /**< Its unique path; "" for the root. */
    uint32_t table[ADF_HT_SIZE];
    uint32_t slot;        /**< Next hash slot to start. */
    uint32_t next;        /**< Next entry on the current chain, 0 if none. */
} adf_frame;

/* ---------------------------------------------------------------------- */
/* Block access                                                            */

static uint32_t adf_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool adf_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool adf_read_block(const adf_volume *volume, uint32_t block,
                           uint8_t *buffer) {
    if (!volume || block >= volume->blocks) return false;
    return adf_read_at(volume->device,
                       volume->base + (int64_t)block * (int64_t)ADF_BSIZE,
                       buffer, ADF_BSIZE);
}

/* Header, list, data and comment blocks all sum to zero over 128 longs. */
static bool adf_checksum_ok(const uint8_t *block) {
    uint32_t sum = 0U;
    size_t index;
    for (index = 0U; index < ADF_BSIZE; index += 4U)
        sum += adf_be32(block + index);
    return sum == 0U;
}

/* A block number that may hold metadata or data: past the boot blocks and
 * inside the filesystem. */
static bool adf_pointer_ok(const adf_volume *volume, uint32_t block) {
    return block >= 2U && block < volume->blocks;
}

/* ---------------------------------------------------------------------- */
/* Volume                                                                  */

static bool adf_root_ok(const uint8_t *block) {
    return adf_be32(block + ADF_OFF_TYPE) == ADF_T_HEADER &&
           adf_be32(block + ADF_OFF_KEY) == 0U &&
           adf_be32(block + ADF_OFF_HIGH_SEQ) == 0U &&
           adf_be32(block + ADF_OFF_HT_SIZE) == ADF_HT_SIZE &&
           adf_be32(block + ADF_OFF_SEC_TYPE) == ADF_ST_ROOT &&
           block[ADF_OFF_NAME] <= ADF_NAME_MAX && adf_checksum_ok(block);
}

static bool adf_is_variant(uint32_t blocks, uint32_t standard) {
    uint32_t per_cylinder = standard / 80U, cylinders;
    for (cylinders = 80U; cylinders <= 83U; ++cylinders)
        if (blocks == per_cylinder * cylinders) return true;
    return false;
}

/* ISO-8859-1 to UTF-8, with C0/C1 controls replaced.  @p capacity counts
 * the terminator; a name that does not fit is cut at a character. */
static void adf_latin1_to_utf8(const uint8_t *raw, size_t size, char *out,
                               size_t capacity) {
    size_t at = 0U, index;
    if (!out || capacity == 0U) return;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c == 0x7FU || (c >= 0x80U && c < 0xA0U)) c = '_';
        if (c < 0x80U) {
            if (at + 1U >= capacity) break;
            out[at++] = (char)c;
        } else {
            if (at + 2U >= capacity) break;
            out[at++] = (char)(0xC0U | (c >> 6U));
            out[at++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[at] = 0;
}

/* True when both boot blocks are zero bytes. */
static bool adf_boot_blank(xx_io_device *device, int64_t base) {
    uint8_t boot[ADF_BOOT_BYTES];
    size_t index;
    if (!adf_read_at(device, base, boot, sizeof(boot))) return false;
    for (index = 0U; index < sizeof(boot); ++index)
        if (boot[index] != 0U) return false;
    return true;
}

/* Find the root block.  Candidates, in order: the middle of the whole image
 * (floppies, 81..83-cylinder images and hardfiles), then a standard DD and
 * a standard HD floppy at the start of a longer or shorter image.  A volume
 * with blank boot blocks is accepted only as an exact standard floppy. */
static bool adf_open_volume(Abstractformat *format, adf_volume *out,
                            uint8_t *root_block) {
    uint8_t boot[4];
    uint8_t block[ADF_BSIZE];
    int64_t total, size, whole;
    uint32_t candidates[3], spans[3];
    size_t count = 0U, index;
    adf_volume volume;
    bool blank = false;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(4U * ADF_BSIZE) ||
        !adf_read_at(format->device, format->base_address, boot,
                     sizeof(boot)))
        return false;
    if (boot[0] != 'D' || boot[1] != 'O' || boot[2] != 'S' || boot[3] > 7U) {
        if (boot[0] != 0U || boot[1] != 0U || boot[2] != 0U || boot[3] != 0U ||
            (size != (int64_t)ADF_DD_BLOCKS * ADF_BSIZE &&
             size != (int64_t)ADF_HD_BLOCKS * ADF_BSIZE) ||
            !adf_boot_blank(format->device, format->base_address))
            return false;
        blank = true;
    }
    whole = size / (int64_t)ADF_BSIZE;
    if (whole <= (int64_t)ADF_MAX_BLOCKS) {
        candidates[count] = (uint32_t)((whole + 1) / 2);
        spans[count++] = (uint32_t)whole;
    }
    if (whole > (int64_t)ADF_DD_ROOT) {
        candidates[count] = ADF_DD_ROOT;
        spans[count++] =
            whole <= (int64_t)ADF_MAX_BLOCKS &&
                    adf_is_variant((uint32_t)whole, ADF_DD_BLOCKS)
                ? (uint32_t)whole
                : (whole < (int64_t)ADF_DD_BLOCKS ? (uint32_t)whole
                                                   : ADF_DD_BLOCKS);
    }
    if (whole > (int64_t)ADF_HD_ROOT) {
        candidates[count] = ADF_HD_ROOT;
        spans[count++] =
            whole <= (int64_t)ADF_MAX_BLOCKS &&
                    adf_is_variant((uint32_t)whole, ADF_HD_BLOCKS)
                ? (uint32_t)whole
                : (whole < (int64_t)ADF_HD_BLOCKS ? (uint32_t)whole
                                                   : ADF_HD_BLOCKS);
    }
    for (index = 0U; index < count; ++index) {
        size_t earlier;
        bool repeated = false;
        for (earlier = 0U; earlier < index; ++earlier)
            if (candidates[earlier] == candidates[index]) repeated = true;
        if (repeated || candidates[index] < 2U ||
            candidates[index] >= spans[index])
            continue;
        if (!adf_read_at(format->device,
                         format->base_address +
                             (int64_t)candidates[index] * (int64_t)ADF_BSIZE,
                         block, sizeof(block)) ||
            !adf_root_ok(block))
            continue;
        xx_mem_zero(&volume, sizeof(volume));
        volume.device = format->device;
        volume.base = format->base_address;
        volume.blocks = spans[index];
        volume.root = candidates[index];
        volume.dos_type = blank ? 0U : boot[3];
        volume.ffs = !blank && (boot[3] & 1U) != 0U;
        volume.long_names = !blank && boot[3] >= 6U;
        volume.blank_boot = blank;
        volume.type_known = !blank;
        volume.format_size = (int64_t)spans[index] * (int64_t)ADF_BSIZE;
        adf_latin1_to_utf8(block + ADF_OFF_NAME + 1U, block[ADF_OFF_NAME],
                           volume.volume_name, sizeof(volume.volume_name));
        *out = volume;
        if (root_block) xx_rt_memcpy(root_block, block, ADF_BSIZE);
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static unsigned adf_fold(unsigned codepoint) {
    if ((codepoint >= 'a' && codepoint <= 'z') ||
        (codepoint >= 0xE0U && codepoint <= 0xFEU && codepoint != 0xF7U))
        return codepoint - 0x20U;
    return codepoint;
}

/* Next code point of a UTF-8 string this reader produced (one or two bytes;
 * anything else is taken bytewise). */
static unsigned adf_next_codepoint(const char **cursor) {
    const uint8_t *at = (const uint8_t *)*cursor;
    if ((at[0] & 0xE0U) == 0xC0U && (at[1] & 0xC0U) == 0x80U) {
        *cursor += 2;
        return ((unsigned)(at[0] & 0x1FU) << 6U) | (unsigned)(at[1] & 0x3FU);
    }
    *cursor += 1;
    return at[0];
}

static uint32_t adf_name_hash(const char *name) {
    uint32_t hash = 2166136261U;
    while (*name) {
        hash ^= adf_fold(adf_next_codepoint(&name));
        hash *= 16777619U;
    }
    return hash;
}

static bool adf_name_equal(const char *first, const char *second) {
    while (*first && *second) {
        if (adf_fold(adf_next_codepoint(&first)) !=
            adf_fold(adf_next_codepoint(&second)))
            return false;
    }
    return *first == 0 && *second == 0;
}

static void adf_names_cleanup(adf_names *names) {
    if (!names) return;
    if (names->slots) xx_mem_free((void *)names->slots);
    if (names->hashes) xx_mem_free(names->hashes);
    xx_mem_zero(names, sizeof(*names));
}

static bool adf_names_contains(const adf_names *names, const char *name,
                               uint32_t hash) {
    size_t mask, at;
    if (names->capacity == 0U) return false;
    mask = names->capacity - 1U;
    for (at = hash & mask; names->slots[at]; at = (at + 1U) & mask)
        if (names->hashes[at] == hash && adf_name_equal(names->slots[at], name))
            return true;
    return false;
}

static void adf_names_place(const char **slots, uint32_t *hashes,
                            size_t capacity, const char *name, uint32_t hash) {
    size_t mask = capacity - 1U, at;
    for (at = hash & mask; slots[at]; at = (at + 1U) & mask) {
    }
    slots[at] = name;
    hashes[at] = hash;
}

/* @p name must outlive the set (it is owned by a member). */
static bool adf_names_add(adf_names *names, const char *name, uint32_t hash) {
    if ((names->used + 1U) * 2U > names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2U : 256U, index;
        const char **slots;
        uint32_t *hashes;
        if (capacity > SIZE_MAX / sizeof(*slots) ||
            capacity < names->capacity)
            return false;
        slots = (const char **)xx_mem_calloc(capacity, sizeof(*slots));
        hashes = (uint32_t *)xx_mem_calloc(capacity, sizeof(*hashes));
        if (!slots || !hashes) {
            if (slots) xx_mem_free((void *)slots);
            if (hashes) xx_mem_free(hashes);
            return false;
        }
        for (index = 0U; index < names->capacity; ++index)
            if (names->slots[index])
                adf_names_place(slots, hashes, capacity, names->slots[index],
                                names->hashes[index]);
        if (names->slots) xx_mem_free((void *)names->slots);
        if (names->hashes) xx_mem_free(names->hashes);
        names->slots = slots;
        names->hashes = hashes;
        names->capacity = capacity;
    }
    adf_names_place(names->slots, names->hashes, names->capacity, name, hash);
    ++names->used;
    return true;
}

static char adf_upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Windows device names, with or without an extension, in any case:
 * CON PRN AUX NUL CONIN$ CONOUT$ CLOCK$ and COM/LPT followed by a digit or
 * a superscript one, two or three. */
static bool adf_reserved_name(const char *name) {
    static const char *const devices[] = {"CON",     "PRN",    "AUX", "NUL",
                                          "CONIN$",  "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, word;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(devices) / sizeof(devices[0]); ++word) {
        const char *device = devices[word];
        for (index = 0U; index < stem; ++index)
            if (!device[index] || adf_upper_ascii(name[index]) != device[index])
                break;
        if (index == stem && device[stem] == 0) return true;
    }
    if (stem >= 4U &&
        ((adf_upper_ascii(name[0]) == 'C' && adf_upper_ascii(name[1]) == 'O' &&
          adf_upper_ascii(name[2]) == 'M') ||
         (adf_upper_ascii(name[0]) == 'L' && adf_upper_ascii(name[1]) == 'P' &&
          adf_upper_ascii(name[2]) == 'T'))) {
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return true;
        /* U+00B9, U+00B2, U+00B3 in UTF-8. */
        if (stem == 5U && (uint8_t)name[3] == 0xC2U &&
            ((uint8_t)name[4] == 0xB9U || (uint8_t)name[4] == 0xB2U ||
             (uint8_t)name[4] == 0xB3U))
            return true;
    }
    return false;
}

/* One Amiga name (ISO-8859-1) as one host-safe UTF-8 path component. */
static char *adf_component(const uint8_t *raw, size_t size) {
    char *result;
    size_t at = 0U, index, capacity = size * 2U + 3U;
    result = (char *)xx_mem_alloc(capacity);
    if (!result) return NULL;
    result[at++] = '_'; /* room for a device-name prefix */
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c == 0x7FU || (c >= 0x80U && c < 0xA0U) ||
            c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
        if (c < 0x80U) {
            result[at++] = (char)c;
        } else {
            result[at++] = (char)(0xC0U | (c >> 6U));
            result[at++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    /* Windows drops trailing dots and spaces, which would merge names and
     * turn "." and ".." into directory references. */
    for (index = at; index > 1U; --index) {
        if (result[index - 1U] != '.' && result[index - 1U] != ' ') break;
        result[index - 1U] = '_';
    }
    if (at == 1U) result[at++] = '_';
    result[at] = 0;
    if (adf_reserved_name(result + 1U)) return result;
    xx_rt_memmove(result, result + 1U, at);
    return result;
}

static char *adf_join(const char *parent, const char *leaf, unsigned suffix) {
    char suffix_text[16];
    const char *dot;
    size_t parent_size, leaf_size, suffix_size = 0U, before, total, at = 0U;
    char *result;
    parent_size = xx_str_len(parent);
    leaf_size = xx_str_len(leaf);
    if (suffix != 0U) {
        if (xx_rt_snprintf(suffix_text, sizeof(suffix_text), "~%u", suffix) < 0)
            return NULL;
        suffix_size = xx_str_len(suffix_text);
    }
    dot = xx_rt_strrchr(leaf, '.');
    before = (dot && dot != leaf && leaf_size - (size_t)(dot - leaf) <= 32U)
                 ? (size_t)(dot - leaf)
                 : leaf_size;
    if (leaf_size > SIZE_MAX / 2U || parent_size > SIZE_MAX / 2U - leaf_size -
                                                       suffix_size - 2U)
        return NULL;
    total = parent_size + 1U + leaf_size + suffix_size + 1U;
    result = (char *)xx_mem_alloc(total);
    if (!result) return NULL;
    if (parent_size != 0U) {
        xx_rt_memcpy(result, parent, parent_size);
        at = parent_size;
        result[at++] = '/';
    }
    xx_rt_memcpy(result + at, leaf, before);
    at += before;
    if (suffix_size != 0U) {
        xx_rt_memcpy(result + at, suffix_text, suffix_size);
        at += suffix_size;
    }
    xx_rt_memcpy(result + at, leaf + before, leaf_size - before);
    at += leaf_size - before;
    result[at] = 0;
    return result;
}

/* A unique path for @p leaf under @p parent: the first free one of "leaf",
 * "leaf~1", "leaf~2", ... compared without case, as both AmigaDOS and
 * Windows do. */
static char *adf_claim(adf_names *names, const char *parent,
                       const char *leaf) {
    unsigned suffix;
    for (suffix = 0U; suffix <= ADF_MAX_COLLISIONS; ++suffix) {
        char *candidate;
        uint32_t hash;
        if (suffix != 0U && ++names->extra_probes > ADF_MAX_EXTRA_PROBES)
            return NULL;
        candidate = adf_join(parent, leaf, suffix);
        if (!candidate) return NULL;
        hash = adf_name_hash(candidate);
        if (!adf_names_contains(names, candidate, hash)) {
            if (!adf_names_add(names, candidate, hash)) {
                xx_mem_free(candidate);
                return NULL;
            }
            return candidate;
        }
        xx_mem_free(candidate);
    }
    return NULL;
}

static char *adf_utf8_copy(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 2U + 1U);
    if (result) adf_latin1_to_utf8(raw, size, result, size * 2U + 1U);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Directory walk                                                          */

static void adf_member_cleanup(adf_member *member) {
    if (member->name) xx_mem_free(member->name);
    if (member->comment) xx_mem_free(member->comment);
    if (member->link_target) xx_mem_free(member->link_target);
    xx_mem_zero(member, sizeof(*member));
}

static void adf_stream_free(void *opaque) {
    adf_stream *stream = (adf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        adf_member_cleanup(&stream->items[index]);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool adf_add_member(adf_stream *stream, adf_member *member) {
    if (stream->count >= ADF_MAX_MEMBERS) return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 64U;
        adf_member *grown;
        if (capacity > ADF_MAX_MEMBERS) capacity = ADF_MAX_MEMBERS;
        grown = (adf_member *)xx_mem_realloc(stream->items,
                                             capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static int64_t adf_timestamp(const uint8_t *field) {
    uint32_t days = adf_be32(field), minutes = adf_be32(field + 4U),
             ticks = adf_be32(field + 8U);
    if (minutes >= 1440U || ticks >= 3000U || days > 0x7FFFFFU) return -1;
    return ADF_EPOCH + (int64_t)days * 86400 + (int64_t)minutes * 60 +
           (int64_t)(ticks / 50U);
}

/* A header block of the tree: type 2, its own number as key, the expected
 * secondary type set, a matching checksum. */
static bool adf_entry_ok(const uint8_t *block, uint32_t number) {
    uint32_t secondary = adf_be32(block + ADF_OFF_SEC_TYPE);
    return adf_be32(block + ADF_OFF_TYPE) == ADF_T_HEADER &&
           adf_be32(block + ADF_OFF_KEY) == number &&
           (secondary == ADF_ST_USERDIR || secondary == ADF_ST_FILE ||
            secondary == ADF_ST_SOFTLINK || secondary == ADF_ST_LINKFILE ||
            secondary == ADF_ST_LINKDIR) &&
           adf_checksum_ok(block);
}

static bool adf_file_header_ok(const adf_volume *volume, uint32_t number,
                               uint8_t *block) {
    return adf_pointer_ok(volume, number) &&
           adf_read_block(volume, number, block) &&
           adf_be32(block + ADF_OFF_TYPE) == ADF_T_HEADER &&
           adf_be32(block + ADF_OFF_KEY) == number &&
           adf_be32(block + ADF_OFF_SEC_TYPE) == ADF_ST_FILE &&
           adf_checksum_ok(block);
}

/* Name, comment and date of one entry header. */
static bool adf_entry_text(const adf_volume *volume, const uint8_t *block,
                           uint32_t number, char **leaf, char **comment,
                           int64_t *timestamp) {
    const uint8_t *name;
    size_t name_size;
    *leaf = NULL;
    *comment = NULL;
    if (volume->long_names) {
        const uint8_t *field = block + ADF_OFF_COMMENT;
        size_t comment_size;
        name_size = field[0];
        if (name_size > ADF_LN_NAME_MAX) return false;
        name = field + 1U;
        comment_size = field[1U + name_size];
        if (comment_size != 0U && 2U + name_size + comment_size <= ADF_LN_FIELD) {
            *comment = adf_utf8_copy(field + 2U + name_size, comment_size);
        } else if (comment_size == 0U) {
            uint32_t comment_block = adf_be32(block + ADF_LN_COMMENT_BLOCK);
            uint8_t extra[ADF_BSIZE];
            if (comment_block != 0U && adf_pointer_ok(volume, comment_block) &&
                adf_read_block(volume, comment_block, extra) &&
                adf_be32(extra) == ADF_T_COMMENT &&
                adf_be32(extra + 4U) == comment_block &&
                adf_be32(extra + 8U) == number && adf_checksum_ok(extra) &&
                extra[24] != 0U && extra[24] <= ADF_COMMENT_MAX)
                *comment = adf_utf8_copy(extra + 25U, extra[24]);
        }
        *timestamp = adf_timestamp(block + ADF_LN_DATE);
    } else {
        name_size = block[ADF_OFF_NAME];
        if (name_size > ADF_NAME_MAX) return false;
        name = block + ADF_OFF_NAME + 1U;
        if (block[ADF_OFF_COMMENT] != 0U &&
            block[ADF_OFF_COMMENT] <= ADF_COMMENT_MAX)
            *comment = adf_utf8_copy(block + ADF_OFF_COMMENT + 1U,
                                     block[ADF_OFF_COMMENT]);
        *timestamp = adf_timestamp(block + ADF_OFF_DATE);
    }
    *leaf = adf_component(name, name_size);
    if (!*leaf) {
        if (*comment) xx_mem_free(*comment);
        *comment = NULL;
        return false;
    }
    return true;
}

/* Blank boot blocks leave OFS or FFS open.  The first file with data settles
 * it: an OFS data block is tagged (type 8, owning header, sequence 1, a size
 * that fits, a zero checksum), a raw FFS block essentially never is. */
static void adf_settle_type(adf_volume *volume, uint32_t header,
                            uint32_t first) {
    uint8_t data[ADF_BSIZE];
    uint32_t size;
    bool ofs;
    if (volume->type_known || !adf_pointer_ok(volume, first) ||
        first == volume->root || first == header ||
        !adf_read_block(volume, first, data))
        return;
    size = adf_be32(data + 12U);
    ofs = adf_be32(data) == ADF_T_DATA && adf_be32(data + 4U) == header &&
          adf_be32(data + 8U) == 1U && size != 0U && size <= ADF_OFS_PAYLOAD &&
          adf_checksum_ok(data);
    volume->ffs = !ofs;
    volume->dos_type = ofs ? 0U : 1U;
    volume->type_known = true;
}

static bool adf_visit(uint8_t *visited, uint32_t block) {
    uint8_t bit = (uint8_t)(1U << (block & 7U));
    if (visited[block >> 3U] & bit) return false;
    visited[block >> 3U] |= bit;
    return true;
}

static void adf_load_table(adf_frame *frame, const uint8_t *block) {
    size_t index;
    for (index = 0U; index < ADF_HT_SIZE; ++index)
        frame->table[index] = adf_be32(block + ADF_OFF_TABLE + index * 4U);
    frame->slot = 0U;
    frame->next = 0U;
}

/* Build the member list.  Damaged entries end their hash chain (its next
 * pointer cannot be trusted) but do not end the walk. */
static bool adf_parse(Abstractformat *format, adf_stream **result,
                      xx_pd_struct *pd) {
    uint8_t root[ADF_BSIZE];
    uint8_t block[ADF_BSIZE];
    uint8_t target[ADF_BSIZE];
    adf_stream *stream;
    adf_frame *frames = NULL;
    adf_names names;
    uint8_t *visited = NULL;
    size_t depth = 0U;
    unsigned long steps = 0UL;
    bool ok = false;
    xx_mem_zero(&names, sizeof(names));
    *result = NULL;
    stream = (adf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!adf_open_volume(format, &stream->volume, root)) goto done;
    visited = (uint8_t *)xx_mem_calloc(((size_t)stream->volume.blocks + 7U) /
                                           8U,
                                       1U);
    frames = (adf_frame *)xx_mem_alloc(ADF_MAX_DEPTH * sizeof(*frames));
    if (!visited || !frames) goto done;
    adf_visit(visited, 0U);
    adf_visit(visited, 1U);
    adf_visit(visited, stream->volume.root);
    frames[0].block = stream->volume.root;
    frames[0].path = "";
    adf_load_table(&frames[0], root);
    depth = 1U;
    while (depth != 0U) {
        adf_frame *frame = &frames[depth - 1U];
        adf_member member;
        uint32_t number, secondary;
        char *leaf = NULL;
        if ((++steps & 0xFFUL) == 0UL && pd && xx_pd_is_stopped(pd)) goto done;
        if (frame->next == 0U) {
            while (frame->slot < ADF_HT_SIZE && frame->table[frame->slot] == 0U)
                ++frame->slot;
            if (frame->slot >= ADF_HT_SIZE) {
                --depth;
                continue;
            }
            frame->next = frame->table[frame->slot++];
        }
        number = frame->next;
        frame->next = 0U;
        if (!adf_pointer_ok(&stream->volume, number) ||
            !adf_visit(visited, number) ||
            !adf_read_block(&stream->volume, number, block) ||
            !adf_entry_ok(block, number) ||
            adf_be32(block + ADF_OFF_PARENT) != frame->block)
            continue;
        frame->next = adf_be32(block + ADF_OFF_HASH_CHAIN);
        xx_mem_zero(&member, sizeof(member));
        member.header = number;
        member.protect = adf_be32(block + ADF_OFF_PROTECT);
        if (!adf_entry_text(&stream->volume, block, number, &leaf,
                            &member.comment, &member.timestamp))
            continue;
        secondary = adf_be32(block + ADF_OFF_SEC_TYPE);
        if (secondary == ADF_ST_USERDIR || secondary == ADF_ST_LINKDIR) {
            member.kind = ADF_KIND_FOLDER;
        } else if (secondary == ADF_ST_FILE) {
            member.kind = ADF_KIND_FILE;
            member.data = number;
            member.first = adf_be32(block + ADF_OFF_TABLE_LAST);
            member.size = adf_be32(block + ADF_OFF_BYTE_SIZE);
            if (member.size != 0U)
                adf_settle_type(&stream->volume, number, member.first);
        } else if (secondary == ADF_ST_LINKFILE) {
            uint32_t real = adf_be32(block + ADF_OFF_REAL_ENTRY);
            if (!adf_file_header_ok(&stream->volume, real, target)) {
                xx_mem_free(leaf);
                adf_member_cleanup(&member);
                continue;
            }
            member.kind = ADF_KIND_FILE;
            member.data = real;
            member.first = adf_be32(target + ADF_OFF_TABLE_LAST);
            member.size = adf_be32(target + ADF_OFF_BYTE_SIZE);
        } else {
            size_t length = 0U;
            while (length < ADF_LINK_PATH_MAX &&
                   block[ADF_OFF_TABLE + length] != 0U)
                ++length;
            member.kind = ADF_KIND_SOFTLINK;
            member.link_target = adf_utf8_copy(block + ADF_OFF_TABLE, length);
            if (!member.link_target) {
                xx_mem_free(leaf);
                adf_member_cleanup(&member);
                goto done;
            }
        }
        member.name = adf_claim(&names, frame->path, leaf);
        xx_mem_free(leaf);
        if (!member.name) {
            adf_member_cleanup(&member);
            continue;
        }
        if (!adf_add_member(stream, &member)) {
            adf_member_cleanup(&member);
            if (stream->count >= ADF_MAX_MEMBERS) break;
            goto done;
        }
        if (secondary == ADF_ST_USERDIR && depth < ADF_MAX_DEPTH) {
            adf_frame *child = &frames[depth];
            child->block = number;
            child->path = stream->items[stream->count - 1U].name;
            adf_load_table(child, block);
            ++depth;
        }
    }
    ok = true;
done:
    adf_names_cleanup(&names);
    if (visited) xx_mem_free(visited);
    if (frames) xx_mem_free(frames);
    if (!ok) {
        adf_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

/* ---------------------------------------------------------------------- */
/* File data                                                               */

typedef struct adf_sink_s {
    xx_io_device *device; /**< NULL only verifies. */
    uint8_t *buffer;
    size_t used;
} adf_sink;

static bool adf_sink_flush(adf_sink *sink) {
    size_t done = 0U;
    if (!sink->device) {
        sink->used = 0U;
        return true;
    }
    while (done < sink->used) {
        ssize_t wrote = xx_io_write(sink->device, sink->buffer + done,
                                    sink->used - done);
        if (wrote <= 0 || (size_t)wrote > sink->used - done) return false;
        done += (size_t)wrote;
    }
    sink->used = 0U;
    return true;
}

static bool adf_sink_put(adf_sink *sink, const uint8_t *data, size_t size) {
    if (!sink->device) return true;
    if (sink->used + size > ADF_COPY_BUFFER && !adf_sink_flush(sink))
        return false;
    xx_rt_memcpy(sink->buffer + sink->used, data, size);
    sink->used += size;
    return true;
}

/* Copy the bytes of the file whose header is @p header, following the block
 * table of the header and then of each extension block.  Every data block
 * of an OFS file must name this header and the expected sequence number; an
 * FFS data block carries no tag, so only blocks that cannot hold data (the
 * root, the header, the table being read) are refused.  A revisited
 * extension block is found with Brent's cycle check and fails the copy. */
static bool adf_copy_file(const adf_volume *volume, uint32_t header,
                          uint64_t expected, adf_sink *sink,
                          xx_pd_struct *pd) {
    uint8_t table[ADF_BSIZE];
    uint8_t data[ADF_BSIZE];
    uint64_t remaining;
    uint32_t sequence = 1U, hops = 0U, current = header;
    uint32_t tortoise = header, power = 1U, run = 0U;
    if (!adf_file_header_ok(volume, header, table)) return false;
    remaining = adf_be32(table + ADF_OFF_BYTE_SIZE);
    if (remaining != expected ||
        remaining > (uint64_t)volume->blocks * ADF_BSIZE)
        return false;
    while (remaining != 0U) {
        uint32_t slot;
        for (slot = 0U; slot < ADF_HT_SIZE && remaining != 0U; ++slot) {
            uint32_t pointer =
                adf_be32(table + ADF_OFF_TABLE_LAST - slot * 4U);
            size_t take;
            if (!adf_pointer_ok(volume, pointer) || pointer == volume->root ||
                pointer == header || pointer == current ||
                !adf_read_block(volume, pointer, data))
                return false;
            if (volume->ffs) {
                take = remaining < ADF_BSIZE ? (size_t)remaining : ADF_BSIZE;
                if (!adf_sink_put(sink, data, take)) return false;
            } else {
                uint32_t size = adf_be32(data + 12U);
                if (adf_be32(data) != ADF_T_DATA ||
                    adf_be32(data + 4U) != header ||
                    adf_be32(data + 8U) != sequence || size == 0U ||
                    size > ADF_OFS_PAYLOAD || !adf_checksum_ok(data))
                    return false;
                take = (uint64_t)size < remaining ? (size_t)size
                                                  : (size_t)remaining;
                if (!adf_sink_put(sink, data + ADF_OFS_HEADER, take))
                    return false;
            }
            remaining -= take;
            ++sequence;
        }
        if (remaining == 0U) break;
        if (pd && xx_pd_is_stopped(pd)) return false;
        /* Extension (list) block: type 16, its own key, parent = header. */
        current = adf_be32(table + ADF_OFF_EXTENSION);
        if (current == tortoise) return false;
        if (++run == power) {
            tortoise = current;
            power = power < UINT32_C(0x80000000) ? power << 1U : power;
            run = 0U;
        }
        if (++hops > volume->blocks || !adf_pointer_ok(volume, current) ||
            !adf_read_block(volume, current, table) ||
            adf_be32(table + ADF_OFF_TYPE) != ADF_T_LIST ||
            adf_be32(table + ADF_OFF_KEY) != current ||
            adf_be32(table + ADF_OFF_PARENT) != header ||
            adf_be32(table + ADF_OFF_SEC_TYPE) != ADF_ST_FILE ||
            !adf_checksum_ok(table))
            return false;
    }
    return adf_sink_flush(sink);
}

static bool adf_emit_file(const adf_volume *volume, const adf_member *member,
                          xx_io_device *destination, xx_pd_struct *pd) {
    adf_sink sink;
    bool result;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    if (destination) {
        sink.buffer = (uint8_t *)xx_mem_alloc(ADF_COPY_BUFFER);
        if (!sink.buffer) return false;
    }
    result = adf_copy_file(volume, member->data, member->size, &sink, pd);
    if (sink.buffer) xx_mem_free(sink.buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool adf_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
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

static const xx_var *adf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool adf_set_record(xx_archive_record *record,
                           const adf_stream *stream,
                           const adf_member *member) {
    bool folder = member->kind == ADF_KIND_FOLDER;
    uint64_t size = member->kind == ADF_KIND_FILE ? member->size : 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        stream->volume.base + (int64_t)member->header * (int64_t)ADF_BSIZE;
    record->header_size = ADF_BSIZE;
    record->data_offset =
        member->kind == ADF_KIND_FILE && size != 0U &&
                adf_pointer_ok(&stream->volume, member->first)
            ? stream->volume.base + (int64_t)member->first * (int64_t)ADF_BSIZE
            : record->header_offset;
    record->compressed_size = (int64_t)size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->protect) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, folder))
        return false;
    if (member->timestamp >= 0 &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        (uint64_t)member->timestamp))
        return false;
    if (member->comment && member->comment[0] &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        member->comment))
        return false;
    if (member->link_target &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                        member->link_target))
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_adf_init(xx_adf *image, xx_io_device *device, int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base_address);
    image->format.endian = XX_ENDIAN_BIG;
    image->format.file_type = XX_ADF_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-amiga-disk-format");
    xx_format_set_extension(&image->format, "adf");
    image->format.check_is_valid = xx_adf_check_is_valid;
    image->format.handle_base_info = xx_adf_handle_base_info;
    image->format.get_format_size = xx_adf_get_format_size;
    image->format.get_number_of_archive_records =
        xx_adf_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_adf_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_adf_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_adf_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_adf_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_adf_free_archive_records_reading;
    image->image_size = -1;
}

xx_adf *xx_adf_create(xx_io_device *device, int64_t base_address) {
    xx_adf *image = (xx_adf *)xx_mem_alloc(sizeof(*image));
    if (image) xx_adf_init(image, device, base_address);
    return image;
}

void xx_adf_destroy(xx_adf *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_adf_free(xx_adf *image) {
    if (!image) return;
    xx_adf_destroy(image);
    xx_mem_free(image);
}

/* Cheap: the boot tag and one root block. */
bool xx_adf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    adf_volume volume;
    (void)pd;
    return adf_open_volume(format, &volume, NULL);
}

bool xx_adf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    adf_stream *stream;
    xx_adf *image;
    if (!format || !adf_parse(format, &stream, pd)) return false;
    image = (xx_adf *)format;
    image->number_of_records = stream->count;
    image->image_size = stream->volume.format_size;
    image->root_block = stream->volume.root;
    image->block_count = stream->volume.blocks;
    image->dos_type = stream->volume.dos_type;
    image->blank_boot = stream->volume.blank_boot ? 1U : 0U;
    xx_rt_memcpy(image->volume_name, stream->volume.volume_name,
                 sizeof(image->volume_name));
    format->number_of_archive_records = stream->count;
    format->format_size = stream->volume.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    adf_stream_free(stream);
    return true;
}

int64_t xx_adf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_adf_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_adf_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_adf_handle_base_info(format, pd))
               ? ((xx_adf *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_adf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    adf_stream *stream;
    xx_archive_record_state *state;
    if (!format || !adf_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        adf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = adf_stream_free;
    state->total_records = stream->count;
    if (!adf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !adf_set_record(&state->current_record, stream, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_adf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_adf_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    adf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (adf_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = adf_set_record(&state->current_record, stream,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_adf_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    adf_stream *stream;
    const adf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (adf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    /* A soft link is not a byte stream; the target is on the record. */
    if (member->kind == ADF_KIND_SOFTLINK) return false;
    path_option = adf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return member->kind == ADF_KIND_FOLDER ||
               adf_emit_file(&stream->volume, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->kind == ADF_KIND_FOLDER) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = adf_emit_file(&stream->volume, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && member->kind == ADF_KIND_FILE && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_adf_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Amiga ADF / AmigaDOS OFS and FFS volumes (DOS\0 .. DOS\7).  xx_adf.h
 * carries the block layouts.
 *
 * Written from the AmigaDOS on-disk structure.  The volume is located by
 * the boot block's "DOS" tag (or, on an exact DD/HD floppy, the root block
 * alone when the boot blocks are zero or foreign) and a root block that must check out in full (type, key, hash
 * table size, secondary type and checksum).  The directory
 * tree is walked from the root's hash table through every hash chain; each
 * header block is claimed once, so cross-linked or looping chains cannot
 * repeat or spin.  File data is followed through the header's block table
 * and its extension chain, never through the OFS "next" links, and every
 * pointer is range-checked against the volume before it is read.
 *
 * A DD/HD floppy image with no root block (a custom-loader disk) has no
 * directory.  Its ProTracker modules, Amiga load files and IFF files are
 * listed instead, see "Disks without a filesystem" below.
 */

#include "xxfclib/codepages/xx_codepage.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/filesystem/adf/xx_adf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"
#include "xx_adf_crunch.h"

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
 * a bounded amount of work.  Each probe handles one path component (at most
 * a few hundred bytes), never a whole path. */
#define ADF_MAX_EXTRA_PROBES UINT32_C(0x100000)
/* Bytes of member text (names, comments, link targets) and bookkeeping one
 * volume may hold; the listing ends there, as it does at ADF_MAX_MEMBERS. */
#define ADF_MAX_TEXT_BYTES (UINT64_C(48) * 1024U * 1024U)
/* Bytes one records session may copy out: hard links (and FFS headers that
 * share data blocks) could otherwise emit the volume many times over. */
#define ADF_EMIT_SLACK (INT64_C(32) * 1024 * 1024)
#define ADF_EMIT_FACTOR 4
#define ADF_NO_PARENT UINT32_MAX
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
    uint32_t blocks; /**< Filesystem span; every pointer is below it. */
    uint32_t root;
    uint8_t dos_type;
    bool ffs;
    bool long_names;
    bool blank_boot; /**< Boot blocks all zero; OFS/FFS still to decide. */
    bool type_known; /**< ffs is settled (always, when the boot tag is DOS). */
    bool carved;     /**< No filesystem: the members are carved files. */
    int64_t format_size;
    char volume_name[96];
} adf_volume;

/* A member keeps only its own path component and its parent's index; the
 * full path is built when a record or an output file needs it, so memory
 * grows with the number of entries, not with their depth. */
typedef struct adf_member_s {
    char *leaf;        /**< Unique (within the parent), host-safe UTF-8. */
    char *comment;     /**< UTF-8 or NULL. */
    char *link_target; /**< Soft links only. */
    uint32_t header;   /**< The entry's own header block. */
    uint32_t data;     /**< File header that supplies the bytes. */
    uint32_t first;    /**< First data block of a file, 0 if none. */
    uint32_t protect;
    uint32_t parent;      /**< Index of the parent member, or ADF_NO_PARENT. */
    uint32_t next_suffix; /**< Next ~N to try for a name equal to this one. */
    uint64_t size;
    int64_t timestamp; /**< Unix seconds, or -1. */
    adf_kind kind;
    bool raw;            /**< Carved: one run of bytes at raw_offset. */
    uint32_t raw_offset; /**< Bytes from the start of the image. */
    uint32_t packed;     /**< Carved and crunched: its length on the disk. */
    uint8_t crunch;      /**< ADFC_NONE, or the cruncher to undo. */
} adf_member;

/* Open-addressed set of (parent, folded leaf); a slot holds index + 1 of the
 * member that owns the name, 0 when empty. */
typedef struct adf_names_s {
    uint32_t *slots;
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
    uint64_t text_bytes; /**< Held by members; capped by ADF_MAX_TEXT_BYTES. */
    int64_t emitted;     /**< File bytes copied so far in this session. */
    int64_t emit_budget;
} adf_stream;

typedef struct adf_frame_s {
    uint32_t block;  /**< Directory (or root) header block. */
    uint32_t member; /**< Its member index; ADF_NO_PARENT for the root. */
    uint32_t table[ADF_HT_SIZE];
    uint32_t slot; /**< Next hash slot to start. */
    uint32_t next; /**< Next entry on the current chain, 0 if none. */
} adf_frame;

/* ---------------------------------------------------------------------- */
/* Block access                                                            */

#include "xxfclib/global/xx_global.h"
static size_t gb_adf_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_adf_read(xx_io_device *device, void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}
static ssize_t gb_adf_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static bool adf_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    const size_t file_io_capacity = gb_adf_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = gb_adf_read(device, (uint8_t *)buffer + done, size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool adf_read_block(const adf_volume *volume, uint32_t block, uint8_t *buffer)
{
    if (!volume || block >= volume->blocks) return false;
    return adf_read_at(volume->device, volume->base + (int64_t)block * (int64_t)ADF_BSIZE, buffer, ADF_BSIZE);
}

/* Header, list, data and comment blocks all sum to zero over 128 longs. */
static bool adf_checksum_ok(const uint8_t *block)
{
    uint32_t sum = 0U;
    size_t index;
    for (index = 0U; index < ADF_BSIZE; index += 4U) sum += xx_data_get_u32(block + index, 4, 0, true);
    return sum == 0U;
}

/* A block number that may hold metadata or data: past the boot blocks and
 * inside the filesystem. */
static bool adf_pointer_ok(const adf_volume *volume, uint32_t block)
{
    return block >= 2U && block < volume->blocks;
}

/* ---------------------------------------------------------------------- */
/* Volume                                                                  */

static bool adf_root_ok(const uint8_t *block)
{
    return xx_data_get_u32(block + ADF_OFF_TYPE, 4, 0, true) == ADF_T_HEADER && xx_data_get_u32(block + ADF_OFF_KEY, 4, 0, true) == 0U &&
           xx_data_get_u32(block + ADF_OFF_HIGH_SEQ, 4, 0, true) == 0U && xx_data_get_u32(block + ADF_OFF_HT_SIZE, 4, 0, true) == ADF_HT_SIZE &&
           xx_data_get_u32(block + ADF_OFF_SEC_TYPE, 4, 0, true) == ADF_ST_ROOT && block[ADF_OFF_NAME] <= ADF_NAME_MAX && adf_checksum_ok(block);
}

static bool adf_is_variant(uint32_t blocks, uint32_t standard)
{
    uint32_t per_cylinder = standard / 80U, cylinders;
    for (cylinders = 80U; cylinders <= 83U; ++cylinders)
        if (blocks == per_cylinder * cylinders) return true;
    return false;
}

/* ISO-8859-1 to UTF-8, with C0/C1 controls replaced.  @p capacity counts
 * the terminator; a name that does not fit is cut at a character. */
static void adf_latin1_to_utf8(const uint8_t *raw, size_t size, char *out, size_t capacity)
{
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
            at += xx_codepage_byte_to_utf8(xx_codepage_by_number(28591), c, (unsigned char *)out + (at), 2);
        }
    }
    out[at] = 0;
}

/* True when both boot blocks are zero bytes. */
static bool adf_boot_blank(xx_io_device *device, int64_t base)
{
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
 * without a DOS tag (blank or foreign boot blocks) is accepted only as an
 * exact standard floppy. */
static bool adf_open_volume(Abstractformat *format, adf_volume *out, uint8_t *root_block)
{
    uint8_t boot[4];
    uint8_t block[ADF_BSIZE];
    int64_t total, size, whole;
    uint32_t candidates[3], spans[3];
    size_t count = 0U, index;
    adf_volume volume;
    bool blank = false, untagged = false;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(4U * ADF_BSIZE) || !adf_read_at(format->device, format->base_address, boot, sizeof(boot))) return false;
    if (boot[0] != 'D' || boot[1] != 'O' || boot[2] != 'S' || boot[3] > 7U) {
        /* No usable tag: only an exact standard floppy, located by its root
         * block alone.  The boot blocks are either all zero or foreign: a
         * custom boot loader, a cracker's tag, "DOS" with a bad flags byte
         * (TOSEC "[b DOS ID missing]").  AmigaDOS refuses such a disk, but
         * the tree behind it is complete on real dumps (Shaq-Fu disks 2-6,
         * Cytadela disk 5, Skaermtrolden Hugo disk 2). */
        if (size != (int64_t)ADF_DD_BLOCKS * ADF_BSIZE && size != (int64_t)ADF_HD_BLOCKS * ADF_BSIZE) return false;
        untagged = true;
        blank = boot[0] == 0U && boot[1] == 0U && boot[2] == 0U && boot[3] == 0U && adf_boot_blank(format->device, format->base_address);
    }
    whole = size / (int64_t)ADF_BSIZE;
    if (whole <= (int64_t)ADF_MAX_BLOCKS) {
        candidates[count] = (uint32_t)((whole + 1) / 2);
        spans[count++] = (uint32_t)whole;
    }
    if (whole > (int64_t)ADF_DD_ROOT) {
        candidates[count] = ADF_DD_ROOT;
        spans[count++] = whole <= (int64_t)ADF_MAX_BLOCKS && adf_is_variant((uint32_t)whole, ADF_DD_BLOCKS)
                             ? (uint32_t)whole
                             : (whole < (int64_t)ADF_DD_BLOCKS ? (uint32_t)whole : ADF_DD_BLOCKS);
    }
    if (whole > (int64_t)ADF_HD_ROOT) {
        candidates[count] = ADF_HD_ROOT;
        spans[count++] = whole <= (int64_t)ADF_MAX_BLOCKS && adf_is_variant((uint32_t)whole, ADF_HD_BLOCKS)
                             ? (uint32_t)whole
                             : (whole < (int64_t)ADF_HD_BLOCKS ? (uint32_t)whole : ADF_HD_BLOCKS);
    }
    for (index = 0U; index < count; ++index) {
        size_t earlier;
        bool repeated = false;
        for (earlier = 0U; earlier < index; ++earlier)
            if (candidates[earlier] == candidates[index]) repeated = true;
        if (repeated || candidates[index] < 2U || candidates[index] >= spans[index]) continue;
        if (!adf_read_at(format->device, format->base_address + (int64_t)candidates[index] * (int64_t)ADF_BSIZE, block, sizeof(block)) || !adf_root_ok(block)) continue;
        xx_mem_zero(&volume, sizeof(volume));
        volume.device = format->device;
        volume.base = format->base_address;
        volume.blocks = spans[index];
        volume.root = candidates[index];
        volume.dos_type = untagged ? 0U : boot[3];
        volume.ffs = !untagged && (boot[3] & 1U) != 0U;
        volume.long_names = !untagged && boot[3] >= 6U;
        volume.blank_boot = blank;
        volume.type_known = !untagged;
        volume.format_size = (int64_t)spans[index] * (int64_t)ADF_BSIZE;
        adf_latin1_to_utf8(block + ADF_OFF_NAME + 1U, block[ADF_OFF_NAME], volume.volume_name, sizeof(volume.volume_name));
        *out = volume;
        if (root_block) xx_rt_memcpy(root_block, block, ADF_BSIZE);
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static unsigned adf_fold(unsigned codepoint)
{
    if ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 0xE0U && codepoint <= 0xFEU && codepoint != 0xF7U)) return codepoint - 0x20U;
    return codepoint;
}

/* Next code point of a UTF-8 string this reader produced (one or two bytes;
 * anything else is taken bytewise). */
static unsigned adf_next_codepoint(const char **cursor)
{
    const uint8_t *at = (const uint8_t *)*cursor;
    if ((at[0] & 0xE0U) == 0xC0U && (at[1] & 0xC0U) == 0x80U) {
        *cursor += 2;
        return ((unsigned)(at[0] & 0x1FU) << 6U) | (unsigned)(at[1] & 0x3FU);
    }
    *cursor += 1;
    return at[0];
}

static uint32_t adf_name_hash(uint32_t parent, const char *name)
{
    uint32_t hash = 2166136261U;
    unsigned shift;
    for (shift = 0U; shift < 32U; shift += 8U) {
        hash ^= (parent >> shift) & 0xFFU;
        hash *= 16777619U;
    }
    while (*name) {
        hash ^= adf_fold(adf_next_codepoint(&name));
        hash *= 16777619U;
    }
    return hash;
}

static bool adf_name_equal(const char *first, const char *second)
{
    while (*first && *second) {
        if (adf_fold(adf_next_codepoint(&first)) != adf_fold(adf_next_codepoint(&second))) return false;
    }
    return *first == 0 && *second == 0;
}

static void adf_names_cleanup(adf_names *names)
{
    if (!names) return;
    if (names->slots) xx_mem_free(names->slots);
    if (names->hashes) xx_mem_free(names->hashes);
    xx_mem_zero(names, sizeof(*names));
}

/* Index of the member already called @p name under @p parent, or
 * ADF_NO_PARENT when the name is free. */
static uint32_t adf_names_find(const adf_names *names, const adf_member *items, uint32_t parent, const char *name, uint32_t hash)
{
    size_t mask, at;
    if (names->capacity == 0U) return ADF_NO_PARENT;
    mask = names->capacity - 1U;
    for (at = hash & mask; names->slots[at]; at = (at + 1U) & mask) {
        const adf_member *other = &items[names->slots[at] - 1U];
        if (names->hashes[at] == hash && other->parent == parent && adf_name_equal(other->leaf, name)) return names->slots[at] - 1U;
    }
    return ADF_NO_PARENT;
}

static void adf_names_place(uint32_t *slots, uint32_t *hashes, size_t capacity, uint32_t slot, uint32_t hash)
{
    size_t mask = capacity - 1U, at;
    for (at = hash & mask; slots[at]; at = (at + 1U) & mask) {
    }
    slots[at] = slot;
    hashes[at] = hash;
}

/* Record that member @p index owns its (parent, leaf). */
static bool adf_names_add(adf_names *names, uint32_t index, uint32_t hash)
{
    if ((names->used + 1U) * 2U > names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2U : 256U, at;
        uint32_t *slots, *hashes;
        if (capacity > SIZE_MAX / sizeof(*slots) || capacity < names->capacity) return false;
        slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(*slots));
        hashes = (uint32_t *)xx_mem_calloc(capacity, sizeof(*hashes));
        if (!slots || !hashes) {
            if (slots) xx_mem_free(slots);
            if (hashes) xx_mem_free(hashes);
            return false;
        }
        for (at = 0U; at < names->capacity; ++at)
            if (names->slots[at]) adf_names_place(slots, hashes, capacity, names->slots[at], names->hashes[at]);
        if (names->slots) xx_mem_free(names->slots);
        if (names->hashes) xx_mem_free(names->hashes);
        names->slots = slots;
        names->hashes = hashes;
        names->capacity = capacity;
    }
    adf_names_place(names->slots, names->hashes, names->capacity, index + 1U, hash);
    ++names->used;
    return true;
}

static char adf_upper_ascii(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Windows device names, with or without an extension, in any case:
 * CON PRN AUX NUL CONIN$ CONOUT$ CLOCK$ and COM/LPT followed by a digit or
 * a superscript one, two or three. */
static bool adf_reserved_name(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, word;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(devices) / sizeof(devices[0]); ++word) {
        const char *device = devices[word];
        for (index = 0U; index < stem; ++index)
            if (!device[index] || adf_upper_ascii(name[index]) != device[index]) break;
        if (index == stem && device[stem] == 0) return true;
    }
    if (stem >= 4U && ((adf_upper_ascii(name[0]) == 'C' && adf_upper_ascii(name[1]) == 'O' && adf_upper_ascii(name[2]) == 'M') ||
                       (adf_upper_ascii(name[0]) == 'L' && adf_upper_ascii(name[1]) == 'P' && adf_upper_ascii(name[2]) == 'T'))) {
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return true;
        /* U+00B9, U+00B2, U+00B3 in UTF-8. */
        if (stem == 5U && (uint8_t)name[3] == 0xC2U && ((uint8_t)name[4] == 0xB9U || (uint8_t)name[4] == 0xB2U || (uint8_t)name[4] == 0xB3U)) return true;
    }
    return false;
}

/* One Amiga name (ISO-8859-1) as one host-safe UTF-8 path component. */
static char *adf_component(const uint8_t *raw, size_t size)
{
    char *result;
    size_t at = 0U, index, capacity = size * 2U + 3U;
    result = (char *)xx_mem_alloc(capacity);
    if (!result) return NULL;
    result[at++] = '_'; /* room for a device-name prefix */
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c == 0x7FU || (c >= 0x80U && c < 0xA0U) || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            c = '_';
        if (c < 0x80U) {
            result[at++] = (char)c;
        } else {
            at += xx_codepage_byte_to_utf8(xx_codepage_by_number(28591), c, (unsigned char *)result + (at), 2);
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

/* True when @p name has the shape of a Windows 8.3 alias ("LONGFI~1.TXT"):
 * at most one dot, a stem of 1..8 characters holding '~' and a digit after
 * it, an extension of at most 3.  On a volume that makes short names, opening
 * such a name can open an earlier member through its alias. */
static bool adf_alias_shaped(const char *name)
{
    size_t stem = 0U, ext = 0U, dots = 0U;
    bool tilde_digit = false;
    const char *at;
    for (at = name; *at; ++at) {
        if ((*at & 0xC0) == 0x80) continue; /* UTF-8 continuation byte */
        if (*at == '.') {
            ++dots;
            continue;
        }
        if (dots == 0U) {
            ++stem;
            if (*at == '~' && at[1] >= '0' && at[1] <= '9') tilde_digit = true;
        } else {
            ++ext;
        }
    }
    return tilde_digit && dots <= 1U && stem >= 1U && stem <= 8U && ext <= 3U;
}

/* @p leaf with "~N" before its extension (N = 0: unchanged); '~' becomes
 * '_' throughout when the result would look like an 8.3 alias. */
static char *adf_join(const char *leaf, unsigned suffix)
{
    char suffix_text[16];
    const char *dot;
    size_t leaf_size, suffix_size = 0U, before, total, at = 0U;
    char *result;
    leaf_size = xx_str_len(leaf);
    if (suffix != 0U) {
        if (xx_rt_snprintf(suffix_text, sizeof(suffix_text), "~%u", suffix) < 0) return NULL;
        suffix_size = xx_str_len(suffix_text);
    }
    dot = xx_rt_strrchr(leaf, '.');
    before = (dot && dot != leaf && leaf_size - (size_t)(dot - leaf) <= 32U) ? (size_t)(dot - leaf) : leaf_size;
    if (leaf_size > SIZE_MAX / 2U) return NULL;
    total = leaf_size + suffix_size + 1U;
    result = (char *)xx_mem_alloc(total);
    if (!result) return NULL;
    xx_rt_memcpy(result + at, leaf, before);
    at += before;
    if (suffix_size != 0U) {
        xx_rt_memcpy(result + at, suffix_text, suffix_size);
        at += suffix_size;
    }
    xx_rt_memcpy(result + at, leaf + before, leaf_size - before);
    at += leaf_size - before;
    result[at] = 0;
    if (adf_alias_shaped(result))
        for (at = 0U; result[at]; ++at)
            if (result[at] == '~') result[at] = '_';
    return result;
}

/* A unique component for @p leaf under member @p parent: the first free one
 * of "leaf", "leaf~1", "leaf~2", ... compared without case, as both AmigaDOS
 * and Windows do.  Suffixes restart where the last collision with the same
 * name stopped, so a run of equal names costs linear work.  On success the
 * caller adds the member and then registers it with @p hash_out. */
static char *adf_claim(adf_names *names, adf_member *items, uint32_t parent, const char *leaf, uint32_t *hash_out)
{
    unsigned suffix;
    uint32_t owner, hash;
    char *candidate = adf_join(leaf, 0U);
    if (!candidate) return NULL;
    hash = adf_name_hash(parent, candidate);
    owner = adf_names_find(names, items, parent, candidate, hash);
    if (owner == ADF_NO_PARENT) {
        *hash_out = hash;
        return candidate;
    }
    xx_mem_free(candidate);
    suffix = items[owner].next_suffix ? items[owner].next_suffix : 1U;
    for (; suffix <= ADF_MAX_COLLISIONS; ++suffix) {
        if (++names->extra_probes > ADF_MAX_EXTRA_PROBES) return NULL;
        candidate = adf_join(leaf, suffix);
        if (!candidate) return NULL;
        hash = adf_name_hash(parent, candidate);
        if (adf_names_find(names, items, parent, candidate, hash) == ADF_NO_PARENT) {
            items[owner].next_suffix = suffix + 1U;
            *hash_out = hash;
            return candidate;
        }
        xx_mem_free(candidate);
    }
    items[owner].next_suffix = suffix;
    return NULL;
}

/* "a/b/c" for member @p index: its leaf behind those of its parents.  The
 * parent chain only points backwards and is at most ADF_MAX_DEPTH long. */
static char *adf_member_path(const adf_member *items, size_t count, uint32_t index)
{
    size_t total = 0U, at, depth = 0U;
    uint32_t walk;
    char *result;
    for (walk = index; walk != ADF_NO_PARENT && walk < count; walk = items[walk].parent) {
        if (++depth > ADF_MAX_DEPTH + 1U) return NULL;
        total += xx_str_len(items[walk].leaf) + 1U;
    }
    if (walk != ADF_NO_PARENT || total == 0U) return NULL;
    result = (char *)xx_mem_alloc(total);
    if (!result) return NULL;
    at = total - 1U;
    result[at] = 0;
    for (walk = index; walk != ADF_NO_PARENT; walk = items[walk].parent) {
        size_t size = xx_str_len(items[walk].leaf);
        at -= size;
        xx_rt_memcpy(result + at, items[walk].leaf, size);
        if (at != 0U) result[--at] = '/';
    }
    return result;
}

static char *adf_utf8_copy(const uint8_t *raw, size_t size)
{
    char *result = (char *)xx_mem_alloc(size * 2U + 1U);
    if (result) adf_latin1_to_utf8(raw, size, result, size * 2U + 1U);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Directory walk                                                          */

static void adf_member_cleanup(adf_member *member)
{
    if (member->leaf) xx_mem_free(member->leaf);
    if (member->comment) xx_mem_free(member->comment);
    if (member->link_target) xx_mem_free(member->link_target);
    xx_mem_zero(member, sizeof(*member));
}

static void adf_stream_free(void *opaque)
{
    adf_stream *stream = (adf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) adf_member_cleanup(&stream->items[index]);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static size_t adf_text_size(const char *text)
{
    return text ? xx_str_len(text) + 1U : 0U;
}

/* Append @p member; false when a limit (members or text bytes) is reached,
 * which ends the listing, or when memory runs out. */
static bool adf_add_member(adf_stream *stream, adf_member *member)
{
    uint64_t text =
        (uint64_t)sizeof(*member) + 2U * sizeof(uint32_t) * 2U + adf_text_size(member->leaf) + adf_text_size(member->comment) + adf_text_size(member->link_target);
    if (stream->count >= ADF_MAX_MEMBERS || text > ADF_MAX_TEXT_BYTES - stream->text_bytes) return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 64U;
        adf_member *grown;
        if (capacity > ADF_MAX_MEMBERS) capacity = ADF_MAX_MEMBERS;
        grown = (adf_member *)xx_mem_realloc(stream->items, capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    stream->text_bytes += text;
    return true;
}

static bool adf_listing_full(const adf_stream *stream)
{
    return stream->count >= ADF_MAX_MEMBERS || stream->text_bytes + (uint64_t)sizeof(adf_member) + 1024U > ADF_MAX_TEXT_BYTES;
}

static int64_t adf_timestamp(const uint8_t *field)
{
    uint32_t days = xx_data_get_u32(field, 4, 0, true), minutes = xx_data_get_u32(field + 4U, 4, 0, true), ticks = xx_data_get_u32(field + 8U, 4, 0, true);
    if (minutes >= 1440U || ticks >= 3000U || days > 0x7FFFFFU) return -1;
    return ADF_EPOCH + (int64_t)days * 86400 + (int64_t)minutes * 60 + (int64_t)(ticks / 50U);
}

/* A header block of the tree: type 2, its own number as key, the expected
 * secondary type set, a matching checksum.  A directory may carry a stale
 * key: disks whose directory blocks were moved by a copier keep the old
 * number there (TOSEC "Amiga Disk Magazine 031": 21 directories, every
 * parent and hash link correct, ADFlib reads all 103 files).  The caller
 * still requires the parent field to name the directory being walked. */
static bool adf_entry_ok(const uint8_t *block, uint32_t number)
{
    uint32_t secondary = xx_data_get_u32(block + ADF_OFF_SEC_TYPE, 4, 0, true);
    return xx_data_get_u32(block + ADF_OFF_TYPE, 4, 0, true) == ADF_T_HEADER &&
           (xx_data_get_u32(block + ADF_OFF_KEY, 4, 0, true) == number || secondary == ADF_ST_USERDIR) &&
           (secondary == ADF_ST_USERDIR || secondary == ADF_ST_FILE || secondary == ADF_ST_SOFTLINK || secondary == ADF_ST_LINKFILE || secondary == ADF_ST_LINKDIR) &&
           adf_checksum_ok(block);
}

static bool adf_file_header_ok(const adf_volume *volume, uint32_t number, uint8_t *block)
{
    return adf_pointer_ok(volume, number) && adf_read_block(volume, number, block) && xx_data_get_u32(block + ADF_OFF_TYPE, 4, 0, true) == ADF_T_HEADER &&
           xx_data_get_u32(block + ADF_OFF_KEY, 4, 0, true) == number && xx_data_get_u32(block + ADF_OFF_SEC_TYPE, 4, 0, true) == ADF_ST_FILE && adf_checksum_ok(block);
}

/* Name, comment and date of one entry header. */
static bool adf_entry_text(const adf_volume *volume, const uint8_t *block, uint32_t number, char **leaf, char **comment, int64_t *timestamp)
{
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
            uint32_t comment_block = xx_data_get_u32(block + ADF_LN_COMMENT_BLOCK, 4, 0, true);
            uint8_t extra[ADF_BSIZE];
            if (comment_block != 0U && adf_pointer_ok(volume, comment_block) && adf_read_block(volume, comment_block, extra) &&
                xx_data_get_u32(extra, 4, 0, true) == ADF_T_COMMENT && xx_data_get_u32(extra + 4U, 4, 0, true) == comment_block &&
                xx_data_get_u32(extra + 8U, 4, 0, true) == number && adf_checksum_ok(extra) && extra[24] != 0U && extra[24] <= ADF_COMMENT_MAX)
                *comment = adf_utf8_copy(extra + 25U, extra[24]);
        }
        *timestamp = adf_timestamp(block + ADF_LN_DATE);
    } else {
        name_size = block[ADF_OFF_NAME];
        if (name_size > ADF_NAME_MAX) return false;
        name = block + ADF_OFF_NAME + 1U;
        if (block[ADF_OFF_COMMENT] != 0U && block[ADF_OFF_COMMENT] <= ADF_COMMENT_MAX) *comment = adf_utf8_copy(block + ADF_OFF_COMMENT + 1U, block[ADF_OFF_COMMENT]);
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

/* Blank or foreign boot blocks leave OFS or FFS open.  The first file with data settles
 * it: an OFS data block is tagged (type 8, owning header, sequence 1, a size
 * that fits, a zero checksum), a raw FFS block essentially never is. */
static void adf_settle_type(adf_volume *volume, uint32_t header, uint32_t first)
{
    uint8_t data[ADF_BSIZE];
    uint32_t size;
    bool ofs;
    if (volume->type_known || !adf_pointer_ok(volume, first) || first == volume->root || first == header || !adf_read_block(volume, first, data)) return;
    size = xx_data_get_u32(data + 12U, 4, 0, true);
    ofs = xx_data_get_u32(data, 4, 0, true) == ADF_T_DATA && xx_data_get_u32(data + 4U, 4, 0, true) == header && xx_data_get_u32(data + 8U, 4, 0, true) == 1U &&
          size != 0U && size <= ADF_OFS_PAYLOAD && adf_checksum_ok(data);
    volume->ffs = !ofs;
    volume->dos_type = ofs ? 0U : 1U;
    volume->type_known = true;
}

static bool adf_visit(uint8_t *visited, uint32_t block)
{
    uint8_t bit = (uint8_t)(1U << (block & 7U));
    if (visited[block >> 3U] & bit) return false;
    visited[block >> 3U] |= bit;
    return true;
}

static void adf_load_table(adf_frame *frame, const uint8_t *block)
{
    size_t index;
    for (index = 0U; index < ADF_HT_SIZE; ++index) frame->table[index] = xx_data_get_u32(block + ADF_OFF_TABLE + index * 4U, 4, 0, true);
    frame->slot = 0U;
    frame->next = 0U;
}

/* ---------------------------------------------------------------------- */
/* Disks without a filesystem                                              */

/* A floppy without a root block keeps its layout in its loader code, so
 * there is no directory to read (9,591 of the TOSEC 2017 ADF images).  What
 * can be recovered exactly are files whose own header gives their full
 * size and that lie on the disk in one piece: ProTracker modules, Amiga
 * load files and IFF files.  Each is taken only after a complete structure
 * check, never on its signature alone.
 *
 * ---- ProTracker modules ----
 * Its
 * 1084-byte header holds the title, the pattern table and all 31 sample
 * lengths, which give the module's full size, and with no filesystem the
 * module lies on the disk in one piece.  A module is taken only when the
 * whole header, the size and the pattern data check out:
 *   - tag M.K., M!K!, FLT4 or 4CHN at 1080;
 *   - every sample: finetune <= 15, volume <= 64, loop inside the sample;
 *   - at least one sample, song length 1..128, pattern numbers <= 63 (127
 *     for M!K!);
 *   - the module ends inside the image;
 *   - pattern cells: sample number <= 31, period 0 or within 8 of a
 *     ProTracker period, at most 1% of the cells wrong, at least one note.
 * Measured on those 9,591 images: 3,107 modules on 1,400 disks.  Modules
 * that a game stores crunched or split are not found. */
#define ADF_MOD_TITLE 20U
#define ADF_MOD_SAMPLES 31U
#define ADF_MOD_SAMPLE 30U
#define ADF_MOD_LENGTH 950U
#define ADF_MOD_TABLE 952U
#define ADF_MOD_TAG 1080U
#define ADF_MOD_HEADER 1084U
#define ADF_MOD_PATTERN 1024U

static bool adf_mod_period_ok(unsigned period)
{
    static const uint16_t base[36] = {856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320,
                                      302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113};
    size_t index;
    for (index = 0U; index < sizeof(base) / sizeof(base[0]); ++index) {
        unsigned distance = period > base[index] ? period - base[index] : base[index] - period;
        if (distance <= 8U) return true;
    }
    return false;
}

static bool adf_mod_tag(const uint8_t *tag)
{
    return !xx_rt_memcmp(tag, "M.K.", 4U) || !xx_rt_memcmp(tag, "M!K!", 4U) || !xx_rt_memcmp(tag, "FLT4", 4U) || !xx_rt_memcmp(tag, "4CHN", 4U);
}

/* Byte size of the module that starts at @p mod, 0 when it is not one.
 * @p limit is the number of bytes left in the image. */
static uint32_t adf_mod_size(const uint8_t *mod, size_t limit)
{
    uint32_t samples = 0U, size, cells, index, bad = 0U, notes = 0U;
    unsigned used = 0U, patterns = 0U, top;
    if (limit < ADF_MOD_HEADER || !adf_mod_tag(mod + ADF_MOD_TAG)) return 0U;
    for (index = 0U; index < ADF_MOD_SAMPLES; ++index) {
        const uint8_t *sample = mod + ADF_MOD_TITLE + ADF_MOD_SAMPLE * index + 22U;
        uint32_t length = ((uint32_t)sample[0] << 8) | sample[1];
        uint32_t start = ((uint32_t)sample[4] << 8) | sample[5];
        uint32_t loop = ((uint32_t)sample[6] << 8) | sample[7];
        if (sample[2] > 15U || sample[3] > 64U) return 0U;
        if (loop > 1U && start + loop > length + 1U) return 0U;
        if (length > 1U) ++used;
        samples += length * 2U;
    }
    if (used == 0U || mod[ADF_MOD_LENGTH] < 1U || mod[ADF_MOD_LENGTH] > 128U) return 0U;
    top = !xx_rt_memcmp(mod + ADF_MOD_TAG, "M!K!", 4U) ? 127U : 63U;
    for (index = 0U; index < 128U; ++index) {
        unsigned number = mod[ADF_MOD_TABLE + index];
        if (number > top) return 0U;
        if (number > patterns) patterns = number;
    }
    ++patterns;
    size = ADF_MOD_HEADER + (uint32_t)patterns * ADF_MOD_PATTERN + samples;
    if ((size_t)size > limit) return 0U;
    cells = (uint32_t)patterns * (ADF_MOD_PATTERN / 4U);
    for (index = 0U; index < cells; ++index) {
        const uint8_t *cell = mod + ADF_MOD_HEADER + (size_t)index * 4U;
        unsigned period;
        if (cell[0] & 0xE0U) {
            ++bad;
            continue;
        }
        period = ((unsigned)(cell[0] & 0x0FU) << 8) | cell[1];
        if (period != 0U) {
            ++notes;
            if (!adf_mod_period_ok(period)) ++bad;
        }
    }
    if (notes == 0U || bad * 100U > cells) return 0U;
    return size;
}

/* ---- Amiga load files ("hunk" executables) ---- */

/* A load file starts with HUNK_HEADER: no resident library names, the hunk
 * count, first = 0, last = count - 1 and one size per hunk.  Each hunk then
 * follows as one CODE, DATA or BSS block that fits its announced size, with
 * optional relocation, symbol and debug blocks, closed by HUNK_END.  Every
 * relocation must name an existing hunk and an offset inside this one, so a
 * file is taken only when it parses to the last HUNK_END and holds code.
 * Overlay files are not taken (their size is not in the header).  Measured
 * on the 8,196 TOSEC images without a filesystem or module: 8,142 load
 * files on 2,904 disks; amitools parses all but 79 of them, and fails on
 * those only for a HUNK_DEBUG shorter than its own header. */
#define ADF_HUNK_NAME UINT32_C(0x3E8)
#define ADF_HUNK_CODE UINT32_C(0x3E9)
#define ADF_HUNK_DATA UINT32_C(0x3EA)
#define ADF_HUNK_BSS UINT32_C(0x3EB)
#define ADF_HUNK_RELOC32 UINT32_C(0x3EC)
#define ADF_HUNK_SYMBOL UINT32_C(0x3F0)
#define ADF_HUNK_DEBUG UINT32_C(0x3F1)
#define ADF_HUNK_END UINT32_C(0x3F2)
#define ADF_HUNK_HEADER UINT32_C(0x3F3)
#define ADF_HUNK_DREL32 UINT32_C(0x3F7)
#define ADF_HUNK_RELOC32SHORT UINT32_C(0x3FC)
#define ADF_HUNK_MAX 4096U
#define ADF_HUNK_MAX_BYTES (UINT64_C(16) * 1024U * 1024U)

static bool adf_room(size_t limit, size_t at, uint64_t need)
{
    return at <= limit && need <= (uint64_t)(limit - at);
}

static bool adf_be32(const uint8_t *data, size_t limit, size_t at, uint32_t *value)
{
    if (!adf_room(limit, at, 4U)) return false;
    *value = ((uint32_t)data[at] << 24) | ((uint32_t)data[at + 1U] << 16) | ((uint32_t)data[at + 2U] << 8) | data[at + 3U];
    return true;
}

static bool adf_be16(const uint8_t *data, size_t limit, size_t at, uint32_t *value)
{
    if (!adf_room(limit, at, 2U)) return false;
    *value = ((uint32_t)data[at] << 8) | data[at + 1U];
    return true;
}

/* Byte size of the load file that starts at @p data, 0 when it is not one.
 * @p sizes is scratch for ADF_HUNK_MAX entries. */
static uint32_t adf_hunk_size(const uint8_t *data, size_t limit, uint32_t *sizes)
{
    uint32_t value, count, first, last, index, entry;
    uint64_t sum = 0U;
    size_t at;
    bool code = false;
    if (!adf_be32(data, limit, 0U, &value) || value != ADF_HUNK_HEADER || !adf_be32(data, limit, 4U, &value) || value != 0U) return 0U;
    if (!adf_be32(data, limit, 8U, &count) || !adf_be32(data, limit, 12U, &first) || !adf_be32(data, limit, 16U, &last)) return 0U;
    if (first != 0U || count < 1U || count > ADF_HUNK_MAX || last != count - 1U) return 0U;
    at = 20U;
    for (index = 0U; index < count; ++index) {
        if (!adf_be32(data, limit, at, &value)) return 0U;
        at += 4U;
        if ((value >> 30) == 3U) { /* extended memory attributes */
            if (!adf_room(limit, at, 4U)) return 0U;
            at += 4U;
        }
        sizes[index] = (value & UINT32_C(0x3FFFFFFF)) * 4U;
        sum += sizes[index];
    }
    if (sum == 0U || sum > ADF_HUNK_MAX_BYTES) return 0U;
    for (index = 0U; index < count; ++index) {
        bool seen = false, closed = false;
        while (!closed) {
            uint32_t type, length, target;
            if (!adf_be32(data, limit, at, &type)) return 0U;
            type &= UINT32_C(0x3FFFFFFF);
            at += 4U;
            if (type == ADF_HUNK_NAME || type == ADF_HUNK_DEBUG) {
                if (!adf_be32(data, limit, at, &length) || length > UINT32_C(0x40000) || !adf_room(limit, at + 4U, (uint64_t)length * 4U)) return 0U;
                at += 4U + (size_t)length * 4U;
            } else if (type == ADF_HUNK_CODE || type == ADF_HUNK_DATA) {
                if (seen || !adf_be32(data, limit, at, &length) || (uint64_t)length * 4U > sizes[index] || !adf_room(limit, at + 4U, (uint64_t)length * 4U)) return 0U;
                if (type == ADF_HUNK_CODE && length != 0U) code = true;
                at += 4U + (size_t)length * 4U;
                seen = true;
            } else if (type == ADF_HUNK_BSS) {
                if (seen || !adf_be32(data, limit, at, &length) || (uint64_t)length * 4U > sizes[index]) return 0U;
                at += 4U;
                seen = true;
            } else if (type == ADF_HUNK_RELOC32) {
                if (!seen) return 0U;
                for (;;) {
                    if (!adf_be32(data, limit, at, &length)) return 0U;
                    at += 4U;
                    if (length == 0U) break;
                    if (!adf_be32(data, limit, at, &target) || target >= count || length > sizes[index] / 4U || !adf_room(limit, at + 4U, (uint64_t)length * 4U)) return 0U;
                    at += 4U;
                    for (entry = 0U; entry < length; ++entry) {
                        (void)adf_be32(data, limit, at + (size_t)entry * 4U, &value);
                        if ((uint64_t)value + 4U > sizes[index]) return 0U;
                    }
                    at += (size_t)length * 4U;
                }
            } else if (type == ADF_HUNK_RELOC32SHORT || type == ADF_HUNK_DREL32) {
                size_t start = at;
                if (!seen) return 0U;
                for (;;) {
                    if (!adf_be16(data, limit, at, &length)) return 0U;
                    at += 2U;
                    if (length == 0U) break;
                    if (!adf_room(limit, at, 2U + (uint64_t)length * 2U)) return 0U;
                    (void)adf_be16(data, limit, at, &target);
                    if (target >= count) return 0U;
                    at += 2U;
                    for (entry = 0U; entry < length; ++entry) {
                        (void)adf_be16(data, limit, at + (size_t)entry * 2U, &value);
                        if ((uint64_t)value + 4U > sizes[index]) return 0U;
                    }
                    at += (size_t)length * 2U;
                }
                if ((at - start) & 2U) at += 2U;
            } else if (type == ADF_HUNK_SYMBOL) {
                if (!seen) return 0U;
                for (;;) {
                    if (!adf_be32(data, limit, at, &length)) return 0U;
                    at += 4U;
                    if (length == 0U) break;
                    length &= UINT32_C(0xFFFFFF);
                    if (length > 64U || !adf_room(limit, at, (uint64_t)length * 4U + 4U)) return 0U;
                    at += (size_t)length * 4U + 4U;
                }
            } else if (type == ADF_HUNK_END) {
                if (!seen) return 0U;
                closed = true;
            } else {
                return 0U;
            }
        }
    }
    return code && at <= limit ? (uint32_t)at : 0U;
}

/* ---- IFF files ---- */

/* "FORM", a big-endian size and a type, then chunks (id, size, data padded
 * to an even length) that must end exactly at the FORM's size.  Only known
 * Amiga types are taken, with at least two chunks; a picture must have a
 * 20-byte BMHD with sane dimensions and a BODY, a sample a 20-byte VHDR and
 * a BODY, an animation an inner FORM.  Measured on the same 8,196 images:
 * 12,716 files on 1,147 disks (6,344 8SVX, 6,040 ILBM, 290 ANIM). */
typedef struct adf_iff_type_s {
    const char *id;
    const char *stem;
    const char *extension;
} adf_iff_type;

static const adf_iff_type adf_iff_types[] = {
    {"ILBM", "picture", ".iff"}, {"PBM ", "picture", ".iff"}, {"ACBM", "picture", ".iff"}, {"DEEP", "picture", ".iff"}, {"RGBN", "picture", ".iff"},
    {"RGB8", "picture", ".iff"}, {"ANIM", "anim", ".anim"},   {"8SVX", "sound", ".8svx"},  {"16SV", "sound", ".8svx"},  {"MAUD", "sound", ".maud"},
    {"AIFF", "sound", ".aiff"},  {"AIFC", "sound", ".aiff"},  {"SMUS", "score", ".smus"},  {"FTXT", "text", ".iff"}};

/* Byte size of the IFF file at @p data, 0 when it is not one.  @p type is
 * its table entry; @p name / @p name_size the first NAME chunk, if any. */
static uint32_t adf_iff_size(const uint8_t *data, size_t limit, const adf_iff_type **type, const uint8_t **name, size_t *name_size)
{
    uint32_t size, length, count = 0U;
    size_t at, end, index, header = 0U, header_size = 0U, body_size = 0U;
    bool body = false, inner = false, picture, sample;
    *type = NULL;
    *name = NULL;
    *name_size = 0U;
    if (limit < 12U || xx_rt_memcmp(data, "FORM", 4U) != 0) return 0U;
    for (index = 0U; index < sizeof(adf_iff_types) / sizeof(adf_iff_types[0]); ++index)
        if (!xx_rt_memcmp(data + 8U, adf_iff_types[index].id, 4U)) *type = &adf_iff_types[index];
    if (!*type || !adf_be32(data, limit, 4U, &size) || size < 12U || !adf_room(limit, 8U, size)) return 0U;
    end = 8U + (size_t)size;
    at = 12U;
    picture = !xx_rt_memcmp(data + 8U, "ILBM", 4U) || !xx_rt_memcmp(data + 8U, "PBM ", 4U);
    sample = !xx_rt_memcmp(data + 8U, "8SVX", 4U);
    while (at < end) {
        const uint8_t *id = data + at;
        if (end - at < 8U) return 0U;
        for (index = 0U; index < 4U; ++index)
            if (id[index] < 0x20U || id[index] > 0x7EU) return 0U;
        if (id[0] == 0x20U) return 0U;
        (void)adf_be32(data, limit, at + 4U, &length);
        if ((uint64_t)length > (uint64_t)(end - at - 8U)) return 0U;
        if (!xx_rt_memcmp(id, picture ? "BMHD" : "VHDR", 4U) && header == 0U) {
            header = at + 8U;
            header_size = length;
        } else if (!xx_rt_memcmp(id, "BODY", 4U) && !body) {
            body = true;
            body_size = length;
        } else if (!xx_rt_memcmp(id, "FORM", 4U)) {
            inner = true;
        } else if (!xx_rt_memcmp(id, "NAME", 4U) && *name_size == 0U) {
            size_t used = 0U, most = length < 40U ? length : 40U;
            while (used < most && data[at + 8U + used] != 0U) ++used;
            *name = data + at + 8U;
            *name_size = used;
        }
        ++count;
        at += 8U + (size_t)length + (length & 1U);
        if (at == end + 1U) at = end; /* odd last chunk, pad byte not counted */
    }
    if (count < 2U) return 0U;
    if (picture) {
        uint32_t width, height;
        if (header == 0U || header_size != 20U || !body || body_size == 0U) return 0U;
        (void)adf_be16(data, limit, header, &width);
        (void)adf_be16(data, limit, header + 2U, &height);
        if (width < 1U || width > 4096U || height < 1U || height > 4096U || data[header + 8U] < 1U || data[header + 8U] > 32U) return 0U;
    }
    if (sample && (header == 0U || header_size != 20U || !body || body_size == 0U)) return 0U;
    if (!xx_rt_memcmp(data + 8U, "ANIM", 4U) && !inner) return 0U;
    return size + 8U;
}

/* ---- Members ---- */

#define ADF_CARVE_NAME 64U

static size_t adf_carve_text(char *raw, size_t size, const char *text)
{
    while (*text) raw[size++] = *text++;
    return size;
}

/* "<stem>_<offset as six hex digits>" */
static size_t adf_carve_offset_name(char *raw, const char *stem, uint32_t offset)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t size = adf_carve_text(raw, 0U, stem), index;
    raw[size++] = '_';
    for (index = 0U; index < 6U; ++index) raw[size++] = digits[(offset >> (20U - 4U * (unsigned)index)) & 0x0FU];
    return size;
}

/* Copy a title of at most @p size bytes without its outer blanks; 0 when it
 * is empty, longer than 30 or not plain ASCII. */
static size_t adf_carve_title(char *raw, const uint8_t *title, size_t size)
{
    size_t first = 0U, index;
    while (first < size && (title[first] == ' ' || (title[first] >= 9U && title[first] <= 13U))) ++first;
    while (size > first && (title[size - 1U] == ' ' || (title[size - 1U] >= 9U && title[size - 1U] <= 13U))) --size;
    if (size == first || size - first > 30U) return 0U;
    for (index = first; index < size; ++index) {
        if (title[index] < 0x20U || title[index] > 0x7EU) return 0U;
        raw[index - first] = (char)title[index];
    }
    return size - first;
}

/* Add one carved run of bytes under the name @p raw (not terminated).  A
 * crunched file is @p packed bytes on the disk and @p length bytes once
 * @p crunch is undone. */
static bool adf_carve_add(adf_stream *stream, adf_names *names, const char *raw, size_t size, uint32_t offset, uint32_t length, unsigned crunch, uint32_t packed)
{
    adf_member member;
    char *leaf;
    uint32_t hash = 0U;
    leaf = adf_component((const uint8_t *)raw, size);
    if (!leaf) return false;
    xx_mem_zero(&member, sizeof(member));
    member.kind = ADF_KIND_FILE;
    member.raw = true;
    member.raw_offset = offset;
    member.crunch = (uint8_t)crunch;
    member.packed = packed;
    member.header = offset / ADF_BSIZE;
    member.size = length;
    member.parent = ADF_NO_PARENT;
    member.timestamp = -1;
    member.leaf = adf_claim(names, stream->items, ADF_NO_PARENT, leaf, &hash);
    xx_mem_free(leaf);
    if (!member.leaf) return false;
    if (!adf_add_member(stream, &member)) {
        adf_member_cleanup(&member);
        return false;
    }
    return adf_names_add(names, (uint32_t)(stream->count - 1U), hash);
}

/* "<title>.mod"; a module without a clean ASCII title is named after its
 * offset. */
static size_t adf_mod_name(char *raw, const uint8_t *mod, uint32_t offset)
{
    size_t size = 0U;
    while (size < ADF_MOD_TITLE && mod[size] != 0U) ++size;
    size = adf_carve_title(raw, mod, size);
    if (size == 0U) size = adf_carve_offset_name(raw, "mod", offset);
    if (size < 4U || raw[size - 4U] != '.' || adf_upper_ascii(raw[size - 3U]) != 'M' || adf_upper_ascii(raw[size - 2U]) != 'O' || adf_upper_ascii(raw[size - 1U]) != 'D')
        size = adf_carve_text(raw, size, ".mod");
    return size;
}

/* Name a decrunched file after what it holds: a load file, an IFF file or
 * a module is named as if it had been found plain, anything else is
 * "data_<offset>.bin".  @p offset is where the crunched file starts. */
static size_t adf_crunch_name(char *raw, const uint8_t *plain, uint32_t size, uint32_t offset, uint32_t *hunks)
{
    const adf_iff_type *type;
    const uint8_t *title;
    size_t title_size, name_size;
    if (size >= 4U && plain[0] == 0U && plain[1] == 0U && plain[2] == 3U && plain[3] == 0xF3U && adf_hunk_size(plain, size, hunks) != 0U)
        return adf_carve_offset_name(raw, "program", offset);
    if (size >= 4U && !xx_rt_memcmp(plain, "FORM", 4U) && adf_iff_size(plain, size, &type, &title, &title_size) != 0U) {
        name_size = title_size ? adf_carve_title(raw, title, title_size) : 0U;
        if (name_size == 0U) name_size = adf_carve_offset_name(raw, type->stem, offset);
        return adf_carve_text(raw, name_size, type->extension);
    }
    if (adf_mod_size(plain, size) != 0U) return adf_mod_name(raw, plain, offset);
    return adf_carve_text(raw, adf_carve_offset_name(raw, "data", offset), ".bin");
}

/* List what can be recovered from a DD/HD floppy image without a root
 * block: ProTracker modules ("<title>.mod"), Amiga load files
 * ("program_<offset>", no extension, as on the Amiga), IFF files (named
 * by their NAME chunk or "<kind>_<offset>", with .iff/.8svx/.anim/...) and
 * crunched data files (PowerPacker, ProPack, Crunch-Mania, Imploder), which
 * are listed decrunched and named after their content.  With no @p stream
 * it only answers whether there is anything.  Files of one kind do not
 * overlap: the scan for that kind resumes behind each file it accepts.
 * Kinds may overlap, so a module or a picture linked into a program is
 * listed as well as the program.  Measured on the 4,566 TOSEC images left
 * without a filesystem, module, load file or IFF file: see
 * xx_adf_crunch.h for what a crunched file must pass. */
static bool adf_carve(Abstractformat *format, adf_stream *stream, adf_names *names)
{
    int64_t total, size;
    uint8_t *image;
    uint32_t *hunks = NULL;
    adfc_state crunch_state;
    size_t at, limit, next_mod = 0U, next_hunk = 0U, next_iff = 0U, next_crunch = 0U;
    bool found = false, failed = false, stop = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* A standard floppy or its 81..83-cylinder overdump. */
    if (size % (int64_t)ADF_BSIZE != 0 || size > (int64_t)ADF_HD_BLOCKS * ADF_BSIZE * 2 ||
        (!adf_is_variant((uint32_t)(size / (int64_t)ADF_BSIZE), ADF_DD_BLOCKS) && !adf_is_variant((uint32_t)(size / (int64_t)ADF_BSIZE), ADF_HD_BLOCKS)))
        return false;
    limit = (size_t)size;
    image = (uint8_t *)xx_mem_alloc(limit);
    hunks = (uint32_t *)xx_mem_alloc(ADF_HUNK_MAX * sizeof(*hunks));
    if (!image || !hunks || !adf_read_at(format->device, format->base_address, image, limit)) {
        if (image) xx_mem_free(image);
        if (hunks) xx_mem_free(hunks);
        return false;
    }
    xx_mem_zero(&crunch_state, sizeof(crunch_state));
    for (at = 0U; !stop && at + 4U <= limit; ++at) {
        char raw[ADF_CARVE_NAME];
        size_t name_size = 0U;
        uint32_t start = 0U, length = 0U, packed = 0U;
        unsigned crunch = ADFC_NONE;
        uint8_t byte = image[at];
        if (byte == 0U) {
            if (at >= next_hunk && image[at + 1U] == 0U && image[at + 2U] == 3U && image[at + 3U] == 0xF3U && (length = adf_hunk_size(image + at, limit - at, hunks)) != 0U) {
                start = (uint32_t)at;
                next_hunk = at + length;
                name_size = adf_carve_offset_name(raw, "program", start);
            }
        } else if (byte == 'F' && at >= next_iff && !xx_rt_memcmp(image + at, "FORM", 4U)) {
            const adf_iff_type *type;
            const uint8_t *title;
            size_t title_size;
            if ((length = adf_iff_size(image + at, limit - at, &type, &title, &title_size)) != 0U) {
                start = (uint32_t)at;
                next_iff = at + length;
                name_size = title_size ? adf_carve_title(raw, title, title_size) : 0U;
                if (name_size == 0U) name_size = adf_carve_offset_name(raw, type->stem, start);
                name_size = adf_carve_text(raw, name_size, type->extension);
            }
        }
        if (length == 0U && at >= ADF_MOD_TAG && at - ADF_MOD_TAG >= next_mod && (byte == 'M' || byte == 'F' || byte == '4') &&
            (length = adf_mod_size(image + at - ADF_MOD_TAG, limit - (at - ADF_MOD_TAG))) != 0U) {
            start = (uint32_t)(at - ADF_MOD_TAG);
            next_mod = (size_t)start + length;
            name_size = adf_mod_name(raw, image + start, start);
        }
        if (length == 0U && at >= next_crunch && adfc_tag(image + at) != ADFC_NONE) {
            uint8_t *plain = NULL;
            if ((packed = adfc_open(image + at, limit - at, &crunch, &plain, &length, &crunch_state)) != 0U) {
                start = (uint32_t)at;
                next_crunch = at + packed;
                name_size = adf_crunch_name(raw, plain, length, start, hunks);
                xx_mem_free(plain);
            } else {
                crunch = ADFC_NONE;
                length = 0U;
            }
        }
        if (length == 0U) continue;
        found = true;
        if (!stream) break;
        if (!adf_carve_add(stream, names, raw, name_size, start, length, crunch, packed)) {
            /* A full listing ends here; anything else (memory) fails the
             * open instead of passing for a complete listing. */
            failed = !adf_listing_full(stream);
            stop = true;
        }
    }
    xx_mem_free(image);
    xx_mem_free(hunks);
    if (crunch_state.scratch) xx_mem_free(crunch_state.scratch);
    if (found && stream && stream->volume.root != 0U) {
        stream->volume.carved = true; /* an empty volume: keep its name and root */
    } else if (found && stream) {
        adf_volume *volume = &stream->volume;
        xx_mem_zero(volume, sizeof(*volume));
        volume->device = format->device;
        volume->base = format->base_address;
        volume->blocks = (uint32_t)(size / (int64_t)ADF_BSIZE);
        volume->type_known = true;
        volume->carved = true;
        volume->format_size = size;
    }
    return found && !failed && (!stream || stream->count != 0U);
}

/* Build the member list.  Damaged entries end their hash chain (its next
 * pointer cannot be trusted) but do not end the walk. */
static bool adf_parse(Abstractformat *format, adf_stream **result, xx_pd_struct *pd)
{
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
    if (!adf_open_volume(format, &stream->volume, root)) {
        if (!adf_carve(format, stream, &names)) goto done;
        stream->emit_budget = stream->volume.format_size * ADF_EMIT_FACTOR + ADF_EMIT_SLACK;
        ok = true;
        goto done;
    }
    visited = (uint8_t *)xx_mem_calloc(((size_t)stream->volume.blocks + 7U) / 8U, 1U);
    frames = (adf_frame *)xx_mem_alloc(ADF_MAX_DEPTH * sizeof(*frames));
    if (!visited || !frames) goto done;
    adf_visit(visited, 0U);
    adf_visit(visited, 1U);
    adf_visit(visited, stream->volume.root);
    frames[0].block = stream->volume.root;
    frames[0].member = ADF_NO_PARENT;
    adf_load_table(&frames[0], root);
    depth = 1U;
    while (depth != 0U) {
        adf_frame *frame = &frames[depth - 1U];
        adf_member member;
        uint32_t number, secondary, hash = 0U;
        char *leaf = NULL;
        if ((++steps & 0xFFUL) == 0UL && pd && xx_pd_is_stopped(pd)) goto done;
        if (frame->next == 0U) {
            while (frame->slot < ADF_HT_SIZE && frame->table[frame->slot] == 0U) ++frame->slot;
            if (frame->slot >= ADF_HT_SIZE) {
                --depth;
                continue;
            }
            frame->next = frame->table[frame->slot++];
        }
        number = frame->next;
        frame->next = 0U;
        if (!adf_pointer_ok(&stream->volume, number) || !adf_visit(visited, number) || !adf_read_block(&stream->volume, number, block) || !adf_entry_ok(block, number) ||
            xx_data_get_u32(block + ADF_OFF_PARENT, 4, 0, true) != frame->block)
            continue;
        frame->next = xx_data_get_u32(block + ADF_OFF_HASH_CHAIN, 4, 0, true);
        xx_mem_zero(&member, sizeof(member));
        member.header = number;
        member.parent = frame->member;
        member.protect = xx_data_get_u32(block + ADF_OFF_PROTECT, 4, 0, true);
        if (!adf_entry_text(&stream->volume, block, number, &leaf, &member.comment, &member.timestamp)) continue;
        secondary = xx_data_get_u32(block + ADF_OFF_SEC_TYPE, 4, 0, true);
        if (secondary == ADF_ST_USERDIR || secondary == ADF_ST_LINKDIR) {
            member.kind = ADF_KIND_FOLDER;
        } else if (secondary == ADF_ST_FILE) {
            member.kind = ADF_KIND_FILE;
            member.data = number;
            member.first = xx_data_get_u32(block + ADF_OFF_TABLE_LAST, 4, 0, true);
            member.size = xx_data_get_u32(block + ADF_OFF_BYTE_SIZE, 4, 0, true);
            if (member.size != 0U) adf_settle_type(&stream->volume, number, member.first);
        } else if (secondary == ADF_ST_LINKFILE) {
            uint32_t real = xx_data_get_u32(block + ADF_OFF_REAL_ENTRY, 4, 0, true);
            if (!adf_file_header_ok(&stream->volume, real, target)) {
                xx_mem_free(leaf);
                adf_member_cleanup(&member);
                continue;
            }
            member.kind = ADF_KIND_FILE;
            member.data = real;
            member.first = xx_data_get_u32(target + ADF_OFF_TABLE_LAST, 4, 0, true);
            member.size = xx_data_get_u32(target + ADF_OFF_BYTE_SIZE, 4, 0, true);
        } else {
            size_t length = 0U;
            while (length < ADF_LINK_PATH_MAX && block[ADF_OFF_TABLE + length] != 0U) ++length;
            member.kind = ADF_KIND_SOFTLINK;
            member.link_target = adf_utf8_copy(block + ADF_OFF_TABLE, length);
            if (!member.link_target) {
                xx_mem_free(leaf);
                adf_member_cleanup(&member);
                goto done;
            }
        }
        member.leaf = adf_claim(&names, stream->items, frame->member, leaf, &hash);
        xx_mem_free(leaf);
        if (!member.leaf) {
            adf_member_cleanup(&member);
            continue;
        }
        if (!adf_add_member(stream, &member)) {
            adf_member_cleanup(&member);
            if (adf_listing_full(stream) || stream->count >= ADF_MAX_MEMBERS) break;
            goto done;
        }
        if (!adf_names_add(&names, (uint32_t)(stream->count - 1U), hash)) goto done;
        if (secondary == ADF_ST_USERDIR && depth < ADF_MAX_DEPTH) {
            adf_frame *child = &frames[depth];
            child->block = number;
            child->member = (uint32_t)(stream->count - 1U);
            adf_load_table(child, block);
            ++depth;
        }
    }
    /* An empty volume (a formatted disk a loader then wrote over) may still
     * hold carvable files; an empty listing stays valid when it does not. */
    if (stream->count == 0U) (void)adf_carve(format, stream, &names);
    stream->emit_budget = stream->volume.format_size * ADF_EMIT_FACTOR + ADF_EMIT_SLACK;
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
    size_t capacity;
} adf_sink;

static bool adf_sink_flush(adf_sink *sink)
{
    const size_t file_io_capacity = sink->capacity;
    size_t done = 0U;
    if (!sink->device) {
        sink->used = 0U;
        return true;
    }
    while (done < sink->used) {
        ssize_t wrote = gb_adf_write(sink->device, sink->buffer + done, sink->used - done, file_io_capacity);
        if (wrote <= 0 || (size_t)wrote > sink->used - done) return false;
        done += (size_t)wrote;
    }
    sink->used = 0U;
    return true;
}

static bool adf_sink_put(adf_sink *sink, const uint8_t *data, size_t size)
{
    if (!sink->device) return true;
    while (size) {
        size_t take = sink->capacity - sink->used;
        if (!take) {
            if (!adf_sink_flush(sink)) return false;
            take = sink->capacity;
        }
        if (take > size) take = size;
        xx_rt_memcpy(sink->buffer + sink->used, data, take);
        sink->used += take;
        data += take;
        size -= take;
    }
    return true;
}

/* Copy the bytes of the file whose header is @p header, following the block
 * table of the header and then of each extension block.  Every data block
 * of an OFS file must be a checksummed data block that names this header;
 * an FFS data block carries no tag, so only blocks that cannot hold data
 * (the root, the header, the table being read) are refused.  A revisited
 * extension block is found with Brent's cycle check and fails the copy.
 *
 * The table is the authority on block order, as it is for AmigaDOS: the OFS
 * sequence number and an extension block's parent field are redundant
 * copies that real disks get wrong (a file whose last block is numbered 1
 * and named by the header's first-data field; an extension block whose
 * parent is the previous extension block or 0).  Both are therefore not
 * compared.  Measured on TOSEC 2017-04-23: of 54 disks an independent
 * structure walk found sound, 51 were refused only for these two fields,
 * among them magazine cover disks (Amiga Format 107-112) that shipped this
 * way.  The files have every block checksummed and owned and add up to the
 * header's byte size.  Reference tools differ here: amitools refuses the
 * sequence number, ADFlib follows the next links and writes an empty file. */
static bool adf_copy_file(const adf_volume *volume, uint32_t header, uint64_t expected, adf_sink *sink, xx_pd_struct *pd)
{
    uint8_t table[ADF_BSIZE];
    uint8_t data[ADF_BSIZE];
    uint64_t remaining;
    uint32_t hops = 0U, current = header;
    uint32_t tortoise = header, power = 1U, run = 0U;
    if (!adf_file_header_ok(volume, header, table)) return false;
    remaining = xx_data_get_u32(table + ADF_OFF_BYTE_SIZE, 4, 0, true);
    if (remaining != expected || remaining > (uint64_t)volume->blocks * ADF_BSIZE) return false;
    while (remaining != 0U) {
        uint32_t slot;
        for (slot = 0U; slot < ADF_HT_SIZE && remaining != 0U; ++slot) {
            uint32_t pointer = xx_data_get_u32(table + ADF_OFF_TABLE_LAST - slot * 4U, 4, 0, true);
            size_t take;
            if (!adf_pointer_ok(volume, pointer) || pointer == volume->root || pointer == header || pointer == current || !adf_read_block(volume, pointer, data))
                return false;
            if (volume->ffs) {
                take = remaining < ADF_BSIZE ? (size_t)remaining : ADF_BSIZE;
                if (!adf_sink_put(sink, data, take)) return false;
            } else {
                uint32_t size = xx_data_get_u32(data + 12U, 4, 0, true);
                if (xx_data_get_u32(data, 4, 0, true) != ADF_T_DATA || xx_data_get_u32(data + 4U, 4, 0, true) != header || size == 0U ||
                    size > ADF_OFS_PAYLOAD || !adf_checksum_ok(data))
                    return false;
                take = (uint64_t)size < remaining ? (size_t)size : (size_t)remaining;
                if (!adf_sink_put(sink, data + ADF_OFS_HEADER, take)) return false;
            }
            remaining -= take;
        }
        if (remaining == 0U) break;
        if (pd && xx_pd_is_stopped(pd)) return false;
        /* Extension (list) block: type 16, its own key, file secondary type. */
        current = xx_data_get_u32(table + ADF_OFF_EXTENSION, 4, 0, true);
        if (current == tortoise) return false;
        if (++run == power) {
            tortoise = current;
            power = power < UINT32_C(0x80000000) ? power << 1U : power;
            run = 0U;
        }
        if (++hops > volume->blocks || !adf_pointer_ok(volume, current) || !adf_read_block(volume, current, table) ||
            xx_data_get_u32(table + ADF_OFF_TYPE, 4, 0, true) != ADF_T_LIST || xx_data_get_u32(table + ADF_OFF_KEY, 4, 0, true) != current ||
            xx_data_get_u32(table + ADF_OFF_SEC_TYPE, 4, 0, true) != ADF_ST_FILE || !adf_checksum_ok(table))
            return false;
    }
    return adf_sink_flush(sink);
}

/* Copy one carved run of bytes. */
static bool adf_copy_raw(const adf_volume *volume, uint32_t offset, uint64_t size, adf_sink *sink, xx_pd_struct *pd)
{
    uint8_t data[8U * ADF_BSIZE];
    uint64_t done = 0U;
    if (size > (uint64_t)volume->format_size || (uint64_t)offset > (uint64_t)volume->format_size - size) return false;
    while (done < size) {
        size_t take = size - done < sizeof(data) ? (size_t)(size - done) : sizeof(data);
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!adf_read_at(volume->device, volume->base + (int64_t)offset + (int64_t)done, data, take) || !adf_sink_put(sink, data, take)) return false;
        done += take;
    }
    return adf_sink_flush(sink);
}

/* Decrunch one carved file and hand out its plain bytes. */
static bool adf_copy_crunched(const adf_volume *volume, const adf_member *member, adf_sink *sink, xx_pd_struct *pd)
{
    uint8_t *packed, *plain;
    bool result = false;
    if (member->size == 0U || member->size > ADFC_MAX_RAW || member->packed == 0U || (int64_t)member->packed > volume->format_size ||
        (int64_t)member->raw_offset > volume->format_size - (int64_t)member->packed)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed);
    plain = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (packed && plain && !(pd && xx_pd_is_stopped(pd)) && adf_read_at(volume->device, volume->base + (int64_t)member->raw_offset, packed, member->packed) &&
        adfc_decode(member->crunch, packed, member->packed, plain, (uint32_t)member->size, NULL))
        result = adf_sink_put(sink, plain, (size_t)member->size) && adf_sink_flush(sink);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

/* Copy (or, with no @p destination, verify) one file.  The bytes count
 * against the session's budget, so shared data cannot be emitted without
 * bound. */
static bool adf_emit_file(adf_stream *stream, const adf_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    const adf_volume *volume = &stream->volume;
    adf_sink sink;
    bool result;
    if (member->size > (uint64_t)INT64_MAX || stream->emitted > stream->emit_budget || (int64_t)member->size > stream->emit_budget - stream->emitted) return false;
    stream->emitted += (int64_t)member->size;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    sink.capacity = gb_adf_capacity();
    if (destination) {
        sink.buffer = (uint8_t *)xx_mem_alloc(sink.capacity);
        if (!sink.buffer) return false;
    }
    if (member->crunch != ADFC_NONE) {
        result = adf_copy_crunched(volume, member, &sink, pd);
    } else {
        result = member->raw ? adf_copy_raw(volume, member->raw_offset, member->size, &sink, pd) : adf_copy_file(volume, member->data, member->size, &sink, pd);
    }
    if (sink.buffer) xx_mem_free(sink.buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool adf_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
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

static const xx_var *adf_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool adf_set_record(xx_archive_record *record, const adf_stream *stream, const adf_member *member)
{
    bool folder = member->kind == ADF_KIND_FOLDER;
    uint64_t size = member->kind == ADF_KIND_FILE ? member->size : 0U;
    uint64_t stored = member->crunch != ADFC_NONE ? member->packed : size;
    char *name;
    bool named;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->volume.base + (member->raw ? (int64_t)member->raw_offset : (int64_t)member->header * (int64_t)ADF_BSIZE);
    record->header_size = member->raw ? 0 : ADF_BSIZE;
    record->data_offset = member->kind == ADF_KIND_FILE && size != 0U && adf_pointer_ok(&stream->volume, member->first)
                              ? stream->volume.base + (int64_t)member->first * (int64_t)ADF_BSIZE
                              : record->header_offset;
    record->compressed_size = (int64_t)stored;
    name = adf_member_path(stream->items, stream->count, (uint32_t)(member - stream->items));
    if (!name) return false;
    named = xx_archive_record_set_original_name(record, name);
    xx_mem_free(name);
    if (!named || !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, stored) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, size) || !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->crunch) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->protect) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, folder))
        return false;
    if (member->timestamp >= 0 && !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, (uint64_t)member->timestamp)) return false;
    if (member->comment && member->comment[0] && !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, member->comment)) return false;
    if (member->link_target && !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET, member->link_target)) return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_adf_init(xx_adf *image, xx_io_device *device, int64_t base_address)
{
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
    image->format.get_number_of_archive_records = xx_adf_get_number_of_archive_records;
    image->format.create_archive_records_reading = xx_adf_create_archive_records_reading;
    image->format.get_current_archive_record = xx_adf_get_current_archive_record;
    image->format.unpack_current_archive_record = xx_adf_unpack_current_archive_record;
    image->format.archive_record_move_to_next = xx_adf_archive_record_move_to_next;
    image->format.free_archive_records_reading = xx_adf_free_archive_records_reading;
    image->image_size = -1;
}

xx_adf *xx_adf_create(xx_io_device *device, int64_t base_address)
{
    xx_adf *image = (xx_adf *)xx_mem_alloc(sizeof(*image));
    if (image) xx_adf_init(image, device, base_address);
    return image;
}

void xx_adf_destroy(xx_adf *image)
{
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_adf_free(xx_adf *image)
{
    if (!image) return;
    xx_adf_destroy(image);
    xx_mem_free(image);
}

/* Cheap: the boot tag and one root block.  Only a DD/HD floppy image with
 * no root block is read in full, to look for a carvable file. */
bool xx_adf_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    adf_volume volume;
    (void)pd;
    return adf_open_volume(format, &volume, NULL) || adf_carve(format, NULL, NULL);
}

bool xx_adf_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
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
    xx_rt_memcpy(image->volume_name, stream->volume.volume_name, sizeof(image->volume_name));
    format->number_of_archive_records = stream->count;
    format->format_size = stream->volume.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    adf_stream_free(stream);
    return true;
}

int64_t xx_adf_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_adf_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_adf_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_adf_handle_base_info(format, pd)) ? ((xx_adf *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_adf_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
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
    if (!adf_copy_options(&state->options, options) || (stream->count != 0U && !adf_set_record(&state->current_record, stream, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_adf_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_adf_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    adf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (adf_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = adf_set_record(&state->current_record, stream, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_adf_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    adf_stream *stream;
    const adf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *name = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (adf_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    /* A soft link is not a byte stream; the target is on the record. */
    if (member->kind == ADF_KIND_SOFTLINK) return false;
    path_option = adf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return member->kind == ADF_KIND_FOLDER || adf_emit_file(stream, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    name = adf_member_path(stream->items, stream->count, (uint32_t)stream->index);
    if (!name) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
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
        result = adf_emit_file(stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && member->kind == ADF_KIND_FILE && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (name) xx_mem_free(name);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_adf_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

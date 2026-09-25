/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ACT Apricot PC / Xi raw floppy image (floptool "apricotpc", ".img").
 * xx_act_apricot_pc_xi_raw.h carries the label and BPB field table.
 * Written from the on-disk structure: the three Apricot disks of the
 * reference corpus (APRIDISK images in F:\ARC\ARC\Apricot, decoded to raw
 * sectors) and the MS-DOS 2.x FAT12 layout.  libdsk's Apricot boot-sector
 * converter (LGPL) was read only to confirm the label offsets; no code was
 * taken from it.
 *
 * Recognition - the format has no magic anywhere:
 *   - the image holds at least cylinders * heads * sectors * 512 bytes;
 *   - the label text at +0 is printable ASCII or NUL, the sector size is
 *     512, and the label geometry is 8..18 sectors, 35..86 cylinders and
 *     1..2 heads;
 *   - the BPB at +0x50 says 512-byte sectors, a power-of-two cluster of at
 *     most 64 sectors, 1..32 reserved sectors, 1..2 FATs of 1..16 sectors, a
 *     non-empty root of at most 1024 entries that fills whole sectors, a
 *     total that equals the label geometry exactly, a media byte of 0xF0 or
 *     0xF8..0xFF, and a FAT large enough for its FAT12 cluster count;
 *   - FAT #0 opens with the media byte followed by 0xFF 0xFF.
 * It is meant for the late (magic-less) probe chain: anything under 140 KiB
 * is rejected without a read, anything else costs one 512-byte read and a
 * 3-byte read.  A PC boot sector can never pass (its jump byte 0xEB / 0xE9
 * is not printable), so this reader and the FAT reader never overlap.
 *
 * Members are the files and directories of the FAT12 volume.  Names are the
 * 8.3 names decoded from code page 437 to UTF-8 and joined with '/'.
 * Deleted entries, long-name fragments, volume labels and the "." / ".."
 * entries are skipped, as are entries whose name holds a control byte or a
 * path separator.  A sibling whose name would land on the same file as an
 * earlier sibling on a case-insensitive host is renamed "BASE_<n>.EXT"; so
 * is a later sibling shaped like an NTFS short-name alias ("NAME~1"), which
 * could otherwise open a file an earlier sibling created.  Extraction
 * refuses reserved punctuation, trailing dots or spaces and Windows device
 * names (CON, NUL, COM1, CONIN$, CLOCK$, ...).
 *
 * Hostile input: every directory cluster is read at most once over the
 * whole walk, a file's cluster chain stops at the first revisit, the member
 * count and nesting depth are capped, and nothing is sized from an unchecked
 * field (a volume is at most 86 * 2 * 18 sectors, so the FAT, the root and a
 * cluster are all small).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the format is registered. */
#ifdef ACT_APRICOT_PC_XI_RAW
#define XX_ACT_APRICOT_PC_XI_RAW_FILE_TYPE XX_FILE_TYPE_ACT_APRICOT_PC_XI_RAW
#else
#define XX_ACT_APRICOT_PC_XI_RAW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define APRC_SECTOR 512U
#define APRC_MIN_SPT 8U
#define APRC_MAX_SPT 18U
#define APRC_MIN_CYL 35U
#define APRC_MAX_CYL 86U
#define APRC_MAX_HEADS 2U
#define APRC_MIN_VOLUME \
    ((int64_t)APRC_MIN_CYL * (int64_t)APRC_MIN_SPT * (int64_t)APRC_SECTOR)
#define APRC_MAX_RESERVED 32U
#define APRC_MAX_FAT_SECTORS 16U
#define APRC_MAX_ROOT 1024U
#define APRC_MAX_SPC 64U
#define APRC_FAT12_LIMIT 4085U
#define APRC_ENTRY 32U
#define APRC_MAX_MEMBERS 65536U
#define APRC_MAX_DEPTH 32U
#define APRC_MAX_PATH 4096U
/* An 8.3 name (12 bytes) plus "_" and up to 10 digits of a rename. */
#define APRC_COMPONENT_MAX 32U

#define APRC_ATTR_VOLUME 0x08U
#define APRC_ATTR_DIRECTORY 0x10U
#define APRC_ATTR_LONG_NAME 0x0FU
#define APRC_ATTR_LONG_MASK 0x3FU

#define APRC_ROOT_PARENT 0xFFFFFFFFU

typedef struct aprc_geometry_s {
    int64_t base;
    int64_t volume_size;
    int64_t fat_offset;  /* absolute */
    int64_t root_offset; /* absolute */
    int64_t data_offset; /* absolute offset of cluster 2 */
    uint32_t cylinders;
    uint32_t heads;
    uint32_t spt;
    uint32_t total_sectors;
    uint32_t reserved;
    uint32_t fats;
    uint32_t fat_sectors;
    uint32_t root_entries;
    uint32_t root_sectors;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t cluster_count; /* valid cluster numbers are 2..cluster_count+1 */
    uint8_t media;
    char label[9];
} aprc_geometry;

typedef struct aprc_member_s {
    char *name;           /* '/'-joined UTF-8 path, owned */
    int64_t entry_offset; /* absolute offset of the directory entry */
    int64_t data_offset;  /* absolute offset of the first cluster, or -1 */
    uint32_t first_cluster;
    uint32_t size;
    uint32_t parent;      /* member index, or APRC_ROOT_PARENT */
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t attributes;
    uint8_t key_length;
    uint8_t key[APRC_COMPONENT_MAX]; /* case-folded CP437 component */
    bool folder;
} aprc_member;

typedef struct aprc_volume_s {
    aprc_geometry geometry;
    uint8_t *fat; /* FAT #0 */
    size_t fat_bytes;
    aprc_member *members;
    size_t count;
    size_t capacity;
} aprc_volume;

typedef struct aprc_pending_s {
    uint32_t cluster;
    uint32_t member; /* the directory's own member index */
    uint32_t depth;  /* depth of the entries it holds */
} aprc_pending;

typedef struct aprc_walk_s {
    xx_io_device *device;
    aprc_volume *volume;
    uint8_t *dir_seen; /* clusters already read as directory data */
    aprc_pending *queue;
    size_t queue_head;
    size_t queue_count;
    size_t queue_capacity;
    uint32_t *hash; /* member index + 1, 0 = empty */
    size_t hash_capacity;
    size_t hash_used;
    size_t dir_first; /* first member of the directory being scanned */
    bool full;        /* member cap reached: stop listing */
    xx_pd_struct *pd;
} aprc_walk;

typedef struct aprc_stream_s {
    aprc_volume volume;
    size_t index;
} aprc_stream;

/* Code page 437, 0x80..0xFF, as Unicode. */
static const uint16_t aprc_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint16_t aprc_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t aprc_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static bool aprc_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool aprc_bit_test(const uint8_t *bits, uint32_t index) {
    return (bits[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void aprc_bit_set(uint8_t *bits, uint32_t index) {
    bits[index >> 3U] = (uint8_t)(bits[index >> 3U] | (1U << (index & 7U)));
}

static bool aprc_cluster_is_data(const aprc_geometry *g, uint32_t cluster) {
    return cluster >= 2U && cluster <= g->cluster_count + 1U;
}

static int64_t aprc_cluster_offset(const aprc_geometry *g, uint32_t cluster) {
    return g->data_offset +
           (int64_t)(cluster - 2U) * (int64_t)g->bytes_per_cluster;
}

/* The FAT12 successor of @p cluster, or 0 when it cannot be read.  A value
 * outside 2..cluster_count+1 (free, bad, end of chain, or garbage) ends a
 * chain; callers test that with aprc_cluster_is_data(). */
static uint32_t aprc_fat_next(const aprc_volume *volume, uint32_t cluster) {
    size_t offset = (size_t)cluster + (size_t)cluster / 2U;
    uint32_t value;
    if (!volume->fat || offset + 1U >= volume->fat_bytes) return 0U;
    value = (uint32_t)volume->fat[offset] |
            ((uint32_t)volume->fat[offset + 1U] << 8U);
    return (cluster & 1U) ? (value >> 4U) : (value & 0x0FFFU);
}

/* ---------------------------------------------------------------------- */
/* Label, BPB and FAT signature                                            */

static bool aprc_parse_label(const uint8_t *s, aprc_geometry *g) {
    uint32_t index;
    uint32_t bps;
    uint32_t spc;
    uint32_t meta;
    uint32_t fat_needed;
    uint32_t geometry_total;
    for (index = 0U; index < 8U; ++index) {
        if (s[index] != 0U && (s[index] < 0x20U || s[index] > 0x7EU))
            return false;
        g->label[index] = (char)s[index];
    }
    g->label[8] = '\0';
    if (aprc_le16(s + 0x0E) != APRC_SECTOR) return false;
    g->spt = aprc_le16(s + 0x10);
    g->cylinders = aprc_le32(s + 0x12);
    g->heads = s[0x16];
    if (g->spt < APRC_MIN_SPT || g->spt > APRC_MAX_SPT ||
        g->cylinders < APRC_MIN_CYL || g->cylinders > APRC_MAX_CYL ||
        g->heads == 0U || g->heads > APRC_MAX_HEADS)
        return false;
    geometry_total = g->cylinders * g->heads * g->spt; /* <= 3096 */

    bps = aprc_le16(s + 0x50);
    spc = s[0x52];
    g->reserved = aprc_le16(s + 0x53);
    g->fats = s[0x55];
    g->root_entries = aprc_le16(s + 0x56);
    g->total_sectors = aprc_le16(s + 0x58);
    g->media = s[0x5A];
    g->fat_sectors = aprc_le16(s + 0x5B);
    if (bps != APRC_SECTOR || spc == 0U || spc > APRC_MAX_SPC ||
        (spc & (spc - 1U)) != 0U || g->reserved == 0U ||
        g->reserved > APRC_MAX_RESERVED || g->fats == 0U || g->fats > 2U ||
        g->root_entries == 0U || g->root_entries > APRC_MAX_ROOT ||
        (g->root_entries * APRC_ENTRY) % APRC_SECTOR != 0U ||
        g->total_sectors != geometry_total ||
        (g->media != 0xF0U && g->media < 0xF8U) || g->fat_sectors == 0U ||
        g->fat_sectors > APRC_MAX_FAT_SECTORS)
        return false;
    g->sectors_per_cluster = spc;
    g->bytes_per_cluster = spc * APRC_SECTOR;
    g->root_sectors = g->root_entries * APRC_ENTRY / APRC_SECTOR;
    meta = g->reserved + g->fats * g->fat_sectors + g->root_sectors;
    if (meta >= g->total_sectors) return false;
    g->cluster_count = (g->total_sectors - meta) / spc;
    if (g->cluster_count == 0U || g->cluster_count >= APRC_FAT12_LIMIT)
        return false;
    fat_needed = ((g->cluster_count + 2U) * 3U + 1U) / 2U;
    if (fat_needed > g->fat_sectors * APRC_SECTOR) return false;
    g->volume_size = (int64_t)g->total_sectors * (int64_t)APRC_SECTOR;
    return true;
}

/* Everything the probe needs: the size gate, the label, the BPB and the FAT
 * signature.  One 512-byte read and one 3-byte read at most. */
static bool aprc_open(Abstractformat *self, aprc_geometry *g) {
    uint8_t sector[APRC_SECTOR];
    uint8_t head[3];
    int64_t total;
    xx_mem_zero(g, sizeof(*g));
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        total - self->base_address < APRC_MIN_VOLUME)
        return false;
    if (!aprc_read_at(self->device, self->base_address, sector,
                      sizeof(sector)) ||
        !aprc_parse_label(sector, g))
        return false;
    if (total - self->base_address < g->volume_size) return false;
    g->base = self->base_address;
    g->fat_offset = g->base + (int64_t)g->reserved * (int64_t)APRC_SECTOR;
    g->root_offset = g->fat_offset + (int64_t)g->fats *
                                         (int64_t)g->fat_sectors *
                                         (int64_t)APRC_SECTOR;
    g->data_offset =
        g->root_offset + (int64_t)g->root_sectors * (int64_t)APRC_SECTOR;
    if (!aprc_read_at(self->device, g->fat_offset, head, sizeof(head)))
        return false;
    return head[0] == g->media && head[1] == 0xFFU && head[2] == 0xFFU;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static uint8_t aprc_fold(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 32U);
    switch (c) { /* CP437 lower-case letters whose capital is also in 437 */
    case 0x87U: return 0x80U; /* c cedilla */
    case 0x81U: return 0x9AU; /* u umlaut */
    case 0x82U: return 0x90U; /* e acute */
    case 0x84U: return 0x8EU; /* a umlaut */
    case 0x86U: return 0x8FU; /* a ring */
    case 0x91U: return 0x92U; /* ae */
    case 0x94U: return 0x99U; /* o umlaut */
    case 0xA4U: return 0xA5U; /* n tilde */
    case 0xE5U: return 0xE4U; /* sigma */
    case 0xEDU: return 0xE8U; /* phi */
    default: return c;
    }
}

static uint32_t aprc_hash(uint32_t parent, const uint8_t *key, size_t length) {
    uint32_t h = 2166136261U;
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        h ^= (parent >> (index * 8U)) & 0xFFU;
        h *= 16777619U;
    }
    for (index = 0U; index < length; ++index) {
        h ^= key[index];
        h *= 16777619U;
    }
    return h;
}

static bool aprc_hash_contains(const aprc_walk *walk, uint32_t parent,
                               const uint8_t *key, size_t length) {
    size_t mask = walk->hash_capacity - 1U;
    size_t slot = (size_t)aprc_hash(parent, key, length) & mask;
    size_t probes;
    for (probes = 0U; probes < walk->hash_capacity; ++probes) {
        uint32_t stored = walk->hash[slot];
        const aprc_member *member;
        if (stored == 0U) return false;
        member = &walk->volume->members[stored - 1U];
        if (member->parent == parent && member->key_length == length &&
            xx_rt_memcmp(member->key, key, length) == 0)
            return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

static void aprc_hash_put(aprc_walk *walk, uint32_t member_index) {
    const aprc_member *member = &walk->volume->members[member_index];
    size_t mask = walk->hash_capacity - 1U;
    size_t slot = (size_t)aprc_hash(member->parent, member->key,
                                    member->key_length) & mask;
    while (walk->hash[slot] != 0U) slot = (slot + 1U) & mask;
    walk->hash[slot] = member_index + 1U;
    ++walk->hash_used;
}

/* Keep the table at most half full; members only ever get added. */
static bool aprc_hash_reserve(aprc_walk *walk) {
    uint32_t *old = walk->hash;
    size_t old_capacity = walk->hash_capacity;
    size_t capacity;
    size_t index;
    if (walk->hash && (walk->hash_used + 1U) * 2U <= walk->hash_capacity)
        return true;
    capacity = old ? old_capacity * 2U : 64U;
    walk->hash = (uint32_t *)xx_mem_calloc(capacity, sizeof(uint32_t));
    if (!walk->hash) {
        walk->hash = old;
        return false;
    }
    walk->hash_capacity = capacity;
    walk->hash_used = 0U;
    for (index = 0U; old && index < old_capacity; ++index)
        if (old[index] != 0U) aprc_hash_put(walk, old[index] - 1U);
    if (old) xx_mem_free(old);
    return true;
}

/* "NAME~1" style: a '~' in the base followed only by one or more digits. */
static bool aprc_alias_shape(const uint8_t *base, size_t base_length) {
    size_t tilde = base_length;
    size_t index;
    for (index = 0U; index < base_length; ++index)
        if (base[index] == '~') tilde = index;
    if (tilde + 1U >= base_length) return false;
    for (index = tilde + 1U; index < base_length; ++index)
        if (base[index] < '0' || base[index] > '9') return false;
    return true;
}

static size_t aprc_compose(uint8_t *out, const uint8_t *base, size_t base_length,
                           const uint8_t *ext, size_t ext_length,
                           uint32_t rename) {
    size_t used = 0U;
    char digits[12];
    int count = 0;
    xx_rt_memcpy(out, base, base_length);
    used = base_length;
    if (rename != 0U) {
        count = xx_rt_snprintf(digits, sizeof(digits), "_%u", (unsigned)rename);
        if (count <= 0 || (size_t)count >= sizeof(digits)) return 0U;
        xx_rt_memcpy(out + used, digits, (size_t)count);
        used += (size_t)count;
    }
    if (ext_length != 0U) {
        out[used++] = '.';
        xx_rt_memcpy(out + used, ext, ext_length);
        used += ext_length;
    }
    return used;
}

/* Join @p parent and the CP437 @p component, converting to UTF-8. */
static char *aprc_join(const char *parent, const uint8_t *component,
                       size_t length) {
    size_t parent_length = parent ? xx_str_len(parent) : 0U;
    size_t needed = parent_length + 1U + length * 3U + 1U;
    size_t used = 0U;
    size_t index;
    char *out;
    if (needed > APRC_MAX_PATH) return NULL;
    out = (char *)xx_mem_alloc(needed);
    if (!out) return NULL;
    if (parent_length != 0U) {
        xx_rt_memcpy(out, parent, parent_length);
        used = parent_length;
        out[used++] = '/';
    }
    for (index = 0U; index < length; ++index) {
        uint8_t c = component[index];
        uint32_t u = c < 0x80U ? (uint32_t)c : (uint32_t)aprc_cp437[c - 0x80U];
        if (u < 0x80U) {
            out[used++] = (char)u;
        } else if (u < 0x800U) {
            out[used++] = (char)(0xC0U | (u >> 6U));
            out[used++] = (char)(0x80U | (u & 0x3FU));
        } else {
            out[used++] = (char)(0xE0U | (u >> 12U));
            out[used++] = (char)(0x80U | ((u >> 6U) & 0x3FU));
            out[used++] = (char)(0x80U | (u & 0x3FU));
        }
    }
    out[used] = '\0';
    return out;
}

static char aprc_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool aprc_stem_is(const char *stem, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (!word[index] || aprc_upper(stem[index]) != word[index]) return false;
    return word[length] == '\0';
}

/* One path component, as the host would create it. */
static bool aprc_safe_component(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index;
    size_t stem = 0U;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (aprc_stem_is(name, stem, devices[index])) return false;
    if (stem >= 4U && (aprc_stem_is(name, 3U, "COM") ||
                       aprc_stem_is(name, 3U, "LPT"))) {
        const unsigned char *tail = (const unsigned char *)name + 3U;
        /* COM0..COM9 and the superscript forms Windows also reserves. */
        if (stem == 4U && tail[0] >= '0' && tail[0] <= '9') return false;
        if (stem == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U))
            return false;
    }
    return true;
}

static bool aprc_safe_path(const char *path) {
    size_t start = 0U;
    size_t index = 0U;
    if (!path || !path[0] || path[0] == '/') return false;
    for (;;) {
        char c = path[index];
        if (c == '/' || c == '\0') {
            if (!aprc_safe_component(path + start, index - start)) return false;
            if (c == '\0') return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* ---------------------------------------------------------------------- */
/* Directory walk                                                          */

static void aprc_volume_cleanup(aprc_volume *volume) {
    size_t index;
    if (!volume) return;
    for (index = 0U; index < volume->count; ++index)
        if (volume->members[index].name) xx_str_free(volume->members[index].name);
    if (volume->members) xx_mem_free(volume->members);
    if (volume->fat) xx_mem_free(volume->fat);
    xx_mem_zero(volume, sizeof(*volume));
}

static bool aprc_enqueue(aprc_walk *walk, uint32_t cluster, uint32_t member,
                         uint32_t depth) {
    if (walk->queue_count == walk->queue_capacity) {
        size_t capacity = walk->queue_capacity ? walk->queue_capacity * 2U : 16U;
        aprc_pending *grown;
        if (capacity > APRC_MAX_MEMBERS) capacity = APRC_MAX_MEMBERS;
        if (capacity <= walk->queue_count) return false;
        grown = (aprc_pending *)xx_mem_realloc(walk->queue,
                                               capacity * sizeof(*grown));
        if (!grown) return false;
        walk->queue = grown;
        walk->queue_capacity = capacity;
    }
    walk->queue[walk->queue_count].cluster = cluster;
    walk->queue[walk->queue_count].member = member;
    walk->queue[walk->queue_count].depth = depth;
    ++walk->queue_count;
    return true;
}

/* Handle one 32-byte entry.  Returns false at the end-of-directory marker or
 * on an allocation failure (walk->full is set on the member cap). */
static bool aprc_entry(aprc_walk *walk, const uint8_t *raw, int64_t offset,
                       uint32_t parent, uint32_t depth, bool *failed) {
    aprc_volume *volume = walk->volume;
    const aprc_geometry *g = &volume->geometry;
    uint8_t base[8];
    uint8_t ext[3];
    uint8_t component[APRC_COMPONENT_MAX];
    uint8_t key[APRC_COMPONENT_MAX];
    size_t base_length = 8U;
    size_t ext_length = 3U;
    size_t length = 0U;
    size_t index;
    uint8_t attributes = raw[11];
    uint32_t rename = 0U;
    aprc_member *member;
    char *name;
    const char *parent_name = NULL;

    if (raw[0] == 0x00U) return false; /* nothing follows */
    if (raw[0] == 0xE5U) return true;  /* deleted */
    if ((attributes & APRC_ATTR_LONG_MASK) == APRC_ATTR_LONG_NAME) return true;
    if (attributes & APRC_ATTR_VOLUME) return true;

    xx_rt_memcpy(base, raw, 8U);
    xx_rt_memcpy(ext, raw + 8U, 3U);
    if (base[0] == 0x05U) base[0] = 0xE5U; /* stands in for a real 0xE5 */
    while (base_length > 0U && base[base_length - 1U] == ' ') --base_length;
    while (ext_length > 0U && ext[ext_length - 1U] == ' ') --ext_length;
    if (base_length == 0U) return true;
    for (index = 0U; index < base_length; ++index)
        if (base[index] < 0x20U || base[index] == '/' || base[index] == '\\')
            return true;
    for (index = 0U; index < ext_length; ++index)
        if (ext[index] < 0x20U || ext[index] == '/' || ext[index] == '\\')
            return true;
    if (ext_length == 0U && base[0] == '.' &&
        (base_length == 1U || (base_length == 2U && base[1] == '.')))
        return true; /* "." and ".." */

    if (volume->count >= APRC_MAX_MEMBERS) {
        walk->full = true;
        return false;
    }

    /* Pick a component no earlier sibling holds on a case-insensitive host. */
    for (;;) {
        length = aprc_compose(component, base, base_length, ext, ext_length,
                              rename);
        if (length == 0U) {
            *failed = true;
            return false;
        }
        for (index = 0U; index < length; ++index) key[index] = aprc_fold(component[index]);
        if (!aprc_hash_contains(walk, parent, key, length) &&
            !(rename == 0U && volume->count > walk->dir_first &&
              aprc_alias_shape(base, base_length)))
            break;
        if (++rename > APRC_MAX_MEMBERS + 2U) {
            *failed = true;
            return false;
        }
    }

    if (parent != APRC_ROOT_PARENT) parent_name = volume->members[parent].name;
    name = aprc_join(parent_name, component, length);
    if (!name) {
        *failed = true;
        return false;
    }
    if (volume->count == volume->capacity) {
        size_t capacity = volume->capacity ? volume->capacity * 2U : 32U;
        aprc_member *grown;
        if (capacity > APRC_MAX_MEMBERS) capacity = APRC_MAX_MEMBERS;
        grown = (aprc_member *)xx_mem_realloc(volume->members,
                                              capacity * sizeof(*grown));
        if (!grown) {
            xx_str_free(name);
            *failed = true;
            return false;
        }
        volume->members = grown;
        volume->capacity = capacity;
    }
    if (!aprc_hash_reserve(walk)) {
        xx_str_free(name);
        *failed = true;
        return false;
    }
    member = &volume->members[volume->count];
    xx_mem_zero(member, sizeof(*member));
    member->name = name;
    member->entry_offset = offset;
    member->attributes = attributes;
    member->folder = (attributes & APRC_ATTR_DIRECTORY) != 0U;
    member->first_cluster = aprc_le16(raw + 26U);
    member->size = member->folder ? 0U : aprc_le32(raw + 28U);
    member->dos_time = aprc_le16(raw + 22U);
    member->dos_date = aprc_le16(raw + 24U);
    member->parent = parent;
    member->key_length = (uint8_t)length;
    xx_rt_memcpy(member->key, key, length);
    member->data_offset = aprc_cluster_is_data(g, member->first_cluster)
                              ? aprc_cluster_offset(g, member->first_cluster)
                              : -1;
    ++volume->count;
    aprc_hash_put(walk, (uint32_t)(volume->count - 1U));
    if (member->folder && depth < APRC_MAX_DEPTH &&
        aprc_cluster_is_data(g, member->first_cluster) &&
        !aprc_enqueue(walk, member->first_cluster,
                      (uint32_t)(volume->count - 1U), depth + 1U)) {
        *failed = true;
        return false;
    }
    return true;
}

static bool aprc_scan_root(aprc_walk *walk) {
    const aprc_geometry *g = &walk->volume->geometry;
    size_t size = (size_t)g->root_sectors * APRC_SECTOR;
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(size);
    size_t position;
    bool failed = false;
    if (!buffer) return false;
    if (!aprc_read_at(walk->device, g->root_offset, buffer, size)) {
        xx_mem_free(buffer);
        return false;
    }
    walk->dir_first = walk->volume->count;
    for (position = 0U; position + APRC_ENTRY <= size; position += APRC_ENTRY) {
        if (!aprc_entry(walk, buffer + position,
                        g->root_offset + (int64_t)position, APRC_ROOT_PARENT,
                        1U, &failed))
            break;
    }
    xx_mem_free(buffer);
    return !failed;
}

/* A subdirectory: its cluster chain, each cluster read at most once over
 * the whole walk, so loops, cross-links and aliased directories all end. */
static bool aprc_scan_directory(aprc_walk *walk, const aprc_pending *item,
                                uint8_t *buffer) {
    aprc_volume *volume = walk->volume;
    const aprc_geometry *g = &volume->geometry;
    uint32_t cluster = item->cluster;
    bool failed = false;
    walk->dir_first = volume->count;
    while (aprc_cluster_is_data(g, cluster) &&
           !aprc_bit_test(walk->dir_seen, cluster)) {
        int64_t offset = aprc_cluster_offset(g, cluster);
        size_t position;
        bool end = false;
        if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;
        aprc_bit_set(walk->dir_seen, cluster);
        if (!aprc_read_at(walk->device, offset, buffer, g->bytes_per_cluster))
            return true; /* unreadable: list what we have */
        for (position = 0U; position + APRC_ENTRY <= g->bytes_per_cluster;
             position += APRC_ENTRY) {
            if (!aprc_entry(walk, buffer + position,
                            offset + (int64_t)position, item->member,
                            item->depth, &failed)) {
                end = true;
                break;
            }
        }
        if (failed) return false;
        if (end || walk->full) break;
        cluster = aprc_fat_next(volume, cluster);
    }
    return true;
}

static bool aprc_parse(Abstractformat *self, aprc_volume *volume,
                       xx_pd_struct *pd) {
    aprc_walk walk;
    uint8_t *buffer = NULL;
    bool ok = false;
    xx_mem_zero(volume, sizeof(*volume));
    if (!aprc_open(self, &volume->geometry)) return false;
    volume->fat_bytes =
        (size_t)volume->geometry.fat_sectors * (size_t)APRC_SECTOR;
    volume->fat = (uint8_t *)xx_mem_alloc(volume->fat_bytes);
    if (!volume->fat ||
        !aprc_read_at(self->device, volume->geometry.fat_offset, volume->fat,
                      volume->fat_bytes)) {
        aprc_volume_cleanup(volume);
        return false;
    }
    xx_mem_zero(&walk, sizeof(walk));
    walk.device = self->device;
    walk.volume = volume;
    walk.pd = pd;
    walk.dir_seen = (uint8_t *)xx_mem_calloc(
        ((size_t)volume->geometry.cluster_count + 2U + 7U) / 8U, 1U);
    buffer = (uint8_t *)xx_mem_alloc(volume->geometry.bytes_per_cluster);
    if (!walk.dir_seen || !buffer || !aprc_hash_reserve(&walk)) goto done;
    if (!aprc_scan_root(&walk)) goto done;
    while (!walk.full && walk.queue_head < walk.queue_count) {
        aprc_pending item = walk.queue[walk.queue_head++];
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!aprc_scan_directory(&walk, &item, buffer)) goto done;
    }
    ok = true;
done:
    if (buffer) xx_mem_free(buffer);
    if (walk.dir_seen) xx_mem_free(walk.dir_seen);
    if (walk.queue) xx_mem_free(walk.queue);
    if (walk.hash) xx_mem_free(walk.hash);
    if (!ok) aprc_volume_cleanup(volume);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

/* Walk the member's chain for exactly ceil(size / cluster) clusters,
 * writing each to @p output (NULL only verifies the chain). */
static bool aprc_copy_member(xx_io_device *device, const aprc_volume *volume,
                             const aprc_member *member, xx_io_device *output,
                             xx_pd_struct *pd) {
    const aprc_geometry *g = &volume->geometry;
    uint64_t remaining = member->size;
    uint32_t cluster = member->first_cluster;
    uint8_t *visited = NULL;
    uint8_t *buffer = NULL;
    bool ok = false;
    if (remaining == 0U) return true;
    if (remaining > (uint64_t)g->cluster_count * g->bytes_per_cluster ||
        !aprc_cluster_is_data(g, cluster))
        return false;
    visited = (uint8_t *)xx_mem_calloc(((size_t)g->cluster_count + 2U + 7U) / 8U,
                                       1U);
    buffer = output ? (uint8_t *)xx_mem_alloc(g->bytes_per_cluster) : NULL;
    if (!visited || (output && !buffer)) goto done;
    for (;;) {
        size_t count = remaining > g->bytes_per_cluster
                           ? (size_t)g->bytes_per_cluster
                           : (size_t)remaining;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!aprc_cluster_is_data(g, cluster) || aprc_bit_test(visited, cluster))
            goto done; /* chain ended early, or looped */
        aprc_bit_set(visited, cluster);
        if (output) {
            size_t written = 0U;
            if (!aprc_read_at(device, aprc_cluster_offset(g, cluster), buffer,
                              count))
                goto done;
            while (written < count) {
                ssize_t put = xx_io_write(output, buffer + written,
                                          count - written);
                if (put <= 0 || (size_t)put > count - written) goto done;
                written += (size_t)put;
            }
        }
        remaining -= count;
        if (remaining == 0U) break;
        cluster = aprc_fat_next(volume, cluster);
    }
    ok = true;
done:
    if (buffer) xx_mem_free(buffer);
    if (visited) xx_mem_free(visited);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Record glue                                                             */

static void aprc_stream_free(void *opaque) {
    aprc_stream *stream = (aprc_stream *)opaque;
    if (!stream) return;
    aprc_volume_cleanup(&stream->volume);
    xx_mem_free(stream);
}

static bool aprc_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *aprc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool aprc_set_record(xx_archive_record *record,
                            const aprc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->entry_offset;
    record->header_size = (int64_t)APRC_ENTRY;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_act_apricot_pc_xi_raw_init(xx_act_apricot_pc_xi_raw *image,
                                   xx_io_device *device, int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_ACT_APRICOT_PC_XI_RAW_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-apricot-disk-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_act_apricot_pc_xi_raw_check_is_valid;
    image->format.handle_base_info = xx_act_apricot_pc_xi_raw_handle_base_info;
    image->format.get_format_size = xx_act_apricot_pc_xi_raw_get_format_size;
    image->format.get_number_of_archive_records =
        xx_act_apricot_pc_xi_raw_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_act_apricot_pc_xi_raw_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_act_apricot_pc_xi_raw_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_act_apricot_pc_xi_raw_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_act_apricot_pc_xi_raw_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_act_apricot_pc_xi_raw_free_archive_records_reading;
}

xx_act_apricot_pc_xi_raw *xx_act_apricot_pc_xi_raw_create(
    xx_io_device *device, int64_t base_address) {
    xx_act_apricot_pc_xi_raw *image =
        (xx_act_apricot_pc_xi_raw *)xx_mem_alloc(sizeof(*image));
    if (image) xx_act_apricot_pc_xi_raw_init(image, device, base_address);
    return image;
}

void xx_act_apricot_pc_xi_raw_destroy(xx_act_apricot_pc_xi_raw *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_act_apricot_pc_xi_raw_free(xx_act_apricot_pc_xi_raw *image) {
    if (!image) return;
    xx_act_apricot_pc_xi_raw_destroy(image);
    xx_mem_free(image);
}

bool xx_act_apricot_pc_xi_raw_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd) {
    aprc_geometry geometry;
    (void)pd;
    return aprc_open(self, &geometry);
}

bool xx_act_apricot_pc_xi_raw_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd) {
    xx_act_apricot_pc_xi_raw *image = (xx_act_apricot_pc_xi_raw *)self;
    aprc_volume volume;
    int64_t total;
    int64_t end;
    if (!self || !aprc_parse(self, &volume, pd)) return false;
    image->number_of_records = volume.count;
    image->cylinders = volume.geometry.cylinders;
    image->heads = volume.geometry.heads;
    image->sectors_per_track = volume.geometry.spt;
    image->total_sectors = volume.geometry.total_sectors;
    image->bytes_per_cluster = volume.geometry.bytes_per_cluster;
    image->cluster_count = volume.geometry.cluster_count;
    image->media = volume.geometry.media;
    xx_rt_memcpy(image->label, volume.geometry.label, sizeof(image->label));
    self->number_of_archive_records = volume.count;
    self->format_size = volume.geometry.volume_size;
    end = self->base_address + volume.geometry.volume_size;
    total = xx_io_total_size(self->device);
    if (total > end) {
        self->overlay_offset = end;
        self->overlay_size = total - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_format_set_version(self, volume.geometry.label);
    self->is_valid = true;
    self->base_info_handled = true;
    aprc_volume_cleanup(&volume);
    return true;
}

int64_t xx_act_apricot_pc_xi_raw_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_act_apricot_pc_xi_raw_handle_base_info(self, pd))
               ? self->format_size : -1;
}

uint64_t xx_act_apricot_pc_xi_raw_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_act_apricot_pc_xi_raw_handle_base_info(self, pd))
               ? ((xx_act_apricot_pc_xi_raw *)self)->number_of_records : 0U;
}

xx_archive_record_state *xx_act_apricot_pc_xi_raw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    aprc_stream *stream;
    xx_archive_record_state *state;
    if (!self) return NULL;
    stream = (aprc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!aprc_parse(self, &stream->volume, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        aprc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = aprc_stream_free;
    state->total_records = (int64_t)stream->volume.count;
    if (!aprc_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->volume.count != 0U) {
        if (!aprc_set_record(&state->current_record,
                             &stream->volume.members[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_act_apricot_pc_xi_raw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_act_apricot_pc_xi_raw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    aprc_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (aprc_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (++stream->index >= stream->volume.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!aprc_set_record(&state->current_record,
                         &stream->volume.members[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_act_apricot_pc_xi_raw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    aprc_stream *stream;
    const aprc_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record ||
        !(stream = (aprc_stream *)state->internal_state) ||
        stream->index >= stream->volume.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->volume.members[stream->index];
    option = aprc_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option)) return false;
    option = aprc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option)
        return member->folder ||
               aprc_copy_member(self->device, &stream->volume, member, NULL,
                                pd);
    if (!aprc_safe_path(member->name)) return false;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = xx_io_file_open(path, "wb");
        created = output != NULL;
        if (!output) goto done;
        result = aprc_copy_member(self->device, &stream->volume, member, output,
                                  pd);
        if (xx_io_close(output) != 0) result = false;
        if (!result && created) (void)xx_rt_remove(path);
    }
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_act_apricot_pc_xi_raw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

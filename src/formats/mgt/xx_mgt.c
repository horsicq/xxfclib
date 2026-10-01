/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MGT disk image - the raw sector dump of a Miles Gordon Technology floppy:
 * DISCiPLE and +D (ZX Spectrum) and SAM Coupe (SAMDOS, MasterDOS).  Written
 * from the published layout of the MGT filesystem; no reference code was
 * ported.
 *
 *   Image      80 cylinders x 2 sides x 10 sectors x 512 bytes = 819200,
 *              cylinder interleaved: cyl 0 side 0, cyl 0 side 1, cyl 1 ...
 *              A track number's bit 7 selects side 1 (128..207).
 *   Directory  side 0 tracks 0..3: 40 sectors, two 256-byte entries each,
 *              80 entries.  MasterDOS may extend it: byte 255 of the first
 *              entry counts extra tracks after track 3 (track 4 sector 1
 *              is skipped when a file - the boot DOS - owns it).  Slots are
 *              numbered physically either way, as SAMdisk numbers them.
 *   Entry      +0      type; bit 7 hidden, bit 6 protected; 0 = free
 *              +1..10  name, space padded
 *              +11     u16 BE sector count
 *              +13     first track, +14 first sector (1..10)
 *              +15     195-byte sector map: bit n (LSB first) is logical
 *                      sector n counted from track 4 sector 1, side 0
 *                      tracks 4..79 then side 1 tracks 0..79 (1560 bits)
 *              +250    MasterDOS directory tag (type 21 entries)
 *              +254    tag of the MasterDOS directory holding the entry
 *   Sector     510 data bytes + next track + next sector.
 *
 * File types: ZX 1 BASIC, 2 number array, 3 string array, 4 CODE, 5 48K
 * snapshot, 6 microdrive, 7 SCREEN$, 8 special, 9 128K snapshot, 10
 * opentype, 11 execute; SAM 16 BASIC, 17/18 arrays, 19 CODE, 20 SCREEN$,
 * 21 MasterDOS directory, 22..31 driver / DOS-specific types.
 *
 * WHAT A MEMBER IS.  ZX types 1,2,3,4,7 and SAM types 16..20 start with the
 * 9-byte tape-style header the DOS writes in front of the data; the member
 * is the data after it, as long as the header says (ZX: u16 LE at +1; SAM:
 * pages at +7 x 16384 + u16 LE at +1).  A length that does not fit in the
 * allocated sectors publishes the whole raw chain instead of truncating.  A
 * 48K snapshot is its 49152 bytes of RAM; every other type is the whole
 * chain (count x 510 bytes), since the entry holds no byte length.
 *
 * DETECTION.  There is no magic.  An image is accepted only when it is
 * exactly 819200 bytes (at offset 0), every used entry of the 80 has a known
 * type, a sector count of 1..1560, a first sector inside the data area that
 * its own map marks, a map whose population equals the count, and no two
 * maps share a sector - and at least one file exists.  A FAT boot sector, a
 * blank disk or random data fails within the first entries.
 *
 * HOSTILE INPUT.  The chain is walked at most `count` steps, every link
 * must name a sector inside the data area that the entry's map owns and
 * that was not visited before; any break fails that member only.  Names
 * are rebuilt from the 10 raw bytes into a host-safe component; duplicate
 * paths (case-insensitive) get the 1-based slot number appended.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mgt/xx_mgt.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef MGT
#define XX_MGT_FILE_TYPE XX_FILE_TYPE_MGT
#else
#define XX_MGT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MGT_SECTOR 512
#define MGT_PAYLOAD 510
#define MGT_SPT 10
#define MGT_CYLS 80
#define MGT_TRACK_BYTES (MGT_SPT * MGT_SECTOR)
#define MGT_IMAGE_SIZE INT64_C(819200)
#define MGT_ENTRY 256
#define MGT_DIR_TRACKS 4
#define MGT_MAIN_ENTRIES 80
#define MGT_MAX_EXTRA 35
#define MGT_MAP_OFFSET 15
#define MGT_MAP_BYTES 195
#define MGT_MAP_BITS 1560
#define MGT_TYPE_DIR 21U
#define MGT_MAX_MEMBERS (MGT_MAIN_ENTRIES + MGT_MAX_EXTRA * 2 * MGT_SPT)
#define MGT_MAX_DEPTH 16
#define MGT_NAME_MAX 160
#define MGT_PATH_MAX 2048

typedef struct mgt_member_s {
    char *name;
    bool folder;
    uint8_t type;
    uint32_t count;
    uint8_t first_track;
    uint8_t first_sector;
    int64_t header_offset;
    int64_t data_offset;
    int64_t skip;
    int64_t size;
    uint8_t map[MGT_MAP_BYTES];
} mgt_member;

typedef struct mgt_stream_s {
    mgt_member *items;
    size_t count;
    size_t index;
    int64_t base;
    uint8_t extra;
} mgt_stream;

/* One raw directory entry awaiting a name. */
typedef struct mgt_slot_s {
    uint8_t entry[MGT_ENTRY];
    int64_t offset;
    uint32_t number;       /* 1-based slot number, used for duplicates */
    uint8_t state;         /* dir path resolution: 0 new, 1 busy, 2 done */
    uint8_t level;         /* nesting level of a resolved directory, 1 = root */
    char *path;            /* resolved path of a directory */
} mgt_slot;

static void xx_mgt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool mgt_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer,
                        size_t size) {
    size_t completed = 0U;
    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool mgt_data_sector(uint8_t track, uint8_t sector) {
    if (sector < 1U || sector > MGT_SPT) return false;
    return (track >= MGT_DIR_TRACKS && track < MGT_CYLS) ||
           (track >= 128U && track < 128U + MGT_CYLS);
}

static int64_t mgt_sector_offset(uint8_t track, uint8_t sector) {
    int64_t cylinder = (int64_t)(track & 0x7FU);
    int64_t side = (int64_t)(track >> 7);
    return ((cylinder * 2 + side) * MGT_SPT + (int64_t)(sector - 1U)) *
           MGT_SECTOR;
}

/* Bit of the sector map for a data sector (caller checked mgt_data_sector). */
static uint32_t mgt_bit(uint8_t track, uint8_t sector) {
    uint32_t logical = (uint32_t)(track & 0x7FU) + ((track & 0x80U) ? 80U : 0U);
    return (logical - MGT_DIR_TRACKS) * MGT_SPT + (uint32_t)(sector - 1U);
}

static bool mgt_map_has(const uint8_t *map, uint32_t bit) {
    return bit < MGT_MAP_BITS && (map[bit >> 3] & (1U << (bit & 7U))) != 0U;
}

static uint32_t mgt_popcount(const uint8_t *map) {
    uint32_t total = 0U;
    size_t index;
    for (index = 0U; index < MGT_MAP_BYTES; ++index) {
        uint8_t value = map[index];
        while (value) {
            total += value & 1U;
            value = (uint8_t)(value >> 1);
        }
    }
    return total;
}

static bool mgt_maps_overlap(const uint8_t *a, const uint8_t *b) {
    size_t index;
    for (index = 0U; index < MGT_MAP_BYTES; ++index) {
        if ((a[index] & b[index]) != 0U) return true;
    }
    return false;
}

static void mgt_map_merge(uint8_t *into, const uint8_t *from) {
    size_t index;
    for (index = 0U; index < MGT_MAP_BYTES; ++index) into[index] |= from[index];
}

static bool mgt_known_type(uint8_t type) {
    return (type >= 1U && type <= 11U) || (type >= 16U && type <= 31U);
}

static bool mgt_headered(uint8_t type) {
    return (type >= 1U && type <= 4U) || type == 7U ||
           (type >= 16U && type <= 20U);
}

/* A used file entry is self-consistent.  Directories are checked apart. */
static bool mgt_file_entry_ok(const uint8_t *entry) {
    uint8_t type = (uint8_t)(entry[0] & 0x3FU);
    uint32_t count = ((uint32_t)entry[11] << 8) | entry[12];
    const uint8_t *map = entry + MGT_MAP_OFFSET;
    if (!mgt_known_type(type) || type == MGT_TYPE_DIR) return false;
    if (count == 0U || count > MGT_MAP_BITS) return false;
    if (!mgt_data_sector(entry[13], entry[14])) return false;
    if (!mgt_map_has(map, mgt_bit(entry[13], entry[14]))) return false;
    return mgt_popcount(map) == count;
}

/* ------------------------------------------------------------ names ----- */

static char mgt_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool mgt_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        if (!word[index] || mgt_upper(name[index]) != word[index]) return false;
    }
    return word[stem] == 0;
}

static bool mgt_device_name(const char *name, size_t stem) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (mgt_stem_is(name, stem, devices[index])) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           (mgt_stem_is(name, 3U, "COM") || mgt_stem_is(name, 3U, "LPT"));
}

static char mgt_safe_char(uint8_t value) {
    if (value < 0x20U || value >= 0x7FU) return '_';
    switch (value) {
    case '/': case '\\': case ':': case '*': case '?': case '"': case '<':
    case '>': case '|':
        return '_';
    default:
        return (char)value;
    }
}

/* 10 raw name bytes -> one safe path component in @p out (>= 16 bytes). */
static void mgt_component(const uint8_t *raw, char *out) {
    size_t begin = 0U, end = 10U, length = 0U, index, stem;
    while (end > 0U && (raw[end - 1U] == 0x20U || raw[end - 1U] == 0U)) --end;
    while (begin < end && raw[begin] == 0x20U) ++begin;
    for (index = begin; index < end; ++index) {
        out[length++] = mgt_safe_char(raw[index]);
    }
    if (length == 0U) out[length++] = '_';
    out[length] = 0;
    /* Windows drops trailing dots and ".." climbs: turn the run into '_'. */
    for (index = length; index > 0U && out[index - 1U] == '.'; --index) {
        out[index - 1U] = '_';
    }
    stem = 0U;
    while (out[stem] && out[stem] != '.') ++stem;
    if (mgt_device_name(out, stem)) {
        for (index = length + 1U; index > stem; --index) out[index] = out[index - 1U];
        out[stem] = '_';
    }
}

static bool mgt_path_taken(const mgt_stream *stream, const char *path) {
    size_t index;
    for (index = 0U; index < stream->count; ++index) {
        if (xx_str_icmp(stream->items[index].name, path) == 0) return true;
    }
    return false;
}

/* parent + "/" + leaf, made unique against every name already published. */
static char *mgt_unique_path(const mgt_stream *stream, const char *parent,
                             const char *leaf, uint32_t number) {
    char buffer[MGT_PATH_MAX];
    const char *dot = xx_str_rchr(leaf, '.');
    size_t stem = (dot && dot != leaf) ? (size_t)(dot - leaf) : xx_str_len(leaf);
    uint32_t attempt;
    int written;

    if (parent && xx_str_len(parent) + xx_str_len(leaf) + 40U > sizeof(buffer)) {
        return NULL;
    }
    for (attempt = 0U; attempt < 1000U; ++attempt) {
        if (attempt == 0U) {
            written = xx_rt_snprintf(buffer, sizeof(buffer), "%s%s%s",
                                     parent ? parent : "", parent ? "/" : "",
                                     leaf);
        } else if (attempt == 1U) {
            written = xx_rt_snprintf(buffer, sizeof(buffer), "%s%s%.*s_%u%s",
                                     parent ? parent : "", parent ? "/" : "",
                                     (int)stem, leaf, (unsigned)number,
                                     leaf + stem);
        } else {
            written = xx_rt_snprintf(buffer, sizeof(buffer),
                                     "%s%s%.*s_%u_%u%s", parent ? parent : "",
                                     parent ? "/" : "", (int)stem, leaf,
                                     (unsigned)number, (unsigned)attempt,
                                     leaf + stem);
        }
        if (written <= 0 || (size_t)written >= sizeof(buffer)) return NULL;
        if (!mgt_path_taken(stream, buffer)) return xx_str_dup(buffer);
    }
    return NULL;
}

/* Every '/'-separated component is non-empty, not "." / "..", ends in no
 * dot, has no unsafe byte and is not a device name. */
static bool mgt_path_safe(const char *name) {
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        char c = *at;
        if (c == '/' || c == 0) {
            size_t length = (size_t)(at - segment);
            size_t stem = 0U;
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ') {
                return false;
            }
            while (stem < length && segment[stem] != '.') ++stem;
            if (stem < 8U) {
                char part[8];
                xx_rt_memcpy(part, segment, stem);
                part[stem] = 0;
                if (mgt_device_name(part, stem)) return false;
            }
            if (c == 0) return true;
            segment = at + 1;
            continue;
        }
        if ((uint8_t)c < 0x20U || (uint8_t)c >= 0x7FU || c == '\\' ||
            c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            return false;
        }
    }
}

/* --------------------------------------------------------------- parse -- */

static void mgt_stream_free(void *pointer) {
    mgt_stream *stream = (mgt_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void mgt_slots_free(mgt_slot *slots, size_t count) {
    size_t index;
    if (!slots) return;
    for (index = 0U; index < count; ++index) xx_str_free(slots[index].path);
    xx_mem_free(slots);
}

/* Resolve the path of directory slot @p at.  Recursion is bounded by
 * MGT_MAX_DEPTH and by the busy state (a tag loop), and a directory never
 * nests deeper than MGT_MAX_DEPTH levels: past that, or on a loop or an
 * unknown tag, it hangs at the root. */
static const mgt_slot *mgt_dir_resolve(mgt_stream *stream, mgt_slot *slots,
                                       size_t slot_count,
                                       const int16_t *by_tag, size_t at,
                                       int depth);

static const mgt_slot *mgt_parent_slot(mgt_stream *stream, mgt_slot *slots,
                                       size_t slot_count,
                                       const int16_t *by_tag, uint8_t tag,
                                       int depth) {
    int16_t owner;
    const mgt_slot *parent;
    if (tag == 0U || depth >= MGT_MAX_DEPTH) return NULL;
    owner = by_tag[tag];
    if (owner < 0 || (size_t)owner >= slot_count) return NULL;
    parent = mgt_dir_resolve(stream, slots, slot_count, by_tag,
                             (size_t)owner, depth + 1);
    if (!parent || !parent->path || parent->level >= MGT_MAX_DEPTH) {
        return NULL;
    }
    return parent;
}

static bool mgt_append(mgt_stream *stream, const mgt_member *member) {
    mgt_member *grown;
    if (stream->count >= (size_t)MGT_MAX_MEMBERS) return false;
    grown = (mgt_member *)xx_mem_realloc(stream->items,
                                         sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static const mgt_slot *mgt_dir_resolve(mgt_stream *stream, mgt_slot *slots,
                                       size_t slot_count,
                                       const int16_t *by_tag, size_t at,
                                       int depth) {
    mgt_slot *slot = &slots[at];
    const mgt_slot *parent;
    char leaf[MGT_NAME_MAX];
    mgt_member member;

    if (slot->state == 2U) return slot;
    if (slot->state == 1U) return NULL; /* loop: caller falls back to root */
    slot->state = 1U;
    parent = mgt_parent_slot(stream, slots, slot_count, by_tag,
                             slot->entry[254], depth);
    mgt_component(slot->entry + 1, leaf);
    slot->path = mgt_unique_path(stream, parent ? parent->path : NULL, leaf,
                                 slot->number);
    slot->level = parent ? (uint8_t)(parent->level + 1U) : 1U;
    slot->state = 2U;
    if (!slot->path) return slot;
    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(slot->path);
    member.folder = true;
    member.type = MGT_TYPE_DIR;
    member.header_offset = slot->offset;
    member.data_offset = -1;
    if (!member.name || !mgt_append(stream, &member)) {
        xx_str_free(member.name);
        xx_str_free(slot->path);
        slot->path = NULL;
    }
    return slot;
}

/* Length rule for a file member (see the header comment). */
static bool mgt_member_extent(Abstractformat *self, mgt_member *member) {
    int64_t chain = (int64_t)member->count * MGT_PAYLOAD;
    member->skip = 0;
    member->size = chain;
    if (mgt_headered(member->type)) {
        uint8_t header[9];
        int64_t length;
        if (!mgt_read_at(self, member->data_offset, header, sizeof(header))) {
            return false;
        }
        length = (int64_t)(header[1] | ((uint32_t)header[2] << 8));
        if (member->type >= 16U) length += (int64_t)header[7] * 16384;
        if (length <= chain - 9) {
            member->skip = 9;
            member->size = length;
        }
    } else if (member->type == 5U && chain > 49152) {
        member->size = 49152;
    }
    return true;
}

static uint32_t mgt_entry_count(const uint8_t *entry) {
    return ((uint32_t)entry[11] << 8) | entry[12];
}

static mgt_stream *mgt_parse(Abstractformat *self, xx_pd_struct *pd,
                             bool probe) {
    uint8_t *dir = NULL;
    mgt_slot *slots = NULL;
    size_t slot_count = 0U;
    mgt_stream *stream = NULL;
    uint8_t used[MGT_MAP_BYTES];
    int16_t by_tag[256];
    int64_t total, span;
    uint32_t files = 0U;
    uint8_t extra = 0U;
    size_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (self->base_address == 0 ? span != MGT_IMAGE_SIZE
                                : span < MGT_IMAGE_SIZE) {
        return NULL;
    }

    dir = (uint8_t *)xx_mem_alloc((size_t)MGT_MAIN_ENTRIES * MGT_ENTRY);
    if (!dir) return NULL;
    xx_mem_zero(used, sizeof(used));

    /* Main directory: side 0 tracks 0..3; validate track by track so garbage
     * is rejected after the first 5 KiB. */
    for (index = 0U; index < MGT_DIR_TRACKS; ++index) {
        size_t slot;
        uint8_t *track = dir + index * MGT_TRACK_BYTES;
        if (!mgt_read_at(self,
                         self->base_address +
                             (int64_t)index * 2 * MGT_TRACK_BYTES,
                         track, MGT_TRACK_BYTES)) {
            goto fail;
        }
        for (slot = 0U; slot < MGT_TRACK_BYTES / MGT_ENTRY; ++slot) {
            const uint8_t *entry = track + slot * MGT_ENTRY;
            uint8_t type = (uint8_t)(entry[0] & 0x3FU);
            if (type == 0U) continue;
            if (type == MGT_TYPE_DIR) continue;
            if (!mgt_file_entry_ok(entry)) goto fail;
            if (mgt_maps_overlap(used, entry + MGT_MAP_OFFSET)) goto fail;
            mgt_map_merge(used, entry + MGT_MAP_OFFSET);
            ++files;
        }
    }
    if (files == 0U) goto fail;

    /* MasterDOS extended directory.  Byte 255 of the first entry is only
     * trusted when the first entry is not a snapshot (whose registers live
     * up there) and no file owns a sector of the claimed tracks. */
    {
        const uint8_t *first = dir;
        uint8_t first_type = (uint8_t)(first[0] & 0x3FU);
        uint8_t claim = first[255];
        if (claim >= 1U && claim <= MGT_MAX_EXTRA && first_type != 5U &&
            first_type != 9U) {
            uint32_t bit;
            bool clash = false;
            for (bit = 1U; bit < (uint32_t)claim * MGT_SPT; ++bit) {
                if (mgt_map_has(used, bit)) {
                    clash = true;
                    break;
                }
            }
            if (!clash) extra = claim;
        }
    }

    if (probe) {
        /* The main directory decides; names and the extension do not. */
        xx_mem_free(dir);
        stream = (mgt_stream *)xx_mem_alloc(sizeof(*stream));
        if (stream) {
            xx_mem_zero(stream, sizeof(*stream));
            stream->base = self->base_address;
            stream->extra = extra;
        }
        return stream;
    }

    slot_count = (size_t)MGT_MAIN_ENTRIES + (size_t)extra * 2U * MGT_SPT;
    slots = (mgt_slot *)xx_mem_alloc(sizeof(*slots) * slot_count);
    if (!slots) goto fail;
    xx_mem_zero(slots, sizeof(*slots) * slot_count);
    for (index = 0U; index < MGT_MAIN_ENTRIES; ++index) {
        size_t sector_index = index / 2U;
        size_t track = sector_index / MGT_SPT;
        size_t sector = sector_index % MGT_SPT;
        xx_rt_memcpy(slots[index].entry, dir + index * MGT_ENTRY, MGT_ENTRY);
        slots[index].offset = self->base_address +
                              (int64_t)track * 2 * MGT_TRACK_BYTES +
                              (int64_t)sector * MGT_SECTOR +
                              (int64_t)(index % 2U) * MGT_ENTRY;
        slots[index].number = (uint32_t)index + 1U;
    }
    xx_mem_free(dir);
    dir = NULL;

    /* Extended area: entries that are not self-consistent, or whose sectors
     * another file already owns, are skipped rather than failing the disk. */
    {
        size_t at = MGT_MAIN_ENTRIES;
        uint32_t t;
        uint8_t sector_buffer[MGT_SECTOR];
        for (t = MGT_DIR_TRACKS; t < (uint32_t)MGT_DIR_TRACKS + extra; ++t) {
            uint8_t s;
            for (s = 1U; s <= MGT_SPT; ++s) {
                int64_t offset;
                uint32_t half;
                if (pd && xx_pd_is_stopped(pd)) goto fail;
                /* Track 4 sector 1 is the boot sector when a file (the DOS)
                 * owns it; otherwise it is an ordinary directory sector. */
                if (t == MGT_DIR_TRACKS && s == 1U && mgt_map_has(used, 0U)) {
                    slots[at].number = (uint32_t)at + 1U;
                    slots[at + 1U].number = (uint32_t)at + 2U;
                    at += 2U;
                    continue;
                }
                offset = self->base_address +
                         mgt_sector_offset((uint8_t)t, s);
                if (!mgt_read_at(self, offset, sector_buffer,
                                 sizeof(sector_buffer))) {
                    goto fail;
                }
                for (half = 0U; half < 2U; ++half) {
                    mgt_slot *slot = &slots[at];
                    const uint8_t *entry = sector_buffer + half * MGT_ENTRY;
                    uint8_t type = (uint8_t)(entry[0] & 0x3FU);
                    slot->offset = offset + (int64_t)half * MGT_ENTRY;
                    slot->number = (uint32_t)at + 1U;
                    ++at;
                    if (type == 0U) continue;
                    if (type != MGT_TYPE_DIR) {
                        if (!mgt_file_entry_ok(entry) ||
                            mgt_maps_overlap(used, entry + MGT_MAP_OFFSET)) {
                            continue;
                        }
                        mgt_map_merge(used, entry + MGT_MAP_OFFSET);
                    }
                    xx_rt_memcpy(slot->entry, entry, MGT_ENTRY);
                }
            }
        }
    }

    stream = (mgt_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->base = self->base_address;
    stream->extra = extra;

    /* Directory tags (first holder wins). */
    for (index = 0U; index < 256U; ++index) by_tag[index] = -1;
    for (index = 0U; index < slot_count; ++index) {
        const uint8_t *entry = slots[index].entry;
        if ((entry[0] & 0x3FU) == MGT_TYPE_DIR && entry[250] != 0U &&
            by_tag[entry[250]] < 0) {
            by_tag[entry[250]] = (int16_t)index;
        }
    }
    /* Directories first, so they keep their names. */
    for (index = 0U; index < slot_count; ++index) {
        const uint8_t *entry = slots[index].entry;
        if ((entry[0] & 0x3FU) != MGT_TYPE_DIR || entry[250] == 0U) continue;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        (void)mgt_dir_resolve(stream, slots, slot_count, by_tag, index, 0);
    }
    for (index = 0U; index < slot_count; ++index) {
        const uint8_t *entry = slots[index].entry;
        uint8_t type = (uint8_t)(entry[0] & 0x3FU);
        const char *parent = NULL;
        char leaf[MGT_NAME_MAX];
        mgt_member member;
        if (type == 0U || type == MGT_TYPE_DIR) continue;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Every directory is resolved by now; an unknown tag means root. */
        if (entry[254] != 0U && by_tag[entry[254]] >= 0) {
            parent = slots[by_tag[entry[254]]].path;
        }
        mgt_component(entry + 1, leaf);
        xx_mem_zero(&member, sizeof(member));
        member.type = type;
        member.count = mgt_entry_count(entry);
        member.first_track = entry[13];
        member.first_sector = entry[14];
        xx_rt_memcpy(member.map, entry + MGT_MAP_OFFSET, MGT_MAP_BYTES);
        member.header_offset = slots[index].offset;
        member.data_offset =
            self->base_address + mgt_sector_offset(entry[13], entry[14]);
        if (!mgt_member_extent(self, &member)) goto fail;
        member.name = mgt_unique_path(stream, parent, leaf,
                                      slots[index].number);
        if (!member.name || !mgt_path_safe(member.name) ||
            !mgt_append(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    mgt_slots_free(slots, slot_count);
    return stream;

fail:
    xx_mem_free(dir);
    mgt_slots_free(slots, slot_count);
    mgt_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool mgt_decode(Abstractformat *self, const mgt_stream *stream,
                       const mgt_member *member, uint8_t **out,
                       size_t *out_size, xx_pd_struct *pd) {
    uint8_t visited[MGT_MAP_BYTES];
    uint8_t sector_buffer[MGT_SECTOR];
    uint8_t *chain = NULL;
    uint8_t track = member->first_track;
    uint8_t sector = member->first_sector;
    uint32_t step;
    int64_t chain_size;

    *out = NULL;
    *out_size = 0U;
    if (member->folder || member->count == 0U ||
        member->count > MGT_MAP_BITS) {
        return false;
    }
    chain_size = (int64_t)member->count * MGT_PAYLOAD;
    if (member->skip < 0 || member->size < 0 ||
        member->skip + member->size > chain_size) {
        return false;
    }
    chain = (uint8_t *)xx_mem_alloc((size_t)chain_size);
    if (!chain) return false;
    xx_mem_zero(visited, sizeof(visited));
    for (step = 0U; step < member->count; ++step) {
        uint32_t bit;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!mgt_data_sector(track, sector)) goto fail;
        bit = mgt_bit(track, sector);
        if (!mgt_map_has(member->map, bit) || mgt_map_has(visited, bit)) {
            goto fail;
        }
        visited[bit >> 3] = (uint8_t)(visited[bit >> 3] | (1U << (bit & 7U)));
        if (!mgt_read_at(self, stream->base + mgt_sector_offset(track, sector),
                         sector_buffer, sizeof(sector_buffer))) {
            goto fail;
        }
        xx_rt_memcpy(chain + (size_t)step * MGT_PAYLOAD, sector_buffer,
                     MGT_PAYLOAD);
        track = sector_buffer[510];
        sector = sector_buffer[511];
    }
    if (member->skip != 0 && member->size != 0) {
        xx_rt_memmove(chain, chain + member->skip, (size_t)member->size);
    }
    *out = chain;
    *out_size = (size_t)member->size;
    return true;

fail:
    xx_mem_free(chain);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_mgt_init(xx_mgt *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MGT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mgt-disk-image");
    xx_format_set_extension(&archive->format, "mgt");
    archive->format.check_is_valid = xx_mgt_check_is_valid;
    archive->format.handle_base_info = xx_mgt_handle_base_info;
    archive->format.get_format_size = xx_mgt_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mgt_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mgt_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mgt_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mgt_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mgt_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mgt_free_archive_records_reading;
    archive->format.destroy = xx_mgt_vtable_destroy;
}

xx_mgt *xx_mgt_create(xx_io_device *device, int64_t base_address) {
    xx_mgt *archive = (xx_mgt *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_mgt_init(archive, device, base_address);
    return archive;
}

void xx_mgt_destroy(xx_mgt *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_mgt_free(xx_mgt *archive) {
    if (!archive) return;
    xx_mgt_destroy(archive);
    xx_mem_free(archive);
}

static void xx_mgt_vtable_destroy(Abstractformat *self) {
    xx_mgt_destroy((xx_mgt *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_mgt_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    mgt_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = mgt_parse(self, pd, true);
    if (!stream) return false;
    mgt_stream_free(stream);
    return true;
}

bool xx_mgt_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mgt *archive = (xx_mgt *)self;
    mgt_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = mgt_parse(self, pd, false);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = MGT_IMAGE_SIZE;
    self->number_of_archive_records = stream->count;
    self->file_type = XX_MGT_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    archive->number_of_records = stream->count;
    archive->extra_directory_tracks = stream->extra;
    mgt_stream_free(stream);
    return true;
}

int64_t xx_mgt_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_mgt_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_mgt *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool mgt_set_record(xx_archive_record *record,
                           const mgt_member *member) {
    uint64_t stored = member->folder ? 0U : (uint64_t)member->count * MGT_SECTOR;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = MGT_ENTRY;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)stored;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->folder
                                              ? 0U
                                              : (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->type) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool mgt_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;
    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *mgt_get_option(const xx_list_s *options,
                                    uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_mgt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    mgt_stream *stream;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    stream = mgt_parse(self, pd, false);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mgt_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = mgt_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!mgt_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !mgt_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_mgt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mgt_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    mgt_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (mgt_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        mgt_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mgt_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    mgt_stream *stream;
    const mgt_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (mgt_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!mgt_path_safe(member->name)) return false;

    path_option = mgt_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (member->folder) return true;
        result = mgt_decode(self, stream, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !mgt_decode(self, stream, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;
        created = output != NULL;
        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_mgt_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

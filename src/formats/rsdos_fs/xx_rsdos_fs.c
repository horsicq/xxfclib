/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RS-DOS, the filesystem of Disk Extended Color BASIC on the Tandy Color
 * Computer, read from a flat floppy image (headerless .dsk, JVC or VDK).
 * xx_rsdos_fs.h carries the layout.  Written from the on-disk structure;
 * MAME imgtool (coco_*_rsdos) and floptool (coco_rsdos) were used only as
 * black-box oracles for sizes and edge cases.  No code was taken from them.
 *
 * There is no magic anywhere: the directory track sits 78336 bytes into a
 * one-sided image.  The probe is one 2560-byte read (granule table plus the
 * nine directory sectors, which are consecutive in every supported layout)
 * after a size and header test, then a bounded walk: 72 entries at most,
 * and a chain of at most 255 granules each, with a visited map.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rsdos_fs/xx_rsdos_fs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as RSDOS_FS is registered there. */
#ifdef RSDOS_FS
#define XX_RSDOS_FS_FILE_TYPE XX_FILE_TYPE_RSDOS_FS
#else
#define XX_RSDOS_FS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RSDOS_SECTOR 256U
#define RSDOS_SPT 18U
#define RSDOS_TRACK (RSDOS_SECTOR * RSDOS_SPT)
#define RSDOS_GRANULE_SECTORS 9U
#define RSDOS_GRANULE (RSDOS_GRANULE_SECTORS * RSDOS_SECTOR)
#define RSDOS_DIR_TRACK 17U
/* Granule table (sector 2) and directory (sectors 3..11): one read. */
#define RSDOS_FAT_SECTOR 2U
#define RSDOS_DIR_SECTORS 9U
#define RSDOS_TABLE_BYTES ((1U + RSDOS_DIR_SECTORS) * RSDOS_SECTOR)
#define RSDOS_ENTRY 32U
#define RSDOS_ENTRIES (RSDOS_DIR_SECTORS * RSDOS_SECTOR / RSDOS_ENTRY)
/* The directory track has to be in the image; 255 is the widest track
 * count a JVC or VDK geometry byte can express. */
#define RSDOS_MIN_TRACKS (RSDOS_DIR_TRACK + 1U)
#define RSDOS_MAX_TRACKS 255U
/* DECB's own geometry; a smaller (truncated) image keeps its 68 granules. */
#define RSDOS_STANDARD_TRACKS 35U
#define RSDOS_MAX_GRANULES 255U
#define RSDOS_FREE 0xFFU
#define RSDOS_LAST 0xC0U
#define RSDOS_LAST_MAX 0xC9U
#define RSDOS_DELETED 0x00U
#define RSDOS_END 0xFFU
#define RSDOS_MAX_TYPE 3U
#define RSDOS_VDK_HEADER_MIN 12U

#define RSDOS_CONTAINER_RAW 0U
#define RSDOS_CONTAINER_JVC 1U
#define RSDOS_CONTAINER_VDK 2U

/* '_' + 8 + '.' + 3 + '~' + two digits + NUL, rounded up. */
#define RSDOS_NAME_BUFFER 24U

typedef struct rsdos_geometry_s {
    int64_t image_size;   /**< Bytes from base_address to the device end. */
    int64_t data_offset;  /**< Device offset of track 0, head 0, sector 1. */
    uint32_t header_size;
    uint32_t container;
    uint32_t sides;
    uint32_t tracks;      /**< Whole tracks per side present. */
    uint32_t granule_count;
} rsdos_geometry;

typedef struct rsdos_member_s {
    char name[RSDOS_NAME_BUFFER];
    int64_t size;
    uint32_t entry;        /**< Directory slot, 0..71. */
    uint32_t granules;     /**< Chain length, 1..granule_count. */
    uint32_t last_sectors; /**< Sectors used in the last granule, 0..9. */
    uint32_t last_bytes;   /**< Bytes used in the last sector, 1..256. */
    uint8_t first_granule;
    uint8_t type;
    uint8_t ascii;
    bool in_range;         /**< Every granule lies inside the image. */
} rsdos_member;

typedef struct rsdos_stream_s {
    rsdos_geometry geometry;
    uint8_t fat[RSDOS_SECTOR];
    rsdos_member items[RSDOS_ENTRIES];
    size_t count;
    size_t index;
    uint32_t free_granules;
} rsdos_stream;

static void xx_rsdos_fs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool rsdos_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Device offset of (track, head 0, sector) in the flat layout. */
static int64_t rsdos_sector_offset(const rsdos_geometry *geometry,
                                   uint32_t track, uint32_t sector) {
    return geometry->data_offset +
           ((int64_t)track * geometry->sides * RSDOS_SPT +
            (int64_t)(sector - 1U)) * RSDOS_SECTOR;
}

/* Granule g: track g / 2, skipping the directory track, head 0, sectors
 * 1..9 or 10..18.  False when that half track is not in the image. */
static bool rsdos_granule_offset(const rsdos_geometry *geometry,
                                 uint32_t granule, int64_t *offset) {
    uint32_t track = granule / 2U;
    if (granule >= geometry->granule_count) return false;
    if (track >= RSDOS_DIR_TRACK) ++track;
    if (track >= geometry->tracks) return false;
    *offset = rsdos_sector_offset(geometry, track,
                                  1U + (granule % 2U) * RSDOS_GRANULE_SECTORS);
    return true;
}

/* Size, container header and geometry.  At most one 12-byte read. */
static bool rsdos_geometry_probe(Abstractformat *format,
                                 rsdos_geometry *out) {
    rsdos_geometry geometry;
    uint8_t header[RSDOS_VDK_HEADER_MIN];
    int64_t total, span, data, track_bytes, tracks;
    uint32_t geometry_tracks;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    xx_mem_zero(&geometry, sizeof(geometry));
    geometry.image_size = span;
    geometry.header_size = (uint32_t)(span % (int64_t)RSDOS_SECTOR);
    geometry.sides = 1U;
    /* Cheapest rejection first: a directory track needs 18 whole tracks. */
    if (span < (int64_t)RSDOS_MIN_TRACKS * RSDOS_TRACK +
                   (int64_t)geometry.header_size)
        return false;
    if (span > (int64_t)RSDOS_MAX_TRACKS * 2 * RSDOS_TRACK +
                   (int64_t)RSDOS_SECTOR)
        return false;
    geometry_tracks = 0U;
    if (geometry.header_size != 0U) {
        size_t wanted = geometry.header_size < sizeof(header)
                            ? (size_t)geometry.header_size : sizeof(header);
        if (!rsdos_read_at(format->device, format->base_address, header,
                           wanted))
            return false;
        if (geometry.header_size >= RSDOS_VDK_HEADER_MIN &&
            header[0] == 'd' && header[1] == 'k' &&
            ((uint32_t)header[2] | ((uint32_t)header[3] << 8U)) ==
                geometry.header_size) {
            /* VDK: tracks and sides from the header; no compression. */
            geometry.container = RSDOS_CONTAINER_VDK;
            geometry.sides = header[9];
            geometry_tracks = header[8];
            if ((header[11] & 0x07U) != 0U) return false;
            if (geometry_tracks < RSDOS_MIN_TRACKS) return false;
        } else {
            /* JVC: the header is the first (size mod 256) bytes. */
            geometry.container = RSDOS_CONTAINER_JVC;
            if (header[0] != RSDOS_SPT) return false;
            if (geometry.header_size > 1U) geometry.sides = header[1];
            if (geometry.header_size > 2U && header[2] != 1U) return false;
            if (geometry.header_size > 3U && header[3] != 1U) return false;
            if (geometry.header_size > 4U && header[4] != 0U) return false;
        }
        if (geometry.sides != 1U && geometry.sides != 2U) return false;
    }
    geometry.data_offset = format->base_address + geometry.header_size;
    data = span - (int64_t)geometry.header_size;
    track_bytes = (int64_t)RSDOS_TRACK * geometry.sides;
    tracks = data / track_bytes;
    if (tracks < (int64_t)RSDOS_MIN_TRACKS) return false;
    if (tracks > (int64_t)RSDOS_MAX_TRACKS) return false;
    geometry.tracks = (uint32_t)tracks;
    if (geometry.container == RSDOS_CONTAINER_VDK) {
        /* A VDK declares its geometry; data past it is not tracks. */
        if (geometry.tracks > geometry_tracks) geometry.tracks = geometry_tracks;
    } else {
        geometry_tracks = geometry.tracks;
    }
    if (geometry_tracks < RSDOS_STANDARD_TRACKS)
        geometry_tracks = RSDOS_STANDARD_TRACKS;
    geometry.granule_count = (geometry_tracks - 1U) * 2U;
    if (geometry.granule_count > RSDOS_MAX_GRANULES)
        geometry.granule_count = RSDOS_MAX_GRANULES;
    *out = geometry;
    return true;
}

/* A granule-table byte is free, a chain end with 0..9 sectors, or a link
 * to a granule of this volume.  Anything else is not an RS-DOS table. */
static bool rsdos_fat_byte_valid(uint8_t value, uint32_t granule_count) {
    return value == RSDOS_FREE ||
           (value >= RSDOS_LAST && value <= RSDOS_LAST_MAX) ||
           (uint32_t)value < granule_count;
}

static char rsdos_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool rsdos_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || rsdos_fold(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Windows resolves these stems to devices whatever the extension. */
static bool rsdos_is_device_stem(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && (name[stem - 1U] == ' ' || name[stem - 1U] == '.'))
        --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (rsdos_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((rsdos_fold(name[0]) == 'C' && rsdos_fold(name[1]) == 'O' &&
             rsdos_fold(name[2]) == 'M') ||
            (rsdos_fold(name[0]) == 'L' && rsdos_fold(name[1]) == 'P' &&
             rsdos_fold(name[2]) == 'T'));
}

static bool rsdos_printable(uint8_t c) { return c >= 0x20U && c <= 0x7EU; }

static char rsdos_safe_char(uint8_t c) {
    if (!rsdos_printable(c) || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|' || c == '~')
        return '_';
    return (char)c;
}

/* Copy @p length field bytes without the trailing space padding, turning
 * leading spaces and trailing dots into '_' so the name survives Win32. */
static size_t rsdos_copy_part(const uint8_t *field, size_t length,
                              char *out) {
    size_t end = length, index, lead = 0U;
    while (end > 0U && field[end - 1U] == ' ') --end;
    while (lead < end && field[lead] == ' ') ++lead;
    for (index = 0U; index < end; ++index)
        out[index] = index < lead ? '_' : rsdos_safe_char(field[index]);
    while (end > 0U && out[end - 1U] == '.') out[--end + 0U] = '_', ++end,
                                                  --end, ++end, (void)0;
    return end;
}

/* "NAME.EXT", or "NAME" when the extension is blank. */
static void rsdos_make_name(const uint8_t *entry, char *out) {
    char stem[16];
    char extension[4];
    size_t stem_length, extension_length, at = 0U, index;
    stem_length = rsdos_copy_part(entry, 8U, stem);
    extension_length = rsdos_copy_part(entry + 8U, 3U, extension);
    /* The probe refuses a blank name, so the stem is never empty. */
    if (stem_length == 0U) stem[stem_length++] = '_';
    for (index = 0U; index < stem_length; ++index)
        if (stem[index] == '.') break;
    if (rsdos_is_device_stem(stem, stem_length)) out[at++] = '_';
    for (index = 0U; index < stem_length; ++index) out[at++] = stem[index];
    if (extension_length != 0U) {
        out[at++] = '.';
        for (index = 0U; index < extension_length; ++index)
            out[at++] = extension[index];
    }
    out[at] = 0;
}

static bool rsdos_same_name(const char *a, const char *b) {
    size_t index;
    for (index = 0U; index < RSDOS_NAME_BUFFER; ++index) {
        if (rsdos_fold(a[index]) != rsdos_fold(b[index])) return false;
        if (a[index] == 0) return true;
    }
    return true;
}

/* Insert "~<entry>" in front of the extension (or at the end). */
static bool rsdos_add_suffix(rsdos_member *member) {
    char digits[4];
    size_t length = xx_str_len(member->name), dot = length, count = 0U;
    size_t index;
    uint32_t value = member->entry;
    for (index = 0U; index < length; ++index)
        if (member->name[index] == '.') dot = index;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    if (length + 1U + count >= RSDOS_NAME_BUFFER) return false;
    for (index = length + 1U; index > dot; --index)
        member->name[index - 1U + 1U + count] = member->name[index - 1U];
    member->name[dot] = '~';
    for (index = 0U; index < count; ++index)
        member->name[dot + 1U + index] = digits[count - 1U - index];
    return true;
}

/* The earliest entry of a case-insensitive group keeps its name; the rest
 * get their unique entry index.  '~' never survives from the disk, so a
 * suffixed name cannot meet a plain one.  72 entries: quadratic is fine. */
static bool rsdos_make_names_unique(rsdos_member *items, size_t count) {
    size_t index, earlier;
    bool *renamed;
    if (count < 2U) return true;
    renamed = (bool *)xx_mem_calloc(count, sizeof(*renamed));
    if (!renamed) return false;
    for (index = 1U; index < count; ++index)
        for (earlier = 0U; earlier < index; ++earlier)
            if (!renamed[earlier] &&
                rsdos_same_name(items[earlier].name, items[index].name)) {
                renamed[index] = true;
                break;
            }
    for (index = 0U; index < count; ++index)
        if (renamed[index] && !rsdos_add_suffix(&items[index])) {
            xx_mem_free(renamed);
            return false;
        }
    xx_mem_free(renamed);
    return true;
}

/* Follow one chain: at most granule_count steps, each granule once. */
static bool rsdos_walk_chain(const rsdos_geometry *geometry,
                             const uint8_t *fat, rsdos_member *member) {
    uint8_t visited[RSDOS_SECTOR];
    uint32_t granule = member->first_granule;
    uint32_t steps = 0U;
    xx_mem_zero(visited, sizeof(visited));
    member->in_range = true;
    for (;;) {
        uint8_t next;
        int64_t offset;
        if (granule >= geometry->granule_count || visited[granule] ||
            ++steps > geometry->granule_count)
            return false;
        visited[granule] = 1U;
        if (!rsdos_granule_offset(geometry, granule, &offset))
            member->in_range = false;
        next = fat[granule];
        if (next >= RSDOS_LAST && next <= RSDOS_LAST_MAX) {
            member->granules = steps;
            member->last_sectors = (uint32_t)(next - RSDOS_LAST);
            return true;
        }
        /* Free (0xFF), 0xCA..0xFE and out-of-range links are all breaks. */
        if ((uint32_t)next >= geometry->granule_count) return false;
        granule = next;
    }
}

static void rsdos_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* The whole volume check: geometry, one table read, the directory walk. */
static bool rsdos_parse(Abstractformat *format, rsdos_stream **result,
                        xx_pd_struct *pd) {
    rsdos_geometry geometry;
    rsdos_stream *stream;
    uint8_t *table;
    uint32_t index;
    if (result) *result = NULL;
    if (!rsdos_geometry_probe(format, &geometry)) return false;
    stream = (rsdos_stream *)xx_mem_calloc(1U, sizeof(*stream));
    table = (uint8_t *)xx_mem_alloc(RSDOS_TABLE_BYTES);
    if (!stream || !table) goto fail;
    stream->geometry = geometry;
    if (!rsdos_read_at(format->device,
                       rsdos_sector_offset(&geometry, RSDOS_DIR_TRACK,
                                           RSDOS_FAT_SECTOR),
                       table, RSDOS_TABLE_BYTES))
        goto fail;
    xx_rt_memcpy(stream->fat, table, RSDOS_SECTOR);
    for (index = 0U; index < geometry.granule_count; ++index) {
        if (!rsdos_fat_byte_valid(stream->fat[index], geometry.granule_count))
            goto fail;
        if (stream->fat[index] == RSDOS_FREE) ++stream->free_granules;
    }
    for (index = 0U; index < RSDOS_ENTRIES; ++index) {
        const uint8_t *entry = table + RSDOS_SECTOR + index * RSDOS_ENTRY;
        rsdos_member *member;
        uint32_t last_bytes, at;
        bool blank = true;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (entry[0] == RSDOS_END) break;
        if (entry[0] == RSDOS_DELETED) continue;
        for (at = 0U; at < 11U; ++at) {
            if (!rsdos_printable(entry[at])) goto fail;
            if (at < 8U && entry[at] != ' ') blank = false;
        }
        if (blank || entry[11] > RSDOS_MAX_TYPE ||
            (entry[12] != 0x00U && entry[12] != 0xFFU))
            goto fail;
        last_bytes = ((uint32_t)entry[14] << 8U) | (uint32_t)entry[15];
        if (last_bytes > RSDOS_SECTOR) goto fail;
        member = &stream->items[stream->count];
        xx_mem_zero(member, sizeof(*member));
        member->entry = index;
        member->first_granule = entry[13];
        member->type = entry[11];
        member->ascii = entry[12];
        if (!rsdos_walk_chain(&geometry, stream->fat, member)) goto fail;
        /* A stored 0 is a full sector, as imgtool reads it. */
        member->last_bytes = last_bytes == 0U ? RSDOS_SECTOR : last_bytes;
        member->size = (int64_t)(member->granules - 1U) * RSDOS_GRANULE;
        if (member->last_sectors != 0U)
            member->size += (int64_t)(member->last_sectors - 1U) *
                                RSDOS_SECTOR + (int64_t)member->last_bytes;
        rsdos_make_name(entry, member->name);
        ++stream->count;
    }
    /* Nothing live is nothing to extract, and too little to tell an empty
     * directory from a 0xFF-filled file. */
    if (stream->count == 0U) goto fail;
    if (!rsdos_make_names_unique(stream->items, stream->count)) goto fail;
    xx_mem_free(table);
    if (result)
        *result = stream;
    else
        rsdos_stream_free(stream);
    return true;
fail:
    if (table) xx_mem_free(table);
    if (stream) rsdos_stream_free(stream);
    return false;
}

/* Stream a member's granules to @p destination (or just read them). */
static bool rsdos_copy_member(Abstractformat *format,
                              const rsdos_stream *stream,
                              const rsdos_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    const rsdos_geometry *geometry = &stream->geometry;
    uint8_t *buffer;
    uint8_t visited[RSDOS_SECTOR];
    uint32_t granule = member->first_granule;
    uint32_t step;
    int64_t remaining = member->size;
    bool ok = true;
    if (!member->in_range || member->size < 0) return false;
    if (member->granules == 0U || member->granules > geometry->granule_count)
        return false;
    buffer = (uint8_t *)xx_mem_alloc(RSDOS_GRANULE);
    if (!buffer) return false;
    xx_mem_zero(visited, sizeof(visited));
    for (step = 0U; ok && step < member->granules; ++step) {
        int64_t offset;
        size_t amount, written = 0U;
        /* The chain was validated at parse time; re-check each hop so a
         * table that changed underneath cannot walk anywhere else. */
        if ((pd && xx_pd_is_stopped(pd)) ||
            granule >= geometry->granule_count || visited[granule] ||
            !rsdos_granule_offset(geometry, granule, &offset)) {
            ok = false;
            break;
        }
        visited[granule] = 1U;
        amount = remaining > (int64_t)RSDOS_GRANULE ? (size_t)RSDOS_GRANULE
                                                    : (size_t)remaining;
        if (step + 1U == member->granules &&
            (int64_t)amount != remaining) {
            ok = false;
            break;
        }
        if (amount != 0U &&
            !rsdos_read_at(format->device, offset, buffer, amount)) {
            ok = false;
            break;
        }
        while (destination && written < amount) {
            ssize_t done = xx_io_write(destination, buffer + written,
                                       amount - written);
            if (done <= 0 || (size_t)done > amount - written) {
                ok = false;
                break;
            }
            written += (size_t)done;
        }
        remaining -= (int64_t)amount;
        if (step + 1U < member->granules) {
            uint8_t next = stream->fat[granule];
            if ((uint32_t)next >= geometry->granule_count) {
                ok = false;
                break;
            }
            granule = next;
        }
    }
    if (ok && remaining != 0) ok = false;
    xx_mem_free(buffer);
    return ok;
}

/* Names come from rsdos_make_name and rsdos_add_suffix; this re-checks the
 * result before anything is created on disk. */
static bool rsdos_safe_output_name(const char *name) {
    size_t length, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (name[0] == ' ' || name[length - 1U] == ' ' ||
        name[length - 1U] == '.')
        return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    return meaningful && !rsdos_is_device_stem(name, length);
}

static bool rsdos_copy_options(xx_list_s *destination,
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

static const xx_var *rsdos_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rsdos_set_record(const rsdos_stream *stream,
                             xx_archive_record *record,
                             const rsdos_member *member) {
    const rsdos_geometry *geometry = &stream->geometry;
    int64_t offset = 0;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        rsdos_sector_offset(geometry, RSDOS_DIR_TRACK, RSDOS_FAT_SECTOR + 1U) +
        (int64_t)member->entry * RSDOS_ENTRY;
    record->header_size = RSDOS_ENTRY;
    record->data_offset =
        rsdos_granule_offset(geometry, member->first_granule, &offset)
            ? offset : 0;
    record->compressed_size = member->size;
    /* Attributes: low byte the DECB file type, next byte the ASCII flag. */
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_ATTRIBUTES,
               (uint64_t)member->type | ((uint64_t)member->ascii << 8U)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_rsdos_fs_init(xx_rsdos_fs *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_RSDOS_FS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-coco-rsdos-disk");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_rsdos_fs_check_is_valid;
    archive->format.handle_base_info = xx_rsdos_fs_handle_base_info;
    archive->format.get_format_size = xx_rsdos_fs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rsdos_fs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rsdos_fs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rsdos_fs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rsdos_fs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rsdos_fs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rsdos_fs_free_archive_records_reading;
    archive->format.destroy = xx_rsdos_fs_vtable_destroy;
    archive->image_size = -1;
}

xx_rsdos_fs *xx_rsdos_fs_create(xx_io_device *device, int64_t base_address) {
    xx_rsdos_fs *archive = (xx_rsdos_fs *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rsdos_fs_init(archive, device, base_address);
    return archive;
}

void xx_rsdos_fs_destroy(xx_rsdos_fs *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rsdos_fs_free(xx_rsdos_fs *archive) {
    if (!archive) return;
    xx_rsdos_fs_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rsdos_fs_vtable_destroy(Abstractformat *self) {
    xx_rsdos_fs_destroy((xx_rsdos_fs *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rsdos_fs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return rsdos_parse(self, NULL, pd);
}

bool xx_rsdos_fs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rsdos_fs *archive = (xx_rsdos_fs *)self;
    rsdos_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    if (!rsdos_parse(self, &stream, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    archive->number_of_records = stream->count;
    archive->image_size = stream->geometry.image_size;
    archive->header_size = stream->geometry.header_size;
    archive->container = stream->geometry.container;
    archive->sides = stream->geometry.sides;
    archive->tracks = stream->geometry.tracks;
    archive->granule_count = stream->geometry.granule_count;
    archive->free_granules = stream->free_granules;
    self->number_of_archive_records = stream->count;
    /* The image is the format: files are scattered over the sector grid. */
    self->format_size = stream->geometry.image_size;
    self->file_type = XX_RSDOS_FS_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_valid = true;
    rsdos_stream_free(stream);
    return true;
}

int64_t xx_rsdos_fs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rsdos_fs_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_rsdos_fs *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_rsdos_fs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    rsdos_stream *stream;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    if (!rsdos_parse(self, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rsdos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = rsdos_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!rsdos_copy_options(&state->options, options) ||
        !rsdos_set_record(stream, &state->current_record,
                          &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rsdos_fs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rsdos_fs_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    rsdos_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (rsdos_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = rsdos_set_record(stream, &state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rsdos_fs_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    rsdos_stream *stream;
    const rsdos_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (rsdos_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!member->in_range) return false;
    path_option = rsdos_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the granules through, which verifies them. */
        return rsdos_copy_member(self, stream, member, NULL, pd);
    if (!rsdos_safe_output_name(member->name)) return false;
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
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = rsdos_copy_member(self, stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rsdos_fs_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

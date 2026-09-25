/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bondwell 2 disk image: a raw single-sided 80-track dump of 256-byte
 * sectors (17 or 18 per track) carrying a CP/M 2.2 filesystem.  The
 * geometry is MAME's bw2 floppy format; the CP/M parameters (2 system
 * tracks, 2K blocks, no skew) are the Bondwell 2 BIOS ones.  The layout and
 * every rule the parser enforces are listed in xx_bondwell_2_disk.h.  The
 * code is written from the CP/M 2.2 on-disk structure; no reference code was
 * copied.
 *
 * The parse is also the late detection probe, so it is ordered cheapest
 * first: the exact image size, then one 4 KiB directory read validated
 * entry by entry, and only then any allocation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bondwell_2_disk/xx_bondwell_2_disk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing shim: the file compiles before the enum exists. */
#ifdef BONDWELL_2_DISK
#define XX_BONDWELL_2_DISK_FILE_TYPE XX_FILE_TYPE_BONDWELL_2_DISK
#else
#define XX_BONDWELL_2_DISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BW2_SECTOR 256
#define BW2_TRACKS 80
#define BW2_SYSTEM_TRACKS 2
#define BW2_BLOCK 2048
#define BW2_RECORD 128
#define BW2_RECORDS_PER_BLOCK (BW2_BLOCK / BW2_RECORD)
#define BW2_POINTERS 16
#define BW2_RECORDS_PER_ENTRY (BW2_POINTERS * BW2_RECORDS_PER_BLOCK)
#define BW2_ENTRY 32
#define BW2_SMALL_DIRECTORY 64
#define BW2_LARGE_DIRECTORY 128
#define BW2_DIRECTORY_BYTES (BW2_LARGE_DIRECTORY * BW2_ENTRY)
#define BW2_FREE 0xE5U
#define BW2_MAX_USER 15U
#define BW2_MAX_EX 31U
#define BW2_MAX_S2 15U
#define BW2_MAX_RC 128U
#define BW2_MAX_S1 128U
/* 18 sectors: 78 * 18 * 256 / 2048 = 175.5, so 175 whole blocks. */
#define BW2_MAX_BLOCKS 175U
/* "user15/" + 8 + "~127" + "." + 3 + NUL = 7 + 8 + 4 + 1 + 3 + 1 = 24. */
#define BW2_NAME_BUFFER 32

typedef struct bw2_geometry_s {
    int64_t image_size;
    int64_t directory_offset; /**< Absolute: base_address + system tracks. */
    uint32_t sectors_per_track;
    uint32_t block_count;
    uint32_t directory_entries;
} bw2_geometry;

typedef struct bw2_member_s {
    char name[BW2_NAME_BUFFER];
    uint8_t blocks[BW2_MAX_BLOCKS]; /**< 0 = hole. */
    uint32_t slots;                 /**< 2K slots covering the size. */
    int64_t size;
    uint32_t attributes;
    uint32_t first_entry;
    uint32_t lowest_extent_entry;
} bw2_member;

typedef struct bw2_stream_s {
    bw2_geometry geometry;
    bw2_member *items;
    size_t count;
    size_t index;
} bw2_stream;

static bool bw2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool bw2_geometry_of(Abstractformat *format, bw2_geometry *out) {
    int64_t total, size;
    uint32_t spt;
    if (!format || !format->device || format->base_address < 0 || !out)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size == (int64_t)BW2_TRACKS * 17 * BW2_SECTOR)
        spt = 17U;
    else if (size == (int64_t)BW2_TRACKS * 18 * BW2_SECTOR)
        spt = 18U;
    else
        return false;
    xx_mem_zero(out, sizeof(*out));
    out->image_size = size;
    out->sectors_per_track = spt;
    out->directory_offset = format->base_address +
                            (int64_t)BW2_SYSTEM_TRACKS * spt * BW2_SECTOR;
    out->block_count = (uint32_t)(((uint32_t)(BW2_TRACKS - BW2_SYSTEM_TRACKS) *
                                   spt * BW2_SECTOR) / BW2_BLOCK);
    return true;
}

/* CP/M's FCB parser splits names at these, so no CP/M program can create a
 * file containing one; control bytes are refused as well. */
static bool bw2_is_name_char(uint8_t c) {
    if (c < 0x21U || c > 0x7EU) return false;
    switch (c) {
    case '.': case ',': case ';': case ':': case '=': case '?': case '*':
    case '<': case '>': case '[': case ']':
        return false;
    default:
        return true;
    }
}

/* A field of `length` 7-bit characters: name characters, then only spaces.
 * `required` demands a non-blank first character. */
static bool bw2_check_field(const uint8_t *field, size_t length,
                            bool required) {
    size_t index;
    bool padding = false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)(field[index] & 0x7FU);
        if (c == ' ') {
            if (index == 0U && required) return false;
            padding = true;
        } else if (padding || !bw2_is_name_char(c)) {
            return false;
        }
    }
    return true;
}

/* -1 invalid, 0 free, 1 a live file entry.  Allocated blocks are marked in
 * `bitmap`; a block seen twice is a cross-link and invalidates the entry. */
static int bw2_check_entry(const uint8_t *entry, uint32_t min_block,
                           uint32_t block_count, uint8_t *bitmap) {
    uint32_t ex, s2, rc, records, needed, index;
    if (entry[0] == BW2_FREE) return 0;
    if (entry[0] > BW2_MAX_USER) return -1;
    if (!bw2_check_field(entry + 1, 8U, true) ||
        !bw2_check_field(entry + 9, 3U, false))
        return -1;
    ex = entry[12];
    s2 = entry[14];
    rc = entry[15];
    if (ex > BW2_MAX_EX || s2 > BW2_MAX_S2 || rc > BW2_MAX_RC ||
        entry[13] > BW2_MAX_S1)
        return -1;
    records = (ex & 1U) * BW2_RECORD + rc;
    needed = (records + BW2_RECORDS_PER_BLOCK - 1U) / BW2_RECORDS_PER_BLOCK;
    for (index = 0U; index < BW2_POINTERS; ++index) {
        uint32_t block = entry[16U + index];
        if (block == 0U) continue;
        if (index >= needed || block < min_block || block >= block_count)
            return -1;
        if ((bitmap[block >> 3U] & (uint8_t)(1U << (block & 7U))) != 0U)
            return -1;
        bitmap[block >> 3U] |= (uint8_t)(1U << (block & 7U));
    }
    return 1;
}

/* Validate entries [first, last); count the live ones into *files. */
static bool bw2_check_range(const uint8_t *directory, uint32_t first,
                            uint32_t last, uint32_t min_block,
                            uint32_t block_count, uint8_t *bitmap,
                            uint32_t *files) {
    uint32_t index;
    for (index = first; index < last; ++index) {
        int kind = bw2_check_entry(directory + (size_t)index * BW2_ENTRY,
                                   min_block, block_count, bitmap);
        if (kind < 0) return false;
        if (kind > 0) ++*files;
    }
    return true;
}

/* Size, directory read and entry validation.  Nothing is allocated. */
static bool bw2_probe(Abstractformat *format, uint8_t *directory,
                      bw2_geometry *geometry, uint32_t *files) {
    uint8_t bitmap[32];
    uint8_t second[32];
    uint32_t low_files = 0U, high_files = 0U;
    if (!bw2_geometry_of(format, geometry) ||
        !bw2_read_at(format->device, geometry->directory_offset, directory,
                     BW2_DIRECTORY_BYTES))
        return false;
    xx_mem_zero(bitmap, sizeof(bitmap));
    /* Block 0 is directory in either layout; block 1 may be data. */
    if (!bw2_check_range(directory, 0U, BW2_SMALL_DIRECTORY, 1U,
                         geometry->block_count, bitmap, &low_files))
        return false;
    geometry->directory_entries = BW2_SMALL_DIRECTORY;
    if ((bitmap[0] & 0x02U) == 0U) {
        xx_rt_memcpy(second, bitmap, sizeof(second));
        if (bw2_check_range(directory, BW2_SMALL_DIRECTORY,
                            BW2_LARGE_DIRECTORY, 2U, geometry->block_count,
                            second, &high_files))
            geometry->directory_entries = BW2_LARGE_DIRECTORY;
        else
            high_files = 0U;
    }
    *files = low_files + high_files;
    return *files != 0U;
}

static bool bw2_same_file(const uint8_t *a, const uint8_t *b) {
    size_t index;
    if (a[0] != b[0]) return false;
    for (index = 1U; index < 12U; ++index)
        if ((a[index] & 0x7FU) != (b[index] & 0x7FU)) return false;
    return true;
}

static uint32_t bw2_logical_extent(const uint8_t *entry) {
    return (uint32_t)entry[14] * (BW2_MAX_EX + 1U) + (uint32_t)entry[12];
}

static uint32_t bw2_attributes(const uint8_t *entry) {
    uint32_t value = 0U;
    if (entry[9] & 0x80U) value |= 0x01U;
    if (entry[10] & 0x80U) value |= 0x02U;
    if (entry[11] & 0x80U) value |= 0x04U;
    if (entry[1] & 0x80U) value |= 0x10U;
    if (entry[2] & 0x80U) value |= 0x20U;
    if (entry[3] & 0x80U) value |= 0x40U;
    if (entry[4] & 0x80U) value |= 0x80U;
    return value;
}

static char bw2_out_char(uint8_t c) {
    c = (uint8_t)(c & 0x7FU);
    if (c < 0x21U || c > 0x7EU) return '_';
    switch (c) {
    case '/': case '\\': case ':': case '*': case '?': case '"': case '<':
    case '>': case '|': case '~': case '.':
        return '_';
    default:
        return (char)c;
    }
}

static char bw2_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool bw2_stem_is(const char *stem, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (word[index] == 0 || bw2_fold(stem[index]) != word[index])
            return false;
    return word[length] == 0;
}

static bool bw2_is_device_stem(const char *stem, size_t length) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (bw2_stem_is(stem, length, devices[index])) return true;
    if (length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (bw2_stem_is(stem, 3U, "COM") || bw2_stem_is(stem, 3U, "LPT")))
        return true;
    return false;
}

static size_t bw2_put_number(char *out, uint32_t value) {
    char digits[10];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "[userN/][_]STEM[~K][.TYP]" into `out` (BW2_NAME_BUFFER bytes). */
static void bw2_make_name(const uint8_t *entry, bool suffix, uint32_t key,
                          char *out) {
    char stem[8], type[3];
    size_t stem_length = 0U, type_length = 0U, at = 0U, index;
    for (index = 0U; index < 8U && (entry[1 + index] & 0x7FU) != ' ';
         ++index)
        stem[stem_length++] = bw2_out_char(entry[1 + index]);
    for (index = 0U; index < 3U && (entry[9 + index] & 0x7FU) != ' ';
         ++index)
        type[type_length++] = bw2_out_char(entry[9 + index]);
    if (entry[0] != 0U) {
        out[at++] = 'u'; out[at++] = 's'; out[at++] = 'e'; out[at++] = 'r';
        at += bw2_put_number(out + at, entry[0]);
        out[at++] = '/';
    }
    if (stem_length == 0U) stem[stem_length++] = '_';
    if (bw2_is_device_stem(stem, stem_length)) out[at++] = '_';
    for (index = 0U; index < stem_length; ++index) out[at++] = stem[index];
    if (suffix) {
        out[at++] = '~';
        at += bw2_put_number(out + at, key);
    }
    if (type_length != 0U) {
        out[at++] = '.';
        for (index = 0U; index < type_length; ++index)
            out[at++] = type[index];
    }
    out[at] = 0;
}

static bool bw2_names_equal(const char *a, const char *b) {
    size_t index = 0U;
    for (;;) {
        if (bw2_fold(a[index]) != bw2_fold(b[index])) return false;
        if (a[index] == 0) return true;
        ++index;
    }
}

static void bw2_stream_free(void *opaque) {
    bw2_stream *stream = (bw2_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool bw2_parse(Abstractformat *format, bw2_stream **result) {
    uint8_t directory[BW2_DIRECTORY_BYTES];
    uint8_t owner[BW2_LARGE_DIRECTORY]; /* member index + 1, 0 = none */
    bw2_geometry geometry;
    bw2_stream *stream = NULL;
    bw2_member *items = NULL;
    uint32_t files = 0U, index, other;
    size_t count = 0U, member_index;
    if (!result || !bw2_probe(format, directory, &geometry, &files))
        return false;
    items = (bw2_member *)xx_mem_calloc(files, sizeof(*items));
    if (!items) return false;
    xx_mem_zero(owner, sizeof(owner));

    /* Group entries into files in directory order; refuse two entries for
     * the same extent group of one file. */
    for (index = 0U; index < geometry.directory_entries; ++index) {
        const uint8_t *entry = directory + (size_t)index * BW2_ENTRY;
        if (entry[0] == BW2_FREE) continue;
        for (other = 0U; other < index; ++other) {
            const uint8_t *earlier = directory + (size_t)other * BW2_ENTRY;
            if (earlier[0] == BW2_FREE || !bw2_same_file(entry, earlier))
                continue;
            if ((bw2_logical_extent(entry) >> 1U) ==
                (bw2_logical_extent(earlier) >> 1U))
                goto fail;
            owner[index] = owner[other];
            break;
        }
        if (owner[index] == 0U) {
            bw2_member *member;
            if (count >= files) goto fail;
            member = &items[count];
            member->first_entry = index;
            member->lowest_extent_entry = index;
            member->attributes = bw2_attributes(entry);
            owner[index] = (uint8_t)(++count);
        }
    }

    /* Size and block map of every file. */
    for (member_index = 0U; member_index < count; ++member_index) {
        bw2_member *member = &items[member_index];
        uint32_t records = 0U, lowest = 0xFFFFFFFFU;
        for (index = 0U; index < geometry.directory_entries; ++index) {
            const uint8_t *entry = directory + (size_t)index * BW2_ENTRY;
            uint32_t extent, end;
            if (owner[index] != member_index + 1U) continue;
            extent = bw2_logical_extent(entry);
            end = extent * BW2_RECORD + entry[15];
            if (end > records) records = end;
            if (extent < lowest) {
                lowest = extent;
                member->lowest_extent_entry = index;
            }
        }
        if ((uint64_t)records * BW2_RECORD >
            (uint64_t)geometry.block_count * BW2_BLOCK)
            goto fail;
        member->size = (int64_t)records * BW2_RECORD;
        member->slots = (records + BW2_RECORDS_PER_BLOCK - 1U) /
                        BW2_RECORDS_PER_BLOCK;
        if (member->slots > BW2_MAX_BLOCKS) goto fail;
        for (index = 0U; index < geometry.directory_entries; ++index) {
            const uint8_t *entry = directory + (size_t)index * BW2_ENTRY;
            uint32_t base, pointer;
            if (owner[index] != member_index + 1U) continue;
            base = (bw2_logical_extent(entry) >> 1U) * BW2_POINTERS;
            for (pointer = 0U; pointer < BW2_POINTERS; ++pointer) {
                uint8_t block = entry[16U + pointer];
                if (block == 0U) continue;
                /* bw2_check_entry kept every pointer inside its entry's
                 * record count, and `records` covers every entry. */
                if (base + pointer >= member->slots) goto fail;
                member->blocks[base + pointer] = block;
            }
        }
        member->attributes = bw2_attributes(
            directory + (size_t)member->lowest_extent_entry * BW2_ENTRY);
    }

    /* Names: unique case-insensitively, suffixing the later duplicates. */
    for (member_index = 0U; member_index < count; ++member_index) {
        bw2_member *member = &items[member_index];
        const uint8_t *entry =
            directory + (size_t)member->first_entry * BW2_ENTRY;
        size_t earlier;
        bw2_make_name(entry, false, 0U, member->name);
        for (earlier = 0U; earlier < member_index; ++earlier) {
            if (bw2_names_equal(items[earlier].name, member->name)) {
                bw2_make_name(entry, true, member->first_entry, member->name);
                break;
            }
        }
    }

    stream = (bw2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->geometry = geometry;
    stream->items = items;
    stream->count = count;
    *result = stream;
    return true;
fail:
    xx_mem_free(items);
    return false;
}

static bool bw2_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written,
                                     size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Stream a file block by block into `destination`, or just read it through
 * when that is NULL.  Holes are written as zeros. */
static bool bw2_copy_member(Abstractformat *format, const bw2_geometry *geometry,
                            const bw2_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *block;
    int64_t remaining = member->size;
    uint32_t slot;
    bool ok = true;
    if (member->size < 0 || member->slots > BW2_MAX_BLOCKS) return false;
    if (member->size == 0) return true;
    block = (uint8_t *)xx_mem_alloc(BW2_BLOCK);
    if (!block) return false;
    for (slot = 0U; ok && slot < member->slots && remaining > 0; ++slot) {
        size_t chunk = remaining > BW2_BLOCK ? (size_t)BW2_BLOCK
                                             : (size_t)remaining;
        uint32_t number = member->blocks[slot];
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (number == 0U) {
            xx_mem_zero(block, chunk);
        } else if (number >= geometry->block_count ||
                   !bw2_read_at(format->device,
                                geometry->directory_offset +
                                    (int64_t)number * BW2_BLOCK,
                                block, chunk)) {
            ok = false;
            break;
        }
        if (destination && !bw2_write_all(destination, block, chunk)) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(block);
    return ok && remaining == 0;
}

/* The names are built by bw2_make_name; re-check before touching the disk:
 * an optional "userN/" folder, then one component of safe characters that
 * is not a device name. */
static bool bw2_safe_output_name(const char *name) {
    size_t length, index, start = 0U, dot;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length >= BW2_NAME_BUFFER) return false;
    if (length > 5U && name[0] == 'u' && name[1] == 's' && name[2] == 'e' &&
        name[3] == 'r') {
        index = 4U;
        while (index < length && name[index] >= '0' && name[index] <= '9')
            ++index;
        if (index == 4U || index > 6U || index >= length || name[index] != '/')
            return false;
        start = index + 1U;
    }
    if (start >= length) return false;
    dot = length;
    for (index = start; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x21U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c == '.') {
            if (dot != length) return false;
            dot = index;
        }
    }
    if (dot == start || dot + 1U == length) return false;
    return !bw2_is_device_stem(name + start, dot - start);
}

static bool bw2_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *bw2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bw2_set_record(const bw2_stream *stream, xx_archive_record *record,
                           const bw2_member *member) {
    int64_t first = stream->geometry.directory_offset;
    if (member->slots != 0U && member->blocks[0] != 0U)
        first += (int64_t)member->blocks[0] * BW2_BLOCK;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->geometry.directory_offset +
                            (int64_t)member->first_entry * BW2_ENTRY;
    record->header_size = BW2_ENTRY;
    record->data_offset = first;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_bondwell_2_disk_init(xx_bondwell_2_disk *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BONDWELL_2_DISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-bondwell-2-disk-image");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_bondwell_2_disk_check_is_valid;
    archive->format.handle_base_info = xx_bondwell_2_disk_handle_base_info;
    archive->format.get_format_size = xx_bondwell_2_disk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bondwell_2_disk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bondwell_2_disk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bondwell_2_disk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bondwell_2_disk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bondwell_2_disk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bondwell_2_disk_free_archive_records_reading;
    archive->image_size = -1;
}

xx_bondwell_2_disk *xx_bondwell_2_disk_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_bondwell_2_disk *archive =
        (xx_bondwell_2_disk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bondwell_2_disk_init(archive, device, base_address);
    return archive;
}

void xx_bondwell_2_disk_destroy(xx_bondwell_2_disk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bondwell_2_disk_free(xx_bondwell_2_disk *archive) {
    if (!archive) return;
    xx_bondwell_2_disk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_bondwell_2_disk_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    uint8_t directory[BW2_DIRECTORY_BYTES];
    bw2_geometry geometry;
    uint32_t files = 0U;
    bw2_stream *stream;
    (void)pd;
    /* The cheap probe rejects garbage without allocating; the full parse
     * then adds the per-file checks (duplicate extents, file size). */
    if (!bw2_probe(format, directory, &geometry, &files)) return false;
    if (!bw2_parse(format, &stream)) return false;
    bw2_stream_free(stream);
    return true;
}

bool xx_bondwell_2_disk_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    bw2_stream *stream;
    xx_bondwell_2_disk *archive;
    (void)pd;
    if (!format || !bw2_parse(format, &stream)) return false;
    archive = (xx_bondwell_2_disk *)format;
    archive->number_of_records = stream->count;
    archive->image_size = stream->geometry.image_size;
    archive->sectors_per_track = stream->geometry.sectors_per_track;
    archive->block_count = stream->geometry.block_count;
    archive->directory_entries = stream->geometry.directory_entries;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->geometry.image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    bw2_stream_free(stream);
    return true;
}

int64_t xx_bondwell_2_disk_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bondwell_2_disk_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_bondwell_2_disk_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bondwell_2_disk_handle_base_info(format, pd))
               ? ((xx_bondwell_2_disk *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_bondwell_2_disk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bw2_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!bw2_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bw2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bw2_stream_free;
    state->total_records = stream->count;
    if (!bw2_copy_options(&state->options, options) ||
        !bw2_set_record(stream, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bondwell_2_disk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_bondwell_2_disk_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    bw2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bw2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bw2_set_record(stream, &state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bondwell_2_disk_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    bw2_stream *stream;
    bw2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (bw2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = bw2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the file through, which verifies it. */
        return bw2_copy_member(format, &stream->geometry, member, NULL, pd);
    if (!bw2_safe_output_name(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
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
        result = bw2_copy_member(format, &stream->geometry, member,
                                 destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bondwell_2_disk_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

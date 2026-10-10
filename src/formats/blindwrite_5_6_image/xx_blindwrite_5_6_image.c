/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BlindWrite 5/6 disc image descriptor (.b5t / .b6t).  xx_blindwrite_5_6_image.h
 * carries the field table and the member layout.  Written from the
 * descriptor's structure; libmirage's image-b6t parser (GPL) was read only to
 * learn the field order and sizes and how the data blocks map sectors onto
 * the .b00/.b01 data files.  No code was taken from it.
 *
 * The descriptor holds no sector data.  It is walked completely, section by
 * section, with every length checked against the bytes that are left, and it
 * must end in the "BWT5 STREAM FOOT" trailer, so a stray signature cannot
 * pass.  The data-file layout is resolved per track: a track's sectors are
 * looked up in the data blocks (which need not be sorted and may split one
 * track over several files), and a track whose sectors are not all covered
 * is still listed but cannot be unpacked.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/blindwrite_5_6_image/xx_blindwrite_5_6_image.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the enumerator is registered. */
#ifdef BLINDWRITE_5_6_IMAGE
#define XX_BLINDWRITE_5_6_IMAGE_FILE_TYPE XX_FILE_TYPE_BLINDWRITE_5_6_IMAGE
#else
#define XX_BLINDWRITE_5_6_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BW_SIGNATURE "BWT5 STREAM SIGN"
#define BW_FOOTER "BWT5 STREAM FOOT"
#define BW_TAG_SIZE 16U
#define BW_DISC_BLOCK_1 112U
/* unknown[32] + INQUIRY identifiers[48] + volume identifier[32] */
#define BW_FIXED_GAP 112U
#define BW_DISC_BLOCK_2 20U
#define BW_DATA_BLOCK 52U
#define BW_SESSION 16U
#define BW_ENTRY 64U
#define BW_ENTRY_EXTRA 8U
#define BW_MAX_SESSIONS 99U
#define BW_MAX_TRACKS 99U
#define BW_MAX_NAME_BYTES 2048U
#define BW_MAX_SECTOR 65536
#define BW_MAX_BASENAME 255U
#define BW_BLOCKS XX_BLINDWRITE_5_6_IMAGE_MAX_BLOCKS
/* The smallest descriptor: header, both disc blocks, the fixed gap, an empty
 * data-block list, one session with one track, length and footer. */
#define BW_MIN_SIZE (BW_TAG_SIZE + BW_DISC_BLOCK_1 + BW_FIXED_GAP + BW_DISC_BLOCK_2 + 8U + BW_SESSION + BW_ENTRY + 4U + BW_TAG_SIZE)

typedef struct bw_block_s {
    uint32_t type;
    uint32_t length_bytes;
    uint32_t offset;
    int32_t start;
    int32_t sectors;
    uint32_t sector_size; /* 0: the block cannot be used */
    char *name;           /* UTF-8, never NULL once parsed */
} bw_block;

typedef struct bw_track_s {
    uint8_t type;
    uint8_t point;
    uint16_t session;
    int32_t start;
    int32_t length;
    uint32_t pregap;
    int64_t entry_offset;
    int64_t size;  /* -1: sectors not all covered by the data blocks */
    char name[16]; /* "track99.cdda" */
} bw_track;

typedef struct bw_image_s {
    bw_block blocks[BW_BLOCKS];
    uint32_t block_count;
    bw_track tracks[BW_MAX_TRACKS];
    uint32_t track_count;
    uint32_t session_count;
    uint16_t disc_type;
    int64_t size;
    uint32_t index; /* record iteration cursor */
    xx_blindwrite_5_6_image *owner;
} bw_image;

typedef struct bw_cursor_s {
    xx_io_device *device;
    int64_t base;
    int64_t position; /* relative to base */
    int64_t end;      /* relative to base */
} bw_cursor;

static const char *const bw_mode_names[7] = {"", "Audio", "Mode 1", "Mode 2", "Mode 2 Form 1", "Mode 2 Form 2", "DVD"};

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static size_t bw_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static bool bw_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t n = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static bool bw_write_all(xx_io_device *device, const uint8_t *buffer, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t n = xx_io_write(device, buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static bool bw_take(bw_cursor *c, void *buffer, uint32_t size)
{
    if (c->end - c->position < (int64_t)size) return false;
    if (!bw_read_at(c->device, c->base + c->position, buffer, size)) return false;
    c->position += size;
    return true;
}

static bool bw_skip(bw_cursor *c, uint32_t size)
{
    if (c->end - c->position < (int64_t)size) return false;
    c->position += size;
    return true;
}

/* UTF-16LE to UTF-8; unpaired surrogates become U+FFFD. */
static char *bw_utf16_to_utf8(const uint8_t *text, uint32_t units)
{
    char *out = (char *)xx_mem_alloc((size_t)units * 3U + 1U);
    size_t length = 0U;
    uint32_t index = 0U;
    if (!out) return NULL;
    while (index < units) {
        uint32_t code = xx_data_get_u16(text + 2U * index++, 2, 0, false);
        if (code >= 0xD800U && code <= 0xDBFFU && index < units) {
            uint32_t low = xx_data_get_u16(text + 2U * index, 2, 0, false);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
                ++index;
            } else {
                code = 0xFFFDU;
            }
        } else if (code >= 0xD800U && code <= 0xDFFFU) {
            code = 0xFFFDU;
        }
        if (code == 0U) break;
        if (code < 0x80U) {
            out[length++] = (char)code;
        } else if (code < 0x800U) {
            out[length++] = (char)(0xC0U | (code >> 6U));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            out[length++] = (char)(0xE0U | (code >> 12U));
            out[length++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        } else {
            out[length++] = (char)(0xF0U | (code >> 18U));
            out[length++] = (char)(0x80U | ((code >> 12U) & 0x3FU));
            out[length++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    out[length] = 0;
    return out;
}

bool xx_blindwrite_5_6_image_test_magic(const uint8_t *magic, size_t magic_size)
{
    return magic && magic_size >= BW_TAG_SIZE && xx_rt_memcmp(magic, BW_SIGNATURE, BW_TAG_SIZE) == 0;
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */

static void bw_image_free(void *opaque)
{
    bw_image *image = (bw_image *)opaque;
    uint32_t index;
    if (!image) return;
    for (index = 0U; index < image->block_count; ++index)
        if (image->blocks[index].name) xx_mem_free(image->blocks[index].name);
    xx_mem_free(image);
}

static bool bw_parse_blocks(bw_image *image, bw_cursor *c)
{
    uint8_t head[BW_DATA_BLOCK];
    uint8_t *name = NULL;
    uint32_t count, path_length, index;
    if (!bw_take(c, head, 8U)) return false;
    count = xx_data_get_u32(head, 4, 0, false);
    path_length = xx_data_get_u32(head + 4, 4, 0, false);
    if (count > BW_BLOCKS || !bw_skip(c, path_length)) return false;
    name = (uint8_t *)xx_mem_alloc(BW_MAX_NAME_BYTES);
    if (!name) return false;
    for (index = 0U; index < count; ++index) {
        bw_block *block = &image->blocks[index];
        uint32_t name_length;
        if (!bw_take(c, head, BW_DATA_BLOCK)) break;
        block->type = xx_data_get_u32(head, 4, 0, false);
        block->length_bytes = xx_data_get_u32(head + 4, 4, 0, false);
        block->offset = xx_data_get_u32(head + 24, 4, 0, false);
        block->start = xx_data_get_i32(head + 40, 4, 0, false);
        block->sectors = xx_data_get_i32(head + 44, 4, 0, false);
        name_length = xx_data_get_u32(head + 48, 4, 0, false);
        if ((name_length & 1U) || name_length > BW_MAX_NAME_BYTES || !bw_take(c, name, name_length) || !bw_skip(c, 4U)) break;
        block->name = bw_utf16_to_utf8(name, name_length / 2U);
        if (!block->name) break;
        image->block_count = index + 1U;
        if (block->sectors > 0 && block->length_bytes % (uint32_t)block->sectors == 0U) {
            uint32_t size = block->length_bytes / (uint32_t)block->sectors;
            if (size >= 1U && size <= (uint32_t)BW_MAX_SECTOR) block->sector_size = size;
        }
    }
    xx_mem_free(name);
    return image->block_count == count;
}

static bool bw_parse_sessions(bw_image *image, bw_cursor *c, uint32_t sessions)
{
    uint8_t entry[BW_ENTRY];
    uint32_t session, index;
    bool seen[BW_MAX_TRACKS + 1U];
    xx_rt_memset(seen, 0, sizeof(seen));
    for (session = 0U; session < sessions; ++session) {
        uint32_t entries;
        if (!bw_take(c, entry, BW_SESSION)) return false;
        entries = entry[2];
        for (index = 0U; index < entries; ++index) {
            int64_t entry_offset = c->position;
            bw_track *track;
            uint8_t type, point;
            if (!bw_take(c, entry, BW_ENTRY)) return false;
            type = entry[0];
            if (type > 6U) return false;
            if (type == 0U) continue;
            if (type != 6U && !bw_skip(c, BW_ENTRY_EXTRA)) return false;
            point = entry[12];
            if (point == 0U || point > BW_MAX_TRACKS || seen[point] || image->track_count >= BW_MAX_TRACKS) return false;
            seen[point] = true;
            track = &image->tracks[image->track_count++];
            track->type = type;
            track->point = point;
            track->session = (uint16_t)(session + 1U);
            track->pregap = xx_data_get_u32(entry + 22, 4, 0, false);
            track->start = xx_data_get_i32(entry + 42, 4, 0, false);
            track->length = xx_data_get_i32(entry + 46, 4, 0, false);
            track->entry_offset = entry_offset;
            if (track->length < 0) return false;
        }
    }
    return image->track_count > 0U;
}

/* The block holding @p sector, first match in descriptor order. */
static const bw_block *bw_find_block(const bw_image *image, int64_t sector, uint32_t *which)
{
    uint32_t index;
    for (index = 0U; index < image->block_count; ++index) {
        const bw_block *block = &image->blocks[index];
        if (block->sector_size && sector >= block->start && sector < (int64_t)block->start + block->sectors) {
            if (which) *which = index;
            return block;
        }
    }
    return NULL;
}

static void bw_resolve(bw_image *image)
{
    uint32_t index;
    for (index = 0U; index < image->track_count; ++index) {
        bw_track *track = &image->tracks[index];
        int64_t sector = track->start, left = track->length, size = 0;
        uint32_t first_size = 0U, steps = 0U;
        const char *extension = "bin";
        while (left > 0 && size >= 0) {
            const bw_block *block = bw_find_block(image, sector, NULL);
            int64_t take;
            if (!block || ++steps > image->block_count) {
                size = -1;
                break;
            }
            if (!first_size) first_size = block->sector_size;
            take = (int64_t)block->start + block->sectors - sector;
            if (take > left) take = left;
            size += take * (int64_t)block->sector_size;
            sector += take;
            left -= take;
        }
        if (!first_size) {
            const bw_block *block = bw_find_block(image, track->start, NULL);
            if (block) first_size = block->sector_size;
        }
        track->size = size;
        if (track->type == 1U) {
            if (first_size == 2352U) extension = "cdda";
        } else if (first_size == 2048U) {
            extension = "iso";
        }
        (void)xx_rt_snprintf(track->name, sizeof(track->name), "track%02u.%s", (unsigned)track->point, extension);
    }
}

static bw_image *bw_load(Abstractformat *format)
{
    uint8_t buffer[BW_DISC_BLOCK_1];
    uint8_t lengths[BW_DISC_BLOCK_2];
    bw_image *image;
    bw_cursor cursor;
    int64_t total;
    uint32_t sessions, info_length;
    uint16_t disc_type;
    bool ok;
    if (!format || !format->device || format->base_address < 0) return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address || total - format->base_address < (int64_t)BW_MIN_SIZE) return NULL;
    cursor.device = format->device;
    cursor.base = format->base_address;
    cursor.position = 0;
    cursor.end = total - format->base_address;
    if (!bw_take(&cursor, buffer, BW_TAG_SIZE) || !xx_blindwrite_5_6_image_test_magic(buffer, BW_TAG_SIZE) || !bw_take(&cursor, buffer, BW_DISC_BLOCK_1)) return NULL;
    disc_type = xx_data_get_u16(buffer + 32, 2, 0, false);
    sessions = xx_data_get_u16(buffer + 34, 2, 0, false);
    if (sessions == 0U || sessions > BW_MAX_SESSIONS) return NULL;
    info_length = (disc_type >= 0x08U && disc_type <= 0x0AU) ? xx_data_get_u16(buffer + 86, 2, 0, false) : xx_data_get_u32(buffer + 108, 4, 0, false);
    if (!bw_skip(&cursor, BW_FIXED_GAP) || !bw_take(&cursor, lengths, BW_DISC_BLOCK_2) || !bw_skip(&cursor, xx_data_get_u32(lengths, 4, 0, false)) || /* mode page 0x2A */
        !bw_skip(&cursor, xx_data_get_u32(lengths + 4, 4, 0, false)) ||                                                                               /* unknown block */
        !bw_skip(&cursor, xx_data_get_u16(buffer + 80, 2, 0, false)) ||                                                                               /* PMA */
        !bw_skip(&cursor, xx_data_get_u16(buffer + 82, 2, 0, false)) ||                                                                               /* ATIP */
        !bw_skip(&cursor, xx_data_get_u16(buffer + 84, 2, 0, false)) ||                                                                               /* CD-TEXT */
        !bw_skip(&cursor, xx_data_get_u32(buffer + 88, 4, 0, false)) ||                                                                               /* BCA */
        !bw_skip(&cursor, xx_data_get_u32(buffer + 104, 4, 0, false)) ||                                                                              /* DVD structures */
        !bw_skip(&cursor, info_length))
        return NULL;
    image = (bw_image *)xx_mem_calloc(1U, sizeof(*image));
    if (!image) return NULL;
    ok = bw_parse_blocks(image, &cursor) && bw_parse_sessions(image, &cursor, sessions) &&
         bw_skip(&cursor, xx_data_get_u32(lengths + 16, 4, 0, false)) && /* internal DPM data */
         bw_skip(&cursor, 4U) &&                                         /* declared length */
         bw_take(&cursor, buffer, BW_TAG_SIZE) && xx_rt_memcmp(buffer, BW_FOOTER, BW_TAG_SIZE) == 0;
    if (!ok) {
        bw_image_free(image);
        return NULL;
    }
    image->session_count = sessions;
    image->disc_type = disc_type;
    image->size = cursor.position;
    image->owner = (xx_blindwrite_5_6_image *)format;
    bw_resolve(image);
    return image;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool bw_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *bw_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bw_set_record(xx_archive_record *record, const bw_track *track)
{
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = track->entry_offset;
    record->header_size = (int64_t)BW_ENTRY;
    /* The payload lives in a data file, not in the descriptor's device. */
    record->data_offset = -1;
    record->compressed_size = track->size >= 0 ? track->size : 0;
    ok = xx_archive_record_set_original_name(record, track->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
         xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, bw_mode_names[track->type]) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (ok && track->size >= 0)
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)track->size) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)track->size);
    return ok;
}

/* True when every data block the track's sectors come from has a device, so
 * no output file is created for a track that cannot be read at all. */
static bool bw_track_attached(const bw_image *image, const bw_track *track)
{
    int64_t sector = track->start, left = track->length;
    uint32_t steps = 0U;
    if (!image->owner || track->size < 0) return false;
    while (left > 0) {
        uint32_t which = 0U;
        const bw_block *block = bw_find_block(image, sector, &which);
        int64_t take;
        if (!block || ++steps > image->block_count || !image->owner->data[which]) return false;
        take = (int64_t)block->start + block->sectors - sector;
        if (take > left) take = left;
        sector += take;
        left -= take;
    }
    return true;
}

/* Copy one track's sectors to @p destination (NULL only reads them). */
static bool bw_copy_track(const bw_image *image, const bw_track *track, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = bw_capacity();
    const xx_blindwrite_5_6_image *owner = image->owner;
    int64_t sector = track->start, left = track->length;
    uint32_t steps = 0U;
    uint8_t *buffer;
    bool result = true;
    if (!owner || track->size < 0) return false;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (left > 0 && result) {
        uint32_t which = 0U;
        const bw_block *block = bw_find_block(image, sector, &which);
        xx_io_device *source;
        int64_t take, offset, length, done = 0;
        if (!block || ++steps > image->block_count || !(source = owner->data[which])) {
            result = false;
            break;
        }
        take = (int64_t)block->start + block->sectors - sector;
        if (take > left) take = left;
        offset = (int64_t)block->offset + (sector - block->start) * (int64_t)block->sector_size;
        length = take * (int64_t)block->sector_size;
        while (done < length) {
            size_t amount = (length - done) > (int64_t)capacity ? capacity : (size_t)(length - done);
            if ((pd && xx_pd_is_stopped(pd)) || !bw_read_at(source, offset + done, buffer, amount) || (destination && !bw_write_all(destination, buffer, amount))) {
                result = false;
                break;
            }
            done += (int64_t)amount;
        }
        sector += take;
        left -= take;
    }
    xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Data files                                                              */

static uint8_t bw_upper(uint8_t c)
{
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

static bool bw_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || bw_upper((uint8_t)name[index]) != (uint8_t)word[index]) return false;
    return word[stem] == 0;
}

/* The last path component of a stored data file name, or NULL when it is
 * not a name the reader will open: empty, only dots and spaces, control or
 * reserved characters, a drive prefix alone, or a Windows device name (CON,
 * NUL, COM1, LPT1.TXT, CONIN$, COM superscript digits ...). */
static const char *bw_safe_basename(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    const char *base = name;
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name) return NULL;
    for (index = 0U; name[index]; ++index)
        if (name[index] == '/' || name[index] == '\\') base = name + index + 1U;
    if (((base[0] >= 'A' && base[0] <= 'Z') || (base[0] >= 'a' && base[0] <= 'z')) && base[1] == ':') base += 2;
    length = xx_str_len(base);
    if (length == 0U || length > BW_MAX_BASENAME) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)base[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return NULL;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return NULL;
    while (stem < length && base[stem] != '.') ++stem;
    while (stem > 0U && base[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (bw_stem_is(base, stem, devices[index])) return NULL;
    if ((bw_stem_is(base, 3U, "COM") || bw_stem_is(base, 3U, "LPT")) && stem >= 4U) {
        const uint8_t *tail = (const uint8_t *)base + 3;
        if (stem == 4U && tail[0] >= '0' && tail[0] <= '9') return NULL;
        /* U+00B9, U+00B2, U+00B3 in UTF-8 */
        if (stem == 5U && tail[0] == 0xC2U && (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U)) return NULL;
    }
    return base;
}

static void bw_release_data(xx_blindwrite_5_6_image *archive, uint32_t index)
{
    if (archive->data_owned[index] && archive->data[index]) xx_io_close(archive->data[index]);
    archive->data[index] = NULL;
    archive->data_owned[index] = false;
}

bool xx_blindwrite_5_6_image_set_data_device(xx_blindwrite_5_6_image *archive, uint32_t block_index, xx_io_device *device)
{
    if (!archive || block_index >= BW_BLOCKS) return false;
    bw_release_data(archive, block_index);
    archive->data[block_index] = device;
    return true;
}

uint32_t xx_blindwrite_5_6_image_open_data_files(xx_blindwrite_5_6_image *archive, const char *descriptor_path)
{
    bw_image *image;
    size_t directory = 0U, index;
    uint32_t block, opened = 0U;
    if (!archive || !descriptor_path) return 0U;
    image = bw_load(&archive->format);
    if (!image) return 0U;
    for (index = 0U; descriptor_path[index]; ++index)
        if (descriptor_path[index] == '/' || descriptor_path[index] == '\\') directory = index + 1U;
    for (block = 0U; block < image->block_count; ++block) {
        const char *base;
        char *path;
        size_t base_length;
        xx_io_device *device;
        if (archive->data[block]) {
            ++opened;
            continue;
        }
        base = bw_safe_basename(image->blocks[block].name);
        if (!base) continue;
        base_length = xx_str_len(base);
        path = (char *)xx_mem_alloc(directory + base_length + 1U);
        if (!path) continue;
        xx_rt_memcpy(path, descriptor_path, directory);
        xx_rt_memcpy(path + directory, base, base_length + 1U);
        device = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!device) continue;
        archive->data[block] = device;
        archive->data_owned[block] = true;
        ++opened;
    }
    bw_image_free(image);
    return opened;
}

uint32_t xx_blindwrite_5_6_image_get_number_of_blocks(xx_blindwrite_5_6_image *archive)
{
    return archive && (archive->format.base_info_handled || xx_blindwrite_5_6_image_handle_base_info(&archive->format, NULL)) ? archive->number_of_blocks : 0U;
}

char *xx_blindwrite_5_6_image_get_block_file_name(xx_blindwrite_5_6_image *archive, uint32_t block_index)
{
    bw_image *image;
    char *result = NULL;
    if (!archive || !(image = bw_load(&archive->format))) return NULL;
    if (block_index < image->block_count) result = xx_str_dup(image->blocks[block_index].name);
    bw_image_free(image);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_blindwrite_5_6_image_init(xx_blindwrite_5_6_image *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BLINDWRITE_5_6_IMAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-b6t");
    xx_format_set_extension(&archive->format, "b6t");
    archive->format.check_is_valid = xx_blindwrite_5_6_image_check_is_valid;
    archive->format.handle_base_info = xx_blindwrite_5_6_image_handle_base_info;
    archive->format.get_format_size = xx_blindwrite_5_6_image_get_format_size;
    archive->format.get_number_of_archive_records = xx_blindwrite_5_6_image_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_blindwrite_5_6_image_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_blindwrite_5_6_image_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_blindwrite_5_6_image_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_blindwrite_5_6_image_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_blindwrite_5_6_image_free_archive_records_reading;
    archive->descriptor_size = -1;
}

xx_blindwrite_5_6_image *xx_blindwrite_5_6_image_create(xx_io_device *device, int64_t base_address)
{
    xx_blindwrite_5_6_image *archive = (xx_blindwrite_5_6_image *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_blindwrite_5_6_image_init(archive, device, base_address);
    return archive;
}

void xx_blindwrite_5_6_image_destroy(xx_blindwrite_5_6_image *archive)
{
    uint32_t index;
    if (!archive) return;
    for (index = 0U; index < BW_BLOCKS; ++index) bw_release_data(archive, index);
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_blindwrite_5_6_image_free(xx_blindwrite_5_6_image *archive)
{
    if (!archive) return;
    xx_blindwrite_5_6_image_destroy(archive);
    xx_mem_free(archive);
}

bool xx_blindwrite_5_6_image_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    bw_image *image;
    (void)pd;
    image = bw_load(format);
    if (!image) return false;
    bw_image_free(image);
    return true;
}

bool xx_blindwrite_5_6_image_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    bw_image *image;
    xx_blindwrite_5_6_image *archive;
    (void)pd;
    if (!format || !(image = bw_load(format))) return false;
    archive = (xx_blindwrite_5_6_image *)format;
    archive->number_of_records = image->track_count;
    archive->number_of_blocks = image->block_count;
    archive->number_of_sessions = image->session_count;
    archive->disc_type = image->disc_type;
    archive->descriptor_size = image->size;
    format->number_of_archive_records = image->track_count;
    format->format_size = image->size;
    format->is_valid = true;
    format->base_info_handled = true;
    bw_image_free(image);
    return true;
}

int64_t xx_blindwrite_5_6_image_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_blindwrite_5_6_image_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_blindwrite_5_6_image_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_blindwrite_5_6_image_handle_base_info(format, pd)) ? ((xx_blindwrite_5_6_image *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_blindwrite_5_6_image_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    bw_image *image;
    xx_archive_record_state *state;
    (void)pd;
    if (!(image = bw_load(format))) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bw_image_free(image);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = image;
    state->free_internal = bw_image_free;
    state->total_records = (int64_t)image->track_count;
    if (!bw_copy_options(&state->options, options) || !bw_set_record(&state->current_record, &image->tracks[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_blindwrite_5_6_image_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_blindwrite_5_6_image_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bw_image *image;
    (void)pd;
    if (!format || !state || state->format != format || !(image = (bw_image *)state->internal_state) || ++image->index >= image->track_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bw_set_record(&state->current_record, &image->tracks[image->index]);
    return state->has_record;
}

bool xx_blindwrite_5_6_image_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bw_image *image;
    const bw_track *track;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(image = (bw_image *)state->internal_state) || image->index >= image->track_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    track = &image->tracks[image->index];
    if (track->size < 0 || !bw_track_attached(image, track)) return false;
    path_option = bw_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return bw_copy_track(image, track, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Member names are built by the reader ("trackNN.ext" from a track
     * number checked to be 1..99 and unique), never taken from the file. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", track->name)
                                                                                                  : xx_str_concat(base, track->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        /* Only a file this call created may be removed on failure. */
        created = true;
        result = bw_copy_track(image, track, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_blindwrite_5_6_image_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

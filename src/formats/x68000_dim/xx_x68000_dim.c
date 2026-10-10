/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Sharp X68000 DIM floppy image written by DIFC.X
 * (read by MAME floptool "dim" and hxcfe "X68000_DIM").
 *
 * Header, 256 bytes:
 *   0x00        media type
 *   0x01..0xAA  track-present map, one byte per track (cylinder * 2 + head),
 *               170 entries = 85 cylinders; non-zero means the track holds
 *               data
 *   0xAB        "DIFC HEADER  " signature
 *   0xBA..0xFE  date, time and comment (ignored here)
 *   0xFF        over-track flag (ignored; the map already says it)
 * Track t is stored at 0x100 + t * track_size whether or not the map marks
 * it present; the file may end after the last present track.
 *
 * Media types (sector size x sectors per track):
 *   0x00 2HD 1024x8, 0x01 2HS 1024x9, 0x02 2HC 512x15, 0x03 2HDE 1024x9,
 *   0x09 2HQ 512x18, 0x11 N88-BASIC 256x26.
 *
 * The member is the raw sector dump in cylinder/head/sector order.  It
 * covers 77 cylinders, or up to the last cylinder the map marks present
 * when that is further.  Tracks the map marks absent are filled with 0xE5
 * (the fill hxcfe uses).  Written from the structure above; the layout was
 * confirmed with black-box runs of hxcfe (X68000_DIM to RAW_LOADER) and
 * floptool (dim to pc98 / imd).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/x68000_dim/xx_x68000_dim.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef X68000_DIM
#define XX_X68000_DIM_FILE_TYPE XX_FILE_TYPE_X68000_DIM
#else
#define XX_X68000_DIM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define X68DIM_HEADER_SIZE 0x100U
#define X68DIM_MAP_OFFSET 0x01U
#define X68DIM_MAP_ENTRIES 170U
#define X68DIM_SIGNATURE_OFFSET 0xABU
#define X68DIM_SIGNATURE "DIFC HEADER"
#define X68DIM_SIGNATURE_SIZE 11U
#define X68DIM_MIN_CYLINDERS 77U
#define X68DIM_HEADS 2U
#define X68DIM_MAX_TRACK_SIZE 9216U
#define X68DIM_FILL 0xE5U
#define X68DIM_MEMBER_NAME "image.img"

typedef struct x68dim_geometry_s {
    uint32_t media_type;
    uint32_t sector_size;
    uint32_t sectors;
    uint32_t track_size;
    uint32_t cylinders;
    uint32_t tracks; /* cylinders * heads */
    uint32_t present_tracks;
    uint32_t stored_tracks; /* whole tracks inside the file, <= tracks */
    uint64_t image_size;    /* tracks * track_size */
    int64_t data_offset;    /* absolute offset of track 0 */
    int64_t archive_size;   /* header plus stored tracks */
    uint8_t map[X68DIM_MAP_ENTRIES];
} x68dim_geometry;

typedef struct x68dim_stream_s {
    x68dim_geometry geometry;
    size_t count;
    size_t index;
} x68dim_stream;

static bool x68dim_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool x68dim_media(uint32_t type, uint32_t *sector_size, uint32_t *sectors)
{
    switch (type) {
        case 0x00U:
            *sector_size = 1024U;
            *sectors = 8U;
            return true; /* 2HD */
        case 0x01U:
            *sector_size = 1024U;
            *sectors = 9U;
            return true; /* 2HS */
        case 0x02U:
            *sector_size = 512U;
            *sectors = 15U;
            return true; /* 2HC */
        case 0x03U:
            *sector_size = 1024U;
            *sectors = 9U;
            return true; /* 2HDE */
        case 0x09U:
            *sector_size = 512U;
            *sectors = 18U;
            return true; /* 2HQ */
        case 0x11U:
            *sector_size = 256U;
            *sectors = 26U;
            return true; /* N88 */
        default: return false;
    }
}

/* One 256-byte read, then arithmetic on the header: the signature, a known
 * media type, and every present track wholly inside the file. */
static bool x68dim_parse(Abstractformat *format, x68dim_geometry *out)
{
    uint8_t header[X68DIM_HEADER_SIZE];
    x68dim_geometry geometry;
    int64_t total, size;
    uint64_t stored;
    uint32_t index, last_present = 0U;
    bool any_present = false;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)X68DIM_HEADER_SIZE || !x68dim_read_at(format->device, format->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header + X68DIM_SIGNATURE_OFFSET, X68DIM_SIGNATURE, X68DIM_SIGNATURE_SIZE) != 0)
        return false;
    xx_mem_zero(&geometry, sizeof(geometry));
    geometry.media_type = header[0];
    if (!x68dim_media(geometry.media_type, &geometry.sector_size, &geometry.sectors)) return false;
    geometry.track_size = geometry.sector_size * geometry.sectors;
    xx_rt_memcpy(geometry.map, header + X68DIM_MAP_OFFSET, X68DIM_MAP_ENTRIES);
    for (index = 0U; index < X68DIM_MAP_ENTRIES; ++index) {
        if (geometry.map[index] != 0U) {
            last_present = index;
            any_present = true;
            ++geometry.present_tracks;
        }
    }
    geometry.cylinders = X68DIM_MIN_CYLINDERS;
    if (any_present && last_present / X68DIM_HEADS + 1U > geometry.cylinders) geometry.cylinders = last_present / X68DIM_HEADS + 1U;
    geometry.tracks = geometry.cylinders * X68DIM_HEADS; /* <= 170 */
    geometry.image_size = (uint64_t)geometry.tracks * geometry.track_size;
    stored = (uint64_t)(size - (int64_t)X68DIM_HEADER_SIZE) / geometry.track_size;
    geometry.stored_tracks = stored < geometry.tracks ? (uint32_t)stored : geometry.tracks;
    /* A track the map promises must be in the file. */
    if (any_present && last_present >= geometry.stored_tracks) return false;
    geometry.data_offset = format->base_address + (int64_t)X68DIM_HEADER_SIZE;
    geometry.archive_size = (int64_t)X68DIM_HEADER_SIZE + (int64_t)geometry.stored_tracks * (int64_t)geometry.track_size;
    *out = geometry;
    return true;
}

static void x68dim_stream_free(void *opaque)
{
    if (opaque) xx_mem_free(opaque);
}

static bool x68dim_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *x68dim_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool x68dim_set_record(xx_archive_record *record, const x68dim_geometry *geometry)
{
    uint64_t stored = (uint64_t)geometry->stored_tracks * geometry->track_size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = geometry->data_offset - X68DIM_HEADER_SIZE;
    record->header_size = X68DIM_HEADER_SIZE;
    record->data_offset = geometry->data_offset;
    record->compressed_size = (int64_t)stored;
    return xx_archive_record_set_original_name(record, X68DIM_MEMBER_NAME) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, geometry->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_x68000_dim_init(xx_x68000_dim *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_X68000_DIM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-x68000-dim");
    xx_format_set_extension(&archive->format, "dim");
    archive->format.check_is_valid = xx_x68000_dim_check_is_valid;
    archive->format.handle_base_info = xx_x68000_dim_handle_base_info;
    archive->format.get_format_size = xx_x68000_dim_get_format_size;
    archive->format.get_number_of_archive_records = xx_x68000_dim_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_x68000_dim_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_x68000_dim_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_x68000_dim_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_x68000_dim_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_x68000_dim_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_x68000_dim *xx_x68000_dim_create(xx_io_device *device, int64_t base_address)
{
    xx_x68000_dim *archive = (xx_x68000_dim *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_x68000_dim_init(archive, device, base_address);
    return archive;
}

void xx_x68000_dim_destroy(xx_x68000_dim *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_x68000_dim_free(xx_x68000_dim *archive)
{
    if (!archive) return;
    xx_x68000_dim_destroy(archive);
    xx_mem_free(archive);
}

bool xx_x68000_dim_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    x68dim_geometry geometry;
    (void)pd;
    return x68dim_parse(format, &geometry);
}

bool xx_x68000_dim_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    x68dim_geometry geometry;
    xx_x68000_dim *archive;
    (void)pd;
    if (!format || !x68dim_parse(format, &geometry)) return false;
    archive = (xx_x68000_dim *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + geometry.archive_size;
    archive->media_type = geometry.media_type;
    archive->cylinders = geometry.cylinders;
    archive->heads = X68DIM_HEADS;
    archive->sectors_per_track = geometry.sectors;
    archive->sector_size = geometry.sector_size;
    archive->track_size = geometry.track_size;
    archive->present_tracks = geometry.present_tracks;
    archive->stored_tracks = geometry.stored_tracks;
    archive->image_size = geometry.image_size;
    format->number_of_archive_records = 1U;
    format->format_size = geometry.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_x68000_dim_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_x68000_dim_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_x68000_dim_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_x68000_dim_handle_base_info(format, pd)) ? ((xx_x68000_dim *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_x68000_dim_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    x68dim_stream *stream;
    xx_archive_record_state *state;
    x68dim_geometry geometry;
    (void)pd;
    if (!x68dim_parse(format, &geometry)) return NULL;
    stream = (x68dim_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->geometry = geometry;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        x68dim_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = x68dim_stream_free;
    state->total_records = 1;
    if (!x68dim_copy_options(&state->options, options) || !x68dim_set_record(&state->current_record, &stream->geometry)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_x68000_dim_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_x68000_dim_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    x68dim_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (x68dim_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = x68dim_set_record(&state->current_record, &stream->geometry);
    return state->has_record;
}

/* Writes the image one track at a time through a single track buffer
 * (at most 9216 bytes): present tracks are copied, absent ones filled. */
static bool x68dim_write_image(xx_io_device *source, const x68dim_geometry *geometry, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t buffer[X68DIM_MAX_TRACK_SIZE];
    uint32_t track;
    if (geometry->track_size == 0U || geometry->track_size > X68DIM_MAX_TRACK_SIZE || geometry->tracks > X68DIM_MAP_ENTRIES) return false;
    for (track = 0U; track < geometry->tracks; ++track) {
        bool present = track < X68DIM_MAP_ENTRIES && geometry->map[track] != 0U && track < geometry->stored_tracks;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (present) {
            if (!x68dim_read_at(source, geometry->data_offset + (int64_t)track * (int64_t)geometry->track_size, buffer, geometry->track_size)) return false;
        } else {
            xx_rt_memset(buffer, X68DIM_FILL, geometry->track_size);
        }
        if (xx_io_write(destination, buffer, geometry->track_size) != (ssize_t)geometry->track_size) return false;
    }
    return true;
}

bool xx_x68000_dim_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    x68dim_stream *stream;
    const x68dim_geometry *geometry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    int64_t total;
    bool result = false;
    bool created = false;
    if (!format || !format->device || !state || state->format != format || !state->has_record || !(stream = (x68dim_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    geometry = &stream->geometry;
    total = xx_io_total_size(format->device);
    if (geometry->data_offset < 0 || total < geometry->data_offset ||
        (uint64_t)geometry->stored_tracks * geometry->track_size > (uint64_t)(total - geometry->data_offset))
        return false;
    path_option = x68dim_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", X68DIM_MEMBER_NAME)
                                                                                                  : xx_str_concat(base, X68DIM_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = x68dim_write_image(format->device, geometry, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_x68000_dim_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

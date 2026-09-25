/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the PC Magazine FLP floppy image.  A 13-byte header
 * names the disk geometry; the sectors follow it stored, one track after
 * another with the heads of a cylinder interleaved, exactly like a raw
 * sector dump.  The member is that sector dump.
 *
 * Header, all fields little endian:
 *   0   3  "PCM"
 *   3   2  0 in every image seen (hxcfe ignores it)
 *   5   2  number of sides (1 or 2)
 *   7   2  number of tracks per side
 *   9   2  sectors per track
 *   11  2  bytes per sector
 * The sector data starts at offset 13 and is sides * tracks * sectors *
 * bytes long.  Written from the structure of the corpus images; the field
 * meanings were confirmed by black-box runs of hxcfe's FLP_IMG loader (its
 * RAW output equals the bytes from offset 13 on for every geometry tried).
 * hxcfe zero-pads a short image; this reader refuses one instead, since a
 * three-byte signature needs the size check to keep false positives out.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pc_magazine_flp/xx_pc_magazine_flp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef PC_MAGAZINE_FLP
#define XX_PC_MAGAZINE_FLP_FILE_TYPE XX_FILE_TYPE_PC_MAGAZINE_FLP
#else
#define XX_PC_MAGAZINE_FLP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PCMFLP_HEADER_SIZE 13
#define PCMFLP_MAX_TRACKS 255U
#define PCMFLP_MAX_SECTORS 255U
#define PCMFLP_MAX_IMAGE (64U * 1024U * 1024U)
#define PCMFLP_MEMBER_NAME "image.img"

typedef struct pcmflp_geometry_s {
    uint32_t version;
    uint32_t sides;
    uint32_t tracks;
    uint32_t sectors;
    uint32_t sector_size;
    uint64_t image_size;
    int64_t data_offset;   /* absolute */
    int64_t archive_size;  /* header plus image, from base_address */
} pcmflp_geometry;

typedef struct pcmflp_stream_s {
    pcmflp_geometry geometry;
    size_t count;
    size_t index;
} pcmflp_stream;

static uint32_t pcmflp_le16(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static bool pcmflp_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pcmflp_sector_size_valid(uint32_t bytes) {
    return bytes == 128U || bytes == 256U || bytes == 512U ||
           bytes == 1024U || bytes == 2048U || bytes == 4096U ||
           bytes == 8192U || bytes == 16384U;
}

/* Everything the header claims is checked against the file before it is
 * trusted: the geometry must be one a floppy controller can produce and
 * the sectors it implies must all be present.  Trailing bytes are allowed
 * and simply fall outside the format. */
static bool pcmflp_parse(Abstractformat *format, pcmflp_geometry *out) {
    uint8_t header[PCMFLP_HEADER_SIZE];
    pcmflp_geometry geometry;
    int64_t total, size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)PCMFLP_HEADER_SIZE ||
        !pcmflp_read_at(format->device, format->base_address, header,
                        sizeof(header)) ||
        header[0] != 'P' || header[1] != 'C' || header[2] != 'M')
        return false;
    xx_mem_zero(&geometry, sizeof(geometry));
    geometry.version = pcmflp_le16(header + 3U);
    geometry.sides = pcmflp_le16(header + 5U);
    geometry.tracks = pcmflp_le16(header + 7U);
    geometry.sectors = pcmflp_le16(header + 9U);
    geometry.sector_size = pcmflp_le16(header + 11U);
    if ((geometry.sides != 1U && geometry.sides != 2U) ||
        geometry.tracks == 0U || geometry.tracks > PCMFLP_MAX_TRACKS ||
        geometry.sectors == 0U || geometry.sectors > PCMFLP_MAX_SECTORS ||
        !pcmflp_sector_size_valid(geometry.sector_size))
        return false;
    /* At most 2 * 255 * 255 * 16384, far inside 64 bits. */
    geometry.image_size = (uint64_t)geometry.sides * geometry.tracks *
                          geometry.sectors * geometry.sector_size;
    if (geometry.image_size > PCMFLP_MAX_IMAGE ||
        geometry.image_size > (uint64_t)(size - PCMFLP_HEADER_SIZE))
        return false;
    geometry.data_offset = format->base_address + PCMFLP_HEADER_SIZE;
    geometry.archive_size = PCMFLP_HEADER_SIZE + (int64_t)geometry.image_size;
    *out = geometry;
    return true;
}

static void pcmflp_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool pcmflp_copy_options(xx_list_s *destination,
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

static const xx_var *pcmflp_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pcmflp_set_record(xx_archive_record *record,
                              const pcmflp_geometry *geometry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = geometry->data_offset - PCMFLP_HEADER_SIZE;
    record->header_size = PCMFLP_HEADER_SIZE;
    record->data_offset = geometry->data_offset;
    record->compressed_size = (int64_t)geometry->image_size;
    return xx_archive_record_set_original_name(record, PCMFLP_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          geometry->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          geometry->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_pc_magazine_flp_init(xx_pc_magazine_flp *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PC_MAGAZINE_FLP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pc-magazine-flp");
    xx_format_set_extension(&archive->format, "flp");
    archive->format.check_is_valid = xx_pc_magazine_flp_check_is_valid;
    archive->format.handle_base_info = xx_pc_magazine_flp_handle_base_info;
    archive->format.get_format_size = xx_pc_magazine_flp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pc_magazine_flp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pc_magazine_flp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pc_magazine_flp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pc_magazine_flp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pc_magazine_flp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pc_magazine_flp_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_pc_magazine_flp *xx_pc_magazine_flp_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_pc_magazine_flp *archive =
        (xx_pc_magazine_flp *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pc_magazine_flp_init(archive, device, base_address);
    return archive;
}

void xx_pc_magazine_flp_destroy(xx_pc_magazine_flp *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pc_magazine_flp_free(xx_pc_magazine_flp *archive) {
    if (!archive) return;
    xx_pc_magazine_flp_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pc_magazine_flp_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    pcmflp_geometry geometry;
    (void)pd;
    return pcmflp_parse(format, &geometry);
}

bool xx_pc_magazine_flp_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    pcmflp_geometry geometry;
    xx_pc_magazine_flp *archive;
    (void)pd;
    if (!format || !pcmflp_parse(format, &geometry)) return false;
    archive = (xx_pc_magazine_flp *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + geometry.archive_size;
    archive->version = geometry.version;
    archive->sides = geometry.sides;
    archive->tracks = geometry.tracks;
    archive->sectors_per_track = geometry.sectors;
    archive->sector_size = geometry.sector_size;
    archive->image_size = geometry.image_size;
    format->number_of_archive_records = 1U;
    format->format_size = geometry.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_pc_magazine_flp_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pc_magazine_flp_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_pc_magazine_flp_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pc_magazine_flp_handle_base_info(format, pd))
               ? ((xx_pc_magazine_flp *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_pc_magazine_flp_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pcmflp_stream *stream;
    xx_archive_record_state *state;
    pcmflp_geometry geometry;
    (void)pd;
    if (!pcmflp_parse(format, &geometry)) return NULL;
    stream = (pcmflp_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->geometry = geometry;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pcmflp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pcmflp_stream_free;
    state->total_records = 1;
    if (!pcmflp_copy_options(&state->options, options) ||
        !pcmflp_set_record(&state->current_record, &stream->geometry)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pc_magazine_flp_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pc_magazine_flp_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    pcmflp_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pcmflp_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pcmflp_set_record(&state->current_record,
                                          &stream->geometry);
    return state->has_record;
}

/* The sectors are stored, so the member is streamed straight from the
 * container; nothing proportional to the image is allocated. */
bool xx_pc_magazine_flp_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    pcmflp_stream *stream;
    const pcmflp_geometry *geometry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    int64_t total;
    bool result = false;
    bool created = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(stream = (pcmflp_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    geometry = &stream->geometry;
    total = xx_io_total_size(format->device);
    if (geometry->data_offset < 0 || total < geometry->data_offset ||
        geometry->image_size > (uint64_t)(total - geometry->data_offset))
        return false;
    path_option = pcmflp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
               ? xx_str_concat3(base, "/", PCMFLP_MEMBER_NAME)
               : xx_str_concat(base, PCMFLP_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = xx_store_unpack_device(format->device, geometry->data_offset,
                                        (int64_t)geometry->image_size,
                                        destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pc_magazine_flp_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

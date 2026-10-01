/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the NEC PC-98 FDI floppy image written by the Anex86
 * emulator (and read by MAME floptool "pc98_fdi" and hxcfe "NEC_FDI").  The
 * name clashes with the ZX Spectrum "FDI" format; the two share nothing.
 *
 * Header, eight little-endian 32-bit fields:
 *   0x00  reserved, 0
 *   0x04  drive type (0x10 2DD 640K, 0x30 2HD 1.44M, 0x90 2HD 1.2M, ...)
 *   0x08  header size: offset of the first sector (4096 in Anex86 images)
 *   0x0C  data size in bytes
 *   0x10  bytes per sector
 *   0x14  sectors per track
 *   0x18  heads (sides)
 *   0x1C  cylinders
 * The rest of the header is padding.  The sectors follow stored, one track
 * after another with the heads of a cylinder interleaved, exactly like a
 * raw sector dump; the member is that dump.  The data size must equal the
 * product of the geometry fields.  Written from the structure above; the
 * extracted bytes were checked against black-box runs of hxcfe (NEC_FDI to
 * RAW_LOADER) and floptool (pc98_fdi to pc98).  Trailing bytes after the
 * data fall outside the format.
 *
 * With only a zero word as fixed bytes the header checks carry the
 * detection, so every field is validated before anything else is read.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef NEC_PC_98_FDI
#define XX_NEC_PC_98_FDI_FILE_TYPE XX_FILE_TYPE_NEC_PC_98_FDI
#else
#define XX_NEC_PC_98_FDI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NECFDI_FIELDS_SIZE 32U
#define NECFDI_MIN_HEADER 32U
#define NECFDI_MAX_HEADER 65536U
#define NECFDI_MAX_HEADS 2U
#define NECFDI_MAX_CYLINDERS 255U
#define NECFDI_MAX_SECTORS 255U
#define NECFDI_MAX_IMAGE (64U * 1024U * 1024U)
#define NECFDI_MEMBER_NAME "image.img"

typedef struct necfdi_geometry_s {
    uint32_t fdd_type;
    uint32_t header_size;
    uint32_t sector_size;
    uint32_t sectors;
    uint32_t heads;
    uint32_t cylinders;
    uint64_t image_size;
    int64_t data_offset;   /* absolute */
    int64_t archive_size;  /* header plus image, from base_address */
} necfdi_geometry;

typedef struct necfdi_stream_s {
    necfdi_geometry geometry;
    size_t count;
    size_t index;
} necfdi_stream;

static uint32_t necfdi_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static bool necfdi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool necfdi_sector_size_valid(uint32_t bytes) {
    return bytes == 128U || bytes == 256U || bytes == 512U ||
           bytes == 1024U || bytes == 2048U || bytes == 4096U ||
           bytes == 8192U || bytes == 16384U;
}

/* One 32-byte read, then pure arithmetic: the geometry must be one a
 * floppy controller can produce, the stored data size must equal it, and
 * the header plus every sector must be present in the file. */
static bool necfdi_parse(Abstractformat *format, necfdi_geometry *out) {
    uint8_t header[NECFDI_FIELDS_SIZE];
    necfdi_geometry geometry;
    uint32_t data_size;
    int64_t total, size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)NECFDI_MIN_HEADER ||
        !necfdi_read_at(format->device, format->base_address, header,
                        sizeof(header)) ||
        necfdi_le32(header) != 0U)
        return false;
    xx_mem_zero(&geometry, sizeof(geometry));
    geometry.fdd_type = necfdi_le32(header + 0x04U);
    geometry.header_size = necfdi_le32(header + 0x08U);
    data_size = necfdi_le32(header + 0x0CU);
    geometry.sector_size = necfdi_le32(header + 0x10U);
    geometry.sectors = necfdi_le32(header + 0x14U);
    geometry.heads = necfdi_le32(header + 0x18U);
    geometry.cylinders = necfdi_le32(header + 0x1CU);
    if (geometry.header_size < NECFDI_MIN_HEADER ||
        geometry.header_size > NECFDI_MAX_HEADER ||
        !necfdi_sector_size_valid(geometry.sector_size) ||
        geometry.sectors == 0U || geometry.sectors > NECFDI_MAX_SECTORS ||
        geometry.heads == 0U || geometry.heads > NECFDI_MAX_HEADS ||
        geometry.cylinders == 0U || geometry.cylinders > NECFDI_MAX_CYLINDERS)
        return false;
    /* At most 2 * 255 * 255 * 16384, far inside 64 bits. */
    geometry.image_size = (uint64_t)geometry.heads * geometry.cylinders *
                          geometry.sectors * geometry.sector_size;
    if (geometry.image_size != (uint64_t)data_size ||
        geometry.image_size > NECFDI_MAX_IMAGE ||
        (uint64_t)geometry.header_size > (uint64_t)size ||
        geometry.image_size > (uint64_t)(size - geometry.header_size))
        return false;
    geometry.data_offset = format->base_address + geometry.header_size;
    geometry.archive_size =
        (int64_t)geometry.header_size + (int64_t)geometry.image_size;
    *out = geometry;
    return true;
}

static void necfdi_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool necfdi_copy_options(xx_list_s *destination,
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

static const xx_var *necfdi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool necfdi_set_record(xx_archive_record *record,
                              const necfdi_geometry *geometry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = geometry->data_offset - geometry->header_size;
    record->header_size = geometry->header_size;
    record->data_offset = geometry->data_offset;
    record->compressed_size = (int64_t)geometry->image_size;
    return xx_archive_record_set_original_name(record, NECFDI_MEMBER_NAME) &&
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

void xx_nec_pc_98_fdi_init(xx_nec_pc_98_fdi *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_NEC_PC_98_FDI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pc98-fdi");
    xx_format_set_extension(&archive->format, "fdi");
    archive->format.check_is_valid = xx_nec_pc_98_fdi_check_is_valid;
    archive->format.handle_base_info = xx_nec_pc_98_fdi_handle_base_info;
    archive->format.get_format_size = xx_nec_pc_98_fdi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_nec_pc_98_fdi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_nec_pc_98_fdi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_nec_pc_98_fdi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_nec_pc_98_fdi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_nec_pc_98_fdi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_nec_pc_98_fdi_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_nec_pc_98_fdi *xx_nec_pc_98_fdi_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_nec_pc_98_fdi *archive =
        (xx_nec_pc_98_fdi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_nec_pc_98_fdi_init(archive, device, base_address);
    return archive;
}

void xx_nec_pc_98_fdi_destroy(xx_nec_pc_98_fdi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_nec_pc_98_fdi_free(xx_nec_pc_98_fdi *archive) {
    if (!archive) return;
    xx_nec_pc_98_fdi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_nec_pc_98_fdi_check_is_valid(Abstractformat *format,
                                     xx_pd_struct *pd) {
    necfdi_geometry geometry;
    (void)pd;
    return necfdi_parse(format, &geometry);
}

bool xx_nec_pc_98_fdi_handle_base_info(Abstractformat *format,
                                       xx_pd_struct *pd) {
    necfdi_geometry geometry;
    xx_nec_pc_98_fdi *archive;
    (void)pd;
    if (!format || !necfdi_parse(format, &geometry)) return false;
    archive = (xx_nec_pc_98_fdi *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + geometry.archive_size;
    archive->fdd_type = geometry.fdd_type;
    archive->header_size = geometry.header_size;
    archive->heads = geometry.heads;
    archive->cylinders = geometry.cylinders;
    archive->sectors_per_track = geometry.sectors;
    archive->sector_size = geometry.sector_size;
    archive->image_size = geometry.image_size;
    format->number_of_archive_records = 1U;
    format->format_size = geometry.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_nec_pc_98_fdi_get_format_size(Abstractformat *format,
                                         xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nec_pc_98_fdi_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_nec_pc_98_fdi_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nec_pc_98_fdi_handle_base_info(format, pd))
               ? ((xx_nec_pc_98_fdi *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_nec_pc_98_fdi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    necfdi_stream *stream;
    xx_archive_record_state *state;
    necfdi_geometry geometry;
    (void)pd;
    if (!necfdi_parse(format, &geometry)) return NULL;
    stream = (necfdi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->geometry = geometry;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        necfdi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = necfdi_stream_free;
    state->total_records = 1;
    if (!necfdi_copy_options(&state->options, options) ||
        !necfdi_set_record(&state->current_record, &stream->geometry)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_nec_pc_98_fdi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_nec_pc_98_fdi_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    necfdi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (necfdi_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = necfdi_set_record(&state->current_record,
                                          &stream->geometry);
    return state->has_record;
}

/* The sectors are stored, so the member is streamed straight from the
 * container; nothing proportional to the image is allocated. */
bool xx_nec_pc_98_fdi_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    necfdi_stream *stream;
    const necfdi_geometry *geometry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    int64_t total;
    bool result = false;
    bool created = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(stream = (necfdi_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    geometry = &stream->geometry;
    total = xx_io_total_size(format->device);
    if (geometry->data_offset < 0 || total < geometry->data_offset ||
        geometry->image_size > (uint64_t)(total - geometry->data_offset))
        return false;
    path_option = necfdi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", NECFDI_MEMBER_NAME)
               : xx_str_concat(base, NECFDI_MEMBER_NAME);
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

void xx_nec_pc_98_fdi_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

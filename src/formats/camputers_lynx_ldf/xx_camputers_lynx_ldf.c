/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Camputers Lynx .LDF: a headerless sector dump, one member per track.
 * xx_camputers_lynx_ldf.h carries the layout.
 *
 * Written from the format description only (512-byte sectors, 10 per track,
 * sectors 1..10 in order, heads of a cylinder together); MAME floptool's
 * camplynx writer and HxC's CAMPUTERSLYNX loader were used as black-box
 * oracles, not as sources.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/camputers_lynx_ldf/xx_camputers_lynx_ldf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared, so the alias macro that is
 * defined next to the enumerator is tested instead. */
#ifdef CAMPUTERS_LYNX_LDF
#define XX_CAMPUTERS_LYNX_LDF_FILE_TYPE XX_FILE_TYPE_CAMPUTERS_LYNX_LDF
#else
#define XX_CAMPUTERS_LYNX_LDF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LDF_TRACK_SIZE ((int64_t)XX_CAMPUTERS_LYNX_LDF_TRACK_SIZE)

typedef struct ldf_geometry_s {
    int64_t image_size;
    uint32_t cylinders;
    uint32_t heads;
} ldf_geometry;

/* The only three sizes any reference accepts; nothing else is an LDF. */
static const ldf_geometry ldf_geometries[] = {
    {INT64_C(40) * 1 * 10 * 512, 40U, 1U},
    {INT64_C(80) * 1 * 10 * 512, 80U, 1U},
    {INT64_C(80) * 2 * 10 * 512, 80U, 2U},
};

typedef struct ldf_stream_s {
    int64_t base;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t count; /**< cylinders * heads, at most 160 */
    uint32_t index;
} ldf_stream;

static bool ldf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Size only: no byte of the image is read, so this cannot be slow or be
 * fooled into a large allocation. */
static bool ldf_parse(Abstractformat *format, ldf_geometry *out) {
    int64_t total, size;
    size_t index;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    for (index = 0U; index < sizeof(ldf_geometries) / sizeof(ldf_geometries[0]);
         ++index) {
        if (size == ldf_geometries[index].image_size) {
            *out = ldf_geometries[index];
            return true;
        }
    }
    return false;
}

/* "track" + two-digit cylinder + "_" + head + ".bin".  Built here, never
 * taken from the image, so it is safe by construction. */
static bool ldf_make_name(uint32_t cylinder, uint32_t head, char *buffer,
                          size_t capacity) {
    static const char prefix[] = "track";
    static const char suffix[] = ".bin";
    size_t used = 0U, index;
    if (!buffer || capacity < 16U || cylinder > 99U || head > 9U) return false;
    for (index = 0U; prefix[index]; ++index) buffer[used++] = prefix[index];
    buffer[used++] = (char)('0' + cylinder / 10U);
    buffer[used++] = (char)('0' + cylinder % 10U);
    buffer[used++] = '_';
    buffer[used++] = (char)('0' + head);
    for (index = 0U; suffix[index]; ++index) buffer[used++] = suffix[index];
    buffer[used] = 0;
    return true;
}

static int64_t ldf_track_offset(const ldf_stream *stream, uint32_t index) {
    return stream->base + (int64_t)index * LDF_TRACK_SIZE;
}

static void ldf_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool ldf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ldf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ldf_set_record(xx_archive_record *record, const ldf_stream *stream) {
    char name[32];
    int64_t offset;
    uint32_t cylinder, head;
    if (!stream || stream->heads == 0U || stream->index >= stream->count)
        return false;
    cylinder = stream->index / stream->heads;
    head = stream->index % stream->heads;
    if (!ldf_make_name(cylinder, head, name, sizeof(name))) return false;
    offset = ldf_track_offset(stream, stream->index);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = offset;
    record->header_size = 0;
    record->data_offset = offset;
    record->compressed_size = LDF_TRACK_SIZE;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)LDF_TRACK_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)LDF_TRACK_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_camputers_lynx_ldf_init(xx_camputers_lynx_ldf *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CAMPUTERS_LYNX_LDF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-camputers-lynx-ldf");
    xx_format_set_extension(&archive->format, "ldf");
    archive->format.check_is_valid = xx_camputers_lynx_ldf_check_is_valid;
    archive->format.handle_base_info = xx_camputers_lynx_ldf_handle_base_info;
    archive->format.get_format_size = xx_camputers_lynx_ldf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_camputers_lynx_ldf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_camputers_lynx_ldf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_camputers_lynx_ldf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_camputers_lynx_ldf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_camputers_lynx_ldf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_camputers_lynx_ldf_free_archive_records_reading;
}

xx_camputers_lynx_ldf *xx_camputers_lynx_ldf_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_camputers_lynx_ldf *archive =
        (xx_camputers_lynx_ldf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_camputers_lynx_ldf_init(archive, device, base_address);
    return archive;
}

void xx_camputers_lynx_ldf_destroy(xx_camputers_lynx_ldf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_camputers_lynx_ldf_free(xx_camputers_lynx_ldf *archive) {
    if (!archive) return;
    xx_camputers_lynx_ldf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_camputers_lynx_ldf_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    ldf_geometry geometry;
    (void)pd;
    return ldf_parse(format, &geometry);
}

bool xx_camputers_lynx_ldf_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    ldf_geometry geometry;
    xx_camputers_lynx_ldf *archive;
    (void)pd;
    if (!format || !ldf_parse(format, &geometry)) return false;
    archive = (xx_camputers_lynx_ldf *)format;
    archive->cylinders = geometry.cylinders;
    archive->heads = geometry.heads;
    archive->number_of_records =
        (uint64_t)geometry.cylinders * (uint64_t)geometry.heads;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = geometry.image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_camputers_lynx_ldf_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_camputers_lynx_ldf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_camputers_lynx_ldf_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_camputers_lynx_ldf_handle_base_info(format, pd))
               ? ((xx_camputers_lynx_ldf *)format)->number_of_records : 0U;
}

bool xx_camputers_lynx_ldf_get_geometry(xx_camputers_lynx_ldf *archive,
                                        uint32_t *cylinders, uint32_t *heads) {
    ldf_geometry geometry;
    if (!archive || !ldf_parse(&archive->format, &geometry)) return false;
    if (cylinders) *cylinders = geometry.cylinders;
    if (heads) *heads = geometry.heads;
    return true;
}

bool xx_camputers_lynx_ldf_read_track(xx_camputers_lynx_ldf *archive,
                                      uint32_t cylinder, uint32_t head,
                                      uint8_t *buffer) {
    ldf_geometry geometry;
    int64_t offset;
    if (!archive || !buffer || !ldf_parse(&archive->format, &geometry) ||
        cylinder >= geometry.cylinders || head >= geometry.heads)
        return false;
    offset = archive->format.base_address +
             ((int64_t)cylinder * geometry.heads + head) * LDF_TRACK_SIZE;
    return ldf_read_at(archive->format.device, offset, buffer,
                       (size_t)LDF_TRACK_SIZE);
}

xx_archive_record_state *xx_camputers_lynx_ldf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ldf_geometry geometry;
    ldf_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!ldf_parse(format, &geometry)) return NULL;
    stream = (ldf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->base = format->base_address;
    stream->cylinders = geometry.cylinders;
    stream->heads = geometry.heads;
    stream->count = geometry.cylinders * geometry.heads;
    stream->index = 0U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ldf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!ldf_copy_options(&state->options, options) ||
        !ldf_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_camputers_lynx_ldf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_camputers_lynx_ldf_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ldf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ldf_stream *)state->internal_state) ||
        stream->index >= stream->count || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ldf_set_record(&state->current_record, stream);
    return state->has_record;
}

bool xx_camputers_lynx_ldf_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ldf_stream *stream;
    ldf_geometry geometry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char name[32];
    uint8_t *track = NULL;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ldf_stream *)state->internal_state) ||
        stream->heads == 0U || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    /* The device may have changed size since the state was built. */
    if (!ldf_parse(format, &geometry) || geometry.cylinders != stream->cylinders ||
        geometry.heads != stream->heads || format->base_address != stream->base)
        return false;
    if (!ldf_make_name(stream->index / stream->heads,
                       stream->index % stream->heads, name, sizeof(name)))
        return false;
    track = (uint8_t *)xx_mem_alloc((size_t)LDF_TRACK_SIZE);
    if (!track ||
        !ldf_read_at(format->device, ldf_track_offset(stream, stream->index),
                     track, (size_t)LDF_TRACK_SIZE))
        goto done;
    path_option = ldf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < (size_t)LDF_TRACK_SIZE) {
            ssize_t amount = xx_io_write(destination, track + written,
                                         (size_t)LDF_TRACK_SIZE - written);
            if (amount <= 0 || (size_t)amount > (size_t)LDF_TRACK_SIZE - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (track) xx_mem_free(track);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_camputers_lynx_ldf_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

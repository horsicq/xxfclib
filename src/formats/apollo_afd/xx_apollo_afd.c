/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Apollo floppy disk image (.afd): a headerless dump of 77 cylinders x 2
 * heads x 8 sectors x 1024 bytes, cylinder-major, head 0 first, sectors in ID
 * order.  xx_apollo_afd.h carries the layout.  The geometry is the one
 * MAME's "apollo" floppy format declares; this is original code, nothing was
 * ported.
 *
 * With no header there is nothing to parse: the exact image size is the
 * whole validity test, and every member's position follows from its index.
 * No allocation depends on file content.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apollo_afd/xx_apollo_afd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as APOLLO_AFD is registered. */
#ifdef APOLLO_AFD
#define XX_APOLLO_AFD_FILE_TYPE XX_FILE_TYPE_APOLLO_AFD
#else
#define XX_APOLLO_AFD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define APOLLO_AFD_CYLINDERS 77U
#define APOLLO_AFD_HEADS 2U
#define APOLLO_AFD_SECTORS 8U
#define APOLLO_AFD_SECTOR_SIZE 1024U
#define APOLLO_AFD_TRACK_SIZE (APOLLO_AFD_SECTORS * APOLLO_AFD_SECTOR_SIZE)
#define APOLLO_AFD_TRACKS (APOLLO_AFD_CYLINDERS * APOLLO_AFD_HEADS)
#define APOLLO_AFD_IMAGE_SIZE \
    ((int64_t)APOLLO_AFD_TRACKS * (int64_t)APOLLO_AFD_TRACK_SIZE)
/* "track" + 2 digits + "_" + 1 digit + ".bin" is 13 characters. */
#define APOLLO_AFD_NAME_BUFFER 16

typedef struct apollo_afd_stream_s {
    uint32_t index;
    uint32_t count;
    int64_t data_start; /**< base_address of the image. */
} apollo_afd_stream;

static bool apollo_afd_read_at(xx_io_device *device, int64_t offset,
                               void *buffer, size_t size) {
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

/* The whole test: an image of exactly the one geometry, starting at
 * base_address and running to the end of the device. */
static bool apollo_afd_geometry_ok(Abstractformat *format) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    return total - format->base_address == APOLLO_AFD_IMAGE_SIZE;
}

/* Track index -> "trackCC_H.bin".  index < APOLLO_AFD_TRACKS, so the
 * cylinder is 0..76 and always fits two digits. */
static void apollo_afd_make_name(uint32_t index, char *name) {
    uint32_t cylinder = index / APOLLO_AFD_HEADS;
    uint32_t head = index % APOLLO_AFD_HEADS;
    name[0] = 't';
    name[1] = 'r';
    name[2] = 'a';
    name[3] = 'c';
    name[4] = 'k';
    name[5] = (char)('0' + (char)((cylinder / 10U) % 10U));
    name[6] = (char)('0' + (char)(cylinder % 10U));
    name[7] = '_';
    name[8] = (char)('0' + (char)head);
    name[9] = '.';
    name[10] = 'b';
    name[11] = 'i';
    name[12] = 'n';
    name[13] = 0;
}

static int64_t apollo_afd_track_offset(const apollo_afd_stream *stream,
                                       uint32_t index) {
    return stream->data_start +
           (int64_t)index * (int64_t)APOLLO_AFD_TRACK_SIZE;
}

static void apollo_afd_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool apollo_afd_open(Abstractformat *format,
                            apollo_afd_stream **result) {
    apollo_afd_stream *stream;
    if (!result || !apollo_afd_geometry_ok(format)) return false;
    stream = (apollo_afd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->index = 0U;
    stream->count = APOLLO_AFD_TRACKS;
    stream->data_start = format->base_address;
    *result = stream;
    return true;
}

static bool apollo_afd_copy_options(xx_list_s *destination,
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

static const xx_var *apollo_afd_option(const xx_list_s *options,
                                       uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool apollo_afd_set_record(xx_archive_record *record,
                                  const apollo_afd_stream *stream) {
    char name[APOLLO_AFD_NAME_BUFFER];
    int64_t offset;
    if (stream->index >= stream->count) return false;
    offset = apollo_afd_track_offset(stream, stream->index);
    apollo_afd_make_name(stream->index, name);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = offset;
    record->header_size = 0;
    record->data_offset = offset;
    record->compressed_size = (int64_t)APOLLO_AFD_TRACK_SIZE;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          APOLLO_AFD_TRACK_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          APOLLO_AFD_TRACK_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_apollo_afd_init(xx_apollo_afd *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_APOLLO_AFD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apollo-afd");
    xx_format_set_extension(&archive->format, "afd");
    archive->format.check_is_valid = xx_apollo_afd_check_is_valid;
    archive->format.handle_base_info = xx_apollo_afd_handle_base_info;
    archive->format.get_format_size = xx_apollo_afd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_apollo_afd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_apollo_afd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_apollo_afd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_apollo_afd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_apollo_afd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_apollo_afd_free_archive_records_reading;
}

xx_apollo_afd *xx_apollo_afd_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_apollo_afd *archive = (xx_apollo_afd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_apollo_afd_init(archive, device, base_address);
    return archive;
}

void xx_apollo_afd_destroy(xx_apollo_afd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_apollo_afd_free(xx_apollo_afd *archive) {
    if (!archive) return;
    xx_apollo_afd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_apollo_afd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return apollo_afd_geometry_ok(format);
}

bool xx_apollo_afd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_apollo_afd *archive;
    (void)pd;
    if (!apollo_afd_geometry_ok(format)) return false;
    archive = (xx_apollo_afd *)format;
    archive->number_of_records = APOLLO_AFD_TRACKS;
    format->number_of_archive_records = APOLLO_AFD_TRACKS;
    format->format_size = APOLLO_AFD_IMAGE_SIZE;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_apollo_afd_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_apollo_afd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_apollo_afd_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_apollo_afd_handle_base_info(format, pd))
               ? ((xx_apollo_afd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_apollo_afd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    apollo_afd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!apollo_afd_open(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        apollo_afd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = apollo_afd_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!apollo_afd_copy_options(&state->options, options) ||
        !apollo_afd_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_apollo_afd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_apollo_afd_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    apollo_afd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (apollo_afd_stream *)state->internal_state) ||
        stream->index >= stream->count || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = apollo_afd_set_record(&state->current_record, stream);
    return state->has_record;
}

bool xx_apollo_afd_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    apollo_afd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *track = NULL;
    char name[APOLLO_AFD_NAME_BUFFER];
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (apollo_afd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    /* A track is 8 KiB and fixed; the buffer never depends on the file. */
    track = (uint8_t *)xx_mem_alloc(APOLLO_AFD_TRACK_SIZE);
    if (!track ||
        !apollo_afd_read_at(format->device,
                            apollo_afd_track_offset(stream, stream->index),
                            track, APOLLO_AFD_TRACK_SIZE))
        goto done;
    path_option = apollo_afd_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: the read above has verified the track. */
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The name is generated from the track index: letters, digits, '_' and
     * ".bin" only, so it carries no separator, drive or "..". */
    apollo_afd_make_name(stream->index, name);
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
        while (written < APOLLO_AFD_TRACK_SIZE) {
            ssize_t amount = xx_io_write(destination, track + written,
                                         APOLLO_AFD_TRACK_SIZE - written);
            if (amount <= 0 ||
                (size_t)amount > APOLLO_AFD_TRACK_SIZE - written) {
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

void xx_apollo_afd_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

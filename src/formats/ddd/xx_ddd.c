/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DDD / DDD Pro (Dalton's Disk Disintegrator) compressed Apple II disk.
 * xx_ddd.h carries the layout.
 *
 * The decoding rules (4-byte lead, 3 zero bits, volume number, 20 favourites
 * per track, the favourite prefix codes, the 0x97 run escape and the
 * 256-byte slack after the stream) follow CiderPress diskimg/DDD.cpp,
 * Copyright (C) 2007 by faddenSoft, LLC, BSD 3-clause license
 * (https://github.com/fadden/ciderpress, LICENSE.txt).  The favourite code
 * table below is taken from that file; the bit reader and the rest are
 * written for xxfclib.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ddd/xx_ddd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef DDD
#define XX_DDD_FILE_TYPE XX_FILE_TYPE_DDD
#else
#define XX_DDD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DDD_LEAD_SIZE 4
#define DDD_NUM_TRACKS 35
#define DDD_TRACK_SIZE 4096
#define DDD_IMAGE_SIZE (DDD_NUM_TRACKS * DDD_TRACK_SIZE)
#define DDD_NUM_FAVORITES 20
/* CiderPress accepts up to 256 bytes behind the stream: a DOS 3.3 file ends
 * on a sector boundary and DDD Pro appends one zero byte. */
#define DDD_MAX_SLACK 256
/* Smallest possible stream: 3 + 8 header bits, then per track 20 x 8
 * favourite bits and 16 maximal runs of 24 bits: 11 + 35 * 544 bits = 2382
 * bytes.  Largest: every byte a 9-bit literal, 11 + 35 * (160 + 4096 * 9)
 * bits = 161982 bytes. */
#define DDD_MIN_STREAM 2382
#define DDD_MAX_STREAM 161982
#define DDD_MIN_FILE (DDD_LEAD_SIZE + DDD_MIN_STREAM)
#define DDD_MAX_FILE (DDD_LEAD_SIZE + DDD_MAX_STREAM + DDD_MAX_SLACK)
#define DDD_READ_CHUNK 4096
#define DDD_PAYLOAD_NAME "image.do"

/* Favourite codes without their leading 1 bit, read MSB first.  Indices
 * 0..1 are 3 bits long, 2..8 4 bits, 9..16 5 bits and 17..19 6 bits
 * (CiderPress DDD.cpp, kFavoriteBitDec, BSD 3-clause). */
static const uint8_t ddd_favorite_code[DDD_NUM_FAVORITES] = {
    0x04, 0x01, 0x0f, 0x0e, 0x0c, 0x0b, 0x0a, 0x06, 0x05, 0x1b,
    0x0f, 0x09, 0x08, 0x03, 0x02, 0x01, 0x00, 0x35, 0x1d, 0x1c};
static const uint8_t ddd_code_start[5] = {0, 2, 9, 17, 20};

typedef struct ddd_bits_s {
    xx_io_device *device;
    int64_t position; /**< Absolute offset of the next byte to fetch. */
    int64_t end;      /**< Absolute end of the readable range. */
    uint8_t buffer[DDD_READ_CHUNK];
    size_t buffer_size;
    size_t buffer_pos;
    uint32_t current;
    int32_t bit_count;
    bool exhausted;
    bool io_error;
} ddd_bits;

typedef struct ddd_info_s {
    int64_t stream_size; /**< Lead + bit stream bytes actually fetched. */
    int64_t format_size; /**< Everything up to EOF (stream + slack). */
    uint32_t volume;
} ddd_info;

typedef struct ddd_stream_s {
    ddd_info info;
    size_t count;
    size_t index;
} ddd_stream;

static bool ddd_fetch_byte(ddd_bits *bits, uint8_t *value) {
    if (bits->buffer_pos >= bits->buffer_size) {
        int64_t left = bits->end - bits->position;
        size_t want;
        ssize_t got;
        if (left <= 0) {
            bits->exhausted = true;
            return false;
        }
        want = left > DDD_READ_CHUNK ? (size_t)DDD_READ_CHUNK : (size_t)left;
        if (xx_io_seek64(bits->device, bits->position, SEEK_SET) != 0) {
            bits->io_error = true;
            return false;
        }
        got = xx_io_read(bits->device, bits->buffer, want);
        if (got <= 0 || (size_t)got > want) {
            bits->io_error = true;
            return false;
        }
        bits->buffer_size = (size_t)got;
        bits->buffer_pos = 0U;
        bits->position += got;
    }
    *value = bits->buffer[bits->buffer_pos++];
    return true;
}

/* Next bit of the stream, most significant bit of each byte first.  Past the
 * end it yields 0 and sets `exhausted`; callers check that flag per code. */
static uint32_t ddd_bit(ddd_bits *bits) {
    if (bits->bit_count == 0) {
        uint8_t value = 0U;
        if (!ddd_fetch_byte(bits, &value)) return 0U;
        bits->current = value;
        bits->bit_count = 8;
    }
    --bits->bit_count;
    return (bits->current >> (uint32_t)bits->bit_count) & 1U;
}

/* An 8-bit field, stored least significant bit first. */
static uint8_t ddd_byte(ddd_bits *bits) {
    uint32_t value = 0U;
    int32_t index;
    for (index = 0; index < 8; ++index) value |= ddd_bit(bits) << index;
    return (uint8_t)value;
}

/* Bytes of the device consumed so far, relative to `start`. */
static int64_t ddd_consumed(const ddd_bits *bits, int64_t start) {
    return bits->position - (int64_t)(bits->buffer_size - bits->buffer_pos) -
           start;
}

/* Decode one track into `out` (4096 bytes).  `strict` rejects literals that
 * repeat a favourite; see xx_ddd.h. */
static bool ddd_track(ddd_bits *bits, uint8_t *out, bool strict) {
    uint8_t favorites[DDD_NUM_FAVORITES];
    uint8_t is_favorite[256];
    size_t at = 0U;
    int32_t index;
    xx_rt_memset(is_favorite, 0, sizeof(is_favorite));
    for (index = 0; index < DDD_NUM_FAVORITES; ++index) {
        favorites[index] = ddd_byte(bits);
        is_favorite[favorites[index]] = 1U;
    }
    if (bits->exhausted || bits->io_error) return false;
    while (at < DDD_TRACK_SIZE) {
        if (!ddd_bit(bits)) {
            uint8_t value = ddd_byte(bits);
            if (strict && is_favorite[value]) return false;
            out[at++] = value;
        } else {
            uint32_t code = (ddd_bit(bits) << 1U) | ddd_bit(bits);
            int32_t length;
            bool found = false;
            for (length = 0; length < 4 && !found; ++length) {
                int32_t fav;
                code = (code << 1U) | ddd_bit(bits);
                for (fav = ddd_code_start[length];
                     fav < ddd_code_start[length + 1]; ++fav) {
                    if (code == ddd_favorite_code[fav]) {
                        out[at++] = favorites[fav];
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                /* 1 + 6 bits matched no favourite: the eighth bit completes
                 * the 0x97 run escape. */
                uint8_t value;
                uint32_t count;
                (void)ddd_bit(bits);
                value = ddd_byte(bits);
                count = ddd_byte(bits);
                if (count == 0U) count = 256U;
                if (count > (uint32_t)(DDD_TRACK_SIZE - at)) return false;
                xx_rt_memset(out + at, value, count);
                at += count;
            }
        }
        if (bits->exhausted || bits->io_error) return false;
    }
    return true;
}

/* Decode the whole disk.  With `destination` the image is written out track
 * by track; without it the stream is only verified. */
static bool ddd_decode(Abstractformat *format, bool strict,
                       xx_io_device *destination, ddd_info *info,
                       xx_pd_struct *pd) {
    ddd_bits *bits;
    uint8_t *track;
    int64_t total, size, start, consumed;
    int32_t index;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < DDD_MIN_FILE || size > DDD_MAX_FILE) return false;
    bits = (ddd_bits *)xx_mem_calloc(1U, sizeof(*bits));
    track = (uint8_t *)xx_mem_alloc(DDD_TRACK_SIZE);
    if (!bits || !track) goto done;
    start = format->base_address;
    bits->device = format->device;
    bits->position = start + DDD_LEAD_SIZE;
    bits->end = total;
    /* Three zero bits, then the volume number. */
    if (ddd_bit(bits) || ddd_bit(bits) || ddd_bit(bits)) goto done;
    info->volume = ddd_byte(bits);
    if (bits->exhausted || bits->io_error) goto done;
    for (index = 0; index < DDD_NUM_TRACKS; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!ddd_track(bits, track, strict)) goto done;
        if (destination) {
            size_t written = 0U;
            while (written < DDD_TRACK_SIZE) {
                ssize_t amount = xx_io_write(destination, track + written,
                                             DDD_TRACK_SIZE - written);
                if (amount <= 0 || (size_t)amount > DDD_TRACK_SIZE - written)
                    goto done;
                written += (size_t)amount;
            }
        }
    }
    consumed = ddd_consumed(bits, start);
    if (consumed < DDD_LEAD_SIZE || consumed > size ||
        size - consumed > DDD_MAX_SLACK)
        goto done;
    info->stream_size = consumed;
    info->format_size = size;
    ok = true;
done:
    if (track) xx_mem_free(track);
    if (bits) xx_mem_free(bits);
    return ok;
}

static void ddd_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool ddd_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ddd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ddd_set_record(Abstractformat *format, xx_archive_record *record,
                           const ddd_info *info) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = DDD_LEAD_SIZE;
    record->data_offset = format->base_address + DDD_LEAD_SIZE;
    record->compressed_size = info->stream_size - DDD_LEAD_SIZE;
    return xx_archive_record_set_original_name(record, DDD_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(info->stream_size - DDD_LEAD_SIZE)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)DDD_IMAGE_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_ddd_init(xx_ddd *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DDD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "ddd");
    archive->format.check_is_valid = xx_ddd_check_is_valid;
    archive->format.handle_base_info = xx_ddd_handle_base_info;
    archive->format.get_format_size = xx_ddd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ddd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ddd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ddd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ddd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ddd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ddd_free_archive_records_reading;
    archive->stream_size = -1;
}

xx_ddd *xx_ddd_create(xx_io_device *device, int64_t base_address) {
    xx_ddd *archive = (xx_ddd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ddd_init(archive, device, base_address);
    return archive;
}

void xx_ddd_destroy(xx_ddd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ddd_free(xx_ddd *archive) {
    if (!archive) return;
    xx_ddd_destroy(archive);
    xx_mem_free(archive);
}

/* The detection probe: the strict decode (see xx_ddd.h). */
bool xx_ddd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ddd_info info;
    return ddd_decode(format, true, NULL, &info, pd);
}

bool xx_ddd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ddd_info info;
    xx_ddd *archive;
    if (!format || !ddd_decode(format, false, NULL, &info, pd)) return false;
    archive = (xx_ddd *)format;
    archive->number_of_records = 1U;
    archive->volume_number = info.volume;
    archive->stream_size = info.stream_size;
    format->number_of_archive_records = 1U;
    format->format_size = info.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ddd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ddd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ddd_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ddd_handle_base_info(format, pd))
               ? ((xx_ddd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ddd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ddd_stream *stream;
    xx_archive_record_state *state;
    ddd_info info;
    if (!ddd_decode(format, false, NULL, &info, pd)) return NULL;
    stream = (ddd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->info = info;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ddd_stream_free;
    state->total_records = 1U;
    if (!ddd_copy_options(&state->options, options) ||
        !ddd_set_record(format, &state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ddd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ddd_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    ddd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ddd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_ddd_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    ddd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    ddd_info info;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ddd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = ddd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ddd_decode(format, false, NULL, &info, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The member name is a constant, never taken from the file. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", DDD_PAYLOAD_NAME)
               : xx_str_concat(base, DDD_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ddd_decode(format, false, destination, &info, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ddd_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

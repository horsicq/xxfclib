/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_tar_common.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Composite readers retain the decoded TAR so its ordinary random-access
 * reader can serve all later record operations.  Keep malformed streams from
 * growing that cache without bound. */
#define XX_TAR_COMMON_MAX_DECODED_SIZE \
    ((size_t)1024U * (size_t)1024U * (size_t)1024U)
#define XX_TAR_COMMON_INITIAL_CAPACITY ((size_t)64U * (size_t)1024U)

typedef struct xx_tar_common_writer_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t position;
    xx_io_device device;
    xx_tar tar;
    xx_archive_write_state *tar_state;
    xx_tar_common_encode_fn encode;
    bool tar_finalized;
    bool finalized;
} xx_tar_common_writer;

static void xx_tar_common_release_payload(xx_tar_common *common) {
    if (!common) return;
    if (common->tar) {
        xx_tar_free(common->tar);
        common->tar = NULL;
    }
    if (common->decoded_device) {
        xx_io_close(common->decoded_device);
        common->decoded_device = NULL;
    }
    if (common->decoded_data) {
        xx_mem_free(common->decoded_data);
        common->decoded_data = NULL;
    }
    common->decoded_size = 0U;
    common->decoded_capacity = 0U;
    common->compressed_size = -1;
    common->valid = false;
}

static bool xx_tar_common_load_failed(xx_tar_common *common) {
    xx_tar_common_release_payload(common);
    if (common) common->attempted = false;
    return false;
}

static ssize_t xx_tar_common_sink_write(xx_io_device *device,
                                        const void *data, size_t size) {
    xx_tar_common *common =
        device ? (xx_tar_common *)device->priv : NULL;
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (!common || (!data && size != 0U)) return -1;
    if (size == 0U) return 0;
    if (common->decoded_size > XX_TAR_COMMON_MAX_DECODED_SIZE ||
        size > XX_TAR_COMMON_MAX_DECODED_SIZE - common->decoded_size) {
        return -1;
    }
    required = common->decoded_size + size;
    if (required > common->decoded_capacity) {
        capacity = common->decoded_capacity;
        if (capacity == 0U) capacity = XX_TAR_COMMON_INITIAL_CAPACITY;
        while (capacity < required) {
            if (capacity > XX_TAR_COMMON_MAX_DECODED_SIZE / 2U) {
                capacity = XX_TAR_COMMON_MAX_DECODED_SIZE;
            } else {
                capacity *= 2U;
            }
        }
        grown = (uint8_t *)xx_mem_realloc(common->decoded_data, capacity);
        if (!grown) return -1;
        common->decoded_data = grown;
        common->decoded_capacity = capacity;
    }
    xx_mem_copy(common->decoded_data + common->decoded_size, data, size);
    common->decoded_size = required;
    return (ssize_t)size;
}

static int64_t xx_tar_common_sink_size(xx_io_device *device) {
    const xx_tar_common *common =
        device ? (const xx_tar_common *)device->priv : NULL;
    return common && (uint64_t)common->decoded_size <= (uint64_t)INT64_MAX
               ? (int64_t)common->decoded_size
               : -1;
}

void xx_tar_common_init(xx_tar_common *common) {
    if (!common) return;
    xx_mem_zero(common, sizeof(*common));
    common->compressed_size = -1;
}

void xx_tar_common_cleanup(xx_tar_common *common) {
    if (!common) return;
    xx_tar_common_release_payload(common);
    common->attempted = false;
}

bool xx_tar_common_load(xx_tar_common *common, Abstractformat *outer,
                        xx_tar_common_decode_fn decode, xx_pd_struct *pd) {
    xx_io_device sink;
    int64_t total_size;
    int64_t compressed_size = -1;
    if (!common || !outer || !outer->device || !decode ||
        outer->base_address < 0 || outer->base_address > LONG_MAX) {
        return false;
    }
    if (common->attempted) return common->valid;
    xx_tar_common_release_payload(common);
    common->attempted = true;

    total_size = xx_io_total_size(outer->device);
    if (total_size <= outer->base_address)
        return xx_tar_common_load_failed(common);

    xx_mem_zero(&sink, sizeof(sink));
    sink.write = xx_tar_common_sink_write;
    sink.total_size = xx_tar_common_sink_size;
    sink.get_total_size = xx_tar_common_sink_size;
    sink.size = xx_tar_common_sink_size;
    sink.priv = common;
    if (!decode(outer, &sink, &compressed_size, pd) ||
        compressed_size <= 0 ||
        compressed_size > total_size - outer->base_address ||
        common->decoded_size == 0U ||
        (uint64_t)common->decoded_size > (uint64_t)INT64_MAX) {
        return xx_tar_common_load_failed(common);
    }

    common->decoded_device =
        xx_io_mem_open_ro(common->decoded_data, common->decoded_size);
    if (!common->decoded_device) {
        return xx_tar_common_load_failed(common);
    }
    common->tar = xx_tar_create(common->decoded_device, 0);
    if (!common->tar ||
        !xx_format_handle_base_info(&common->tar->format, pd)) {
        return xx_tar_common_load_failed(common);
    }
    common->compressed_size = compressed_size;
    common->valid = true;
    return true;
}

bool xx_tar_common_handle_base_info(xx_tar_common *common,
                                    Abstractformat *outer,
                                    xx_tar_common_decode_fn decode,
                                    xx_pd_struct *pd) {
    int64_t total_size;
    int64_t stream_end;
    if (!xx_tar_common_load(common, outer, decode, pd) ||
        !common->tar || common->compressed_size >
            INT64_MAX - outer->base_address) {
        if (outer) {
            outer->format_size = -1;
            outer->overlay_offset = -1;
            outer->overlay_size = 0;
            outer->number_of_archive_records = 0U;
            outer->is_valid = false;
            outer->base_info_handled = false;
        }
        return false;
    }
    total_size = xx_io_total_size(outer->device);
    stream_end = outer->base_address + common->compressed_size;
    outer->format_size = common->compressed_size;
    if (total_size > stream_end) {
        outer->overlay_offset = stream_end;
        outer->overlay_size = total_size - stream_end;
    } else {
        outer->overlay_offset = -1;
        outer->overlay_size = 0;
    }
    outer->number_of_archive_records =
        common->tar->format.number_of_archive_records;
    outer->format_type = XX_TYPE_ARCHIVE;
    outer->is_archive = true;
    outer->is_executable = false;
    outer->is_crypted = false;
    outer->is_valid = true;
    outer->base_info_handled = true;
    return true;
}

int64_t xx_tar_common_get_format_size(xx_tar_common *common,
                                      Abstractformat *outer,
                                      xx_tar_common_decode_fn decode,
                                      xx_pd_struct *pd) {
    if (!outer || (!outer->base_info_handled &&
                   !xx_tar_common_handle_base_info(common, outer, decode,
                                                   pd))) {
        return -1;
    }
    return outer->format_size;
}

uint64_t xx_tar_common_get_number_of_archive_records(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, xx_pd_struct *pd) {
    if (!outer || (!outer->base_info_handled &&
                   !xx_tar_common_handle_base_info(common, outer, decode,
                                                   pd))) {
        return 0U;
    }
    return outer->number_of_archive_records;
}

xx_archive_record_state *xx_tar_common_create_archive_records_reading(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, const xx_list_s *options,
    xx_pd_struct *pd) {
    if (!outer || (!outer->base_info_handled &&
                   !xx_tar_common_handle_base_info(common, outer, decode,
                                                   pd)) ||
        !common || !common->tar) {
        return NULL;
    }
    return xx_tar_create_archive_records_reading(&common->tar->format,
                                                 options, pd);
}

const xx_archive_record *xx_tar_common_get_current_archive_record(
    xx_tar_common *common, xx_archive_record_state *state) {
    return common && common->tar
               ? xx_tar_get_current_archive_record(&common->tar->format,
                                                   state)
               : NULL;
}

bool xx_tar_common_unpack_current_archive_record(
    xx_tar_common *common, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    return common && common->tar &&
           xx_tar_unpack_current_archive_record(&common->tar->format, state,
                                                pd);
}

bool xx_tar_common_archive_record_move_to_next(
    xx_tar_common *common, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    return common && common->tar &&
           xx_tar_archive_record_move_to_next(&common->tar->format, state,
                                              pd);
}

void xx_tar_common_free_archive_records_reading(
    xx_tar_common *common, xx_archive_record_state *state) {
    if (common && common->tar) {
        xx_tar_free_archive_records_reading(&common->tar->format, state);
    } else {
        xx_archive_record_state_free(state);
    }
}

const char *xx_tar_common_data_struct_id_to_string(
    xx_tar_common *common, uint32_t id) {
    return xx_tar_data_struct_id_to_string(
        common && common->tar ? &common->tar->format : NULL, id);
}

uint32_t xx_tar_common_data_struct_string_to_id(
    xx_tar_common *common, const char *name) {
    return xx_tar_data_struct_string_to_id(
        common && common->tar ? &common->tar->format : NULL, name);
}

xx_data_struct_state *xx_tar_common_create_data_structs_reading(
    xx_tar_common *common, Abstractformat *outer,
    xx_tar_common_decode_fn decode, xx_pd_struct *pd) {
    if (!outer || (!outer->base_info_handled &&
                   !xx_tar_common_handle_base_info(common, outer, decode,
                                                   pd)) ||
        !common || !common->tar) {
        return NULL;
    }
    return xx_tar_create_data_structs_reading(&common->tar->format, pd);
}

const xx_data_struct *xx_tar_common_get_current_data_struct(
    xx_tar_common *common, xx_data_struct_state *state) {
    return common && common->tar
               ? xx_tar_get_current_data_struct(&common->tar->format, state)
               : NULL;
}

bool xx_tar_common_data_struct_move_to_next(
    xx_tar_common *common, xx_data_struct_state *state,
    xx_pd_struct *pd) {
    return common && common->tar &&
           xx_tar_data_struct_move_to_next(&common->tar->format, state, pd);
}

void xx_tar_common_free_data_structs_reading(
    xx_tar_common *common, xx_data_struct_state *state) {
    if (common && common->tar) {
        xx_tar_free_data_structs_reading(&common->tar->format, state);
    } else {
        xx_data_struct_state_free(state);
    }
}

xx_data_struct_record_state *
xx_tar_common_create_data_struct_records_reading(
    xx_tar_common *common, const xx_data_struct *data_struct,
    xx_pd_struct *pd) {
    return common && common->tar
               ? xx_tar_create_data_struct_records_reading(
                     &common->tar->format, data_struct, pd)
               : NULL;
}

const xx_data_struct_record *xx_tar_common_get_current_data_struct_record(
    xx_tar_common *common, xx_data_struct_record_state *state) {
    return common && common->tar
               ? xx_tar_get_current_data_struct_record(
                     &common->tar->format, state)
               : NULL;
}

bool xx_tar_common_data_struct_record_move_to_next(
    xx_tar_common *common, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    return common && common->tar &&
           xx_tar_data_struct_record_move_to_next(&common->tar->format,
                                                  state, pd);
}

void xx_tar_common_free_data_struct_records_reading(
    xx_tar_common *common, xx_data_struct_record_state *state) {
    if (common && common->tar) {
        xx_tar_free_data_struct_records_reading(&common->tar->format, state);
    } else {
        xx_data_struct_record_state_free(state);
    }
}

static bool xx_tar_common_copy_write_options(
    xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool xx_tar_common_writer_reserve(xx_tar_common_writer *writer,
                                         size_t required) {
    size_t capacity;
    uint8_t *grown;
    if (!writer || required > XX_TAR_COMMON_MAX_DECODED_SIZE) return false;
    if (required <= writer->capacity) return true;
    capacity = writer->capacity ? writer->capacity
                                : XX_TAR_COMMON_INITIAL_CAPACITY;
    while (capacity < required) {
        if (capacity > XX_TAR_COMMON_MAX_DECODED_SIZE / 2U) {
            capacity = XX_TAR_COMMON_MAX_DECODED_SIZE;
        } else {
            capacity *= 2U;
        }
    }
    grown = (uint8_t *)xx_mem_realloc(writer->data, capacity);
    if (!grown) return false;
    writer->data = grown;
    writer->capacity = capacity;
    return true;
}

static ssize_t xx_tar_common_writer_read(xx_io_device *device,
                                         void *data, size_t size) {
    xx_tar_common_writer *writer =
        device ? (xx_tar_common_writer *)device->priv : NULL;
    size_t available;
    size_t amount;
    if (!writer || (!data && size != 0U)) return -1;
    if (size == 0U) return 0;
    if (writer->position >= writer->size) return 0;
    available = writer->size - writer->position;
    amount = size < available ? size : available;
    xx_mem_copy(data, writer->data + writer->position, amount);
    writer->position += amount;
    return (ssize_t)amount;
}

static ssize_t xx_tar_common_writer_write(xx_io_device *device,
                                          const void *data, size_t size) {
    xx_tar_common_writer *writer =
        device ? (xx_tar_common_writer *)device->priv : NULL;
    size_t required;
    if (!writer || (!data && size != 0U)) return -1;
    if (size == 0U) return 0;
    if (writer->position > XX_TAR_COMMON_MAX_DECODED_SIZE ||
        size > XX_TAR_COMMON_MAX_DECODED_SIZE - writer->position) {
        return -1;
    }
    required = writer->position + size;
    if (!xx_tar_common_writer_reserve(writer, required)) return -1;
    if (writer->position > writer->size) {
        xx_mem_zero(writer->data + writer->size,
                    writer->position - writer->size);
    }
    xx_mem_copy(writer->data + writer->position, data, size);
    writer->position = required;
    if (required > writer->size) writer->size = required;
    return (ssize_t)size;
}

static int xx_tar_common_writer_seek(xx_io_device *device, long offset,
                                     int origin) {
    xx_tar_common_writer *writer =
        device ? (xx_tar_common_writer *)device->priv : NULL;
    size_t base;
    size_t position;
    uint64_t distance;
    if (!writer) return -1;
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = writer->position;
    else if (origin == SEEK_END) base = writer->size;
    else return -1;

    if (offset >= 0L) {
        distance = (uint64_t)(unsigned long)offset;
        if (distance >
            (uint64_t)XX_TAR_COMMON_MAX_DECODED_SIZE - (uint64_t)base) {
            return -1;
        }
        position = base + (size_t)distance;
    } else {
        /* Avoid negating LONG_MIN directly. */
        distance = (uint64_t)(unsigned long)(-(offset + 1L)) + 1U;
        if (distance > (uint64_t)base) return -1;
        position = base - (size_t)distance;
    }
    writer->position = position;
    return 0;
}

static int64_t xx_tar_common_writer_size(xx_io_device *device) {
    const xx_tar_common_writer *writer =
        device ? (const xx_tar_common_writer *)device->priv : NULL;
    return writer ? (int64_t)writer->size : -1;
}

static void xx_tar_common_writer_free(void *pointer) {
    xx_tar_common_writer *writer = (xx_tar_common_writer *)pointer;
    if (!writer) return;
    if (writer->tar_state) {
        xx_format_free_archive_records_writing(&writer->tar.format,
                                               writer->tar_state);
        writer->tar_state = NULL;
    }
    xx_tar_destroy(&writer->tar);
    if (writer->data) xx_mem_free(writer->data);
    xx_mem_free(writer);
}

xx_archive_write_state *xx_tar_common_create_archive_records_writing(
    Abstractformat *outer, const xx_list_s *options,
    xx_tar_common_encode_fn encode, xx_pd_struct *pd) {
    xx_archive_write_state *state;
    xx_tar_common_writer *writer;
    if (!outer || !outer->device || !outer->device->write ||
        !outer->device->seek || !encode ||
        outer->base_address < 0 || outer->base_address > LONG_MAX ||
        (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    writer = (xx_tar_common_writer *)xx_mem_calloc(1U, sizeof(*writer));
    if (!state || !writer) {
        if (state) xx_mem_free(state);
        if (writer) xx_mem_free(writer);
        return NULL;
    }
    xx_archive_write_state_init(state, outer);
    if (!xx_tar_common_copy_write_options(&state->options, options)) {
        xx_archive_write_state_free(state);
        xx_mem_free(writer);
        return NULL;
    }
    writer->device.read = xx_tar_common_writer_read;
    writer->device.write = xx_tar_common_writer_write;
    writer->device.seek = xx_tar_common_writer_seek;
    writer->device.total_size = xx_tar_common_writer_size;
    writer->device.get_total_size = xx_tar_common_writer_size;
    writer->device.size = xx_tar_common_writer_size;
    writer->device.priv = writer;
    writer->encode = encode;
    xx_tar_init(&writer->tar, &writer->device, 0);
    writer->tar_state = xx_format_create_archive_records_writing(
        &writer->tar.format, NULL, pd);
    if (!writer->tar_state) {
        xx_tar_destroy(&writer->tar);
        xx_archive_write_state_free(state);
        xx_mem_free(writer);
        return NULL;
    }
    state->internal_state = writer;
    state->free_internal = xx_tar_common_writer_free;
    state->current_index = -1;
    state->total_records = 0;
    outer->format_size = -1;
    outer->number_of_archive_records = 0U;
    outer->overlay_offset = -1;
    outer->overlay_size = 0;
    outer->is_valid = false;
    outer->base_info_handled = false;
    return state;
}

bool xx_tar_common_pack_archive_record(
    Abstractformat *outer, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd) {
    xx_tar_common_writer *writer;
    bool result;
    if (!outer || !state || state->format != outer || !record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    writer = (xx_tar_common_writer *)state->internal_state;
    if (writer->tar_finalized || writer->finalized || !writer->tar_state) {
        return false;
    }
    result = xx_format_pack_archive_record(
        &writer->tar.format, writer->tar_state, record, source_dev, pd);
    if (result) {
        state->current_index = writer->tar_state->current_index;
        state->total_records = writer->tar_state->total_records;
    }
    return result;
}

bool xx_tar_common_finalize_archive_records_writing(
    Abstractformat *outer, xx_archive_write_state *state,
    xx_pd_struct *pd) {
    xx_tar_common_writer *writer;
    int64_t compressed_size = -1;
    int64_t total_size;
    int64_t stream_end;
    if (!outer || !state || state->format != outer ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    writer = (xx_tar_common_writer *)state->internal_state;
    if (writer->finalized || !writer->tar_state || !writer->encode) {
        return false;
    }
    if (!writer->tar_finalized) {
        if (!xx_format_finalize_archive_records_writing(
                &writer->tar.format, writer->tar_state, pd)) {
            return false;
        }
        writer->tar_finalized = true;
    }
    writer->position = 0U;
    if (writer->size == 0U ||
        (uint64_t)writer->size > (uint64_t)INT64_MAX ||
        !writer->encode(outer, &state->options, &writer->device,
                        (int64_t)writer->size, &compressed_size, pd) ||
        compressed_size <= 0 ||
        compressed_size > INT64_MAX - outer->base_address) {
        return false;
    }
    writer->finalized = true;
    state->current_index = writer->tar_state->current_index;
    state->total_records = writer->tar_state->total_records;
    outer->format_size = compressed_size;
    outer->number_of_archive_records =
        state->total_records > 0 ? (uint64_t)state->total_records : 0U;
    outer->is_valid = true;
    /* The transport is valid, but there is no parsed read cache for the new
     * bytes yet. A subsequent read/getter must rebuild that cache. */
    outer->base_info_handled = false;
    stream_end = outer->base_address + compressed_size;
    total_size = xx_io_total_size(outer->device);
    if (total_size > stream_end) {
        outer->overlay_offset = stream_end;
        outer->overlay_size = total_size - stream_end;
    } else {
        outer->overlay_offset = -1;
        outer->overlay_size = 0;
    }
    return true;
}

void xx_tar_common_free_archive_records_writing(
    Abstractformat *outer, xx_archive_write_state *state) {
    (void)outer;
    xx_archive_write_state_free(state);
}

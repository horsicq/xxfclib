/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zstd/xx_zstd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_ZSTD_PAYLOAD_NAME "payload"
#define XX_ZSTD_MAGIC UINT32_C(0xFD2FB528)
#define XX_ZSTD_SKIP_MAGIC UINT32_C(0x184D2A50)
#define XX_ZSTD_BLOCK_MAX (128U * 1024U)
/* The native decoder is deliberately bounded and one-shot.  Keep format
 * recognition within the same allocation limit as TAR+Zstandard. */
#define XX_ZSTD_MAX_STREAM_SIZE ((uint64_t)1024U * 1024U * 1024U)

typedef struct xx_zstd_counter_s {
    xx_io_device *target;
    uint64_t written;
    bool failed;
} xx_zstd_counter;

static void xx_zstd_vtable_destroy(Abstractformat *self);

static uint32_t xx_zstd_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t xx_zstd_read_u24le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U);
}

static bool xx_zstd_read_exact_at(xx_io_device *device, int64_t offset,
                                  void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_zstd_take(const uint8_t **cursor, const uint8_t *end,
                         size_t count) {
    if (!cursor || !*cursor || !end || (size_t)(end - *cursor) < count) {
        return false;
    }
    *cursor += count;
    return true;
}

static bool xx_zstd_read_variable(const uint8_t **cursor, const uint8_t *end,
                                  unsigned count, uint64_t *value) {
    uint64_t result = 0U;
    unsigned index;
    if (!cursor || !*cursor || !value || count > 8U ||
        (size_t)(end - *cursor) < count) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        result |= (uint64_t)(*cursor)[index] << (8U * index);
    }
    *cursor += count;
    *value = result;
    return true;
}

/* Scan complete standard/skippable frames and determine the exact output
 * allocation the native decoder needs.  Frames without a content-size field
 * cannot be passed safely to its bounded one-shot interface, so they are
 * rejected rather than guessed. */
static bool xx_zstd_scan_frames(const uint8_t *source, size_t size,
                                uint64_t *uncompressed_size,
                                uint64_t *frame_count) {
    const uint8_t *cursor = source;
    const uint8_t *end;
    uint64_t total = 0U;
    uint64_t frames = 0U;

    if (!source || !uncompressed_size || !frame_count || size < 4U) {
        return false;
    }
    end = source + size;
    while (cursor < end) {
        uint32_t magic;
        uint8_t descriptor;
        unsigned content_size_flag;
        unsigned dictionary_flag;
        unsigned dictionary_size;
        unsigned content_size_bytes;
        bool single_segment;
        bool checksum;
        uint64_t dictionary_id = 0U;
        uint64_t frame_size = 0U;
        bool last_block = false;

        if ((size_t)(end - cursor) < 4U) return false;
        magic = xx_zstd_read_u32le(cursor);
        cursor += 4U;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_ZSTD_SKIP_MAGIC) {
            uint32_t skipped_size;
            if ((size_t)(end - cursor) < 4U) return false;
            skipped_size = xx_zstd_read_u32le(cursor);
            cursor += 4U;
            if (!xx_zstd_take(&cursor, end, (size_t)skipped_size)) {
                return false;
            }
            continue;
        }
        if (magic != XX_ZSTD_MAGIC || cursor == end) return false;

        descriptor = *cursor++;
        if ((descriptor & UINT8_C(0x18)) != 0U) return false;
        content_size_flag = descriptor >> 6U;
        single_segment = (descriptor & UINT8_C(0x20)) != 0U;
        checksum = (descriptor & UINT8_C(0x04)) != 0U;
        dictionary_flag = descriptor & UINT8_C(0x03);
        if (!single_segment) {
            uint8_t window_descriptor;
            if (cursor == end) return false;
            window_descriptor = *cursor++;
            if ((window_descriptor >> 3U) >= 54U) return false;
        }
        dictionary_size = dictionary_flag == 0U ? 0U :
                          dictionary_flag == 1U ? 1U :
                          dictionary_flag == 2U ? 2U : 4U;
        if (!xx_zstd_read_variable(&cursor, end, dictionary_size,
                                   &dictionary_id) || dictionary_id != 0U) {
            return false;
        }
        content_size_bytes = content_size_flag == 0U ?
                                 (single_segment ? 1U : 0U) :
                             content_size_flag == 1U ? 2U :
                             content_size_flag == 2U ? 4U : 8U;
        if (content_size_bytes == 0U ||
            !xx_zstd_read_variable(&cursor, end, content_size_bytes,
                                   &frame_size)) {
            return false;
        }
        if (content_size_bytes == 2U) frame_size += 256U;
        if (frame_size > XX_ZSTD_MAX_STREAM_SIZE ||
            total > XX_ZSTD_MAX_STREAM_SIZE - frame_size) {
            return false;
        }
        total += frame_size;

        while (!last_block) {
            uint32_t header;
            unsigned block_type;
            size_t block_size;
            size_t encoded_size;
            if ((size_t)(end - cursor) < 3U) return false;
            header = xx_zstd_read_u24le(cursor);
            cursor += 3U;
            last_block = (header & 1U) != 0U;
            block_type = (header >> 1U) & 3U;
            block_size = (size_t)(header >> 3U);
            if (block_type == 3U || block_size > XX_ZSTD_BLOCK_MAX) {
                return false;
            }
            encoded_size = block_type == 1U ? 1U : block_size;
            if (!xx_zstd_take(&cursor, end, encoded_size)) return false;
        }
        if (checksum && !xx_zstd_take(&cursor, end, 4U)) return false;
        if (frames == UINT64_MAX) return false;
        ++frames;
    }
    if (frames == 0U) return false;
    *uncompressed_size = total;
    *frame_count = frames;
    return true;
}

static bool xx_zstd_scan_device(Abstractformat *self,
                                uint64_t *uncompressed_size,
                                uint64_t *frame_count,
                                int64_t *stream_size,
                                xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    bool result = false;

    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !frame_count || !stream_size ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < 4 ||
        (uint64_t)(total_size - self->base_address) >
            XX_ZSTD_MAX_STREAM_SIZE ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_zstd_read_exact_at(self->device, self->base_address,
                                         input, (size_t)input_size)) {
        goto cleanup;
    }
    result = xx_zstd_scan_frames(input, (size_t)input_size,
                                 uncompressed_size, frame_count);
    if (result) *stream_size = input_size;

cleanup:
    xx_mem_free(input);
    return result;
}

static ssize_t xx_zstd_counter_write(xx_io_device *device, const void *data,
                                     size_t size) {
    xx_zstd_counter *counter =
        device ? (xx_zstd_counter *)device->priv : NULL;
    size_t done = 0U;
    if (!counter || (!data && size != 0U) ||
        (uint64_t)size > UINT64_MAX - counter->written) {
        if (counter) counter->failed = true;
        return -1;
    }
    while (counter->target && done < size) {
        ssize_t amount = xx_io_write(counter->target,
                                     (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            counter->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    counter->written += (uint64_t)size;
    return (ssize_t)size;
}

static bool xx_zstd_decode_stream(Abstractformat *self,
                                  xx_io_device *destination,
                                  uint64_t *uncompressed_size,
                                  uint64_t *frame_count,
                                  int64_t *stream_size,
                                  xx_pd_struct *pd) {
    xx_zstd_counter counter;
    xx_io_device sink;
    uint64_t declared_size;
    uint64_t frames;
    int64_t input_size;

    if (!self || !uncompressed_size || !frame_count || !stream_size ||
        !xx_zstd_scan_device(self, &declared_size, &frames, &input_size,
                             pd)) {
        return false;
    }
    xx_mem_zero(&counter, sizeof(counter));
    xx_mem_zero(&sink, sizeof(sink));
    counter.target = destination;
    sink.write = xx_zstd_counter_write;
    sink.priv = &counter;
    if (!xx_zstd_unpack_device_to_device(self->device, self->base_address,
                                         input_size, &sink, declared_size,
                                         pd) || counter.failed ||
        counter.written != declared_size) {
        return false;
    }
    *uncompressed_size = counter.written;
    *frame_count = frames;
    *stream_size = input_size;
    return true;
}

static bool xx_zstd_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
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

static const xx_var *xx_zstd_find_option(const xx_list_s *options,
                                         uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_zstd_populate_record(Abstractformat *self,
                                    xx_archive_record *record) {
    const xx_zstd *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 4) {
        return false;
    }
    archive = (const xx_zstd *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 4;
    record->data_offset = self->base_address + 4;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_ZSTD_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_zstd_init(xx_zstd *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZSTD;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/zstd");
    xx_format_set_extension(&archive->format, "zst");
    archive->format.check_is_valid = xx_zstd_check_is_valid;
    archive->format.handle_base_info = xx_zstd_handle_base_info;
    archive->format.get_format_size = xx_zstd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zstd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zstd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zstd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zstd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zstd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zstd_free_archive_records_reading;
    archive->format.destroy = xx_zstd_vtable_destroy;
    archive->stream_end = -1;
}

xx_zstd *xx_zstd_create(xx_io_device *device, int64_t base_address) {
    xx_zstd *archive = (xx_zstd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_zstd_init(archive, device, base_address);
    return archive;
}

void xx_zstd_destroy(xx_zstd *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_frames = 0U;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_zstd_vtable_destroy(Abstractformat *self) {
    xx_zstd_destroy((xx_zstd *)self);
}

void xx_zstd_free(xx_zstd *archive) {
    if (!archive) return;
    xx_zstd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_zstd_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t uncompressed_size;
    uint64_t frame_count;
    int64_t stream_size;
    return xx_zstd_scan_device(self, &uncompressed_size, &frame_count,
                               &stream_size, pd);
}

bool xx_zstd_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t uncompressed_size;
    uint64_t frame_count;
    int64_t stream_size;
    int64_t total_size;
    xx_zstd *archive;

    if (!self || !xx_zstd_check_is_valid(self, pd) ||
        !xx_zstd_decode_stream(self, NULL, &uncompressed_size, &frame_count,
                               &stream_size, pd)) {
        if (self) {
            archive = (xx_zstd *)self;
            archive->number_of_frames = 0U;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    total_size = xx_io_total_size(self->device);
    archive = (xx_zstd *)self;
    archive->number_of_frames = frame_count;
    archive->uncompressed_size = uncompressed_size;
    archive->stream_end = self->base_address + stream_size;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = archive->stream_end < total_size
                               ? archive->stream_end
                               : -1;
    self->overlay_size = archive->stream_end < total_size
                             ? total_size - archive->stream_end
                             : 0;
    self->file_type = XX_FILE_TYPE_ZSTD;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_zstd_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_zstd_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_zstd_unpack_to_device(xx_zstd *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    uint64_t uncompressed_size;
    uint64_t frame_count;
    int64_t stream_size;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_zstd_decode_stream(&archive->format, destination,
                               &uncompressed_size, &frame_count,
                               &stream_size, pd)) {
        return false;
    }
    return stream_size == archive->format.format_size &&
           frame_count == archive->number_of_frames &&
           uncompressed_size == archive->uncompressed_size;
}

xx_archive_record_state *xx_zstd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_zstd_copy_options(&state->options, options) ||
        !xx_zstd_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_zstd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zstd_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_zstd_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_zstd *archive = (xx_zstd *)self;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = xx_zstd_find_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        uint64_t size;
        uint64_t frames;
        int64_t compressed_size;
        return xx_zstd_decode_stream(self, NULL, &size, &frames,
                                     &compressed_size, pd) &&
               compressed_size == self->format_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", XX_ZSTD_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_ZSTD_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_zstd_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_zstd_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_zstd_get_number_of_frames(const xx_zstd *archive) {
    return archive ? archive->number_of_frames : 0U;
}

uint64_t xx_zstd_get_uncompressed_size(const xx_zstd *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_zstd_get_stream_end(const xx_zstd *archive) {
    return archive ? archive->stream_end : -1;
}

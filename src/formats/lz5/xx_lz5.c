/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lz5/xx_lz5.h"

#include "xxfclib/algo/lz5/xx_lz5.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_LZ5_PAYLOAD_NAME "payload"
#define XX_LZ5_MAGIC UINT32_C(0x184D2205)
#define XX_LZ5_SKIP_MAGIC UINT32_C(0x184D2A50)
#define XX_LZ5_MAX_STREAM_SIZE ((uint64_t)1024U * 1024U * 1024U)

#define XX_LZ5_XXH_P1 UINT32_C(2654435761)
#define XX_LZ5_XXH_P2 UINT32_C(2246822519)
#define XX_LZ5_XXH_P3 UINT32_C(3266489917)
#define XX_LZ5_XXH_P4 UINT32_C(668265263)
#define XX_LZ5_XXH_P5 UINT32_C(374761393)

static void xx_lz5_vtable_destroy(Abstractformat *self);

static uint32_t xx_lz5_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t xx_lz5_read_u64le(const uint8_t *data) {
    return (uint64_t)xx_lz5_read_u32le(data) |
           ((uint64_t)xx_lz5_read_u32le(data + 4U) << 32U);
}

static uint32_t xx_lz5_rotl32(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t xx_lz5_xxh32(const uint8_t *data, size_t size) {
    uint32_t hash = XX_LZ5_XXH_P5 + (uint32_t)size;
    size_t offset = 0U;
    while (size - offset >= 4U) {
        hash += xx_lz5_read_u32le(data + offset) * XX_LZ5_XXH_P3;
        hash = xx_lz5_rotl32(hash, 17U) * XX_LZ5_XXH_P4;
        offset += 4U;
    }
    while (offset < size) {
        hash += (uint32_t)data[offset++] * XX_LZ5_XXH_P5;
        hash = xx_lz5_rotl32(hash, 11U) * XX_LZ5_XXH_P1;
    }
    hash ^= hash >> 15U;
    hash *= XX_LZ5_XXH_P2;
    hash ^= hash >> 13U;
    hash *= XX_LZ5_XXH_P3;
    return hash ^ (hash >> 16U);
}

static bool xx_lz5_block_limit(uint8_t descriptor, size_t *limit) {
    unsigned id;
    size_t value = 64U * 1024U;
    unsigned index;
    if (!limit) return false;
    id = (descriptor >> 4U) & 7U;
    if (id == 0U) return false;
    for (index = 1U; index < id; ++index) {
        if (value > SIZE_MAX / 4U) return false;
        value *= 4U;
    }
    *limit = value;
    return true;
}

static bool xx_lz5_read_exact_at(xx_io_device *device, int64_t offset,
                                 void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_lz5_take(const uint8_t **cursor, const uint8_t *end,
                        size_t count) {
    if (!cursor || !*cursor || !end || (size_t)(end - *cursor) < count) {
        return false;
    }
    *cursor += count;
    return true;
}

/* The native LZ5 codec uses a bounded exact-size destination.  Files which
 * omit their standard content-size field are not guessed by this reader. */
static bool xx_lz5_scan_frames(const uint8_t *source, size_t size,
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
        const uint8_t *descriptor_start;
        uint8_t flags;
        uint8_t descriptor;
        size_t block_limit;
        uint64_t content_size;
        bool block_checksum;
        bool content_checksum;

        if ((size_t)(end - cursor) < 4U) return false;
        magic = xx_lz5_read_u32le(cursor);
        cursor += 4U;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_LZ5_SKIP_MAGIC) {
            uint32_t skipped_size;
            if ((size_t)(end - cursor) < 4U) return false;
            skipped_size = xx_lz5_read_u32le(cursor);
            cursor += 4U;
            if (!xx_lz5_take(&cursor, end, (size_t)skipped_size)) {
                return false;
            }
            continue;
        }
        if (magic != XX_LZ5_MAGIC || (size_t)(end - cursor) < 3U) {
            return false;
        }
        descriptor_start = cursor;
        flags = *cursor++;
        descriptor = *cursor++;
        if ((flags >> 6U) != 1U || (flags & UINT8_C(0x03)) != 0U ||
            (flags & UINT8_C(0x08)) == 0U ||
            (descriptor & UINT8_C(0x8F)) != 0U ||
            !xx_lz5_block_limit(descriptor, &block_limit) ||
            (size_t)(end - cursor) < 8U) {
            return false;
        }
        content_size = xx_lz5_read_u64le(cursor);
        cursor += 8U;
        if (cursor == end ||
            *cursor != (uint8_t)(xx_lz5_xxh32(
                descriptor_start, (size_t)(cursor - descriptor_start)) >> 8U)) {
            return false;
        }
        ++cursor;
        if (content_size > XX_LZ5_MAX_STREAM_SIZE ||
            total > XX_LZ5_MAX_STREAM_SIZE - content_size) return false;
        total += content_size;
        block_checksum = (flags & UINT8_C(0x10)) != 0U;
        content_checksum = (flags & UINT8_C(0x04)) != 0U;
        for (;;) {
            uint32_t stored_size;
            size_t block_size;
            if ((size_t)(end - cursor) < 4U) return false;
            stored_size = xx_lz5_read_u32le(cursor);
            cursor += 4U;
            if (stored_size == 0U) break;
            block_size = (size_t)(stored_size & UINT32_C(0x7FFFFFFF));
            if (block_size == 0U || block_size > block_limit ||
                !xx_lz5_take(&cursor, end, block_size)) return false;
            if (block_checksum && !xx_lz5_take(&cursor, end, 4U)) return false;
        }
        if (content_checksum && !xx_lz5_take(&cursor, end, 4U)) return false;
        if (frames == UINT64_MAX) return false;
        ++frames;
    }
    if (frames == 0U) return false;
    *uncompressed_size = total;
    *frame_count = frames;
    return true;
}

static bool xx_lz5_scan_device(Abstractformat *self,
                               uint64_t *uncompressed_size,
                               uint64_t *frame_count,
                               int64_t *stream_size, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !frame_count || !stream_size ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < 4 ||
        (uint64_t)(total_size - self->base_address) > XX_LZ5_MAX_STREAM_SIZE ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (input && xx_lz5_read_exact_at(self->device, self->base_address, input,
                                      (size_t)input_size)) {
        result = xx_lz5_scan_frames(input, (size_t)input_size,
                                    uncompressed_size, frame_count);
        if (result) *stream_size = input_size;
    }
    xx_mem_free(input);
    return result;
}

static bool xx_lz5_write_all(xx_io_device *device, const void *data,
                             size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_lz5_decode_stream(Abstractformat *self,
                                 xx_io_device *destination,
                                 uint64_t *uncompressed_size,
                                 uint64_t *frame_count,
                                 int64_t *stream_size, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    uint64_t declared_size;
    uint64_t frames;
    size_t written = 0U;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !frame_count || !stream_size ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < 4 ||
        (uint64_t)(total_size - self->base_address) > XX_LZ5_MAX_STREAM_SIZE ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_lz5_read_exact_at(self->device, self->base_address,
                                        input, (size_t)input_size) ||
        !xx_lz5_scan_frames(input, (size_t)input_size, &declared_size,
                            &frames) || declared_size > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }
    output = (uint8_t *)xx_mem_alloc(declared_size == 0U ? 1U :
                                     (size_t)declared_size);
    if (!output || !xx_lz5_decompress_memory(input, (size_t)input_size,
                                             output, (size_t)declared_size,
                                             &written) ||
        written != (size_t)declared_size ||
        (destination && !xx_lz5_write_all(destination, output, written, pd))) {
        goto cleanup;
    }
    *uncompressed_size = declared_size;
    *frame_count = frames;
    *stream_size = input_size;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

static bool xx_lz5_copy_options(xx_list_s *destination,
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

static const xx_var *xx_lz5_find_option(const xx_list_s *options,
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

static bool xx_lz5_populate_record(Abstractformat *self,
                                   xx_archive_record *record) {
    const xx_lz5 *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 4) return false;
    archive = (const xx_lz5 *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 4;
    record->data_offset = self->base_address + 4;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_LZ5_PAYLOAD_NAME) &&
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

void xx_lz5_init(xx_lz5 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LZ5;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lz5");
    xx_format_set_extension(&archive->format, "lz5");
    archive->format.check_is_valid = xx_lz5_check_is_valid;
    archive->format.handle_base_info = xx_lz5_handle_base_info;
    archive->format.get_format_size = xx_lz5_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lz5_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lz5_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lz5_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lz5_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lz5_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lz5_free_archive_records_reading;
    archive->format.destroy = xx_lz5_vtable_destroy;
    archive->stream_end = -1;
}

xx_lz5 *xx_lz5_create(xx_io_device *device, int64_t base_address) {
    xx_lz5 *archive = (xx_lz5 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lz5_init(archive, device, base_address);
    return archive;
}

void xx_lz5_destroy(xx_lz5 *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_frames = 0U;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_lz5_vtable_destroy(Abstractformat *self) {
    xx_lz5_destroy((xx_lz5 *)self);
}

void xx_lz5_free(xx_lz5 *archive) {
    if (!archive) return;
    xx_lz5_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lz5_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    uint64_t frames;
    int64_t stream_size;
    return xx_lz5_scan_device(self, &size, &frames, &stream_size, pd);
}

bool xx_lz5_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    uint64_t frames;
    int64_t stream_size;
    int64_t total_size;
    xx_lz5 *archive;
    if (!self || !xx_lz5_check_is_valid(self, pd) ||
        !xx_lz5_decode_stream(self, NULL, &size, &frames, &stream_size, pd)) {
        if (self) {
            archive = (xx_lz5 *)self;
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
    archive = (xx_lz5 *)self;
    archive->number_of_frames = frames;
    archive->uncompressed_size = size;
    archive->stream_end = self->base_address + stream_size;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = archive->stream_end < total_size ?
                               archive->stream_end : -1;
    self->overlay_size = archive->stream_end < total_size ?
                             total_size - archive->stream_end : 0;
    self->file_type = XX_FILE_TYPE_LZ5;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lz5_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_lz5_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_lz5_unpack_to_device(xx_lz5 *archive, xx_io_device *destination,
                             xx_pd_struct *pd) {
    uint64_t size;
    uint64_t frames;
    int64_t stream_size;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_lz5_decode_stream(&archive->format, destination, &size, &frames,
                              &stream_size, pd)) return false;
    return stream_size == archive->format.format_size &&
           frames == archive->number_of_frames &&
           size == archive->uncompressed_size;
}

xx_archive_record_state *xx_lz5_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_lz5_copy_options(&state->options, options) ||
        !xx_lz5_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_lz5_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_lz5_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_lz5_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_lz5 *archive = (xx_lz5 *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_lz5_find_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        uint64_t size;
        uint64_t frames;
        int64_t compressed_size;
        return xx_lz5_decode_stream(self, NULL, &size, &frames,
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
        destination_path = xx_str_concat3(base_path, "/", XX_LZ5_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_LZ5_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_lz5_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_lz5_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_lz5_get_number_of_frames(const xx_lz5 *archive) {
    return archive ? archive->number_of_frames : 0U;
}

uint64_t xx_lz5_get_uncompressed_size(const xx_lz5 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_lz5_get_stream_end(const xx_lz5 *archive) {
    return archive ? archive->stream_end : -1;
}

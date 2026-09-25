/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HUF archives contain a single LSB-first Huffman tree that applies to all
 * member names and data.  The encoded streams have no individual length;
 * each data stream is bounded by the next stream start in the container.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/huf/xx_huf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XX_HUF_MAGIC UINT16_C(0x01bd)
#define XX_HUF_HEADER_SIZE 10U
#define XX_HUF_RECORD_SIZE 13U
#define XX_HUF_MAX_MEMBERS 65535U
#define XX_HUF_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_HUF_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_HUF_MAX_NAME_SIZE 256U
#define XX_HUF_MAX_NAME_WINDOW 8448U
#define XX_HUF_MAX_TREE_SIZE 64U

typedef struct xx_huf_member_s {
    char *name;
    int64_t record_offset;
    int64_t name_offset;
    int64_t data_offset;
    int64_t stream_size;
    uint32_t uncompressed_size;
    uint8_t flags;
} xx_huf_member;

typedef struct xx_huf_stream_s {
    xx_huf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    xx_huf_tree tree;
} xx_huf_stream;

static void xx_huf_vtable_destroy(Abstractformat *self);

static uint16_t xx_huf_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_huf_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_huf_read_exact_at(xx_io_device *device, int64_t offset,
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

static int xx_huf_compare_offsets(const void *left, const void *right) {
    int64_t a = *(const int64_t *)left;
    int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

static bool xx_huf_name_has_unsafe_segment(const char *name, size_t start,
                                           size_t end) {
    return end == start ||
           (end == start + 1U && name[start] == '.') ||
           (end == start + 2U && name[start] == '.' &&
            name[start + 1U] == '.');
}

static bool xx_huf_normalize_name(char *name, size_t length) {
    size_t index;
    size_t segment_start = 0U;
    if (!name || length == 0U || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)name[index];
        if (value < 0x20U || value > 0x7eU || value == '"' ||
            value == '*' || value == '<' || value == '>' || value == '?' ||
            value == '|' || value == ':') {
            return false;
        }
        if (value == '/' || value == '\\') {
            if (xx_huf_name_has_unsafe_segment(name, segment_start, index)) {
                return false;
            }
            name[index] = '/';
            segment_start = index + 1U;
        }
    }
    if (xx_huf_name_has_unsafe_segment(name, segment_start, length)) {
        return false;
    }
    name[length] = '\0';
    return true;
}

static void xx_huf_stream_free(void *opaque) {
    xx_huf_stream *stream = (xx_huf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_mem_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_huf_parse(Abstractformat *format, xx_huf_stream **result,
                         xx_pd_struct *pd) {
    uint8_t header[XX_HUF_HEADER_SIZE];
    uint8_t symbols[XX_HUF_MAX_SYMBOLS];
    uint8_t tree_bits[XX_HUF_MAX_TREE_SIZE];
    uint8_t *directory = NULL;
    int64_t *starts = NULL;
    xx_huf_stream *stream = NULL;
    int64_t total_size;
    int64_t input_size;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t directory_end;
    int64_t tree_offset;
    int64_t tree_size;
    uint16_t member_count;
    uint16_t symbol_count;
    size_t tree_bytes_used;
    size_t index;
    bool valid = false;

    if (result) *result = NULL;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address ||
        (uint64_t)(total_size - format->base_address) > XX_HUF_MAX_INPUT) {
        return false;
    }
    input_size = total_size - format->base_address;
    if (input_size < (int64_t)(XX_HUF_HEADER_SIZE + XX_HUF_RECORD_SIZE) ||
        !xx_huf_read_exact_at(format->device, format->base_address, header,
                              sizeof(header)) ||
        xx_huf_read16le(header) != XX_HUF_MAGIC) {
        return false;
    }
    member_count = xx_huf_read16le(header + 2U);
    symbol_count = xx_huf_read16le(header + 4U);
    directory_offset = (int64_t)xx_huf_read32le(header + 6U);
    if (member_count == 0U || member_count > XX_HUF_MAX_MEMBERS ||
        symbol_count == 0U || symbol_count > XX_HUF_MAX_SYMBOLS ||
        directory_offset <= 9 || directory_offset >= input_size) {
        return false;
    }
    directory_size = (int64_t)member_count * XX_HUF_RECORD_SIZE;
    if (directory_size <= 0 || directory_size > input_size ||
        directory_offset > input_size - directory_size) {
        return false;
    }
    directory_end = directory_offset + directory_size;
    tree_offset = (int64_t)XX_HUF_HEADER_SIZE + symbol_count;
    if (tree_offset >= directory_offset) return false;
    tree_size = directory_offset - tree_offset;
    if (tree_size <= 0 || tree_size > (int64_t)sizeof(tree_bits) ||
        !xx_huf_read_exact_at(format->device,
                              format->base_address + XX_HUF_HEADER_SIZE,
                              symbols, symbol_count) ||
        !xx_huf_read_exact_at(format->device,
                              format->base_address + tree_offset, tree_bits,
                              (size_t)tree_size)) {
        return false;
    }

    stream = (xx_huf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream ||
        !xx_huf_build_tree(tree_bits, (size_t)tree_size, symbols,
                           symbol_count, &stream->tree, &tree_bytes_used) ||
        tree_bytes_used != (size_t)tree_size) {
        goto cleanup;
    }
    stream->items = (xx_huf_member *)xx_mem_calloc(member_count,
                                                    sizeof(*stream->items));
    starts = (int64_t *)xx_mem_alloc((size_t)member_count * 2U *
                                     sizeof(*starts));
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!stream->items || !starts || !directory ||
        !xx_huf_read_exact_at(format->device,
                              format->base_address + directory_offset,
                              directory, (size_t)directory_size)) {
        goto cleanup;
    }
    stream->count = member_count;
    for (index = 0U; index < stream->count; ++index) {
        const uint8_t *record = directory + index * XX_HUF_RECORD_SIZE;
        xx_huf_member *member = &stream->items[index];
        int64_t name_relative = (int64_t)xx_huf_read32le(record);
        int64_t data_relative = (int64_t)xx_huf_read32le(record + 8U);
        if ((pd && xx_pd_is_stopped(pd)) ||
            xx_huf_read32le(record + 4U) > XX_HUF_MAX_OUTPUT ||
            name_relative < directory_end || name_relative >= input_size ||
            data_relative < directory_end || data_relative >= input_size) {
            goto cleanup;
        }
        member->record_offset = format->base_address + directory_offset +
                                (int64_t)index * XX_HUF_RECORD_SIZE;
        member->name_offset = format->base_address + name_relative;
        member->data_offset = format->base_address + data_relative;
        member->uncompressed_size = xx_huf_read32le(record + 4U);
        member->flags = record[12U];
        starts[index * 2U] = name_relative;
        starts[index * 2U + 1U] = data_relative;
    }
    xx_rt_qsort(starts, stream->count * 2U, sizeof(*starts), xx_huf_compare_offsets);
    for (index = 0U; index < stream->count; ++index) {
        xx_huf_member *member = &stream->items[index];
        uint8_t *coded_name = NULL;
        char decoded_name[XX_HUF_MAX_NAME_SIZE + 1U];
        size_t decoded_length = 0U;
        size_t name_window;
        size_t start_index;
        int64_t name_relative = member->name_offset - format->base_address;
        int64_t data_relative = member->data_offset - format->base_address;
        int64_t stream_end = input_size;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        name_window = (size_t)(input_size - name_relative);
        if (name_window > XX_HUF_MAX_NAME_WINDOW) {
            name_window = XX_HUF_MAX_NAME_WINDOW;
        }
        coded_name = (uint8_t *)xx_mem_alloc(name_window);
        if (!coded_name ||
            !xx_huf_read_exact_at(format->device, member->name_offset,
                                  coded_name, name_window) ||
            !xx_huf_decode_cstring(&stream->tree, coded_name, name_window,
                                   decoded_name, sizeof(decoded_name),
                                   XX_HUF_MAX_NAME_SIZE, &decoded_length,
                                   NULL) ||
            !xx_huf_normalize_name(decoded_name, decoded_length)) {
            xx_mem_free(coded_name);
            goto cleanup;
        }
        member->name = (char *)xx_mem_alloc(decoded_length + 1U);
        if (!member->name) {
            xx_mem_free(coded_name);
            goto cleanup;
        }
        xx_rt_memcpy(member->name, decoded_name, decoded_length + 1U);
        xx_mem_free(coded_name);

        for (start_index = 0U; start_index < stream->count * 2U;
             ++start_index) {
            if (starts[start_index] > data_relative) {
                stream_end = starts[start_index];
                break;
            }
        }
        if (stream_end <= data_relative) goto cleanup;
        member->stream_size = stream_end - data_relative;
    }
    stream->archive_size = input_size;
    valid = true;
cleanup:
    xx_mem_free(directory);
    xx_mem_free(starts);
    if (!valid) {
        xx_huf_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool xx_huf_copy_options(xx_list_s *destination,
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

static const xx_var *xx_huf_find_option(const xx_list_s *options,
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

static bool xx_huf_populate_record(xx_archive_record *record,
                                   const xx_huf_member *member) {
    if (!record || !member || !member->name || member->stream_size <= 0) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = XX_HUF_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->stream_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->stream_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_huf_decode_member(Abstractformat *format,
                                 const xx_huf_stream *stream,
                                 const xx_huf_member *member,
                                 uint8_t **plain, size_t *plain_size,
                                 xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    bool result = false;
    if (plain) *plain = NULL;
    if (plain_size) *plain_size = 0U;
    if (!format || !format->device || !stream || !member || !plain ||
        !plain_size || member->stream_size <= 0 ||
        (uint64_t)member->stream_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)member->stream_size);
    output = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0U ?
                                         (size_t)member->uncompressed_size :
                                         1U);
    if (!packed || !output ||
        !xx_huf_read_exact_at(format->device, member->data_offset, packed,
                              (size_t)member->stream_size) ||
        (member->uncompressed_size != 0U &&
         !xx_huf_decode_memory(&stream->tree, packed,
                               (size_t)member->stream_size, output,
                               (size_t)member->uncompressed_size, NULL)) ||
        (pd && xx_pd_is_stopped(pd))) {
        goto cleanup;
    }
    *plain = output;
    *plain_size = member->uncompressed_size;
    output = NULL;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(packed);
    return result;
}

void xx_huf_init(xx_huf *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_HUF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-huf");
    xx_format_set_extension(&archive->format, "huf");
    archive->format.check_is_valid = xx_huf_check_is_valid;
    archive->format.handle_base_info = xx_huf_handle_base_info;
    archive->format.get_format_size = xx_huf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_huf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_huf_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_huf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_huf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_huf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_huf_free_archive_records_reading;
    archive->format.destroy = xx_huf_vtable_destroy;
}

xx_huf *xx_huf_create(xx_io_device *device, int64_t base_address) {
    xx_huf *archive = (xx_huf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_huf_init(archive, device, base_address);
    return archive;
}

void xx_huf_destroy(xx_huf *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

static void xx_huf_vtable_destroy(Abstractformat *self) {
    xx_huf_destroy((xx_huf *)self);
}

void xx_huf_free(xx_huf *archive) {
    if (!archive) return;
    xx_huf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_huf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_huf_stream *stream = NULL;
    bool result = xx_huf_parse(self, &stream, pd);
    xx_huf_stream_free(stream);
    return result;
}

bool xx_huf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_huf_stream *stream = NULL;
    xx_huf *archive;
    if (!self || !xx_huf_parse(self, &stream, pd)) {
        if (self) {
            archive = (xx_huf *)self;
            archive->number_of_records = 0U;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_huf *)self;
    archive->number_of_records = stream->count;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_HUF;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_huf_stream_free(stream);
    return true;
}

int64_t xx_huf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_huf_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_huf *)self)->number_of_records;
}

xx_archive_record_state *xx_huf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_huf_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!self || !xx_huf_parse(self, &stream, pd) || !stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_huf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_huf_stream_free;
    state->total_records = stream->count;
    state->current_index = 0;
    if (!xx_huf_copy_options(&state->options, options) ||
        !xx_huf_populate_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_huf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_huf_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_huf_stream *stream;
    if (!self || !state || state->format != self ||
        !(stream = (xx_huf_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (stream->index + 1U >= stream->count) {
        state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    state->has_record = xx_huf_populate_record(&state->current_record,
                                                &stream->items[stream->index]);
    return state->has_record;
}

bool xx_huf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_huf_stream *stream;
    xx_huf_member *member;
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (xx_huf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    member = &stream->items[stream->index];
    if (!xx_huf_decode_member(self, stream, member, &plain, &plain_size, pd)) {
        goto cleanup;
    }
    path_value = xx_huf_find_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        result = true;
        goto cleanup;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) goto cleanup;
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        destination_path = xx_str_concat(base_path, member->name);
    }
    if (!destination_path ||
        !xx_store_create_dirs_a(destination_path, false)) {
        goto cleanup;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        if (!output) goto cleanup;
        result = true;
        while (written < plain_size) {
            ssize_t amount;
            if (pd && xx_pd_is_stopped(pd)) {
                result = false;
                break;
            }
            amount = xx_io_write(output, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(output) != 0) result = false;
    }
cleanup:
    if (!result && destination_path && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    xx_str_free(owned_path);
    xx_mem_free(plain);
    return result;
}

void xx_huf_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

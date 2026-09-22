/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/aldus/xx_aldus.h"

#include "xxfclib/algo/aldus/xx_aldus_lzw.h"
#include "xxfclib/algo/aldus/xx_aldus_lzsh.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ALDUS_HEADER_MIN 0x40U
#define ALDUS_HEADER_LZW 100U
#define ALDUS_HEADER_NEW 200U
#define ALDUS_SUBHEADER 22U
#define ALDUS_TRAILER 18U
#define ALDUS_LZW_BLOCK 20000U
#define ALDUS_NEW_BLOCK 16384U
#define ALDUS_MAX_BLOCKS 0x100000U

typedef enum aldus_generation_e {
    ALDUS_GENERATION_UNKNOWN = 0,
    ALDUS_GENERATION_LZW = 1,
    ALDUS_GENERATION_PKZP = 2,
    ALDUS_GENERATION_LZSH = 3
} aldus_generation;

typedef struct aldus_stream {
    aldus_generation generation;
    char *name;
    uint16_t *blocks;
    uint32_t block_count;
    uint32_t last_block_size;
    uint32_t original_size;
    uint32_t timestamp;
    int64_t header_size;
    int64_t data_offset;
    int64_t stream_size;
    int64_t archive_size;
} aldus_stream;

static uint16_t be16(const uint8_t *value) {
    return (uint16_t)(((uint16_t)value[0] << 8U) | value[1]);
}

static uint32_t be32(const uint8_t *value) {
    return ((uint32_t)be16(value) << 16U) | be16(value + 2U);
}

static uint32_t le32(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static bool range_inside(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool read_at(xx_io_device *device, int64_t offset, void *buffer,
                    size_t size) {
    size_t done = 0U;
    if (!device || !buffer || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t result = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (result <= 0 || (size_t)result > size - done) return false;
        done += (size_t)result;
    }
    return true;
}

static aldus_generation magic_generation(const uint8_t *magic) {
    if (xx_rt_memcmp(magic, "ALDUS LZW   1.00", 16U) == 0)
        return ALDUS_GENERATION_LZW;
    if (xx_rt_memcmp(magic, "ALDUS PKZP  2.00", 16U) == 0)
        return ALDUS_GENERATION_PKZP;
    if (xx_rt_memcmp(magic, "ADOBE LZSH  3.00", 16U) == 0)
        return ALDUS_GENERATION_LZSH;
    return ALDUS_GENERATION_UNKNOWN;
}

static bool valid_name_bytes(const uint8_t *name, size_t size) {
    size_t index;
    if (!name || size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7eU ||
            name[index] == '/' || name[index] == '\\' || name[index] == ':')
            return false;
    }
    return true;
}

static bool safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void stream_free(void *opaque) {
    aldus_stream *stream = (aldus_stream *)opaque;
    if (!stream) return;
    if (stream->blocks) xx_mem_free(stream->blocks);
    if (stream->name) xx_str_free(stream->name);
    xx_mem_free(stream);
}

static bool parse(Abstractformat *format, aldus_stream **result) {
    uint8_t header[ALDUS_HEADER_MIN];
    uint8_t subheader[ALDUS_SUBHEADER];
    uint8_t trailer[ALDUS_TRAILER];
    uint8_t *table = NULL;
    aldus_stream *stream = NULL;
    int64_t total, size, table_offset, data_offset, declared_size;
    int64_t table_size, packed_total = 0;
    uint32_t block_size, last_block, block_count, original;
    size_t name_size, index;
    aldus_generation generation;

    if (!format || !format->device || !result) return false;
    total = xx_io_total_size(format->device);
    if (format->base_address < 0 || total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)ALDUS_HEADER_MIN + ALDUS_SUBHEADER ||
        !read_at(format->device, format->base_address, header, sizeof(header)))
        return false;
    generation = magic_generation(header);
    if (generation == ALDUS_GENERATION_UNKNOWN) return false;

    {
        uint16_t header_size = be16(header + 16U);
        uint16_t expected = generation == ALDUS_GENERATION_LZW
                                ? ALDUS_HEADER_LZW : ALDUS_HEADER_NEW;
        if (header_size != expected ||
            !range_inside(size, header_size, ALDUS_SUBHEADER)) return false;
    }
    for (name_size = 0U; name_size < 32U && header[0x12U + name_size] != 0U;
         ++name_size) {}
    if (name_size == 0U || name_size == 32U ||
        !valid_name_bytes(header + 0x12U, name_size)) return false;
    for (index = name_size + 1U; index < 32U; ++index)
        if (header[0x12U + index] != 0U) return false;

    original = be32(header + 0x32U);
    if (!read_at(format->device,
                 format->base_address + (int64_t)be16(header + 16U),
                 subheader, sizeof(subheader))) return false;
    if (be16(subheader) != ALDUS_SUBHEADER) return false;
    block_size = be16(subheader + 2U);
    last_block = be16(subheader + 4U);
    block_count = be32(subheader + 6U);
    table_offset = be32(subheader + 10U);
    data_offset = be32(subheader + 14U);
    declared_size = be32(subheader + 18U);
    if (block_count == 0U || block_count > ALDUS_MAX_BLOCKS ||
        last_block == 0U || last_block > block_size ||
        block_size != (generation == ALDUS_GENERATION_LZW
                           ? ALDUS_LZW_BLOCK : ALDUS_NEW_BLOCK))
        return false;
    if ((int64_t)(block_count - 1U) * block_size + last_block != original)
        return false;
    table_size = (int64_t)block_count * 2;
    if (table_offset != (int64_t)be16(header + 16U) + ALDUS_SUBHEADER ||
        data_offset != table_offset + table_size || declared_size != size ||
        !range_inside(size, table_offset, table_size) ||
        !range_inside(size, data_offset, ALDUS_TRAILER)) return false;
    if ((uint64_t)table_size > SIZE_MAX) return false;

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    stream = (aldus_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!table || !stream ||
        !read_at(format->device, format->base_address + table_offset, table,
                 (size_t)table_size)) goto fail;
    stream->blocks = (uint16_t *)xx_mem_alloc((size_t)block_count *
                                               sizeof(*stream->blocks));
    stream->name = (char *)xx_mem_alloc(name_size + 1U);
    if (!stream->blocks || !stream->name) goto fail;
    xx_rt_memcpy(stream->name, header + 0x12U, name_size);
    stream->name[name_size] = 0;
    for (index = 0U; index < block_count; ++index) {
        uint16_t packed = be16(table + index * 2U);
        if (packed < 2U || (packed & 1U) != 0U ||
            packed_total > INT64_MAX - packed) goto fail;
        stream->blocks[index] = packed;
        packed_total += packed;
    }
    if (!range_inside(size, data_offset, packed_total) ||
        data_offset > INT64_MAX - packed_total ||
        data_offset + packed_total > INT64_MAX - ALDUS_TRAILER ||
        data_offset + packed_total + ALDUS_TRAILER != size ||
        !read_at(format->device,
                 format->base_address + data_offset + packed_total,
                 trailer, sizeof(trailer))) goto fail;
    for (index = 0U; index < sizeof(trailer); ++index)
        if (trailer[index] != 0U) goto fail;

    stream->generation = generation;
    stream->block_count = block_count;
    stream->last_block_size = last_block;
    stream->original_size = original;
    stream->timestamp = le32(header + 0x36U);
    stream->header_size = be16(header + 16U);
    stream->data_offset = data_offset;
    stream->stream_size = data_offset + packed_total - stream->header_size;
    stream->archive_size = size;
    xx_mem_free(table);
    *result = stream;
    return true;

fail:
    if (table) xx_mem_free(table);
    stream_free(stream);
    return false;
}

static bool copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *source_meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copied;
        if (!source_meta) continue;
        xx_meta_init(&copied, source_meta->meta_id);
        if (!xx_var_copy(&copied.var, &source_meta->var) ||
            !xx_list_append(destination, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool set_record(xx_archive_record *record, const aldus_stream *stream,
                       int64_t base_address) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->data_offset = base_address + stream->header_size;
    record->compressed_size = stream->stream_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          stream->generation) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          stream->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool decode_member(Abstractformat *format, const aldus_stream *stream,
                          uint8_t **plain, size_t *plain_size) {
    uint8_t *output = NULL;
    uint8_t *input = NULL;
    int64_t offset;
    size_t written = 0U;
    uint32_t index;
    if (!format || !stream || !plain || !plain_size ||
        (stream->generation != ALDUS_GENERATION_LZW &&
         stream->generation != ALDUS_GENERATION_PKZP &&
         stream->generation != ALDUS_GENERATION_LZSH) ||
        (uint64_t)stream->original_size > SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc(stream->original_size);
    if (!output) return false;
    offset = format->base_address + stream->data_offset;
    for (index = 0U; index < stream->block_count; ++index) {
        size_t packed_size = stream->blocks[index];
        size_t unpacked_size = index + 1U == stream->block_count
                                   ? stream->last_block_size
                                   : (stream->generation == ALDUS_GENERATION_LZW
                                          ? ALDUS_LZW_BLOCK : ALDUS_NEW_BLOCK);
        size_t block_written = 0U;
        input = (uint8_t *)xx_mem_alloc(packed_size);
        if (!input || !read_at(format->device, offset, input, packed_size) ||
            unpacked_size > (size_t)stream->original_size - written ||
            !(stream->generation == ALDUS_GENERATION_LZW
                  ? xx_aldus_lzw_decode_block(input, packed_size,
                                              output + written, unpacked_size,
                                              &block_written)
                  : stream->generation == ALDUS_GENERATION_PKZP
                        ? xx_dcl_decode_memory(input, packed_size,
                                               output + written, unpacked_size,
                                               &block_written)
                        : xx_aldus_lzsh_decode_block(input, packed_size,
                                                     output + written,
                                                     unpacked_size,
                                                     &block_written)) ||
            block_written != unpacked_size) goto fail;
        xx_mem_free(input);
        input = NULL;
        written += block_written;
        offset += packed_size;
    }
    if (written != stream->original_size) goto fail;
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (input) xx_mem_free(input);
    xx_mem_free(output);
    return false;
}

void xx_aldus_init(xx_aldus *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_ALDUS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-aldus-setup");
    xx_format_set_extension(&archive->format, "aldus");
    archive->format.check_is_valid = xx_aldus_check_is_valid;
    archive->format.handle_base_info = xx_aldus_handle_base_info;
    archive->format.get_format_size = xx_aldus_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_aldus_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_aldus_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_aldus_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_aldus_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_aldus_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_aldus_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_aldus *xx_aldus_create(xx_io_device *device, int64_t base_address) {
    xx_aldus *archive = (xx_aldus *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_aldus_init(archive, device, base_address);
    return archive;
}

void xx_aldus_destroy(xx_aldus *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_aldus_free(xx_aldus *archive) {
    if (!archive) return;
    xx_aldus_destroy(archive);
    xx_mem_free(archive);
}

bool xx_aldus_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    aldus_stream *stream;
    (void)pd;
    if (!parse(format, &stream)) return false;
    stream_free(stream);
    return true;
}

bool xx_aldus_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    aldus_stream *stream;
    xx_aldus *archive;
    (void)pd;
    if (!format || !parse(format, &stream)) return false;
    archive = (xx_aldus *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = 1U;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    stream_free(stream);
    return true;
}

int64_t xx_aldus_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aldus_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_aldus_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aldus_handle_base_info(format, pd))
               ? ((xx_aldus *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_aldus_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    aldus_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = stream_free;
    state->total_records = 1U;
    if (!copy_options(&state->options, options) ||
        !set_record(&state->current_record, stream, format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_aldus_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_aldus_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    (void)pd;
    if (!format || !state || state->format != format || !state->has_record)
        return false;
    state->current_index = 1U;
    state->has_record = false;
    return false;
}

bool xx_aldus_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    aldus_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool ok = false;
    if (!format || !state || state->format != format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (aldus_stream *)state->internal_state;
    if (!stream || !safe_output_name(stream->name) ||
        !decode_member(format, stream, &plain, &plain_size)) goto done;
    path_option = option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        ok = true;
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
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        ok = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        xx_io_close(destination);
    }
done:
    if (!ok && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}

void xx_aldus_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

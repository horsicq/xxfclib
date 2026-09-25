/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Artisoft ARTIPACK stores LZHUF members consecutively, then places a fixed
 * central directory at EOF.  Each payload begins with its own LE size dword;
 * that prefix is deliberately kept out of the LZH1 bitstream.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/artipack/xx_artipack.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define ARTIPACK_HEADER_SIZE 32U
#define ARTIPACK_RECORD_SIZE 34U
#define ARTIPACK_NAME_SIZE 13U
#define ARTIPACK_SIZE_PREFIX 4U
#define ARTIPACK_MAX_MEMBERS 65535U

typedef struct artipack_member_s {
    char *name;
    int64_t record_offset;
    int64_t data_offset;
    int64_t stream_offset;
    uint32_t compressed_size;
    uint32_t original_size;
    uint16_t dos_date;
    uint16_t dos_time;
} artipack_member;

typedef struct artipack_stream_s {
    artipack_member *items;
    size_t count;
    size_t index;
    int64_t directory_offset;
    int64_t archive_size;
} artipack_stream;

static uint16_t artipack_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t artipack_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool artipack_read_at(xx_io_device *device, int64_t offset,
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

static char *artipack_name(const uint8_t *bytes) {
    char *result;
    size_t index, length = ARTIPACK_NAME_SIZE;
    if (!bytes) return NULL;
    for (index = 0U; index < ARTIPACK_NAME_SIZE; ++index) {
        if (bytes[index] == 0U) {
            length = index;
            break;
        }
    }
    if (length == 0U) return NULL;
    for (index = 0U; index < length; ++index) {
        if (bytes[index] < 0x21U || bytes[index] > 0x7eU ||
            bytes[index] == '/' || bytes[index] == '\\' || bytes[index] == ':')
            return NULL;
    }
    for (index = length; index < ARTIPACK_NAME_SIZE; ++index) {
        if (bytes[index] != 0U) return NULL;
    }
    if ((length == 1U && bytes[0] == '.') ||
        (length == 2U && bytes[0] == '.' && bytes[1] == '.'))
        return NULL;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, bytes, length);
    result[length] = 0;
    return result;
}

static bool artipack_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    for (at = name; *at; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value < 0x20U || value == ':' || value == '<' || value == '>' ||
            value == '"' || value == '|' || value == '?' || value == '*' ||
            value == '/' || value == '\\')
            return false;
    }
    return xx_rt_strcmp(name, ".") != 0 && xx_rt_strcmp(name, "..") != 0;
}

static void artipack_stream_free(void *opaque) {
    artipack_stream *stream = (artipack_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool artipack_add_member(artipack_stream *stream,
                                const artipack_member *member) {
    artipack_member *grown;
    if (!stream || !member || stream->count >= ARTIPACK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (artipack_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool artipack_parse(Abstractformat *format, artipack_stream **result,
                           xx_pd_struct *pd) {
    uint8_t header[ARTIPACK_HEADER_SIZE];
    artipack_stream *stream = NULL;
    int64_t total, size, directory_offset, directory_size, expected;
    uint16_t count;
    uint16_t index;
    bool valid = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(ARTIPACK_HEADER_SIZE + ARTIPACK_RECORD_SIZE +
                         ARTIPACK_SIZE_PREFIX) ||
        !artipack_read_at(format->device, format->base_address, header,
                          sizeof(header)) ||
        xx_rt_memcmp(header, "ARTIPACK", 8U) != 0 || artipack_le16(header + 8U) != 0x0100U)
        return false;
    count = artipack_le16(header + 10U);
    directory_offset = (int64_t)artipack_le32(header + 12U);
    directory_size = (int64_t)count * ARTIPACK_RECORD_SIZE;
    if (count == 0U || directory_offset < (int64_t)ARTIPACK_HEADER_SIZE ||
        directory_size > size || directory_offset > size - directory_size ||
        directory_offset + directory_size != size)
        return false;
    stream = (artipack_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    expected = ARTIPACK_HEADER_SIZE;
    for (index = 0U; index < count; ++index) {
        uint8_t record[ARTIPACK_RECORD_SIZE];
        uint8_t inner_size[ARTIPACK_SIZE_PREFIX];
        artipack_member member;
        int64_t record_offset = directory_offset +
                                (int64_t)index * ARTIPACK_RECORD_SIZE;
        if (pd && xx_pd_is_stopped(pd) ||
            !artipack_read_at(format->device, format->base_address + record_offset,
                               record, sizeof(record)))
            goto done;
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = artipack_name(record);
        member.data_offset = (int64_t)artipack_le32(record + 14U);
        member.original_size = artipack_le32(record + 18U);
        member.compressed_size = artipack_le32(record + 22U);
        member.dos_date = artipack_le16(record + 26U);
        member.dos_time = artipack_le16(record + 28U);
        if (!member.name || member.data_offset != expected ||
            member.compressed_size < ARTIPACK_SIZE_PREFIX ||
            member.data_offset > directory_offset ||
            (uint64_t)member.compressed_size >
                (uint64_t)(directory_offset - member.data_offset)) {
            if (member.name) xx_mem_free(member.name);
            goto done;
        }
        if (!artipack_read_at(format->device,
                              format->base_address + member.data_offset,
                              inner_size, sizeof(inner_size)) ||
            artipack_le32(inner_size) != member.original_size) {
            xx_mem_free(member.name);
            goto done;
        }
        member.record_offset = format->base_address + record_offset;
        member.data_offset += format->base_address;
        member.stream_offset = member.data_offset + ARTIPACK_SIZE_PREFIX;
        expected += member.compressed_size;
        if (!artipack_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto done;
        }
    }
    if (expected != directory_offset) goto done;
    stream->directory_offset = format->base_address + directory_offset;
    stream->archive_size = size;
    valid = true;
done:
    if (!valid) {
        artipack_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool artipack_copy_options(xx_list_s *destination,
                                  const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *artipack_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool artipack_set_record(xx_archive_record *record,
                                const artipack_member *member) {
    uint32_t stream_size = member->compressed_size - ARTIPACK_SIZE_PREFIX;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = ARTIPACK_RECORD_SIZE;
    record->data_offset = member->stream_offset;
    record->compressed_size = stream_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool artipack_decode_member(Abstractformat *format,
                                   const artipack_member *member,
                                   uint8_t **plain, size_t *plain_size) {
    uint32_t stream_size;
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded;
    if (!format || !member || !plain || !plain_size) return false;
    stream_size = member->compressed_size - ARTIPACK_SIZE_PREFIX;
    packed = (uint8_t *)xx_mem_alloc(stream_size != 0U ? stream_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U ?
                                         member->original_size : 1U);
    if (!packed || !output ||
        (stream_size != 0U && !artipack_read_at(format->device,
                                                member->stream_offset, packed,
                                                stream_size)))
        goto done;
    if (member->original_size == 0U) {
        decoded = stream_size == 0U;
    } else {
        decoded = xx_lzh1_decode_memory(packed, stream_size, output,
                                        member->original_size, &written);
    }
    if (!decoded || written != member->original_size) goto done;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_artipack_init(xx_artipack *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARTIPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-artipack");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_artipack_check_is_valid;
    archive->format.handle_base_info = xx_artipack_handle_base_info;
    archive->format.get_format_size = xx_artipack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_artipack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_artipack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_artipack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_artipack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_artipack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_artipack_free_archive_records_reading;
}

xx_artipack *xx_artipack_create(xx_io_device *device, int64_t base_address) {
    xx_artipack *archive = (xx_artipack *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_artipack_init(archive, device, base_address);
    return archive;
}

void xx_artipack_destroy(xx_artipack *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_artipack_free(xx_artipack *archive) {
    if (!archive) return;
    xx_artipack_destroy(archive);
    xx_mem_free(archive);
}

bool xx_artipack_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    artipack_stream *stream;
    if (!artipack_parse(format, &stream, pd)) return false;
    artipack_stream_free(stream);
    return true;
}

bool xx_artipack_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    artipack_stream *stream;
    xx_artipack *archive;
    if (!format || !artipack_parse(format, &stream, pd)) return false;
    archive = (xx_artipack *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    artipack_stream_free(stream);
    return true;
}

int64_t xx_artipack_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_artipack_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_artipack_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_artipack_handle_base_info(format, pd))
               ? ((xx_artipack *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_artipack_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    artipack_stream *stream;
    xx_archive_record_state *state;
    if (!artipack_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        artipack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = artipack_stream_free;
    state->total_records = stream->count;
    if (!artipack_copy_options(&state->options, options) ||
        !artipack_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_artipack_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_artipack_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    artipack_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (artipack_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = artipack_set_record(&state->current_record,
                                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_artipack_unpack_current_archive_record(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    artipack_stream *stream;
    artipack_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (artipack_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!artipack_safe_output_name(member->name) ||
        !artipack_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = artipack_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
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
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_artipack_free_archive_records_reading(Abstractformat *format,
                                              xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

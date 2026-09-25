/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for Amiga-Magazin AMPK v2--v4 archives.  AMPK's record walk is more
 * authoritative than several deliberately unreliable size hints, notably the
 * packed-size field on stored members.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ampk/xx_ampk.h"

#include "xxfclib/algo/ampk/xx_ampk.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define AMPK_HEADER_SIZE 20U
#define AMPK_MIN_SIZE 40U
#define AMPK_PREFIX_SIZE 6U
#define AMPK_TRAILER_SIZE 18U
#define AMPK_MAX_DEPTH 64U
#define AMPK_MAX_MEMBERS 65535U
#define AMPK_RECORD_END 0U
#define AMPK_RECORD_DIRECTORY 1U
#define AMPK_RECORD_FILE 2U

typedef struct ampk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    uint32_t original_size;
    uint32_t declared_packed_size;
    uint8_t method;
    uint8_t attributes;
} ampk_member;

typedef struct ampk_stream_s {
    ampk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint8_t version;
    bool complete;
} ampk_stream;

static uint16_t ampk_be16(const uint8_t *bytes) {
    return ((uint16_t)bytes[0] << 8U) | bytes[1];
}

static uint32_t ampk_be32(const uint8_t *bytes) {
    return ((uint32_t)ampk_be16(bytes) << 16U) | ampk_be16(bytes + 2U);
}

static bool ampk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ampk_valid_component(const uint8_t *name, size_t size) {
    size_t index;
    if (!name || size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t value = name[index];
        if (value < 0x20U || value == 0x7fU || value == '/' || value == '\\')
            return false;
    }
    return true;
}

static char *ampk_component(const uint8_t *bytes, size_t size) {
    char *result;
    size_t index, length = size;
    if (!ampk_valid_component(bytes, size)) return NULL;
    result = (char *)xx_mem_alloc(size + 2U);
    if (!result) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t value = bytes[index];
        result[index] = (value == '"' || value == '*' || value == ':' ||
                         value == '<' || value == '>' || value == '?' ||
                         value == '|') ? '_' : (char)value;
    }
    while (length != 0U && (result[length - 1U] == ' ' ||
                             result[length - 1U] == '.')) --length;
    if (length == 0U) result[length++] = '_';
    result[length] = 0;
    return result;
}

static char *ampk_join_path(char *const *directories, size_t depth,
                            const char *component) {
    size_t index, length = 0U, at = 0U;
    char *result;
    if (!component || !component[0]) return NULL;
    for (index = 0U; index < depth; ++index) {
        size_t part;
        if (!directories[index] ||
            (part = xx_str_len(directories[index])) > SIZE_MAX - length - 1U)
            return NULL;
        length += part + 1U;
    }
    if (xx_str_len(component) > SIZE_MAX - length - 1U) return NULL;
    length += xx_str_len(component);
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < depth; ++index) {
        size_t part = xx_str_len(directories[index]);
        xx_rt_memcpy(result + at, directories[index], part);
        at += part;
        result[at++] = '/';
    }
    xx_rt_memcpy(result + at, component, xx_str_len(component));
    result[length] = 0;
    return result;
}

static bool ampk_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value == ':' || value == '<' || value == '>' || value == '"' ||
            value == '|' || value == '?' || value == '*' ||
            (value != 0U && value < 0x20U)) return false;
        if (value == '/' || value == '\\' || value == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (value == 0U) return true;
            segment = at + 1;
        }
    }
}

static void ampk_stream_free(void *opaque) {
    ampk_stream *stream = (ampk_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool ampk_add_member(ampk_stream *stream, const ampk_member *member) {
    ampk_member *grown;
    if (!stream || !member || stream->count >= AMPK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (ampk_member *)xx_mem_realloc(stream->items,
                                          (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool ampk_parse(Abstractformat *format, ampk_stream **result) {
    uint8_t header[AMPK_HEADER_SIZE];
    char *directories[AMPK_MAX_DEPTH] = { NULL };
    ampk_stream *stream = NULL;
    int64_t total, size, cursor;
    uint16_t declared_directories, declared_files;
    uint32_t declared_original, declared_data;
    uint64_t total_original = 0U, total_data = 0U;
    size_t depth = 0U, directory_count = 0U;
    bool stop_walk = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)AMPK_MIN_SIZE ||
        !ampk_read_at(format->device, format->base_address, header,
                      sizeof(header)) || xx_rt_memcmp(header, "AMPK", 4U) != 0 ||
        header[4] < 2U || header[4] > 4U || header[5] != 0U)
        return false;
    declared_directories = ampk_be16(header + 6U);
    declared_files = ampk_be16(header + 8U);
    declared_original = ampk_be32(header + 10U);
    declared_data = ampk_be32(header + 14U);
    if (declared_files == 0U || declared_original == 0U || declared_data == 0U ||
        declared_data > (uint64_t)(size - (int64_t)AMPK_HEADER_SIZE) ||
        size - (int64_t)declared_data <
            (int64_t)declared_files * (int64_t)(AMPK_PREFIX_SIZE + 1U +
                                                 AMPK_TRAILER_SIZE) ||
        declared_original < declared_data / 4U)
        return false;
    stream = (ampk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->version = header[4];
    cursor = AMPK_HEADER_SIZE;
    while (cursor < size && !stop_walk) {
        uint8_t prefix[AMPK_PREFIX_SIZE];
        uint8_t name_size;
        if (size - cursor < (int64_t)sizeof(prefix) ||
            !ampk_read_at(format->device, format->base_address + cursor,
                          prefix, sizeof(prefix))) break;
        name_size = prefix[1];
        if (prefix[0] == AMPK_RECORD_END) {
            if (xx_rt_memcmp(prefix, "\0\0\0\0\0\0", sizeof(prefix)) != 0 ||
                depth == 0U) break;
            xx_mem_free(directories[--depth]);
            directories[depth] = NULL;
            cursor += (int64_t)sizeof(prefix);
        } else if (prefix[0] == AMPK_RECORD_DIRECTORY) {
            uint8_t raw_name[255];
            char *component;
            if (name_size == 0U || depth >= AMPK_MAX_DEPTH ||
                directory_count >= AMPK_MAX_MEMBERS ||
                size - cursor < (int64_t)AMPK_PREFIX_SIZE + name_size ||
                !ampk_read_at(format->device,
                              format->base_address + cursor + AMPK_PREFIX_SIZE,
                              raw_name, name_size)) break;
            component = ampk_component(raw_name, name_size);
            if (!component) break;
            directories[depth++] = component;
            ++directory_count;
            cursor += (int64_t)AMPK_PREFIX_SIZE + name_size;
        } else if (prefix[0] == AMPK_RECORD_FILE) {
            uint8_t record[AMPK_PREFIX_SIZE + 255U + AMPK_TRAILER_SIZE];
            size_t header_size;
            const uint8_t *trailer;
            char *component = NULL;
            ampk_member member;
            uint64_t data_size;
            if (name_size == 0U || ampk_be32(prefix + 2U) != 0U ||
                size - cursor < (int64_t)AMPK_PREFIX_SIZE + name_size +
                                    AMPK_TRAILER_SIZE) break;
            header_size = AMPK_PREFIX_SIZE + name_size + AMPK_TRAILER_SIZE;
            if (!ampk_read_at(format->device, format->base_address + cursor,
                              record, header_size) ||
                !ampk_valid_component(record + AMPK_PREFIX_SIZE, name_size))
                break;
            trailer = record + AMPK_PREFIX_SIZE + name_size;
            xx_mem_zero(&member, sizeof(member));
            member.original_size = ampk_be32(trailer);
            member.declared_packed_size = ampk_be32(trailer + 4U);
            member.method = trailer[8];
            member.attributes = trailer[13];
            if (member.method > 3U) break;
            data_size = member.method == 0U ? member.original_size
                                             : member.declared_packed_size;
            if (data_size > INT64_MAX || member.original_size == 0U &&
                data_size != 0U || data_size > (uint64_t)(size - cursor -
                                                            (int64_t)header_size))
                break;
            component = ampk_component(record + AMPK_PREFIX_SIZE, name_size);
            member.name = ampk_join_path(directories, depth, component);
            xx_mem_free(component);
            if (!member.name) goto fail;
            member.header_offset = format->base_address + cursor;
            member.header_size = (int64_t)header_size;
            member.data_offset = member.header_offset + member.header_size;
            member.data_size = (int64_t)data_size;
            if (!ampk_add_member(stream, &member)) {
                xx_mem_free(member.name);
                goto fail;
            }
            total_original += member.original_size;
            total_data += data_size;
            cursor = member.data_offset - format->base_address +
                     (int64_t)data_size;
        } else {
            stop_walk = true;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = cursor;
    stream->complete = cursor == size && stream->count == declared_files &&
                       directory_count == declared_directories &&
                       total_original == declared_original &&
                       total_data == declared_data;
    for (depth = 0U; depth < AMPK_MAX_DEPTH; ++depth) {
        if (directories[depth]) xx_mem_free(directories[depth]);
    }
    *result = stream;
    return true;
fail:
    for (depth = 0U; depth < AMPK_MAX_DEPTH; ++depth) {
        if (directories[depth]) xx_mem_free(directories[depth]);
    }
    ampk_stream_free(stream);
    return false;
}

static bool ampk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ampk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool ampk_set_record(xx_archive_record *record,
                            const ampk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool ampk_decode_member(Abstractformat *format,
                               const ampk_member *member,
                               uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->data_size < 0 ||
        member->original_size > SIZE_MAX) return false;
    if (member->method == 0U && (uint64_t)member->data_size !=
                                   member->original_size) return false;
    packed = (uint8_t *)xx_mem_alloc(member->data_size != 0
                                         ? (size_t)member->data_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U
                                         ? member->original_size : 1U);
    if (!packed || !output ||
        (member->data_size != 0 &&
         !ampk_read_at(format->device, member->data_offset, packed,
                       (size_t)member->data_size))) goto fail;
    if (member->method == 0U) {
        if (member->original_size != 0U)
            xx_mem_copy(output, packed, member->original_size);
        written = member->original_size;
        decoded = true;
    } else if (member->method == 1U) {
        decoded = xx_ampk_lzari_decode_memory(packed, (size_t)member->data_size,
                                               output, member->original_size,
                                               &written);
    } else if (member->method == 2U) {
        decoded = xx_ampk_lzss_decode_memory(packed, (size_t)member->data_size,
                                              output, member->original_size,
                                              &written);
    } else if (member->method == 3U) {
        decoded = xx_lzh1_decode_memory(packed, (size_t)member->data_size,
                                        output, member->original_size,
                                        &written);
    }
    if (!decoded || written != member->original_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_ampk_init(xx_ampk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_AMPK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ampk");
    xx_format_set_extension(&archive->format, "ampk");
    archive->format.check_is_valid = xx_ampk_check_is_valid;
    archive->format.handle_base_info = xx_ampk_handle_base_info;
    archive->format.get_format_size = xx_ampk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ampk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ampk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ampk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ampk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ampk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ampk_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_ampk *xx_ampk_create(xx_io_device *device, int64_t base_address) {
    xx_ampk *archive = (xx_ampk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ampk_init(archive, device, base_address);
    return archive;
}

void xx_ampk_destroy(xx_ampk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ampk_free(xx_ampk *archive) {
    if (!archive) return;
    xx_ampk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ampk_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ampk_stream *stream;
    (void)pd;
    if (!ampk_parse(format, &stream)) return false;
    ampk_stream_free(stream);
    return true;
}

bool xx_ampk_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ampk_stream *stream;
    xx_ampk *archive;
    (void)pd;
    if (!format || !ampk_parse(format, &stream)) return false;
    archive = (xx_ampk *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->version = stream->version;
    archive->is_complete = stream->complete;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = stream->complete ? -1 : archive->archive_end;
    format->overlay_size = stream->complete ? 0 :
                           xx_io_total_size(format->device) - archive->archive_end;
    format->is_valid = true;
    format->base_info_handled = true;
    ampk_stream_free(stream);
    return true;
}

int64_t xx_ampk_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ampk_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ampk_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ampk_handle_base_info(format, pd))
               ? ((xx_ampk *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ampk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ampk_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!ampk_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ampk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ampk_stream_free;
    state->total_records = stream->count;
    if (!ampk_copy_options(&state->options, options) ||
        !ampk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ampk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ampk_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    ampk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ampk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ampk_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ampk_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    ampk_stream *stream;
    ampk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ampk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!ampk_safe_output_name(member->name) ||
        !ampk_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = ampk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_ampk_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

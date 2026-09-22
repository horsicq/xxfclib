/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Acorn RISC OS ArcFS/Spark archive layout.  Its
 * directory table is a pre-order stream: a zero-status entry closes the most
 * recent directory, while bit 31 of the data offset introduces one.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcfs/xx_arcfs.h"

#include "xxfclib/algo/arcfs/xx_arcfs_lzw.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ARCFS_HEADER_SIZE 96U
#define ARCFS_ENTRY_SIZE 36U
#define ARCFS_MAX_MEMBERS 100000U
#define ARCFS_MAX_DEPTH 64U
#define ARCFS_STATUS_END 0x00U
#define ARCFS_STATUS_DELETED 0x01U
#define ARCFS_METHOD_STORED 0x82U
#define ARCFS_METHOD_PACKED 0x83U
#define ARCFS_METHOD_CRUNCHED 0x88U
#define ARCFS_METHOD_COMPRESSED 0xffU

typedef struct arcfs_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint32_t original_size;
    uint32_t attributes;
    uint8_t method;
    uint8_t max_bits;
    bool folder;
} arcfs_member;

typedef struct arcfs_stream_s {
    arcfs_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} arcfs_stream;

static uint32_t arcfs_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool arcfs_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool arcfs_known_method(uint8_t method) {
    return method == ARCFS_METHOD_STORED || method == ARCFS_METHOD_PACKED ||
           method == ARCFS_METHOD_CRUNCHED || method == ARCFS_METHOD_COMPRESSED;
}

static char *arcfs_component(const uint8_t *raw, size_t size) {
    char *result;
    size_t index, length = 0U;
    if (!raw || size == 0U) return NULL;
    while (length < size && raw[length] != 0U) ++length;
    if (length == 0U) return NULL;
    result = (char *)xx_mem_alloc(length + 2U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];
        /* ArcFS names are byte strings.  Preserve portable printable ASCII
         * and make everything else a safe UTF-8 path component. */
        result[index] = (value < 0x20U || value >= 0x7fU || value == '/' ||
                         value == '\\' || value == ':' || value == '<' ||
                         value == '>' || value == '"' || value == '|' ||
                         value == '?' || value == '*') ? '_' : (char)value;
    }
    while (length != 0U && (result[length - 1U] == ' ' ||
                             result[length - 1U] == '.')) --length;
    if ((length == 1U && result[0] == '.') ||
        (length == 2U && result[0] == '.' && result[1] == '.'))
        length = 0U;
    if (length == 0U) result[length++] = '_';
    result[length] = 0;
    return result;
}

static char *arcfs_join_path(char *const *directories, size_t depth,
                             const char *component) {
    char *result;
    size_t index, length = 0U, at = 0U;
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

static bool arcfs_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value == ':' || value == '<' || value == '>' || value == '"' ||
            value == '|' || value == '?' || value == '*' ||
            (value != 0U && value < 0x20U))
            return false;
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

static void arcfs_stream_free(void *opaque) {
    arcfs_stream *stream = (arcfs_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool arcfs_add_member(arcfs_stream *stream, const arcfs_member *member) {
    arcfs_member *grown;
    if (!stream || !member || stream->count >= ARCFS_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (arcfs_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool arcfs_parse(Abstractformat *format, arcfs_stream **result) {
    uint8_t header[ARCFS_HEADER_SIZE];
    char *directories[ARCFS_MAX_DEPTH] = { NULL };
    arcfs_stream *stream = NULL;
    int64_t total, size, directory_size, data_base, archive_size;
    uint64_t entry_count, entry_index;
    size_t depth = 0U;
    bool valid = false;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)ARCFS_HEADER_SIZE ||
        !arcfs_read_at(format->device, format->base_address, header,
                       sizeof(header)) ||
        xx_rt_memcmp(header, "Archive\0", 8U) != 0)
        return false;
    directory_size = (int64_t)arcfs_le32(header + 8U);
    data_base = (int64_t)arcfs_le32(header + 12U);
    if (directory_size <= 0 || directory_size % ARCFS_ENTRY_SIZE != 0 ||
        data_base < (int64_t)ARCFS_HEADER_SIZE || data_base > size ||
        directory_size > size - (int64_t)ARCFS_HEADER_SIZE)
        return false;
    entry_count = (uint64_t)(directory_size / ARCFS_ENTRY_SIZE);
    if (entry_count > ARCFS_MAX_MEMBERS) return false;
    stream = (arcfs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    archive_size = data_base;
    if ((int64_t)ARCFS_HEADER_SIZE + directory_size > archive_size)
        archive_size = (int64_t)ARCFS_HEADER_SIZE + directory_size;

    for (entry_index = 0U; entry_index < entry_count; ++entry_index) {
        uint8_t entry[ARCFS_ENTRY_SIZE];
        uint8_t status;
        uint32_t raw_offset;
        char *component = NULL;
        arcfs_member member;
        int64_t entry_offset = (int64_t)ARCFS_HEADER_SIZE +
                               (int64_t)entry_index * ARCFS_ENTRY_SIZE;
        if (!arcfs_read_at(format->device, format->base_address + entry_offset,
                           entry, sizeof(entry)))
            goto done;
        status = entry[0];
        if (status == ARCFS_STATUS_DELETED) continue;
        if (status == ARCFS_STATUS_END) {
            if (depth == 0U) break;
            xx_mem_free(directories[--depth]);
            directories[depth] = NULL;
            continue;
        }
        component = arcfs_component(entry + 1U, 11U);
        if (!component) goto done;
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = arcfs_join_path(directories, depth, component);
        xx_mem_free(component);
        component = NULL;
        if (!member.name) goto done;
        member.header_offset = format->base_address + entry_offset;
        member.method = status;
        member.original_size = arcfs_le32(entry + 12U);
        member.attributes = arcfs_le32(entry + 24U);
        member.max_bits = (uint8_t)((member.attributes >> 8U) & 0xffU);
        raw_offset = arcfs_le32(entry + 32U);
        if (raw_offset & UINT32_C(0x80000000)) {
            member.folder = true;
            member.data_offset = -1;
            if (!arcfs_add_member(stream, &member)) {
                xx_mem_free(member.name);
                goto done;
            }
            if (depth >= ARCFS_MAX_DEPTH) goto done;
            directories[depth++] = arcfs_component(entry + 1U, 11U);
            if (!directories[depth - 1U]) goto done;
            continue;
        }
        {
            uint64_t relative = (uint64_t)data_base +
                                (uint64_t)(raw_offset & UINT32_C(0x7fffffff));
            uint64_t declared = arcfs_le32(entry + 28U);
            uint64_t available;
            if (relative > (uint64_t)size || relative > INT64_MAX) {
                xx_mem_free(member.name);
                goto done;
            }
            available = (uint64_t)size - relative;
            if (declared > available) declared = available;
            member.data_offset = format->base_address + (int64_t)relative;
            member.packed_size = (int64_t)declared;
            if ((int64_t)relative + member.packed_size > archive_size)
                archive_size = (int64_t)relative + member.packed_size;
        }
        if (!arcfs_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto done;
        }
    }
    if (stream->count == 0U) goto done;
    stream->archive_size = archive_size;
    valid = true;
done:
    while (depth != 0U) {
        xx_mem_free(directories[--depth]);
        directories[depth] = NULL;
    }
    if (!valid) {
        arcfs_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool arcfs_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *arcfs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool arcfs_set_record(xx_archive_record *record,
                             const arcfs_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = ARCFS_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->folder ? 0 : member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->folder ? 0U :
                                                           (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->folder ? 0U : member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool arcfs_decode_member(Abstractformat *format,
                                const arcfs_member *member, uint8_t **plain,
                                size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->folder ||
        member->packed_size < 0)
        return false;
    if (!arcfs_known_method(member->method) ||
        member->original_size > SIZE_MAX ||
        (uint64_t)member->packed_size > SIZE_MAX)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0 ?
                                         (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U ?
                                         member->original_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !arcfs_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size)))
        goto done;
    if (member->method == ARCFS_METHOD_STORED) {
        if ((uint64_t)member->packed_size != member->original_size) goto done;
        if (member->original_size != 0U)
            xx_rt_memcpy(output, packed, member->original_size);
        written = member->original_size;
        decoded = true;
    } else if (member->method == ARCFS_METHOD_PACKED) {
        decoded = xx_arcfs_rle90_decode_memory(packed, (size_t)member->packed_size,
                                               output, member->original_size,
                                               &written);
    } else {
        decoded = xx_arcfs_lzw_decode_memory(
            packed, (size_t)member->packed_size, output, member->original_size,
            member->max_bits, member->method == ARCFS_METHOD_CRUNCHED,
            &written);
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

void xx_arcfs_init(xx_arcfs *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARCFS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arcfs");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_arcfs_check_is_valid;
    archive->format.handle_base_info = xx_arcfs_handle_base_info;
    archive->format.get_format_size = xx_arcfs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arcfs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arcfs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_arcfs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arcfs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arcfs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arcfs_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arcfs *xx_arcfs_create(xx_io_device *device, int64_t base_address) {
    xx_arcfs *archive = (xx_arcfs *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arcfs_init(archive, device, base_address);
    return archive;
}

void xx_arcfs_destroy(xx_arcfs *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arcfs_free(xx_arcfs *archive) {
    if (!archive) return;
    xx_arcfs_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arcfs_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    arcfs_stream *stream;
    (void)pd;
    if (!arcfs_parse(format, &stream)) return false;
    arcfs_stream_free(stream);
    return true;
}

bool xx_arcfs_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    arcfs_stream *stream;
    xx_arcfs *archive;
    int64_t total;
    (void)pd;
    if (!format || !arcfs_parse(format, &stream)) return false;
    archive = (xx_arcfs *)format;
    total = xx_io_total_size(format->device);
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = archive->archive_end < total ? archive->archive_end : -1;
    format->overlay_size = archive->archive_end < total ?
                               total - archive->archive_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    arcfs_stream_free(stream);
    return true;
}

int64_t xx_arcfs_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcfs_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_arcfs_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcfs_handle_base_info(format, pd))
               ? ((xx_arcfs *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_arcfs_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    arcfs_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!arcfs_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arcfs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = arcfs_stream_free;
    state->total_records = stream->count;
    if (!arcfs_copy_options(&state->options, options) ||
        !arcfs_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_arcfs_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_arcfs_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    arcfs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (arcfs_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = arcfs_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_arcfs_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    arcfs_stream *stream;
    arcfs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (arcfs_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!arcfs_safe_output_name(member->name)) goto done;
    if (member->folder) {
        result = true;
    } else if (!arcfs_decode_member(format, member, &plain, &plain_size)) {
        goto done;
    } else {
        result = true;
    }
    path_option = arcfs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) goto done;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) {
        result = false;
        goto done;
    }
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) {
        result = false;
        goto done;
    }
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) {
        result = false;
        goto done;
    }
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) {
            result = false;
            goto done;
        }
        written = 0U;
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
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arcfs_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

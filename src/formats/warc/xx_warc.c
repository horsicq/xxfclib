/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native WARC 1.0/1.1 reader.  WARC is a sequence of RFC-style records; only
 * resource/response records with a safely mappable target URI become archive
 * entries, while metadata records are still parsed to keep framing strict.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/warc/xx_warc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define WARC_MAX_HEADER_SIZE (1024U * 1024U)
#define WARC_MAX_RECORDS 100000U
#define WARC_COPY_BUFFER_SIZE 65536U

typedef struct warc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint64_t data_size;
    uint8_t type;
} warc_member;

typedef struct warc_stream_s {
    warc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} warc_stream;

static bool warc_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static uint8_t warc_lower(uint8_t value) {
    return value >= 'A' && value <= 'Z' ? (uint8_t)(value + ('a' - 'A')) :
                                          value;
}

static bool warc_eq_ci(const uint8_t *value, size_t size, const char *text) {
    size_t index;
    if (!value || !text || xx_rt_strlen(text) != size) return false;
    for (index = 0U; index < size; ++index) {
        if (warc_lower(value[index]) != warc_lower((uint8_t)text[index]))
            return false;
    }
    return true;
}

static void warc_trim(const uint8_t **value, size_t *size) {
    if (!value || !size || !*value) return;
    while (*size != 0U && (**value == ' ' || **value == '\t')) {
        ++*value;
        --*size;
    }
    while (*size != 0U && ((*value)[*size - 1U] == ' ' ||
                            (*value)[*size - 1U] == '\t'))
        --*size;
}

static bool warc_token(const uint8_t *value, size_t size) {
    size_t index;
    if (!value || size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = value[index];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || xx_rt_strchr("!#$%&'*+-.^_`|~", c))
            continue;
        return false;
    }
    return true;
}

static bool warc_decimal(const uint8_t *value, size_t size, uint64_t *result) {
    uint64_t number = 0U;
    size_t index;
    if (!value || !result || size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = value[index];
        uint64_t digit;
        if (c < '0' || c > '9') return false;
        digit = (uint64_t)(c - '0');
        if (number > (UINT64_MAX - digit) / 10U) return false;
        number = number * 10U + digit;
    }
    *result = number;
    return true;
}

static bool warc_date_valid(const uint8_t *value, size_t size,
                            bool version_10) {
    size_t index;
    if (!value || size < 4U || size > 64U) return false;
    for (index = 0U; index < size; ++index) {
        if (value[index] < 0x20U || value[index] == 0x7fU) return false;
    }
    for (index = 0U; index < 4U; ++index) {
        if (value[index] < '0' || value[index] > '9') return false;
    }
    if (!version_10) return true;
    return size == 20U && value[4U] == '-' && value[7U] == '-' &&
           value[10U] == 'T' && value[13U] == ':' && value[16U] == ':' &&
           value[19U] == 'Z' && value[5U] >= '0' && value[5U] <= '9' &&
           value[6U] >= '0' && value[6U] <= '9' && value[8U] >= '0' &&
           value[8U] <= '9' && value[9U] >= '0' && value[9U] <= '9' &&
           value[11U] >= '0' && value[11U] <= '9' && value[12U] >= '0' &&
           value[12U] <= '9' && value[14U] >= '0' && value[14U] <= '9' &&
           value[15U] >= '0' && value[15U] <= '9' && value[17U] >= '0' &&
           value[17U] <= '9' && value[18U] >= '0' && value[18U] <= '9';
}

static bool warc_record_id_valid(const uint8_t *value, size_t size) {
    size_t index;
    if (!value || size < 4U || value[0] != '<' || value[size - 1U] != '>')
        return false;
    for (index = 1U; index + 1U < size; ++index) {
        if (value[index] <= 0x20U || value[index] == 0x7fU) return false;
    }
    return true;
}

static bool warc_safe_uri_name(const uint8_t *uri, size_t uri_size,
                               char **result) {
    size_t colon = 0U;
    size_t path_offset;
    size_t path_size;
    char *name;
    size_t index;
    size_t component_start;
    if (!uri || !result || uri_size < 4U) return false;
    while (colon < uri_size && uri[colon] != ':') {
        uint8_t c = uri[colon];
        if ((colon == 0U && !((c >= 'A' && c <= 'Z') ||
                              (c >= 'a' && c <= 'z'))) ||
            (colon != 0U && !((c >= 'A' && c <= 'Z') ||
                               (c >= 'a' && c <= 'z') ||
                               (c >= '0' && c <= '9') || c == '+' ||
                               c == '-' || c == '.')))
            return false;
        ++colon;
    }
    if (colon == 0U || colon + 3U > uri_size || uri[colon] != ':' ||
        uri[colon + 1U] != '/' || uri[colon + 2U] != '/')
        return false;
    if (warc_eq_ci(uri, colon, "file")) {
        path_offset = colon + 3U;
    } else if (warc_eq_ci(uri, colon, "http") ||
               warc_eq_ci(uri, colon, "https") ||
               warc_eq_ci(uri, colon, "ftp")) {
        size_t slash = colon + 3U;
        while (slash < uri_size && uri[slash] != '/') ++slash;
        if (slash == uri_size) return false;
        path_offset = slash + 1U;
    } else {
        return false;
    }
    if (path_offset >= uri_size || uri[path_offset] == '/' ||
        uri[uri_size - 1U] == '/')
        return false;
    path_size = uri_size - path_offset;
    name = (char *)xx_mem_alloc(path_size + 1U);
    if (!name) return false;
    component_start = 0U;
    for (index = 0U; index < path_size; ++index) {
        uint8_t c = uri[path_offset + index];
        if (c < 0x20U || c == 0x7fU || c == '\\') {
            xx_mem_free(name);
            return false;
        }
        if (c == '/') {
            size_t component_size = index - component_start;
            if (component_size == 0U ||
                (component_size == 1U && name[component_start] == '.') ||
                (component_size == 2U && name[component_start] == '.' &&
                 name[component_start + 1U] == '.')) {
                xx_mem_free(name);
                return false;
            }
            name[index] = '/';
            component_start = index + 1U;
        } else {
            name[index] = (c >= 0x7fU || c == ':' || c == '<' || c == '>' ||
                           c == '"' || c == '|' || c == '?' || c == '*')
                              ? '_'
                              : (char)c;
        }
    }
    if (component_start >= path_size ||
        (path_size - component_start == 1U && name[component_start] == '.') ||
        (path_size - component_start == 2U && name[component_start] == '.' &&
         name[component_start + 1U] == '.')) {
        xx_mem_free(name);
        return false;
    }
    name[path_size] = 0;
    *result = name;
    return true;
}

static bool warc_safe_output_name(const char *name) {
    const char *at;
    const char *component;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    component = name;
    for (at = name;; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value == 0U || value == '/') {
            size_t length = (size_t)(at - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.'))
                return false;
            if (value == 0U) return true;
            component = at + 1;
        } else if (value < 0x20U || value == ':' || value == '<' ||
                   value == '>' || value == '"' || value == '|' ||
                   value == '?' || value == '*' || value == '\\') {
            return false;
        }
    }
}

static void warc_stream_free(void *opaque) {
    warc_stream *stream = (warc_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool warc_add_member(warc_stream *stream, const warc_member *member) {
    warc_member *grown;
    size_t index;
    if (!stream || !member || !member->name || stream->count >= WARC_MAX_RECORDS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    for (index = 0U; index < stream->count; ++index) {
        if (xx_rt_strcmp(stream->items[index].name, member->name) == 0) return false;
    }
    grown = (warc_member *)xx_mem_realloc(stream->items,
                                           (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool warc_read_header(Abstractformat *format, int64_t relative_offset,
                             uint8_t **header, size_t *header_size) {
    int64_t total;
    int64_t size;
    size_t limit;
    uint8_t *buffer;
    size_t index;
    if (!format || !format->device || !header || !header_size ||
        relative_offset < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (relative_offset > size - 4) return false;
    limit = (size_t)(size - relative_offset);
    if (limit > WARC_MAX_HEADER_SIZE) limit = WARC_MAX_HEADER_SIZE;
    buffer = (uint8_t *)xx_mem_alloc(limit);
    if (!buffer || !warc_read_at(format->device,
                                 format->base_address + relative_offset,
                                 buffer, limit)) {
        if (buffer) xx_mem_free(buffer);
        return false;
    }
    for (index = 0U; index + 3U < limit; ++index) {
        if (buffer[index] == '\r' && buffer[index + 1U] == '\n' &&
            buffer[index + 2U] == '\r' && buffer[index + 3U] == '\n') {
            *header = buffer;
            *header_size = index + 4U;
            return true;
        }
    }
    xx_mem_free(buffer);
    return false;
}

static bool warc_parse_record(Abstractformat *format, int64_t relative_offset,
                              warc_member *member, bool *visible,
                              int64_t *next_offset, xx_pd_struct *pd) {
    uint8_t *header = NULL;
    size_t header_size = 0U;
    size_t position = 0U;
    bool version_10;
    const uint8_t *type = NULL, *target = NULL, *content_length = NULL;
    const uint8_t *date = NULL, *record_id = NULL;
    size_t type_size = 0U, target_size = 0U, content_length_size = 0U;
    size_t date_size = 0U, record_id_size = 0U;
    uint64_t data_size;
    int64_t total;
    int64_t size;
    int64_t data_relative;
    uint8_t separator[4];
    bool result = false;
    if (!format || !member || !visible || !next_offset ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    xx_rt_memset(member, 0, sizeof(*member));
    *visible = false;
    *next_offset = 0;
    if (!warc_read_header(format, relative_offset, &header, &header_size))
        goto done;
    {
        size_t end = 0U;
        while (end + 1U < header_size &&
               !(header[end] == '\r' && header[end + 1U] == '\n'))
            ++end;
        if (end + 1U >= header_size ||
            !(warc_eq_ci(header, end, "WARC/1.0") ||
              warc_eq_ci(header, end, "WARC/1.1")))
            goto done;
        version_10 = warc_eq_ci(header, end, "WARC/1.0");
        position = end + 2U;
    }
    while (position < header_size) {
        size_t end = position;
        size_t colon = position;
        const uint8_t *value;
        size_t value_size;
        while (end + 1U < header_size &&
               !(header[end] == '\r' && header[end + 1U] == '\n'))
            ++end;
        if (end + 1U >= header_size) goto done;
        if (end == position) {
            position = end + 2U;
            break;
        }
        if (header[position] == ' ' || header[position] == '\t') goto done;
        while (colon < end && header[colon] != ':') ++colon;
        if (colon == position || colon == end ||
            !warc_token(header + position, colon - position))
            goto done;
        value = header + colon + 1U;
        value_size = end - colon - 1U;
        warc_trim(&value, &value_size);
        if (warc_eq_ci(header + position, colon - position, "WARC-Type")) {
            if (type) goto done;
            type = value;
            type_size = value_size;
        } else if (warc_eq_ci(header + position, colon - position,
                              "WARC-Target-URI")) {
            if (target) goto done;
            target = value;
            target_size = value_size;
        } else if (warc_eq_ci(header + position, colon - position,
                              "Content-Length")) {
            if (content_length) goto done;
            content_length = value;
            content_length_size = value_size;
        } else if (warc_eq_ci(header + position, colon - position,
                              "WARC-Date")) {
            if (date) goto done;
            date = value;
            date_size = value_size;
        } else if (warc_eq_ci(header + position, colon - position,
                              "WARC-Record-ID")) {
            if (record_id) goto done;
            record_id = value;
            record_id_size = value_size;
        }
        position = end + 2U;
    }
    if (position != header_size || !type || !content_length || !date ||
        !record_id || !warc_token(type, type_size) ||
        !warc_decimal(content_length, content_length_size, &data_size) ||
        !warc_date_valid(date, date_size, version_10) ||
        !warc_record_id_valid(record_id, record_id_size))
        goto done;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) goto done;
    size = total - format->base_address;
    data_relative = relative_offset + (int64_t)header_size;
    if (data_relative < relative_offset || data_relative > size ||
        data_size > (uint64_t)(size - data_relative) ||
        (uint64_t)(size - data_relative) - data_size < 4U)
        goto done;
    if (!warc_read_at(format->device, format->base_address + data_relative +
                                      (int64_t)data_size,
                      separator, sizeof(separator)) ||
        xx_rt_memcmp(separator, "\r\n\r\n", sizeof(separator)) != 0)
        goto done;
    member->header_offset = format->base_address + relative_offset;
    member->header_size = (int64_t)header_size;
    member->data_offset = format->base_address + data_relative;
    member->data_size = data_size;
    member->type = warc_eq_ci(type, type_size, "response") ? 1U :
                   (warc_eq_ci(type, type_size, "resource") ? 2U : 0U);
    if (member->type != 0U && target &&
        warc_safe_uri_name(target, target_size, &member->name))
        *visible = true;
    *next_offset = data_relative + (int64_t)data_size + 4U;
    result = *next_offset > relative_offset && *next_offset <= size;
done:
    if (!result && member->name) {
        xx_mem_free(member->name);
        member->name = NULL;
    }
    if (header) xx_mem_free(header);
    return result;
}

static bool warc_parse(Abstractformat *format, warc_stream **result,
                       xx_pd_struct *pd) {
    warc_stream *stream = NULL;
    int64_t total;
    int64_t size;
    int64_t offset = 0;
    size_t physical_count = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 12) return false;
    stream = (warc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (offset < size) {
        warc_member member;
        bool visible;
        int64_t next_offset;
        if ((pd && xx_pd_is_stopped(pd)) || physical_count >= WARC_MAX_RECORDS ||
            !warc_parse_record(format, offset, &member, &visible,
                               &next_offset, pd))
            goto fail;
        if (visible) {
            if (!warc_add_member(stream, &member)) {
                if (member.name) xx_mem_free(member.name);
                goto fail;
            }
        } else if (member.name) {
            xx_mem_free(member.name);
        }
        offset = next_offset;
        ++physical_count;
    }
    if (physical_count == 0U || offset != size) goto fail;
    stream->archive_size = offset;
    *result = stream;
    return true;
fail:
    warc_stream_free(stream);
    return false;
}

static bool warc_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *warc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool warc_set_record(xx_archive_record *record,
                            const warc_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_warc_init(xx_warc *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.file_type = XX_FILE_TYPE_WARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/warc");
    xx_format_set_extension(&archive->format, "warc");
    archive->format.check_is_valid = xx_warc_check_is_valid;
    archive->format.handle_base_info = xx_warc_handle_base_info;
    archive->format.get_format_size = xx_warc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_warc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_warc_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_warc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_warc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_warc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_warc_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_warc *xx_warc_create(xx_io_device *device, int64_t base_address) {
    xx_warc *archive = (xx_warc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_warc_init(archive, device, base_address);
    return archive;
}

void xx_warc_destroy(xx_warc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_warc_free(xx_warc *archive) {
    if (!archive) return;
    xx_warc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_warc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    warc_stream *stream = NULL;
    if (!warc_parse(format, &stream, pd)) return false;
    warc_stream_free(stream);
    return true;
}

bool xx_warc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    warc_stream *stream;
    xx_warc *archive;
    int64_t total;
    if (!format || !warc_parse(format, &stream, pd)) return false;
    archive = (xx_warc *)format;
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
    warc_stream_free(stream);
    return true;
}

int64_t xx_warc_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_warc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_warc_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_warc_handle_base_info(format, pd))
               ? ((xx_warc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_warc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    warc_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!warc_parse(format, &stream, pd) || stream->count == 0U) {
        if (stream) warc_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        warc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = warc_stream_free;
    state->total_records = stream->count;
    if (!warc_copy_options(&state->options, options) ||
        !warc_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_warc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_warc_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    warc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (warc_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = warc_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_warc_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    warc_stream *stream;
    warc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t buffer[WARC_COPY_BUFFER_SIZE];
    uint64_t copied = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (warc_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!warc_safe_output_name(member->name)) goto done;
    path_option = warc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
        if (!destination) goto done;
        result = true;
        while (copied < member->data_size) {
            size_t amount = member->data_size - copied > sizeof(buffer)
                                ? sizeof(buffer)
                                : (size_t)(member->data_size - copied);
            size_t done = 0U;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !warc_read_at(format->device, member->data_offset +
                                                  (int64_t)copied,
                              buffer, amount)) {
                result = false;
                break;
            }
            while (done < amount) {
                ssize_t wrote = xx_io_write(destination, buffer + done,
                                            amount - done);
                if (wrote <= 0 || (size_t)wrote > amount - done) {
                    result = false;
                    break;
                }
                done += (size_t)wrote;
            }
            if (!result) break;
            copied += amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_warc_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

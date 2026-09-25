/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * UNIX V7 and 2BSD ar archives use a two-byte 0177545 magic followed by
 * packed binary member records.  The 32-bit fields use PDP middle-endian
 * order, which is neither ordinary little-endian nor big-endian.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pdp11ar/xx_pdp11ar.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define PDP11AR_MAGIC_SIZE 2U
#define PDP11AR_HEADER_SIZE 26U
#define PDP11AR_NAME_SIZE 14U
#define PDP11AR_MAX_MEMBERS 100000U
#define PDP11AR_ZERO_SCAN_CHUNK 65536U
#define PDP11AR_MODE_IFMT 0xf000U
#define PDP11AR_MODE_IFREG 0x8000U

typedef struct pdp11ar_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    uint32_t size;
    uint32_t timestamp;
    uint16_t mode;
    uint8_t uid;
    uint8_t gid;
} pdp11ar_member;

typedef struct pdp11ar_stream_s {
    pdp11ar_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} pdp11ar_stream;

static uint16_t pdp11ar_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t pdp11ar_middle32(const uint8_t *bytes) {
    return ((uint32_t)bytes[1] << 24U) | ((uint32_t)bytes[0] << 16U) |
           ((uint32_t)bytes[3] << 8U) | bytes[2];
}

static bool pdp11ar_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pdp11ar_zero_tail(Abstractformat *format, int64_t offset,
                              int64_t size, xx_pd_struct *pd) {
    uint8_t buffer[PDP11AR_ZERO_SCAN_CHUNK];
    if (!format || offset < 0 || size < 0) return false;
    while (size != 0) {
        size_t portion = size > (int64_t)sizeof(buffer) ? sizeof(buffer) :
                                                          (size_t)size;
        size_t index;
        if (pd && xx_pd_is_stopped(pd) ||
            !pdp11ar_read_at(format->device, offset, buffer, portion))
            return false;
        for (index = 0U; index < portion; ++index) {
            if (buffer[index] != 0U) return false;
        }
        offset += (int64_t)portion;
        size -= (int64_t)portion;
    }
    return true;
}

static char *pdp11ar_name(const uint8_t *bytes) {
    size_t index, length = PDP11AR_NAME_SIZE;
    char *result;
    if (!bytes) return NULL;
    for (index = 0U; index < PDP11AR_NAME_SIZE; ++index) {
        if (bytes[index] == 0U) {
            length = index;
            break;
        }
    }
    if (length == 0U) return NULL;
    for (index = 0U; index < length; ++index) {
        if (bytes[index] < 0x20U || bytes[index] > 0x7eU ||
            bytes[index] == '/' || bytes[index] == '\\')
            return NULL;
    }
    for (index = length; index < PDP11AR_NAME_SIZE; ++index) {
        if (bytes[index] != 0U) return NULL;
    }
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, bytes, length);
    result[length] = 0;
    return result;
}

static bool pdp11ar_safe_output_name(const char *name) {
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

static void pdp11ar_stream_free(void *opaque) {
    pdp11ar_stream *stream = (pdp11ar_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool pdp11ar_add_member(pdp11ar_stream *stream,
                               const pdp11ar_member *member) {
    pdp11ar_member *grown;
    if (!stream || !member || stream->count >= PDP11AR_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (pdp11ar_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool pdp11ar_parse(Abstractformat *format, pdp11ar_stream **result,
                          xx_pd_struct *pd) {
    uint8_t magic[PDP11AR_MAGIC_SIZE];
    pdp11ar_stream *stream = NULL;
    int64_t total, size, offset;
    bool valid = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(PDP11AR_MAGIC_SIZE + PDP11AR_HEADER_SIZE) ||
        !pdp11ar_read_at(format->device, format->base_address, magic,
                         sizeof(magic)) ||
        pdp11ar_le16(magic) != 0xff65U)
        return false;
    stream = (pdp11ar_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    offset = PDP11AR_MAGIC_SIZE;
    while (offset != size) {
        uint8_t header[PDP11AR_HEADER_SIZE];
        pdp11ar_member member;
        int64_t data_offset;
        int64_t stride;
        if (pd && xx_pd_is_stopped(pd) || stream->count >= PDP11AR_MAX_MEMBERS ||
            size - offset < (int64_t)sizeof(header) ||
            !pdp11ar_read_at(format->device, format->base_address + offset,
                             header, sizeof(header)))
            goto done;
        if (header[0] == 0U) {
            if (stream->count == 0U ||
                !pdp11ar_zero_tail(format, format->base_address + offset,
                                    size - offset, pd))
                goto done;
            stream->archive_size = offset;
            valid = true;
            goto done;
        }
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = pdp11ar_name(header);
        member.mode = pdp11ar_le16(header + 20U);
        if (!member.name ||
            (stream->count == 0U &&
             (member.mode & PDP11AR_MODE_IFMT) != PDP11AR_MODE_IFREG) ||
            (stream->count != 0U && member.mode != 0U &&
             (member.mode & PDP11AR_MODE_IFMT) != PDP11AR_MODE_IFREG)) {
            if (member.name) xx_mem_free(member.name);
            goto done;
        }
        member.size = pdp11ar_middle32(header + 22U);
        data_offset = offset + PDP11AR_HEADER_SIZE;
        if ((uint64_t)member.size > (uint64_t)(size - data_offset)) {
            xx_mem_free(member.name);
            goto done;
        }
        member.header_offset = format->base_address + offset;
        member.data_offset = format->base_address + data_offset;
        member.timestamp = pdp11ar_middle32(header + 14U);
        member.uid = header[18U];
        member.gid = header[19U];
        if (!pdp11ar_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto done;
        }
        stride = PDP11AR_HEADER_SIZE + (int64_t)member.size +
                 (int64_t)(member.size & 1U);
        if (stride > size - offset) {
            /* Some recovery images omit only the final odd-byte pad. */
            if ((member.size & 1U) && data_offset + (int64_t)member.size == size) {
                offset = size;
                break;
            }
            goto done;
        }
        offset += stride;
    }
    if (stream->count == 0U) goto done;
    stream->archive_size = size;
    valid = true;
done:
    if (!valid) {
        pdp11ar_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool pdp11ar_copy_options(xx_list_s *destination,
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

static const xx_var *pdp11ar_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool pdp11ar_set_record(xx_archive_record *record,
                               const pdp11ar_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = PDP11AR_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool pdp11ar_verify_member(Abstractformat *format,
                                  const pdp11ar_member *member,
                                  xx_pd_struct *pd) {
    uint8_t buffer[32768];
    int64_t offset;
    uint32_t remaining;
    if (!format || !member) return false;
    offset = member->data_offset;
    remaining = member->size;
    while (remaining != 0U) {
        size_t portion = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        if (pd && xx_pd_is_stopped(pd) ||
            !pdp11ar_read_at(format->device, offset, buffer, portion))
            return false;
        offset += (int64_t)portion;
        remaining -= (uint32_t)portion;
    }
    return true;
}

void xx_pdp11ar_init(xx_pdp11ar *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PDP11AR;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-archive");
    xx_format_set_extension(&archive->format, "a");
    archive->format.check_is_valid = xx_pdp11ar_check_is_valid;
    archive->format.handle_base_info = xx_pdp11ar_handle_base_info;
    archive->format.get_format_size = xx_pdp11ar_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pdp11ar_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pdp11ar_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pdp11ar_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pdp11ar_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pdp11ar_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pdp11ar_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_pdp11ar *xx_pdp11ar_create(xx_io_device *device, int64_t base_address) {
    xx_pdp11ar *archive = (xx_pdp11ar *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pdp11ar_init(archive, device, base_address);
    return archive;
}

void xx_pdp11ar_destroy(xx_pdp11ar *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pdp11ar_free(xx_pdp11ar *archive) {
    if (!archive) return;
    xx_pdp11ar_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pdp11ar_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pdp11ar_stream *stream;
    if (!pdp11ar_parse(format, &stream, pd)) return false;
    pdp11ar_stream_free(stream);
    return true;
}

bool xx_pdp11ar_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pdp11ar_stream *stream;
    xx_pdp11ar *archive;
    int64_t total;
    if (!format || !pdp11ar_parse(format, &stream, pd)) return false;
    archive = (xx_pdp11ar *)format;
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
    pdp11ar_stream_free(stream);
    return true;
}

int64_t xx_pdp11ar_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pdp11ar_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_pdp11ar_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pdp11ar_handle_base_info(format, pd))
               ? ((xx_pdp11ar *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_pdp11ar_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pdp11ar_stream *stream;
    xx_archive_record_state *state;
    if (!pdp11ar_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pdp11ar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pdp11ar_stream_free;
    state->total_records = stream->count;
    if (!pdp11ar_copy_options(&state->options, options) ||
        !pdp11ar_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pdp11ar_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pdp11ar_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    pdp11ar_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pdp11ar_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pdp11ar_set_record(&state->current_record,
                                           &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pdp11ar_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    pdp11ar_stream *stream;
    pdp11ar_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pdp11ar_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!pdp11ar_safe_output_name(member->name) ||
        !pdp11ar_verify_member(format, member, pd))
        goto done;
    path_option = pdp11ar_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    result = xx_store_unpack_device_to_file(format->device, member->data_offset,
                                            member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pdp11ar_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

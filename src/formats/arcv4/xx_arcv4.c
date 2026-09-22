/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Eschalon Setup ARCV 4.00 reader.  FILE and DATA chunks strictly alternate;
 * the DATA size is authoritative because split-volume FILE records put
 * 0xffffffff in their packed-size field.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcv4/xx_arcv4.h"

#include "xxfclib/algo/arcv4/xx_arcv4.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define ARCV4_HEADER_SIZE 0x79cU
#define ARCV4_ZERO_OFFSET 0x08U
#define ARCV4_ZERO_SIZE 0x84U
#define ARCV4_AA_OFFSET 0x9cU
#define ARCV4_AA_SIZE 64U
#define ARCV4_TAIL_ZERO_OFFSET 0x59cU
#define ARCV4_TAIL_ZERO_SIZE 16U
#define ARCV4_FILE_PROLOGUE_SIZE 16U
#define ARCV4_DATA_HEADER_SIZE 32U
#define ARCV4_MAX_BODY_SIZE 65536U
#define ARCV4_MAX_NAME_SIZE 4096U
#define ARCV4_MAX_TAG_SIZE 256U
#define ARCV4_MAX_MEMBERS 100000U

typedef struct arcv4_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t original_size;
    uint32_t crc32;
    uint32_t method;
    uint32_t flags;
    uint64_t filetime;
    bool spanned;
} arcv4_member;

typedef struct arcv4_stream_s {
    arcv4_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint16_t subvariant;
} arcv4_stream;

static uint16_t arcv4_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t arcv4_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint64_t arcv4_le64(const uint8_t *bytes) {
    return (uint64_t)arcv4_le32(bytes) |
           ((uint64_t)arcv4_le32(bytes + 4U) << 32U);
}

static bool arcv4_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool arcv4_filled(const uint8_t *data, size_t offset, size_t size,
                         uint8_t value) {
    size_t index;
    if (!data) return false;
    for (index = 0U; index < size; ++index) {
        if (data[offset + index] != value) return false;
    }
    return true;
}

static char *arcv4_name(const uint8_t *raw, size_t size) {
    char *result;
    size_t index, length = size;
    if (!raw || size == 0U) return NULL;
    result = (char *)xx_mem_alloc(size + 2U);
    if (!result) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t value = raw[index];
        if (value < 0x20U || value == 0x7fU) {
            xx_mem_free(result);
            return NULL;
        }
        /* Names are ANSI byte strings.  Flatten path separators and map
         * nonportable bytes so output remains an unambiguous UTF-8 path. */
        result[index] = (value >= 0x7fU || value == '/' || value == '\\' ||
                         value == ':' || value == '<' || value == '>' ||
                         value == '"' || value == '|' || value == '?' ||
                         value == '*') ? '_' : (char)value;
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

static bool arcv4_safe_output_name(const char *name) {
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

static void arcv4_stream_free(void *opaque) {
    arcv4_stream *stream = (arcv4_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool arcv4_add_member(arcv4_stream *stream,
                             const arcv4_member *member) {
    arcv4_member *grown;
    if (!stream || !member || stream->count >= ARCV4_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (arcv4_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool arcv4_parse(Abstractformat *format, arcv4_stream **result,
                        xx_pd_struct *pd) {
    uint8_t header[ARCV4_HEADER_SIZE];
    arcv4_stream *stream = NULL;
    int64_t total, size, offset;
    uint16_t subvariant;
    bool valid = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(ARCV4_HEADER_SIZE + ARCV4_FILE_PROLOGUE_SIZE) ||
        !arcv4_read_at(format->device, format->base_address, header,
                       sizeof(header)) ||
        xx_rt_memcmp(header, "ARCV", 4U) != 0 || arcv4_le16(header + 4U) != 0x0400U)
        return false;
    subvariant = arcv4_le16(header + 6U);
    if ((subvariant != 1U && subvariant != 5U) ||
        !arcv4_filled(header, ARCV4_ZERO_OFFSET, ARCV4_ZERO_SIZE, 0U) ||
        !arcv4_filled(header, ARCV4_AA_OFFSET, ARCV4_AA_SIZE, 0xaaU) ||
        !arcv4_filled(header, ARCV4_TAIL_ZERO_OFFSET, ARCV4_TAIL_ZERO_SIZE, 0U))
        return false;
    stream = (arcv4_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->subvariant = subvariant;
    offset = ARCV4_HEADER_SIZE;
    for (;;) {
        uint8_t prologue[ARCV4_FILE_PROLOGUE_SIZE];
        uint32_t chunk_type;
        uint32_t header_size;
        uint32_t record_size;
        if (pd && xx_pd_is_stopped(pd) || stream->count >= ARCV4_MAX_MEMBERS ||
            offset > size - (int64_t)sizeof(prologue) ||
            !arcv4_read_at(format->device, format->base_address + offset,
                           prologue, sizeof(prologue)))
            goto done;
        chunk_type = arcv4_le32(prologue + 4U);
        header_size = arcv4_le32(prologue + 8U);
        record_size = arcv4_le32(prologue + 12U);
        if (xx_rt_memcmp(prologue, "EOFM", 4U) == 0) {
            if (stream->count == 0U || (chunk_type & 0xffU) != 3U ||
                header_size != ARCV4_FILE_PROLOGUE_SIZE || record_size != 0U)
                goto done;
            stream->archive_size = offset + ARCV4_FILE_PROLOGUE_SIZE;
            valid = true;
            goto done;
        }
        if (xx_rt_memcmp(prologue, "FILE", 4U) != 0 || (chunk_type & 0xffU) != 1U ||
            header_size != ARCV4_FILE_PROLOGUE_SIZE || record_size < 40U ||
            record_size > ARCV4_MAX_BODY_SIZE ||
            offset > size - (int64_t)ARCV4_FILE_PROLOGUE_SIZE - record_size)
            goto done;
        {
            uint8_t *body = NULL;
            uint32_t tag_size, name_size;
            size_t position;
            const uint8_t *tail;
            uint8_t data_header[ARCV4_DATA_HEADER_SIZE];
            arcv4_member member;
            int64_t body_offset = offset + ARCV4_FILE_PROLOGUE_SIZE;
            int64_t data_header_offset = body_offset + record_size;
            body = (uint8_t *)xx_mem_alloc(record_size);
            if (!body || !arcv4_read_at(format->device,
                                        format->base_address + body_offset,
                                        body, record_size)) {
                if (body) xx_mem_free(body);
                goto done;
            }
            tag_size = arcv4_le32(body);
            if (tag_size > ARCV4_MAX_TAG_SIZE || tag_size > record_size - 4U) {
                xx_mem_free(body);
                goto done;
            }
            position = 4U + tag_size;
            if (position > record_size - 4U) {
                xx_mem_free(body);
                goto done;
            }
            name_size = arcv4_le32(body + position);
            position += 4U;
            if (name_size == 0U || name_size > ARCV4_MAX_NAME_SIZE ||
                name_size > record_size - position ||
                record_size - position - name_size != 32U) {
                xx_mem_free(body);
                goto done;
            }
            xx_rt_memset(&member, 0, sizeof(member));
            member.name = arcv4_name(body + position, name_size);
            position += name_size;
            tail = body + position;
            member.original_size = arcv4_le32(tail);
            member.flags = arcv4_le32(tail + 4U);
            member.filetime = arcv4_le64(tail + 8U);
            member.packed_size = arcv4_le32(tail + 24U);
            member.method = arcv4_le32(tail + 28U);
            if (!member.name || member.flags > 0xffU ||
                (member.method != 0U && member.method != 2U) ||
                !arcv4_read_at(format->device,
                               format->base_address + data_header_offset,
                               data_header, sizeof(data_header))) {
                if (member.name) xx_mem_free(member.name);
                xx_mem_free(body);
                goto done;
            }
            if (xx_rt_memcmp(data_header, "DATA", 4U) != 0 ||
                ((arcv4_le32(data_header + 4U) & 0xffU) != 1U &&
                 (arcv4_le32(data_header + 4U) & 0xffU) != 5U) ||
                arcv4_le32(data_header + 8U) != ARCV4_DATA_HEADER_SIZE ||
                arcv4_le32(data_header + 16U) != 0U ||
                arcv4_le32(data_header + 24U) != 0U ||
                arcv4_le32(data_header + 28U) != 0U) {
                xx_mem_free(member.name);
                xx_mem_free(body);
                goto done;
            }
            member.packed_size = arcv4_le32(data_header + 12U);
            member.crc32 = arcv4_le32(data_header + 20U);
            member.spanned = arcv4_le32(tail + 24U) == UINT32_MAX ||
                             (arcv4_le32(data_header + 4U) & 0xffU) == 1U;
            member.header_offset = format->base_address + offset;
            member.header_size = ARCV4_FILE_PROLOGUE_SIZE + record_size +
                                 ARCV4_DATA_HEADER_SIZE;
            member.data_offset = format->base_address + data_header_offset +
                                 ARCV4_DATA_HEADER_SIZE;
            if ((!member.spanned && arcv4_le32(tail + 24U) != member.packed_size) ||
                (!member.spanned && member.method == 0U &&
                 member.packed_size != member.original_size) ||
                member.data_offset - format->base_address > size ||
                member.packed_size > (uint64_t)(size -
                                                 (member.data_offset - format->base_address)) ||
                !arcv4_add_member(stream, &member)) {
                xx_mem_free(member.name);
                xx_mem_free(body);
                goto done;
            }
            xx_mem_free(body);
            offset = member.data_offset - format->base_address + member.packed_size;
        }
    }
done:
    if (!valid) {
        arcv4_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool arcv4_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *arcv4_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool arcv4_set_record(xx_archive_record *record,
                             const arcv4_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->filetime) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool arcv4_decode_member(Abstractformat *format,
                                const arcv4_member *member, uint8_t **plain,
                                size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->spanned)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0U ?
                                         member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U ?
                                         member->original_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0U &&
         !arcv4_read_at(format->device, member->data_offset, packed,
                        member->packed_size)))
        goto done;
    if (member->method == 0U) {
        if (member->packed_size != member->original_size) goto done;
        if (member->original_size != 0U)
            xx_rt_memcpy(output, packed, member->original_size);
        written = member->original_size;
        decoded = true;
    } else {
        decoded = xx_arcv4_decode_memory(packed, member->packed_size, output,
                                         member->original_size, &written);
    }
    if (!decoded || written != member->original_size ||
        xx_crc32_calc(0U, output, written) != member->crc32)
        goto done;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_arcv4_init(xx_arcv4 *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARCV4;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arcv4");
    xx_format_set_extension(&archive->format, "arv");
    archive->format.check_is_valid = xx_arcv4_check_is_valid;
    archive->format.handle_base_info = xx_arcv4_handle_base_info;
    archive->format.get_format_size = xx_arcv4_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arcv4_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arcv4_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_arcv4_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arcv4_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arcv4_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arcv4_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arcv4 *xx_arcv4_create(xx_io_device *device, int64_t base_address) {
    xx_arcv4 *archive = (xx_arcv4 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arcv4_init(archive, device, base_address);
    return archive;
}

void xx_arcv4_destroy(xx_arcv4 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arcv4_free(xx_arcv4 *archive) {
    if (!archive) return;
    xx_arcv4_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arcv4_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    arcv4_stream *stream;
    if (!arcv4_parse(format, &stream, pd)) return false;
    arcv4_stream_free(stream);
    return true;
}

bool xx_arcv4_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    arcv4_stream *stream;
    xx_arcv4 *archive;
    int64_t total;
    if (!format || !arcv4_parse(format, &stream, pd)) return false;
    archive = (xx_arcv4 *)format;
    total = xx_io_total_size(format->device);
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->subvariant = stream->subvariant;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = archive->archive_end < total ? archive->archive_end : -1;
    format->overlay_size = archive->archive_end < total ?
                               total - archive->archive_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    arcv4_stream_free(stream);
    return true;
}

int64_t xx_arcv4_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcv4_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_arcv4_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcv4_handle_base_info(format, pd))
               ? ((xx_arcv4 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_arcv4_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    arcv4_stream *stream;
    xx_archive_record_state *state;
    if (!arcv4_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arcv4_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = arcv4_stream_free;
    state->total_records = stream->count;
    if (!arcv4_copy_options(&state->options, options) ||
        !arcv4_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_arcv4_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_arcv4_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    arcv4_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (arcv4_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = arcv4_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_arcv4_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    arcv4_stream *stream;
    arcv4_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (arcv4_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!arcv4_safe_output_name(member->name) ||
        !arcv4_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = arcv4_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arcv4_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

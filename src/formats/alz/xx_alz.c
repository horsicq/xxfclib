/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the ALZip ALZ container.  The container's method values
 * are 0 (stored), 1 (BZip2), and 2 (raw Deflate).  Encryption is described
 * by the format but deliberately remains fail-closed until its cipher format
 * is implemented.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/alz/xx_alz.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ALZ_HEADER_SIZE 12U
#define ALZ_ENTRY_FIXED_SIZE 9U
#define ALZ_MAX_MEMBERS 1048576U
#define ALZ_MAGIC_ENTRY UINT32_C(0x015a4c42)
#define ALZ_MAGIC_CONTROL_1 UINT32_C(0x015a4c43)
#define ALZ_MAGIC_CONTROL_2 UINT32_C(0x025a4c43)

typedef struct alz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t dos_time;
    uint8_t method;
    uint8_t attributes;
    uint8_t descriptor;
    bool encrypted;
    bool folder;
} alz_member;

typedef struct alz_stream_s {
    alz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} alz_stream;

static uint16_t alz_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t alz_le32(const uint8_t *bytes) {
    return (uint32_t)alz_le16(bytes) |
           ((uint32_t)alz_le16(bytes + 2U) << 16U);
}

static uint64_t alz_le(const uint8_t *bytes, unsigned count) {
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index)
        result |= (uint64_t)bytes[index] << (index * 8U);
    return result;
}

static bool alz_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Normalize only the filesystem-facing representation.  ALZ names are ANSI
 * byte strings, so non-ASCII bytes are retained verbatim for the caller's
 * configured code page while separators and traversal components are made
 * harmless. */
static char *alz_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start ||
            (end - start == 1U && bytes[start] == '.')) continue;
        if (end - start == 2U && bytes[start] == '.' && bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool alz_safe_output_name(const char *name) {
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

static void alz_stream_free(void *opaque) {
    alz_stream *stream = (alz_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool alz_add_member(alz_stream *stream, const alz_member *member) {
    alz_member *grown;
    if (!stream || !member || stream->count >= ALZ_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (alz_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool alz_parse(Abstractformat *format, alz_stream **result) {
    uint8_t header[ALZ_HEADER_SIZE];
    alz_stream *stream = NULL;
    int64_t total, size, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)ALZ_HEADER_SIZE ||
        !alz_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        xx_rt_memcmp(header, "ALZ\1", 4U) != 0 ||
        xx_rt_memcmp(header + 8U, "BLZ\1", 4U) != 0)
        return false;
    stream = (alz_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = 8;
    while (cursor < size) {
        uint8_t magic_bytes[4];
        uint32_t magic;
        if (size - cursor < 4 ||
            !alz_read_at(format->device, format->base_address + cursor,
                         magic_bytes, sizeof(magic_bytes))) goto fail;
        magic = alz_le32(magic_bytes);
        cursor += 4;
        if (magic == ALZ_MAGIC_CONTROL_1) {
            uint8_t ignored[8];
            if (size - cursor < (int64_t)sizeof(ignored) ||
                !alz_read_at(format->device, format->base_address + cursor,
                             ignored, sizeof(ignored))) goto fail;
            cursor += (int64_t)sizeof(ignored);
            continue;
        }
        if (magic == ALZ_MAGIC_CONTROL_2) continue;
        if (magic == ALZ_MAGIC_ENTRY) {
            uint8_t fixed[ALZ_ENTRY_FIXED_SIZE];
            uint8_t variable[22];
            uint8_t *raw_name = NULL;
            alz_member member;
            uint16_t name_size;
            uint8_t width;
            uint64_t packed, unpacked;
            size_t variable_size;
            int64_t header_offset = cursor - 4;
            if (size - cursor < (int64_t)sizeof(fixed) ||
                !alz_read_at(format->device, format->base_address + cursor,
                             fixed, sizeof(fixed))) goto fail;
            cursor += (int64_t)sizeof(fixed);
            name_size = alz_le16(fixed);
            width = (uint8_t)(fixed[7] >> 4U);
            if (width > 8U) goto fail;
            xx_mem_zero(&member, sizeof(member));
            member.attributes = fixed[2];
            member.dos_time = alz_le32(fixed + 3U);
            member.descriptor = fixed[7];
            member.encrypted = (fixed[7] & 1U) != 0U;
            member.folder = width == 0U;
            packed = 0U;
            unpacked = 0U;
            if (width != 0U) {
                variable_size = 6U + (size_t)width * 2U;
                if (size - cursor < (int64_t)variable_size ||
                    !alz_read_at(format->device, format->base_address + cursor,
                                 variable, variable_size)) goto fail;
                cursor += (int64_t)variable_size;
                member.method = variable[0];
                member.crc32 = alz_le32(variable + 2U);
                packed = alz_le(variable + 6U, width);
                unpacked = alz_le(variable + 6U + width, width);
                if (packed > INT64_MAX || unpacked > SIZE_MAX) goto fail;
            }
            if (name_size > (uint64_t)(size - cursor)) goto fail;
            if (name_size != 0U) {
                raw_name = (uint8_t *)xx_mem_alloc(name_size);
                if (!raw_name ||
                    !alz_read_at(format->device, format->base_address + cursor,
                                 raw_name, name_size)) {
                    if (raw_name) xx_mem_free(raw_name);
                    goto fail;
                }
            }
            member.name = alz_normalize_name(raw_name, name_size);
            if (raw_name) xx_mem_free(raw_name);
            if (!member.name) goto fail;
            cursor += name_size;
            if (packed > (uint64_t)(size - cursor) ||
                (member.encrypted && width != 0U && packed < 12U) ||
                format->base_address > INT64_MAX - cursor) {
                xx_str_free(member.name);
                goto fail;
            }
            member.data_offset = format->base_address + cursor;
            member.header_offset = format->base_address + header_offset;
            member.header_size = cursor - header_offset;
            member.packed_size = (int64_t)packed;
            member.unpacked_size = unpacked;
            if (!alz_add_member(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            cursor += (int64_t)packed;
            continue;
        }
        goto fail;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    alz_stream_free(stream);
    return false;
}

static bool alz_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *alz_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool alz_set_record(xx_archive_record *record,
                           const alz_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->descriptor) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool alz_decode_member(Abstractformat *format, const alz_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->encrypted ||
        member->packed_size < 0 || member->unpacked_size > SIZE_MAX)
        return false;
    if (member->folder) {
        *plain = NULL;
        *plain_size = 0U;
        return true;
    }
    output_size = (size_t)member->unpacked_size;
    if (member->method == 0U && (uint64_t)member->packed_size !=
                                   member->unpacked_size)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !alz_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size))) goto fail;
    if (member->method == 0U) {
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else if (member->method == 1U) {
        decoded = xx_bzip2_decompress_memory(packed,
                                              (size_t)member->packed_size,
                                              output, output_size, &written);
    } else if (member->method == 2U) {
        decoded = xx_deflate_decompress_memory(packed,
                                                (size_t)member->packed_size,
                                                output, output_size, &written,
                                                false);
    }
    if (!decoded || written != output_size ||
        xx_crc32_calc(0U, output, written) != member->crc32) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_alz_init(xx_alz *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ALZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-alz");
    xx_format_set_extension(&archive->format, "alz");
    archive->format.check_is_valid = xx_alz_check_is_valid;
    archive->format.handle_base_info = xx_alz_handle_base_info;
    archive->format.get_format_size = xx_alz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_alz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_alz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_alz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_alz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_alz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_alz_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_alz *xx_alz_create(xx_io_device *device, int64_t base_address) {
    xx_alz *archive = (xx_alz *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_alz_init(archive, device, base_address);
    return archive;
}

void xx_alz_destroy(xx_alz *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_alz_free(xx_alz *archive) {
    if (!archive) return;
    xx_alz_destroy(archive);
    xx_mem_free(archive);
}

bool xx_alz_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    alz_stream *stream;
    (void)pd;
    if (!alz_parse(format, &stream)) return false;
    alz_stream_free(stream);
    return true;
}

bool xx_alz_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    alz_stream *stream;
    xx_alz *archive;
    (void)pd;
    if (!format || !alz_parse(format, &stream)) return false;
    archive = (xx_alz *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    alz_stream_free(stream);
    return true;
}

int64_t xx_alz_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_alz_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_alz_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_alz_handle_base_info(format, pd))
               ? ((xx_alz *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_alz_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    alz_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!alz_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        alz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = alz_stream_free;
    state->total_records = stream->count;
    if (!alz_copy_options(&state->options, options) ||
        !alz_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_alz_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_alz_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    alz_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (alz_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = alz_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_alz_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    alz_stream *stream;
    alz_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (alz_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!alz_safe_output_name(member->name) ||
        !alz_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = alz_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
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
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_alz_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

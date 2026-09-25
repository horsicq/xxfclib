/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Dunfield ImageDisk container.  An ASCII banner ends
 * at 0x1A and one record per physical track follows; each sector is stored,
 * unavailable, or a single repeated byte.  A track is the unit the format
 * is built from, so each track becomes one member.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/imd/xx_imd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef IMD
#define XX_IMD_FILE_TYPE XX_FILE_TYPE_IMD
#else
#define XX_IMD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IMD_MAX_MEMBERS 65536U
#define IMD_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct imd_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} imd_member;

typedef struct imd_stream_s {
    imd_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} imd_stream;

static uint16_t imd_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t imd_le32(const uint8_t *b) {
    return (uint32_t)imd_le16(b) | ((uint32_t)imd_le16(b + 2U) << 16U);
}

static bool imd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *imd_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void imd_stream_free(void *opaque) {
    imd_stream *stream = (imd_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool imd_add_member(imd_stream *stream, const imd_member *member) {
    imd_member *grown;
    if (!stream || !member || stream->count >= IMD_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (imd_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define IMD_MAX_SECTOR_SIZE 8192
#define IMD_MAX_BANNER 1024

/* One ImageDisk track record.  Both the walker and the decoder run through
 * this, so a track can never be measured with one layout and expanded with
 * another.  `output` is NULL while measuring. */
static bool imd_track(const uint8_t *data, size_t size, size_t *cursor,
                      uint8_t *output, size_t output_size, size_t *produced) {
    uint8_t mode, head_flags, sector_count, size_code, head;
    size_t position = *cursor;
    size_t sizes_position = 0U;
    size_t total = 0U;
    uint32_t index;
    uint32_t fixed_size = 0U;
    if (size - position < 5U) return false;
    mode = data[position];
    head_flags = data[position + 2U];
    sector_count = data[position + 3U];
    size_code = data[position + 4U];
    head = (uint8_t)(head_flags & 0x3fU);
    position += 5U;
    if ((mode > 6U && mode != 9U) || sector_count == 0U || head > 1U ||
        (head_flags & 0x3cU) != 0U)
        return false;
    if (size - position < sector_count) return false;
    position += sector_count;
    if (head_flags & 0x80U) {
        if (size - position < sector_count) return false;
        position += sector_count;
    }
    if (head_flags & 0x40U) {
        if (size - position < sector_count) return false;
        position += sector_count;
    }
    if (size_code == 0xffU) {
        if (size - position < (size_t)sector_count * 2U) return false;
        sizes_position = position;
        position += (size_t)sector_count * 2U;
    } else {
        if (size_code > 6U) return false;
        fixed_size = 128U << size_code;
    }
    for (index = 0U; index < sector_count; ++index) {
        uint32_t sector_size = fixed_size;
        uint8_t status;
        if (sizes_position != 0U)
            sector_size = imd_le16(data + sizes_position + index * 2U);
        if (sector_size == 0U || sector_size > IMD_MAX_SECTOR_SIZE) return false;
        if (size - position < 1U) return false;
        status = data[position++];
        if (sector_size > IMD_MAX_OUTPUT - total) return false;
        if (status == 0U) {
            if (output) {
                if (total + sector_size > output_size) return false;
                xx_mem_zero(output + total, sector_size);
            }
        } else if (status == 1U || status == 3U || status == 5U ||
                   status == 7U) {
            if (size - position < sector_size) return false;
            if (output) {
                if (total + sector_size > output_size) return false;
                xx_mem_copy(output + total, data + position, sector_size);
            }
            position += sector_size;
        } else if (status == 2U || status == 4U || status == 6U ||
                   status == 8U) {
            uint8_t fill;
            if (size - position < 1U) return false;
            fill = data[position++];
            if (output) {
                uint32_t at;
                if (total + sector_size > output_size) return false;
                for (at = 0U; at < sector_size; ++at)
                    output[total + at] = fill;
            }
        } else {
            return false;
        }
        total += sector_size;
    }
    *cursor = position;
    if (produced) *produced = total;
    return true;
}

/* "IMD v.vv: dd/mm/yyyy hh:mm:ss" and a free comment, terminated by 0x1A, then
 * one record per physical track.  A track is the unit the format itself is
 * built from, so each becomes a member holding that track's sectors in the
 * order the file stores them. */
static bool imd_parse(Abstractformat *format, imd_stream **result) {
    uint8_t *data = NULL;
    imd_stream *stream = NULL;
    int64_t total, size;
    size_t banner = 0U, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 16 || (uint64_t)size > IMD_MAX_OUTPUT) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    if (!imd_read_at(format->device, format->base_address, data,
                     (size_t)size) ||
        data[0] != 'I' || data[1] != 'M' || data[2] != 'D' || data[3] != ' ')
        goto fail;
    while (banner < (size_t)size && banner < IMD_MAX_BANNER &&
           data[banner] != 0x1a)
        ++banner;
    if (banner < 4U || banner >= (size_t)size || data[banner] != 0x1a)
        goto fail;
    cursor = banner + 1U;
    stream = (imd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    while (cursor < (size_t)size) {
        imd_member member;
        size_t start = cursor;
        size_t produced = 0U;
        int cylinder, head;
        if (!imd_track(data, (size_t)size, &cursor, NULL, 0U, &produced) ||
            produced == 0U)
            goto fail;
        cylinder = (int)data[start + 1U];
        head = (int)(data[start + 2U] & 0x3fU);
        xx_mem_zero(&member, sizeof(member));
        member.name = imd_make_name("track", cylinder, head, ".bin");
        member.header_offset = format->base_address + (int64_t)start;
        member.header_size = 5;
        member.data_offset = format->base_address + (int64_t)start;
        member.packed_size = (int64_t)(cursor - start);
        member.unpacked_size = (uint64_t)produced;
        member.method = data[start];
        member.decode = true;
        if (!member.name || !imd_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    xx_mem_free(data);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (data) xx_mem_free(data);
    imd_stream_free(stream);
    return false;
}

static bool imd_decode(Abstractformat *format, const imd_member *member,
                       uint8_t **plain, size_t *plain_size) {
    uint8_t *packed;
    uint8_t *output;
    size_t cursor = 0U;
    size_t produced = 0U;
    if (member->packed_size <= 0 ||
        (uint64_t)member->packed_size > IMD_MAX_OUTPUT ||
        member->unpacked_size == 0U || member->unpacked_size > IMD_MAX_OUTPUT)
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!packed || !output ||
        !imd_read_at(format->device, member->data_offset, packed,
                     (size_t)member->packed_size) ||
        !imd_track(packed, (size_t)member->packed_size, &cursor, output,
                   (size_t)member->unpacked_size, &produced) ||
        produced != (size_t)member->unpacked_size ||
        cursor != (size_t)member->packed_size) {
        if (packed) xx_mem_free(packed);
        if (output) xx_mem_free(output);
        return false;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = produced;
    return true;
}

static bool imd_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *imd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool imd_set_record(xx_archive_record *record,
                           const imd_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool imd_extract(Abstractformat *format, const imd_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return imd_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > IMD_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !imd_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_imd_init(xx_imd *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IMD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-imagedisk");
    xx_format_set_extension(&archive->format, "imd");
    archive->format.check_is_valid = xx_imd_check_is_valid;
    archive->format.handle_base_info = xx_imd_handle_base_info;
    archive->format.get_format_size = xx_imd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_imd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_imd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_imd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_imd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_imd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_imd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_imd *xx_imd_create(xx_io_device *device, int64_t base_address) {
    xx_imd *archive = (xx_imd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_imd_init(archive, device, base_address);
    return archive;
}

void xx_imd_destroy(xx_imd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_imd_free(xx_imd *archive) {
    if (!archive) return;
    xx_imd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_imd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    imd_stream *stream;
    (void)pd;
    if (!imd_parse(format, &stream)) return false;
    imd_stream_free(stream);
    return true;
}

bool xx_imd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    imd_stream *stream;
    xx_imd *archive;
    (void)pd;
    if (!format || !imd_parse(format, &stream)) return false;
    archive = (xx_imd *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    imd_stream_free(stream);
    return true;
}

int64_t xx_imd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_imd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_imd_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_imd_handle_base_info(format, pd))
               ? ((xx_imd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_imd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    imd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!imd_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        imd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = imd_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!imd_copy_options(&state->options, options) ||
        !imd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_imd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_imd_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    imd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (imd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = imd_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_imd_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    imd_stream *stream;
    imd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (imd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!imd_extract(format, member, &plain, &plain_size)) goto done;
    path_option = imd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_imd_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

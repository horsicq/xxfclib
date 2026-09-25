/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the DiskDupe DDI image.  A per-track table gives each
 * written track's position inside the data area; the member is the flat
 * image those tracks reassemble into.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskdupe/xx_diskdupe.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef DISKDUPE
#define XX_DISKDUPE_FILE_TYPE XX_FILE_TYPE_DISKDUPE
#else
#define XX_DISKDUPE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DISKDUPE_MAX_MEMBERS 65536U
#define DISKDUPE_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct diskdupe_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} diskdupe_member;

typedef struct diskdupe_stream_s {
    diskdupe_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} diskdupe_stream;

static uint16_t diskdupe_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t diskdupe_le32(const uint8_t *b) {
    return (uint32_t)diskdupe_le16(b) | ((uint32_t)diskdupe_le16(b + 2U) << 16U);
}

static bool diskdupe_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *diskdupe_make_name(const char *prefix, int a, int b,
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

static void diskdupe_stream_free(void *opaque) {
    diskdupe_stream *stream = (diskdupe_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool diskdupe_add_member(diskdupe_stream *stream, const diskdupe_member *member) {
    diskdupe_member *grown;
    if (!stream || !member || stream->count >= DISKDUPE_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (diskdupe_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define DISKDUPE_TABLE_OFFSET 0x48
#define DISKDUPE_ENTRY_SIZE 5

/* DiskDupe's DDI: the ASCII banner "MSD Image Version 1 " and a 0x1A, a small
 * fixed header whose bytes 0x43 and 0x44 carry the track count and the
 * allocation block size, then one five byte entry per track - a 24-bit start,
 * in blocks from the start of the data area, plus two status bytes.
 *
 * Nothing stores where the data area begins or how many tracks were actually
 * written.  Both follow from the table: the starts of the written tracks rise
 * in exact multiples of the per-track block count, so the highest start fixes
 * the number of stored tracks, and the data area must then end at the end of
 * the file.  A file whose arithmetic does not close is rejected. */
static bool diskdupe_layout(const uint8_t *table, uint32_t track_count,
                            uint32_t *track_blocks, uint32_t *stored) {
    uint32_t index;
    uint32_t count = 0U;
    int64_t previous = -1;
    uint32_t step = 0U;
    for (index = 0U; index < track_count; ++index) {
        const uint8_t *entry = table + (size_t)index * DISKDUPE_ENTRY_SIZE;
        uint32_t value = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8U) |
                         ((uint32_t)entry[2] << 16U);
        if (entry[3] != 1U && entry[3] != 2U) return false;
        if ((int64_t)value <= previous) continue;
        if (count == 1U) {
            step = value;
            if (step == 0U) return false;
        } else if (count > 1U) {
            if (step == 0U || value != count * step) return false;
        }
        previous = (int64_t)value;
        ++count;
    }
    if (count < 2U || step == 0U) return false;
    *track_blocks = step;
    *stored = count;
    return true;
}

static bool diskdupe_parse(Abstractformat *format, diskdupe_stream **result) {
    static const char magic[21] = {'M', 'S', 'D', ' ', 'I', 'm', 'a',
                                   'g', 'e', ' ', 'V', 'e', 'r', 's',
                                   'i', 'o', 'n', ' ', '1', ' ', 0x1a};
    uint8_t header[DISKDUPE_TABLE_OFFSET];
    uint8_t *table = NULL;
    diskdupe_stream *stream = NULL;
    diskdupe_member member;
    int64_t total, size, table_size, data_start;
    uint32_t track_count, block_size, track_blocks = 0U, stored = 0U;
    uint64_t track_bytes, image_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)DISKDUPE_TABLE_OFFSET ||
        !diskdupe_read_at(format->device, format->base_address, header,
                          sizeof(header)) ||
        xx_rt_memcmp(header, magic, sizeof(magic)) != 0)
        return false;
    track_count = header[0x43];
    block_size = diskdupe_le16(header + 0x44U);
    if (track_count < 2U || block_size < 128U || block_size > 4096U ||
        (block_size & (block_size - 1U)) != 0U)
        return false;
    table_size = (int64_t)track_count * DISKDUPE_ENTRY_SIZE;
    if (table_size > size - DISKDUPE_TABLE_OFFSET) return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return false;
    if (!diskdupe_read_at(format->device,
                          format->base_address + DISKDUPE_TABLE_OFFSET, table,
                          (size_t)table_size) ||
        !diskdupe_layout(table, track_count, &track_blocks, &stored))
        goto fail;
    track_bytes = (uint64_t)track_blocks * block_size;
    image_size = track_bytes * track_count;
    if (track_bytes == 0U || track_bytes > DISKDUPE_MAX_OUTPUT ||
        image_size > DISKDUPE_MAX_OUTPUT || stored > track_count)
        goto fail;
    if ((uint64_t)(size - DISKDUPE_TABLE_OFFSET - table_size) <
        track_bytes * stored)
        goto fail;
    data_start = size - (int64_t)(track_bytes * stored);
    if (data_start < DISKDUPE_TABLE_OFFSET + table_size) goto fail;
    stream = (diskdupe_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(&member, sizeof(member));
    member.name = diskdupe_make_name("image", -1, -1, ".img");
    member.header_offset = format->base_address;
    member.header_size = DISKDUPE_TABLE_OFFSET + table_size;
    member.data_offset = format->base_address + data_start;
    member.packed_size = size - data_start;
    member.unpacked_size = image_size;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !diskdupe_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        goto fail;
    }
    xx_mem_free(table);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    diskdupe_stream_free(stream);
    return false;
}

static bool diskdupe_decode(Abstractformat *format, const diskdupe_member *member,
                            uint8_t **plain, size_t *plain_size) {
    uint8_t header[DISKDUPE_TABLE_OFFSET];
    uint8_t *table = NULL;
    uint8_t *output = NULL;
    uint32_t track_count, block_size, track_blocks = 0U, stored = 0U;
    uint32_t index;
    uint64_t track_bytes;
    int64_t table_size;
    int64_t previous = -1;
    if (member->unpacked_size == 0U || member->unpacked_size > DISKDUPE_MAX_OUTPUT ||
        !diskdupe_read_at(format->device, member->header_offset, header,
                          sizeof(header)))
        return false;
    track_count = header[0x43];
    block_size = diskdupe_le16(header + 0x44U);
    table_size = (int64_t)track_count * DISKDUPE_ENTRY_SIZE;
    if (member->header_size != DISKDUPE_TABLE_OFFSET + table_size) return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!table || !output ||
        !diskdupe_read_at(format->device,
                          member->header_offset + DISKDUPE_TABLE_OFFSET, table,
                          (size_t)table_size) ||
        !diskdupe_layout(table, track_count, &track_blocks, &stored))
        goto fail;
    track_bytes = (uint64_t)track_blocks * block_size;
    if (track_bytes * track_count != member->unpacked_size) goto fail;
    /* An unwritten track keeps the byte a DOS format leaves behind. */
    xx_rt_memset(output, 0xf6, (size_t)member->unpacked_size);
    for (index = 0U; index < track_count; ++index) {
        const uint8_t *entry = table + (size_t)index * DISKDUPE_ENTRY_SIZE;
        uint32_t value = (uint32_t)entry[0] | ((uint32_t)entry[1] << 8U) |
                         ((uint32_t)entry[2] << 16U);
        uint64_t source;
        if ((int64_t)value <= previous) continue;
        previous = (int64_t)value;
        source = (uint64_t)value * block_size;
        if (source > (uint64_t)member->packed_size ||
            track_bytes > (uint64_t)member->packed_size - source)
            goto fail;
        if (!diskdupe_read_at(format->device,
                              member->data_offset + (int64_t)source,
                              output + (uint64_t)index * track_bytes,
                              (size_t)track_bytes))
            goto fail;
    }
    xx_mem_free(table);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
fail:
    if (table) xx_mem_free(table);
    if (output) xx_mem_free(output);
    return false;
}

static bool diskdupe_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *diskdupe_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool diskdupe_set_record(xx_archive_record *record,
                           const diskdupe_member *member) {
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
static bool diskdupe_extract(Abstractformat *format, const diskdupe_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return diskdupe_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > DISKDUPE_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !diskdupe_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_diskdupe_init(xx_diskdupe *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DISKDUPE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-diskdupe");
    xx_format_set_extension(&archive->format, "ddi");
    archive->format.check_is_valid = xx_diskdupe_check_is_valid;
    archive->format.handle_base_info = xx_diskdupe_handle_base_info;
    archive->format.get_format_size = xx_diskdupe_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_diskdupe_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_diskdupe_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_diskdupe_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_diskdupe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_diskdupe_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_diskdupe_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_diskdupe *xx_diskdupe_create(xx_io_device *device, int64_t base_address) {
    xx_diskdupe *archive = (xx_diskdupe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_diskdupe_init(archive, device, base_address);
    return archive;
}

void xx_diskdupe_destroy(xx_diskdupe *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_diskdupe_free(xx_diskdupe *archive) {
    if (!archive) return;
    xx_diskdupe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_diskdupe_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    diskdupe_stream *stream;
    (void)pd;
    if (!diskdupe_parse(format, &stream)) return false;
    diskdupe_stream_free(stream);
    return true;
}

bool xx_diskdupe_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    diskdupe_stream *stream;
    xx_diskdupe *archive;
    (void)pd;
    if (!format || !diskdupe_parse(format, &stream)) return false;
    archive = (xx_diskdupe *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    diskdupe_stream_free(stream);
    return true;
}

int64_t xx_diskdupe_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskdupe_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_diskdupe_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskdupe_handle_base_info(format, pd))
               ? ((xx_diskdupe *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_diskdupe_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    diskdupe_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!diskdupe_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        diskdupe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = diskdupe_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!diskdupe_copy_options(&state->options, options) ||
        !diskdupe_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_diskdupe_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_diskdupe_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    diskdupe_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (diskdupe_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = diskdupe_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_diskdupe_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    diskdupe_stream *stream;
    diskdupe_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (diskdupe_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!diskdupe_extract(format, member, &plain, &plain_size)) goto done;
    path_option = diskdupe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_diskdupe_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

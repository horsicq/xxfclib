/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the DiscJuggler CDI image.  The descriptor lives at the
 * end of the file; its track blocks give the stored extents, which are the
 * members - raw sectors at the track's own sector size.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskjuggler/xx_diskjuggler.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef DISKJUGGLER
#define XX_DISKJUGGLER_FILE_TYPE XX_FILE_TYPE_DISKJUGGLER
#else
#define XX_DISKJUGGLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DISKJUGGLER_MAX_MEMBERS 65536U
#define DISKJUGGLER_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct diskjuggler_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} diskjuggler_member;

typedef struct diskjuggler_stream_s {
    diskjuggler_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} diskjuggler_stream;

static uint16_t diskjuggler_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t diskjuggler_le32(const uint8_t *b) {
    return (uint32_t)diskjuggler_le16(b) | ((uint32_t)diskjuggler_le16(b + 2U) << 16U);
}

static bool diskjuggler_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *diskjuggler_make_name(const char *prefix, int a, int b,
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

static void diskjuggler_stream_free(void *opaque) {
    diskjuggler_stream *stream = (diskjuggler_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool diskjuggler_add_member(diskjuggler_stream *stream, const diskjuggler_member *member) {
    diskjuggler_member *grown;
    if (!stream || !member || stream->count >= DISKJUGGLER_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (diskjuggler_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define DISKJUGGLER_MAX_HEADER (16 * 1024 * 1024)
#define DISKJUGGLER_MAX_IMAGE INT64_C(0x80000000)

static const uint8_t diskjuggler_mark[10] = {0,    0,    1,    0,    0,
                                             0,    0xff, 0xff, 0xff, 0xff};

static bool diskjuggler_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

/* DiscJuggler writes its descriptor LAST: the final eight bytes are a version
 * word and the position of the header, and the header is a run of track blocks
 * each introduced by two adjacent ten-byte sentinels.  Block tails differ
 * between producers, so the blocks are located by the sentinel pair and every
 * field is validated; the reconstructed extents must tile the data area exactly
 * up to the header, which is the check that makes the scan trustworthy. */
static bool diskjuggler_parse(Abstractformat *format,
                              diskjuggler_stream **result) {
    uint8_t footer[8];
    uint8_t *header = NULL;
    diskjuggler_stream *stream = NULL;
    int64_t total, size, header_position, header_size, cursor, data_offset;
    uint32_t version, header_offset;
    int32_t sessions, expected_session = 0, expected_track = 0, index = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 16 || size > DISKJUGGLER_MAX_IMAGE ||
        !diskjuggler_read_at(format->device, format->base_address + size - 8,
                             footer, sizeof(footer)))
        return false;
    version = diskjuggler_le32(footer);
    header_offset = diskjuggler_le32(footer + 4U);
    if (version != 0x80000004U && version != 0x80000005U &&
        version != 0x80000006U)
        return false;
    header_position = (version == 0x80000006U)
                          ? size - (int64_t)header_offset
                          : (int64_t)header_offset;
    if (header_position <= 0 || header_position >= size - 8) return false;
    header_size = size - header_position;
    if (header_size < 16 || header_size > DISKJUGGLER_MAX_HEADER) return false;
    header = (uint8_t *)xx_mem_alloc((size_t)header_size);
    if (!header) return false;
    if (!diskjuggler_read_at(format->device,
                             format->base_address + header_position, header,
                             (size_t)header_size))
        goto fail;
    sessions = (int32_t)diskjuggler_le16(header);
    if (sessions < 1 || sessions > 99) goto fail;
    stream = (diskjuggler_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    data_offset = 0;
    for (cursor = 0; cursor <= header_size - 20; ++cursor) {
        int64_t track_head, index_count_position, indices_size, fixed;
        int64_t cd_text_size, pregap, sectors, pregap_bytes, packed_bytes;
        int32_t name_length, index_count, mode, session, number, sector_size;
        uint32_t cd_text_count, read_mode;
        diskjuggler_member member;
        if (xx_rt_memcmp(header + cursor, diskjuggler_mark, 10) != 0 ||
            xx_rt_memcmp(header + cursor + 10, diskjuggler_mark, 10) != 0)
            continue;
        track_head = cursor + 8;
        if (!diskjuggler_within(header_size, track_head + 0x10, 1)) continue;
        name_length = header[track_head + 0x10];
        index_count_position = track_head + 0x30 + name_length;
        if (!diskjuggler_within(header_size, index_count_position, 2)) continue;
        index_count = (int32_t)diskjuggler_le16(header + index_count_position);
        if (index_count < 1 || index_count > 64) continue;
        indices_size = (int64_t)index_count * 4;
        if (!diskjuggler_within(header_size, index_count_position + 2,
                                indices_size + 4))
            continue;
        cd_text_count =
            diskjuggler_le32(header + index_count_position + 2 + indices_size);
        if (cd_text_count > 4096U) continue;
        cd_text_size = (int64_t)cd_text_count * 18;
        fixed = track_head + 0x36 + name_length + indices_size + cd_text_size;
        if (!diskjuggler_within(header_size, fixed, 0x2e)) continue;
        mode = header[fixed + 2];
        read_mode = diskjuggler_le32(header + fixed + 0x2a);
        if (mode > 2 || read_mode > 2U) continue;
        session = (int32_t)diskjuggler_le32(header + fixed + 0x0a);
        number = (int32_t)diskjuggler_le32(header + fixed + 0x0e);
        if (session < 0 || session >= sessions || number < 0 || number > 999)
            continue;
        pregap = (int64_t)diskjuggler_le32(header + index_count_position + 2);
        sectors = (index_count >= 2)
                      ? (int64_t)diskjuggler_le32(header +
                                                  index_count_position + 6)
                      : (int64_t)diskjuggler_le32(header + fixed + 0x16);
        if (sectors <= 0 || sectors > 1000000 || pregap > 1000000) continue;
        sector_size = read_mode == 0U ? 2048 : (read_mode == 1U ? 2336 : 2352);
        if (session == expected_session + 1 && number == 0) {
            ++expected_session;
            expected_track = 0;
        }
        if (session != expected_session || number != expected_track) goto fail;
        ++expected_track;
        pregap_bytes = pregap * sector_size;
        packed_bytes = sectors * sector_size;
        if (pregap != 0) {
            if (!diskjuggler_within(header_position, data_offset, pregap_bytes))
                goto fail;
            xx_mem_zero(&member, sizeof(member));
            member.name =
                diskjuggler_make_name("track", index, -1, ".pregap.bin");
            member.header_offset =
                format->base_address + header_position + cursor;
            member.header_size = 20;
            member.data_offset = format->base_address + data_offset;
            member.packed_size = pregap_bytes;
            member.unpacked_size = (uint64_t)pregap_bytes;
            if (!member.name || !diskjuggler_add_member(stream, &member)) {
                if (member.name) xx_mem_free(member.name);
                goto fail;
            }
            data_offset += pregap_bytes;
        }
        if (!diskjuggler_within(header_position, data_offset, packed_bytes))
            goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = diskjuggler_make_name("track", index, -1, ".bin");
        member.header_offset = format->base_address + header_position + cursor;
        member.header_size = 20;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = packed_bytes;
        member.unpacked_size = (uint64_t)packed_bytes;
        member.method = (uint32_t)sector_size;
        if (!member.name || !diskjuggler_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
        data_offset += packed_bytes;
        ++index;
        cursor += 19;
    }
    if (stream->count == 0U || data_offset != header_position ||
        sessions < expected_session + 1 || sessions > expected_session + 2)
        goto fail;
    xx_mem_free(header);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (header) xx_mem_free(header);
    diskjuggler_stream_free(stream);
    return false;
}

static bool diskjuggler_decode(Abstractformat *format,
                               const diskjuggler_member *member,
                               uint8_t **plain, size_t *plain_size) {
    (void)format;
    (void)member;
    (void)plain;
    (void)plain_size;
    return false;
}

static bool diskjuggler_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *diskjuggler_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool diskjuggler_set_record(xx_archive_record *record,
                           const diskjuggler_member *member) {
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
static bool diskjuggler_extract(Abstractformat *format, const diskjuggler_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return diskjuggler_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > DISKJUGGLER_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !diskjuggler_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_diskjuggler_init(xx_diskjuggler *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DISKJUGGLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-discjuggler");
    xx_format_set_extension(&archive->format, "cdi");
    archive->format.check_is_valid = xx_diskjuggler_check_is_valid;
    archive->format.handle_base_info = xx_diskjuggler_handle_base_info;
    archive->format.get_format_size = xx_diskjuggler_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_diskjuggler_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_diskjuggler_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_diskjuggler_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_diskjuggler_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_diskjuggler_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_diskjuggler_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_diskjuggler *xx_diskjuggler_create(xx_io_device *device, int64_t base_address) {
    xx_diskjuggler *archive = (xx_diskjuggler *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_diskjuggler_init(archive, device, base_address);
    return archive;
}

void xx_diskjuggler_destroy(xx_diskjuggler *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_diskjuggler_free(xx_diskjuggler *archive) {
    if (!archive) return;
    xx_diskjuggler_destroy(archive);
    xx_mem_free(archive);
}

bool xx_diskjuggler_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    diskjuggler_stream *stream;
    (void)pd;
    if (!diskjuggler_parse(format, &stream)) return false;
    diskjuggler_stream_free(stream);
    return true;
}

bool xx_diskjuggler_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    diskjuggler_stream *stream;
    xx_diskjuggler *archive;
    (void)pd;
    if (!format || !diskjuggler_parse(format, &stream)) return false;
    archive = (xx_diskjuggler *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    diskjuggler_stream_free(stream);
    return true;
}

int64_t xx_diskjuggler_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskjuggler_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_diskjuggler_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskjuggler_handle_base_info(format, pd))
               ? ((xx_diskjuggler *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_diskjuggler_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    diskjuggler_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!diskjuggler_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        diskjuggler_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = diskjuggler_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!diskjuggler_copy_options(&state->options, options) ||
        !diskjuggler_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_diskjuggler_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_diskjuggler_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    diskjuggler_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (diskjuggler_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = diskjuggler_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_diskjuggler_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    diskjuggler_stream *stream;
    diskjuggler_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (diskjuggler_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!diskjuggler_extract(format, member, &plain, &plain_size)) goto done;
    path_option = diskjuggler_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_diskjuggler_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

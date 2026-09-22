/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Sydex CopyQM floppy image.  The container is a
 * fixed 0x85-byte header, a comment whose length the header gives, and a
 * run-length stream.  Nothing stores the decoded length, so it is measured.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/copyqm/xx_copyqm.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef COPYQM
#define XX_COPYQM_FILE_TYPE XX_FILE_TYPE_COPYQM
#else
#define XX_COPYQM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define COPYQM_MAX_MEMBERS 65536U
#define COPYQM_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct copyqm_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} copyqm_member;

typedef struct copyqm_stream_s {
    copyqm_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} copyqm_stream;

static uint16_t copyqm_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t copyqm_le32(const uint8_t *b) {
    return (uint32_t)copyqm_le16(b) | ((uint32_t)copyqm_le16(b + 2U) << 16U);
}

static bool copyqm_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *copyqm_make_name(const char *prefix, int a, int b,
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

static void copyqm_stream_free(void *opaque) {
    copyqm_stream *stream = (copyqm_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool copyqm_add_member(copyqm_stream *stream, const copyqm_member *member) {
    copyqm_member *grown;
    if (!stream || !member || stream->count >= COPYQM_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (copyqm_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define COPYQM_HEADER_SIZE 0x85
#define COPYQM_COMMENT_LENGTH_OFFSET 0x6f

/* Walk the RLE stream once.  A 16-bit signed token: zero ends the image, a
 * negative one repeats the single byte that follows -token times, a positive
 * one copies that many literal bytes.  Nothing in the container declares the
 * decoded length, so it can only be measured - and the measurement is also the
 * bound every later allocation uses. */
static bool copyqm_run(const uint8_t *packed, size_t packed_size,
                       uint8_t *output, size_t output_size, size_t *produced) {
    size_t offset = 0U;
    size_t total = 0U;
    while (offset + 2U <= packed_size) {
        int32_t token = (int32_t)(int16_t)copyqm_le16(packed + offset);
        offset += 2U;
        if (token == 0) break;
        if (token < 0) {
            size_t count = (size_t)(-(int64_t)token);
            uint8_t value;
            if (offset >= packed_size) break;
            value = packed[offset++];
            if (count > COPYQM_MAX_OUTPUT - total) return false;
            if (output) {
                size_t index;
                if (total + count > output_size) return false;
                for (index = 0U; index < count; ++index)
                    output[total + index] = value;
            }
            total += count;
        } else {
            size_t run = (size_t)token;
            bool truncated = false;
            if (run > packed_size - offset) {
                run = packed_size - offset;
                truncated = true;
            }
            if (run > COPYQM_MAX_OUTPUT - total) return false;
            if (output) {
                if (total + run > output_size) return false;
                if (run != 0U) xx_mem_copy(output + total, packed + offset, run);
            }
            total += run;
            offset += run;
            if (truncated) break;
        }
    }
    if (produced) *produced = total;
    return total != 0U;
}

static bool copyqm_parse(Abstractformat *format, copyqm_stream **result) {
    uint8_t header[COPYQM_HEADER_SIZE];
    uint8_t *packed = NULL;
    copyqm_stream *stream = NULL;
    copyqm_member member;
    int64_t total, size, data_offset, packed_size;
    size_t measured = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)COPYQM_HEADER_SIZE ||
        !copyqm_read_at(format->device, format->base_address, header,
                        sizeof(header)) ||
        header[0] != 'C' || header[1] != 'Q' || header[2] != 0x14)
        return false;
    data_offset = (int64_t)COPYQM_HEADER_SIZE +
                  (int64_t)header[COPYQM_COMMENT_LENGTH_OFFSET];
    if (data_offset >= size) return false;
    packed_size = size - data_offset;
    if ((uint64_t)packed_size > COPYQM_MAX_OUTPUT) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    if (!packed) return false;
    if (!copyqm_read_at(format->device, format->base_address + data_offset,
                        packed, (size_t)packed_size) ||
        !copyqm_run(packed, (size_t)packed_size, NULL, 0U, &measured)) {
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    if (measured == 0U || measured > COPYQM_MAX_OUTPUT) return false;
    stream = (copyqm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = copyqm_make_name("image", -1, -1, ".img");
    member.header_offset = format->base_address;
    member.header_size = data_offset;
    member.data_offset = format->base_address + data_offset;
    member.packed_size = packed_size;
    member.unpacked_size = (uint64_t)measured;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !copyqm_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        copyqm_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool copyqm_decode(Abstractformat *format, const copyqm_member *member,
                          uint8_t **plain, size_t *plain_size) {
    uint8_t *packed;
    uint8_t *output;
    size_t produced = 0U;
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > COPYQM_MAX_OUTPUT ||
        member->unpacked_size == 0U ||
        member->unpacked_size > COPYQM_MAX_OUTPUT)
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!packed || !output ||
        !copyqm_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size) ||
        !copyqm_run(packed, (size_t)member->packed_size, output,
                    (size_t)member->unpacked_size, &produced) ||
        produced != (size_t)member->unpacked_size) {
        if (packed) xx_mem_free(packed);
        if (output) xx_mem_free(output);
        return false;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = produced;
    return true;
}

static bool copyqm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *copyqm_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool copyqm_set_record(xx_archive_record *record,
                           const copyqm_member *member) {
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
static bool copyqm_extract(Abstractformat *format, const copyqm_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return copyqm_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > COPYQM_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !copyqm_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_copyqm_init(xx_copyqm *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_COPYQM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-copyqm");
    xx_format_set_extension(&archive->format, "cqm");
    archive->format.check_is_valid = xx_copyqm_check_is_valid;
    archive->format.handle_base_info = xx_copyqm_handle_base_info;
    archive->format.get_format_size = xx_copyqm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_copyqm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_copyqm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_copyqm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_copyqm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_copyqm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_copyqm_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_copyqm *xx_copyqm_create(xx_io_device *device, int64_t base_address) {
    xx_copyqm *archive = (xx_copyqm *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_copyqm_init(archive, device, base_address);
    return archive;
}

void xx_copyqm_destroy(xx_copyqm *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_copyqm_free(xx_copyqm *archive) {
    if (!archive) return;
    xx_copyqm_destroy(archive);
    xx_mem_free(archive);
}

bool xx_copyqm_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    copyqm_stream *stream;
    (void)pd;
    if (!copyqm_parse(format, &stream)) return false;
    copyqm_stream_free(stream);
    return true;
}

bool xx_copyqm_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    copyqm_stream *stream;
    xx_copyqm *archive;
    (void)pd;
    if (!format || !copyqm_parse(format, &stream)) return false;
    archive = (xx_copyqm *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    copyqm_stream_free(stream);
    return true;
}

int64_t xx_copyqm_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copyqm_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_copyqm_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copyqm_handle_base_info(format, pd))
               ? ((xx_copyqm *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_copyqm_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    copyqm_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!copyqm_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        copyqm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = copyqm_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!copyqm_copy_options(&state->options, options) ||
        !copyqm_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_copyqm_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_copyqm_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    copyqm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (copyqm_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = copyqm_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_copyqm_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    copyqm_stream *stream;
    copyqm_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (copyqm_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!copyqm_extract(format, member, &plain, &plain_size)) goto done;
    path_option = copyqm_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_copyqm_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

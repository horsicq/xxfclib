/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Jasc Software installer archive (.CMP, and the
 * SETUP.INF that ships beside it and is the same container holding a single
 * member).  XArchive has no module for it; the layout below was derived from
 * the 13 corpus samples, and every field named here is confirmed against
 * F:\ARC\U3.exe's own output - member names, sizes and timestamps all agree,
 * and the decoded bytes are identical.
 *
 * There is no archive header.  The file is a chain of members, each a
 * 17-byte record followed immediately by its payload, and a single 0x00
 * terminator byte at end of file:
 *
 *   0x00  1   header size minus two, i.e. name length + 15.  Checking this
 *             against the name length field is the container's only internal
 *             redundancy and is what makes a magic-less format detectable.
 *   0x01  1   header check byte (its derivation is not established, so it is
 *             published as metadata and never enforced)
 *   0x02  4   compressed size (LE)
 *   0x06  4   uncompressed size (LE)
 *   0x0A  4   modification time as a Unix time_t (LE).  Confirmed: the
 *             SETUP.INF samples carry 240593, and the reference extractor
 *             stamps that member 1970-01-03.
 *   0x0E  2   CRC-16/ARC of the uncompressed data - the same CRC LHA uses
 *   0x10  1   name length
 *   0x11  n   file name, no terminator
 *
 * The payload is a raw LHA -lh5- stream (8 KiB window): its leading 16-bit
 * big-endian block symbol count is exactly what the first two payload bytes
 * hold.  -lh4- was ruled out on the larger members, which decode only with
 * the 8 KiB window.  Every decode is checked against the stored CRC-16, so a
 * wrong decode cannot be reported as a right one.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jasc/xx_jasc.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef JASC
#define XX_JASC_FILE_TYPE XX_FILE_TYPE_JASC
#else
#define XX_JASC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define JASC_HEADER_FIXED 17
#define JASC_HEADER_BIAS 15
#define JASC_TERMINATOR_SIZE 1
#define JASC_LH_METHOD 5
#define JASC_MAX_MEMBERS 65536U
#define JASC_MAX_NAME 255U
/* Nothing in the corpus expands past a few megabytes; this only bounds a
 * corrupt size field before it reaches an allocation. */
#define JASC_MAX_UNPACKED INT64_C(0x10000000)

typedef struct jasc_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t unix_time;
    uint16_t crc16;
    uint8_t check;
} jasc_member;

typedef struct jasc_stream_s {
    jasc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} jasc_stream;

static uint16_t jasc_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t jasc_le32(const uint8_t *bytes) {
    return (uint32_t)jasc_le16(bytes) |
           ((uint32_t)jasc_le16(bytes + 2U) << 16U);
}

static bool jasc_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* DOS names with no path component; anything a path could not carry makes
 * the header - and so the archive - invalid rather than being repaired. */
static char *jasc_copy_name(const uint8_t *raw, size_t length) {
    char *name;
    size_t index;
    if (length == 0U || length > JASC_MAX_NAME) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c > 0x7EU || c == (uint8_t)'/' ||
            c == (uint8_t)'\\' || c == (uint8_t)':' || c == (uint8_t)'*' ||
            c == (uint8_t)'?' || c == (uint8_t)'"' || c == (uint8_t)'<' ||
            c == (uint8_t)'>' || c == (uint8_t)'|') return NULL;
    }
    if (raw[0] == (uint8_t)'.' &&
        (length == 1U || (length == 2U && raw[1] == (uint8_t)'.')))
        return NULL;
    if (raw[length - 1U] == (uint8_t)' ') return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, raw, length);
    name[length] = 0;
    return name;
}

static bool jasc_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' || c < 0x20U)
            return false;
    }
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    return true;
}

static void jasc_stream_free(void *opaque) {
    jasc_stream *stream = (jasc_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool jasc_add_member(jasc_stream *stream, const jasc_member *member) {
    jasc_member *grown;
    if (!stream || !member || stream->count >= JASC_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (jasc_member *)xx_mem_realloc(stream->items,
                                          (stream->count + 1U) *
                                              sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool jasc_parse(Abstractformat *format, jasc_stream **result) {
    jasc_stream *stream = NULL;
    int64_t total, size, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < JASC_HEADER_FIXED + 1 + JASC_TERMINATOR_SIZE) return false;
    stream = (jasc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = 0;
    /* The chain stops one byte short of end of file: the last byte is the
     * archive terminator, 0x00 throughout the corpus. */
    while (cursor < size - JASC_TERMINATOR_SIZE) {
        uint8_t fixed[JASC_HEADER_FIXED];
        uint8_t raw_name[JASC_MAX_NAME];
        jasc_member member;
        uint32_t packed, unpacked;
        uint8_t name_length;
        int64_t header_size, data_offset;
        if (size - cursor < JASC_HEADER_FIXED ||
            !jasc_read_at(format->device, format->base_address + cursor,
                          fixed, sizeof(fixed))) goto fail;
        name_length = fixed[16];
        /* The redundancy check: the leading size byte and the name length
         * must agree, which is what stands in for a magic number here. */
        if (name_length == 0U ||
            fixed[0] != (uint8_t)(name_length + JASC_HEADER_BIAS)) goto fail;
        packed = jasc_le32(fixed + 2U);
        unpacked = jasc_le32(fixed + 6U);
        header_size = JASC_HEADER_FIXED + (int64_t)name_length;
        if (size - cursor < header_size) goto fail;
        if (!jasc_read_at(format->device,
                          format->base_address + cursor + JASC_HEADER_FIXED,
                          raw_name, name_length)) goto fail;
        data_offset = cursor + header_size;
        /* Bound the declared extents against the real file before either is
         * used to read or allocate. */
        if ((int64_t)packed > size - JASC_TERMINATOR_SIZE - data_offset)
            goto fail;
        if ((int64_t)unpacked > JASC_MAX_UNPACKED) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = jasc_copy_name(raw_name, name_length);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + cursor;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.unix_time = jasc_le32(fixed + 10U);
        member.crc16 = jasc_le16(fixed + 14U);
        member.check = fixed[1];
        if (!jasc_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor = data_offset + (int64_t)packed;
    }
    /* The chain has to land exactly on the terminator. */
    if (stream->count == 0U || cursor != size - JASC_TERMINATOR_SIZE)
        goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    jasc_stream_free(stream);
    return false;
}

static bool jasc_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *jasc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool jasc_set_record(xx_archive_record *record,
                            const jasc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          JASC_LH_METHOD) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->unix_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->check) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool jasc_decode(Abstractformat *format, const jasc_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U, output_size;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size <= 0 ||
        member->unpacked_size > (uint64_t)SIZE_MAX) return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        !jasc_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)) goto fail;
    if (!xx_lzh5_decode_memory(packed, (size_t)member->packed_size, output,
                               output_size, JASC_LH_METHOD, &written) ||
        written != output_size) goto fail;
    /* The stored CRC is the anchor: a decode that does not reproduce it is
     * reported as a failure, never as output. */
    if (xx_crc16(XX_CRC_TYPE_CRC16_ARC, output, written) != member->crc16)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_jasc_init(xx_jasc *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_JASC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-jasc-cmp");
    xx_format_set_extension(&archive->format, "cmp");
    archive->format.check_is_valid = xx_jasc_check_is_valid;
    archive->format.handle_base_info = xx_jasc_handle_base_info;
    archive->format.get_format_size = xx_jasc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jasc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jasc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jasc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jasc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jasc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jasc_free_archive_records_reading;
}

xx_jasc *xx_jasc_create(xx_io_device *device, int64_t base_address) {
    xx_jasc *archive = (xx_jasc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_jasc_init(archive, device, base_address);
    return archive;
}

void xx_jasc_destroy(xx_jasc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_jasc_free(xx_jasc *archive) {
    if (!archive) return;
    xx_jasc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_jasc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    jasc_stream *stream;
    (void)pd;
    if (!jasc_parse(format, &stream)) return false;
    jasc_stream_free(stream);
    return true;
}

bool xx_jasc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    jasc_stream *stream;
    xx_jasc *archive;
    (void)pd;
    if (!format || !jasc_parse(format, &stream)) return false;
    archive = (xx_jasc *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    jasc_stream_free(stream);
    return true;
}

int64_t xx_jasc_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_jasc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_jasc_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_jasc_handle_base_info(format, pd))
               ? ((xx_jasc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_jasc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    jasc_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!jasc_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        jasc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = jasc_stream_free;
    state->total_records = stream->count;
    if (!jasc_copy_options(&state->options, options) ||
        !jasc_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_jasc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_jasc_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    jasc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (jasc_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = jasc_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jasc_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    jasc_stream *stream;
    jasc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (jasc_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!jasc_safe_output_name(member->name) ||
        !jasc_decode(format, member, &plain, &plain_size)) goto done;
    path_option = jasc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_jasc_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

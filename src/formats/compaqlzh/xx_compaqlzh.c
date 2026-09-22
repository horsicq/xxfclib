/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Compaq's "CPQ_LZH" single-member wrapper, the container
 * used by Compaq's early-90s driver and option-ROM distribution diskettes.
 *
 * The container is a fixed 29-byte header:
 *
 *   +0x00   7  "CPQ_LZH"
 *   +0x07  13  original DOS member name, NUL padded
 *   +0x14   1  DOS attribute byte (0x20, the archive bit, in every sample)
 *   +0x15   2  DOS time, little endian
 *   +0x17   2  DOS date, little endian
 *   +0x19   4  uncompressed size, little endian
 *   +0x1d   .  packed stream, running to end of file
 *
 * No compressed size and no checksum are stored, so the packed extent is
 * simply "the rest of the file" and the only integrity anchor is that the
 * codec must produce EXACTLY the declared number of bytes.
 *
 * The payload is a raw LHA -lh1- stream (LZSS over an adaptive Huffman tree,
 * 4 KiB ring) with no LHA member header of its own - the same conclusion
 * XArchive reaches in core/xlegacystorearchive.cpp, and confirmed here by a
 * byte-exact decode of all 24 corpus samples.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/compaqlzh/xx_compaqlzh.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef COMPAQLZH
#define XX_COMPAQLZH_FILE_TYPE XX_FILE_TYPE_COMPAQLZH
#else
#define XX_COMPAQLZH_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define COMPAQLZH_HEADER_SIZE 29U
#define COMPAQLZH_NAME_SIZE 13U
/* A 29-byte header may not ask for an unbounded allocation; nothing Compaq
 * shipped in this wrapper is anywhere near this large. */
#define COMPAQLZH_MAX_UNPACKED UINT32_C(0x10000000)

typedef struct compaqlzh_member_s {
    char *name;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint8_t attributes;
} compaqlzh_member;

typedef struct compaqlzh_stream_s {
    compaqlzh_member member;
    size_t count;
    size_t index;
    int64_t archive_size;
} compaqlzh_stream;

static uint16_t compaqlzh_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t compaqlzh_le32(const uint8_t *bytes) {
    return (uint32_t)compaqlzh_le16(bytes) |
           ((uint32_t)compaqlzh_le16(bytes + 2U) << 16U);
}

static bool compaqlzh_read_at(xx_io_device *device, int64_t offset,
                              void *buffer, size_t size) {
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

/* The stored name is a NUL padded DOS 8.3 name.  Only the filesystem-facing
 * representation is normalized; the bytes themselves are kept for the
 * caller's configured code page. */
static char *compaqlzh_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if (!bytes || size == 0U) return NULL;
    while (size != 0U && bytes[size - 1U] == 0U) --size;
    if (size == 0U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        uint8_t c = bytes[input++];
        if (c == '/' || c == '\\' || c < 0x20U || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    if (output == 1U && name[0] == '.') name[0] = '_';
    if (output == 2U && name[0] == '.' && name[1] == '.') {
        name[0] = '_';
        name[1] = '_';
    }
    name[output] = 0;
    return name;
}

static bool compaqlzh_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' ||
            (c != 0U && c < 0x20U)) return false;
        if (c == 0U) break;
    }
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    return true;
}

static void compaqlzh_stream_free(void *opaque) {
    compaqlzh_stream *stream = (compaqlzh_stream *)opaque;
    if (!stream) return;
    if (stream->member.name) xx_str_free(stream->member.name);
    xx_mem_free(stream);
}

static bool compaqlzh_parse(Abstractformat *format,
                            compaqlzh_stream **result) {
    uint8_t header[COMPAQLZH_HEADER_SIZE];
    compaqlzh_stream *stream = NULL;
    int64_t total, size;
    uint32_t unpacked;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* A header with no payload behind it is not a container. */
    if (size <= (int64_t)COMPAQLZH_HEADER_SIZE ||
        !compaqlzh_read_at(format->device, format->base_address, header,
                           sizeof(header)) ||
        xx_rt_memcmp(header, "CPQ_LZH", 7U) != 0)
        return false;
    unpacked = compaqlzh_le32(header + 0x19U);
    if (unpacked == 0U || unpacked > COMPAQLZH_MAX_UNPACKED) return false;
    stream = (compaqlzh_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->member.name = compaqlzh_normalize_name(header + 7U,
                                                   COMPAQLZH_NAME_SIZE);
    if (!stream->member.name) {
        compaqlzh_stream_free(stream);
        return false;
    }
    stream->member.attributes = header[0x14U];
    stream->member.dos_time = ((uint32_t)compaqlzh_le16(header + 0x17U) << 16U) |
                              compaqlzh_le16(header + 0x15U);
    stream->member.data_offset = format->base_address +
                                 (int64_t)COMPAQLZH_HEADER_SIZE;
    stream->member.packed_size = size - (int64_t)COMPAQLZH_HEADER_SIZE;
    stream->member.unpacked_size = unpacked;
    stream->count = 1U;
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool compaqlzh_copy_options(xx_list_s *destination,
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

static const xx_var *compaqlzh_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool compaqlzh_set_record(xx_archive_record *record,
                                 const compaqlzh_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->data_offset -
                            (int64_t)COMPAQLZH_HEADER_SIZE;
    record->header_size = (int64_t)COMPAQLZH_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool compaqlzh_decode_member(Abstractformat *format,
                                    const compaqlzh_member *member,
                                    uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size <= 0 || member->unpacked_size == 0U ||
        member->unpacked_size > COMPAQLZH_MAX_UNPACKED ||
        (uint64_t)member->packed_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!packed || !output ||
        !compaqlzh_read_at(format->device, member->data_offset, packed,
                           (size_t)member->packed_size) ||
        !xx_lzh1_decode_memory(packed, (size_t)member->packed_size, output,
                               output_size, &written) ||
        written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_compaqlzh_init(xx_compaqlzh *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_COMPAQLZH_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compaq-lzh");
    xx_format_set_extension(&archive->format, "");
    archive->format.check_is_valid = xx_compaqlzh_check_is_valid;
    archive->format.handle_base_info = xx_compaqlzh_handle_base_info;
    archive->format.get_format_size = xx_compaqlzh_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_compaqlzh_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_compaqlzh_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_compaqlzh_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_compaqlzh_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_compaqlzh_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_compaqlzh_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_compaqlzh *xx_compaqlzh_create(xx_io_device *device, int64_t base_address) {
    xx_compaqlzh *archive = (xx_compaqlzh *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_compaqlzh_init(archive, device, base_address);
    return archive;
}

void xx_compaqlzh_destroy(xx_compaqlzh *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_compaqlzh_free(xx_compaqlzh *archive) {
    if (!archive) return;
    xx_compaqlzh_destroy(archive);
    xx_mem_free(archive);
}

bool xx_compaqlzh_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    compaqlzh_stream *stream;
    (void)pd;
    if (!compaqlzh_parse(format, &stream)) return false;
    compaqlzh_stream_free(stream);
    return true;
}

bool xx_compaqlzh_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    compaqlzh_stream *stream;
    xx_compaqlzh *archive;
    (void)pd;
    if (!format || !compaqlzh_parse(format, &stream)) return false;
    archive = (xx_compaqlzh *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    compaqlzh_stream_free(stream);
    return true;
}

int64_t xx_compaqlzh_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_compaqlzh_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_compaqlzh_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_compaqlzh_handle_base_info(format, pd))
               ? ((xx_compaqlzh *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_compaqlzh_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    compaqlzh_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!compaqlzh_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        compaqlzh_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = compaqlzh_stream_free;
    state->total_records = stream->count;
    if (!compaqlzh_copy_options(&state->options, options) ||
        !compaqlzh_set_record(&state->current_record, &stream->member)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_compaqlzh_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_compaqlzh_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    compaqlzh_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (compaqlzh_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = compaqlzh_set_record(&state->current_record,
                                             &stream->member);
    return state->has_record;
}

bool xx_compaqlzh_unpack_current_archive_record(Abstractformat *format,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    compaqlzh_stream *stream;
    compaqlzh_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (compaqlzh_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->member;
    if (!compaqlzh_safe_output_name(member->name) ||
        !compaqlzh_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = compaqlzh_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!xx_store_create_dirs_a(path, false)) goto done;
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

void xx_compaqlzh_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

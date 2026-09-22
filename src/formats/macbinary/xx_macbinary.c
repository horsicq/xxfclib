/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for MacBinary I, II and III (Yves Lempereur's MacBinary II
 * standard and the later MacBinary III addendum).  The container is a single
 * 128-byte header followed by the data fork and then the resource fork, each
 * padded up to a 128-byte boundary.  Both forks are stored verbatim, so they
 * are surfaced as two stored archive records.
 *
 * MacBinary has no leading magic number.  MacBinary III adds the signature
 * "mBIN" at offset 102, and II/III carry a CRC-16/XMODEM of the first 124
 * header bytes at offset 124; MacBinary I carries neither.  Validation is
 * therefore structural -- the reserved zero bytes, the Pascal name length and
 * the two fork lengths landing inside the real file -- with the CRC used as a
 * confirming check whenever the version byte says it is present.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/macbinary/xx_macbinary.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef MACBINARY
#define XX_MACBINARY_FILE_TYPE XX_FILE_TYPE_MACBINARY
#else
#define XX_MACBINARY_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MB_HEADER_SIZE 128U
#define MB_BLOCK_SIZE 128U
#define MB_MAX_NAME 63U
/* Both forks are 24-bit quantities on the classic Mac file system. */
#define MB_MAX_FORK UINT32_C(0x007FFFFF)
#define MB_OFF_VERSION 0U
#define MB_OFF_NAME_LENGTH 1U
#define MB_OFF_NAME 2U
#define MB_OFF_TYPE 65U
#define MB_OFF_CREATOR 69U
#define MB_OFF_FINDER_FLAGS 73U
#define MB_OFF_ZERO_74 74U
#define MB_OFF_PROTECTED 81U
#define MB_OFF_ZERO_82 82U
#define MB_OFF_DATA_LENGTH 83U
#define MB_OFF_RSRC_LENGTH 87U
#define MB_OFF_CREATED 91U
#define MB_OFF_MODIFIED 95U
#define MB_OFF_SIGNATURE 102U
#define MB_OFF_SECONDARY 120U
#define MB_OFF_VERSION_WRITTEN 122U
#define MB_OFF_VERSION_NEEDED 123U
#define MB_OFF_CRC 124U

typedef struct mb_member_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    bool resource;
} mb_member;

typedef struct mb_stream_s {
    mb_member items[2];
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t type;
    uint32_t creator;
    uint32_t created;
    uint32_t modified;
    uint8_t finder_flags;
    uint8_t protected_flag;
    uint8_t version_written;
    bool has_signature;
    bool crc_verified;
} mb_stream;

static uint32_t mb_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t mb_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static bool mb_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static int64_t mb_round_up(int64_t value) {
    int64_t remainder = value % (int64_t)MB_BLOCK_SIZE;
    return remainder == 0 ? value
                          : value + ((int64_t)MB_BLOCK_SIZE - remainder);
}

/* The name is Mac Roman and may legally hold bytes a file system would choke
 * on, so only the filesystem-facing representation is sanitized. */
static char *mb_normalize_name(const uint8_t *bytes, size_t size,
                               const char *suffix) {
    size_t suffix_length = suffix ? xx_str_len(suffix) : 0U;
    size_t input, output = 0U;
    char *name;
    if (size > MB_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(size + suffix_length + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU)
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ' ||
                            name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    for (input = 0U; input < suffix_length; ++input)
        name[output++] = suffix[input];
    name[output] = 0;
    return name;
}

/* Without a verified CRC there is nothing in the header that is unique to
 * MacBinary, so the payload fields themselves have to carry the evidence: a
 * Finder name is Mac Roman text and never holds control codes, and the type
 * and creator are four printable characters each.  Without this, any file
 * whose first bytes happen to be zero and a small count -- AppleDouble, for
 * one -- would be accepted. */
static bool mb_header_is_plausible(const uint8_t *header, size_t name_length) {
    size_t index;
    for (index = 0U; index < name_length; ++index)
        if (header[MB_OFF_NAME + index] < 0x20U) return false;
    for (index = MB_OFF_TYPE; index < MB_OFF_TYPE + 8U; ++index)
        if (header[index] < 0x20U || header[index] > 0x7eU) return false;
    return true;
}

static void mb_stream_free(void *opaque) {
    mb_stream *stream = (mb_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    xx_mem_free(stream);
}

static bool mb_parse(Abstractformat *format, mb_stream **result) {
    uint8_t header[MB_HEADER_SIZE];
    mb_stream *stream;
    int64_t total, size;
    int64_t data_end, rsrc_end;
    uint32_t data_length, rsrc_length;
    uint8_t name_length, version_written;
    bool crc_expected;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < (int64_t)MB_HEADER_SIZE ||
        !mb_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;

    /* Structural invariants shared by every MacBinary generation. */
    if (header[MB_OFF_VERSION] != 0U || header[MB_OFF_ZERO_74] != 0U ||
        header[MB_OFF_ZERO_82] != 0U)
        return false;
    name_length = header[MB_OFF_NAME_LENGTH];
    if (name_length == 0U || name_length > MB_MAX_NAME) return false;

    data_length = mb_be32(header + MB_OFF_DATA_LENGTH);
    rsrc_length = mb_be32(header + MB_OFF_RSRC_LENGTH);
    if (data_length > MB_MAX_FORK || rsrc_length > MB_MAX_FORK) return false;
    /* Bound the declared fork lengths against the real file before they are
     * used for anything: the padded total must fit inside what exists. */
    data_end = (int64_t)MB_HEADER_SIZE + mb_round_up((int64_t)data_length);
    if (data_end > size) return false;
    rsrc_end = data_end + mb_round_up((int64_t)rsrc_length);
    if (rsrc_end > size) return false;

    version_written = header[MB_OFF_VERSION_WRITTEN];
    crc_expected = version_written >= 129U;
    if (crc_expected &&
        xx_crc16_xmodem_calc(0U, header, MB_OFF_CRC) !=
            mb_be16(header + MB_OFF_CRC)) {
        /* A MacBinary II header whose CRC does not verify is still accepted
         * only when nothing else in it is suspect: some writers left the
         * field stale.  Everything below is a hard requirement then. */
        if (mb_be16(header + MB_OFF_SECONDARY) != 0U ||
            header[MB_OFF_VERSION_NEEDED] > version_written)
            return false;
        crc_expected = false;
    } else if (!crc_expected) {
        /* MacBinary I has neither signature nor CRC, so the reserved areas
         * must be exactly as the standard describes them. */
        if (mb_be16(header + MB_OFF_SECONDARY) != 0U ||
            header[MB_OFF_VERSION_NEEDED] != 0U)
            return false;
    }
    if (!crc_expected && !mb_header_is_plausible(header, name_length))
        return false;

    stream = (mb_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->type = mb_be32(header + MB_OFF_TYPE);
    stream->creator = mb_be32(header + MB_OFF_CREATOR);
    stream->created = mb_be32(header + MB_OFF_CREATED);
    stream->modified = mb_be32(header + MB_OFF_MODIFIED);
    stream->finder_flags = header[MB_OFF_FINDER_FLAGS];
    stream->protected_flag = header[MB_OFF_PROTECTED];
    stream->version_written = version_written;
    stream->has_signature =
        xx_rt_memcmp(header + MB_OFF_SIGNATURE, "mBIN", 4U) == 0;
    stream->crc_verified = crc_expected;

    stream->items[stream->count].name =
        mb_normalize_name(header + MB_OFF_NAME, name_length, NULL);
    if (!stream->items[stream->count].name) {
        mb_stream_free(stream);
        return false;
    }
    stream->items[stream->count].data_offset =
        format->base_address + (int64_t)MB_HEADER_SIZE;
    stream->items[stream->count].data_size = (int64_t)data_length;
    stream->items[stream->count].resource = false;
    ++stream->count;

    if (rsrc_length != 0U) {
        stream->items[stream->count].name =
            mb_normalize_name(header + MB_OFF_NAME, name_length, ".rsrc");
        if (!stream->items[stream->count].name) {
            mb_stream_free(stream);
            return false;
        }
        stream->items[stream->count].data_offset =
            format->base_address + data_end;
        stream->items[stream->count].data_size = (int64_t)rsrc_length;
        stream->items[stream->count].resource = true;
        ++stream->count;
    }
    stream->archive_size = rsrc_end;
    *result = stream;
    return true;
}

static bool mb_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *mb_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mb_set_record(xx_archive_record *record, const mb_stream *stream,
                          const mb_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->data_offset - (int64_t)MB_HEADER_SIZE;
    record->header_size = (int64_t)MB_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    if (member->resource) record->header_offset = -1;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          stream->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          stream->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_macbinary_init(xx_macbinary *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_MACBINARY_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-macbinary");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_macbinary_check_is_valid;
    archive->format.handle_base_info = xx_macbinary_handle_base_info;
    archive->format.get_format_size = xx_macbinary_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_macbinary_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_macbinary_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_macbinary_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_macbinary_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_macbinary_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_macbinary_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_macbinary *xx_macbinary_create(xx_io_device *device, int64_t base_address) {
    xx_macbinary *archive = (xx_macbinary *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_macbinary_init(archive, device, base_address);
    return archive;
}

void xx_macbinary_destroy(xx_macbinary *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_macbinary_free(xx_macbinary *archive) {
    if (!archive) return;
    xx_macbinary_destroy(archive);
    xx_mem_free(archive);
}

bool xx_macbinary_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    mb_stream *stream;
    (void)pd;
    if (!mb_parse(format, &stream)) return false;
    mb_stream_free(stream);
    return true;
}

bool xx_macbinary_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    mb_stream *stream;
    xx_macbinary *archive;
    (void)pd;
    if (!format || !mb_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_macbinary *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->version = stream->version_written;
    archive->has_signature = stream->has_signature;
    archive->crc_verified = stream->crc_verified;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_MACBINARY_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    mb_stream_free(stream);
    return true;
}

int64_t xx_macbinary_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_macbinary_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_macbinary_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_macbinary_handle_base_info(format, pd))
               ? ((xx_macbinary *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_macbinary_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mb_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!mb_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!mb_copy_options(&state->options, options) ||
        !mb_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_macbinary_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_macbinary_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    mb_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mb_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = mb_set_record(&state->current_record, stream,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_macbinary_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    mb_stream *stream;
    const mb_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mb_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = mb_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        int64_t total = xx_io_total_size(format->device);
        return member->data_offset >= 0 && member->data_size >= 0 &&
               member->data_offset <= total &&
               member->data_size <= total - member->data_offset;
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
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset,
                                            member->data_size, path, pd);
done:
    if (!result && path) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_macbinary_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

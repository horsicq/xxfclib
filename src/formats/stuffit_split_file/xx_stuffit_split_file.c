/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * StuffIt split file: the output of StuffIt's "Segment" command, which cuts
 * one Mac file (usually a .sit or .sea archive) into pieces "name.1",
 * "name.2", ... that fit on floppies.  Every segment starts with the same
 * 100-byte header; the bytes after the headers, joined in part order, are
 * the file's resource fork followed by its data fork.
 *
 *   off  size  field
 *     0     2  B0 56
 *     2     2  part number, big endian, 1-based (the high byte is 0 in
 *              every known writer: fewer than 256 parts)
 *     4     1  file name length, 1..63
 *     5    63  file name, Mac Roman
 *    68     4  file type
 *    72     4  file creator
 *    76     2  Finder flags
 *    78     4  creation date, seconds since 1904
 *    82     4  modification date
 *    86     4  resource fork length (whole file)
 *    90     4  data fork length (whole file)
 *    94     6  per-segment bookkeeping, not needed to join
 *
 * Bytes 68..93 are identical in every segment of one set.  This reader
 * sees one device, so:
 *   - a first segment that holds both forks entirely (a one-segment set, or
 *     a joined set) yields the data fork as "name" and the resource fork as
 *     "name.rsrc", like the MacBinary reader;
 *   - any other segment yields its payload as one record "name.partN", so
 *     that concatenating the records of parts 1..N in order rebuilds the
 *     resource fork followed by the data fork.
 * The layout was written from the header description above; unar's
 * XADStuffItSplitParser (LGPL) was used only to confirm the field offsets
 * and as the extraction oracle.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stuffit_split_file/xx_stuffit_split_file.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef STUFFIT_SPLIT_FILE
#define XX_STUFFIT_SPLIT_FILE_FILE_TYPE XX_FILE_TYPE_STUFFIT_SPLIT_FILE
#else
#define XX_STUFFIT_SPLIT_FILE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SSF_HEADER_SIZE 100U
#define SSF_MAX_NAME 63U
#define SSF_OFF_PART 2U
#define SSF_OFF_NAME_LENGTH 4U
#define SSF_OFF_NAME 5U
#define SSF_OFF_TYPE 68U
#define SSF_OFF_CREATOR 72U
#define SSF_OFF_FINDER_FLAGS 76U
#define SSF_OFF_CREATED 78U
#define SSF_OFF_MODIFIED 82U
#define SSF_OFF_RSRC_LENGTH 86U
#define SSF_OFF_DATA_LENGTH 90U
/* An HFS fork is at most 2^31 - 1 bytes. */
#define SSF_MAX_FORK UINT32_C(0x7FFFFFFF)

typedef struct ssf_member_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    bool resource;
} ssf_member;

typedef struct ssf_stream_s {
    ssf_member items[2];
    size_t count;
    size_t index;
    int64_t header_offset;
    int64_t archive_size;
    uint32_t part;
    uint32_t type;
    uint32_t creator;
    uint32_t modified;
    uint32_t rsrc_length;
    uint32_t data_length;
    uint16_t finder_flags;
    bool complete;
} ssf_stream;

/* Mac OS Roman, 0x80..0xFF, as Unicode code points (Apple's ROMAN.TXT with
 * the Mac OS 8.5 euro sign at 0xDB). */
static const uint16_t k_ssf_mac_roman_high[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static uint32_t ssf_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t ssf_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static bool ssf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static size_t ssf_put_utf8(char *out, uint32_t code_point) {
    if (code_point < 0x80U) {
        out[0] = (char)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        out[0] = (char)(0xC0U | (code_point >> 6U));
        out[1] = (char)(0x80U | (code_point & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code_point >> 12U));
    out[1] = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code_point & 0x3FU));
    return 3U;
}

static char ssf_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ssf_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || ssf_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* Windows device names, with or without an extension, in any case and with
 * trailing spaces before the extension. */
static bool ssf_is_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ssf_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((ssf_upper(name[0]) == 'C' && ssf_upper(name[1]) == 'O' &&
             ssf_upper(name[2]) == 'M') ||
            (ssf_upper(name[0]) == 'L' && ssf_upper(name[1]) == 'P' &&
             ssf_upper(name[2]) == 'T'));
}

/* The Mac Roman name becomes one safe UTF-8 path component: separators,
 * wildcards and control codes become '_', trailing dots and spaces go (so
 * "." and ".." cannot survive), an empty result becomes "_" and a Windows
 * device name gets a '_' prefix.  @p suffix (at most 16 bytes) follows. */
static char *ssf_normalize_name(const uint8_t *bytes, size_t size,
                                const char *suffix) {
    size_t suffix_length = suffix ? xx_str_len(suffix) : 0U;
    size_t input, output = 0U;
    char *name;
    if (size > SSF_MAX_NAME || suffix_length > 16U) return NULL;
    name = (char *)xx_mem_alloc(1U + size * 3U + suffix_length + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU)
            name[output++] = '_';
        else if (c < 0x80U)
            name[output++] = (char)c;
        else
            output += ssf_put_utf8(name + output,
                                   k_ssf_mac_roman_high[c - 0x80U]);
    }
    while (output != 0U && (name[output - 1U] == ' ' ||
                            name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    if (ssf_is_device_name(name, output)) {
        xx_rt_memmove(name + 1U, name, output);
        name[0] = '_';
        ++output;
    }
    for (input = 0U; input < suffix_length; ++input)
        name[output++] = suffix[input];
    name[output] = 0;
    return name;
}

static void ssf_stream_free(void *opaque) {
    ssf_stream *stream = (ssf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    xx_mem_free(stream);
}

static bool ssf_add_member(ssf_stream *stream, const uint8_t *header,
                           const char *suffix, int64_t offset, int64_t size,
                           bool resource) {
    ssf_member *member;
    if (stream->count >= sizeof(stream->items) / sizeof(stream->items[0]))
        return false;
    member = &stream->items[stream->count];
    member->name = ssf_normalize_name(header + SSF_OFF_NAME,
                                      header[SSF_OFF_NAME_LENGTH], suffix);
    if (!member->name) return false;
    member->data_offset = offset;
    member->data_size = size;
    member->resource = resource;
    ++stream->count;
    return true;
}

/* Validates the segment header at the base address and, when @p result is
 * not NULL, builds the member list.  Everything is decided from the 100
 * header bytes and the device size, so a late probe costs one read. */
static bool ssf_parse(Abstractformat *format, ssf_stream **result) {
    uint8_t header[SSF_HEADER_SIZE];
    ssf_stream *stream;
    int64_t total, size, payload, need;
    uint32_t rsrc_length, data_length, part;
    uint8_t name_length;
    size_t index;
    bool complete;

    if (result) *result = NULL;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < (int64_t)SSF_HEADER_SIZE ||
        !ssf_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;

    if (header[0] != 0xB0U || header[1] != 0x56U) return false;
    part = ssf_be16(header + SSF_OFF_PART);
    if (part == 0U || part > 0xFFU) return false;
    name_length = header[SSF_OFF_NAME_LENGTH];
    if (name_length == 0U || name_length > SSF_MAX_NAME) return false;
    /* A Finder name holds no control codes; the one exception is the
     * custom-icon file "Icon\r", whose CR is last. */
    for (index = 0U; index < name_length; ++index) {
        uint8_t c = header[SSF_OFF_NAME + index];
        if (c == 0x0DU && index + 1U == name_length && index != 0U) continue;
        if (c < 0x20U) return false;
    }
    rsrc_length = ssf_be32(header + SSF_OFF_RSRC_LENGTH);
    data_length = ssf_be32(header + SSF_OFF_DATA_LENGTH);
    if (rsrc_length > SSF_MAX_FORK || data_length > SSF_MAX_FORK) return false;

    payload = size - (int64_t)SSF_HEADER_SIZE;
    need = (int64_t)rsrc_length + (int64_t)data_length; /* < 2^32 */
    complete = part == 1U && payload >= need;
    if (!complete) {
        /* A partial segment carries some of the forks and never more than
         * all of them: the first one, by the case above, strictly less. */
        if (payload <= 0 || payload > need) return false;
    }
    if (!result) return true;

    stream = (ssf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->header_offset = format->base_address;
    stream->part = part;
    stream->type = ssf_be32(header + SSF_OFF_TYPE);
    stream->creator = ssf_be32(header + SSF_OFF_CREATOR);
    stream->finder_flags = ssf_be16(header + SSF_OFF_FINDER_FLAGS);
    stream->modified = ssf_be32(header + SSF_OFF_MODIFIED);
    stream->rsrc_length = rsrc_length;
    stream->data_length = data_length;
    stream->complete = complete;

    if (complete) {
        int64_t rsrc_offset = format->base_address + (int64_t)SSF_HEADER_SIZE;
        int64_t data_offset = rsrc_offset + (int64_t)rsrc_length;
        /* The data fork is surfaced when present or when the file has no
         * fork at all, so that every valid set has at least one record. */
        if ((data_length != 0U || rsrc_length == 0U) &&
            !ssf_add_member(stream, header, NULL, data_offset,
                            (int64_t)data_length, false)) {
            ssf_stream_free(stream);
            return false;
        }
        if (rsrc_length != 0U &&
            !ssf_add_member(stream, header, ".rsrc", rsrc_offset,
                            (int64_t)rsrc_length, true)) {
            ssf_stream_free(stream);
            return false;
        }
        stream->archive_size = (int64_t)SSF_HEADER_SIZE + need;
    } else {
        char suffix[16];
        size_t at = 0U;
        suffix[at++] = '.';
        suffix[at++] = 'p';
        suffix[at++] = 'a';
        suffix[at++] = 'r';
        suffix[at++] = 't';
        if (part >= 100U) suffix[at++] = (char)('0' + part / 100U);
        if (part >= 10U) suffix[at++] = (char)('0' + (part / 10U) % 10U);
        suffix[at++] = (char)('0' + part % 10U);
        suffix[at] = 0;
        if (!ssf_add_member(stream, header, suffix,
                            format->base_address + (int64_t)SSF_HEADER_SIZE,
                            payload, false)) {
            ssf_stream_free(stream);
            return false;
        }
        stream->archive_size = size;
    }
    *result = stream;
    return true;
}

static bool ssf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ssf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ssf_set_record(xx_archive_record *record, const ssf_stream *stream,
                           const ssf_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->resource ? -1 : stream->header_offset;
    record->header_size = (int64_t)SSF_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
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

void xx_stuffit_split_file_init(xx_stuffit_split_file *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_STUFFIT_SPLIT_FILE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stuffit");
    xx_format_set_extension(&archive->format, "1");
    archive->format.check_is_valid = xx_stuffit_split_file_check_is_valid;
    archive->format.handle_base_info = xx_stuffit_split_file_handle_base_info;
    archive->format.get_format_size = xx_stuffit_split_file_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stuffit_split_file_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stuffit_split_file_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stuffit_split_file_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stuffit_split_file_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stuffit_split_file_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stuffit_split_file_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_stuffit_split_file *xx_stuffit_split_file_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_stuffit_split_file *archive =
        (xx_stuffit_split_file *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_stuffit_split_file_init(archive, device, base_address);
    return archive;
}

void xx_stuffit_split_file_destroy(xx_stuffit_split_file *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_stuffit_split_file_free(xx_stuffit_split_file *archive) {
    if (!archive) return;
    xx_stuffit_split_file_destroy(archive);
    xx_mem_free(archive);
}

bool xx_stuffit_split_file_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    (void)pd;
    return ssf_parse(format, NULL);
}

bool xx_stuffit_split_file_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    ssf_stream *stream;
    xx_stuffit_split_file *archive;
    (void)pd;
    if (!format || !ssf_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_stuffit_split_file *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->part_number = stream->part;
    archive->rsrc_length = stream->rsrc_length;
    archive->data_length = stream->data_length;
    archive->is_complete = stream->complete;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_STUFFIT_SPLIT_FILE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    ssf_stream_free(stream);
    return true;
}

int64_t xx_stuffit_split_file_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit_split_file_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_stuffit_split_file_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit_split_file_handle_base_info(format, pd))
               ? ((xx_stuffit_split_file *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_stuffit_split_file_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ssf_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!ssf_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ssf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ssf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!ssf_copy_options(&state->options, options) ||
        !ssf_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stuffit_split_file_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stuffit_split_file_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ssf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ssf_stream *)state->internal_state) ||
        stream->index >= stream->count || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ssf_set_record(&state->current_record, stream,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stuffit_split_file_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ssf_stream *stream;
    const ssf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ssf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = ssf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    /* The store helper deletes its own output on failure. */
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset,
                                            member->data_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_stuffit_split_file_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

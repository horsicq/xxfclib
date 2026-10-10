/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PPMd stream files (.pmd) written by Dmitry Shkarin's PPMd var.H / var.I
 * compressors.  xx_ppmd.h carries the field table.
 *
 * Written from the format's structure.  Header layout and the variant /
 * restore / name-length rules agree with 7-Zip's PpmdHandler.cpp (Igor
 * Pavlov, public domain), which was read for understanding only.
 *
 * Detection is the 4-byte signature plus the header fields a real file
 * always satisfies: variant H or I, order >= 2, a supported restore method,
 * a name of at most 512 printable bytes that fits in the file, and a first
 * code word the range decoder accepts.  No model memory is allocated for
 * detection.  handle_base_info then decodes each member (without writing)
 * to find where its stream ends, which is the only way to learn the packed
 * size, the unpacked size and whether another member follows.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ppmd/xx_ppmd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include "xx_ppmd_codec.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: picks up the real file type as soon as PPMD is
 * registered in xxfc_defs.h. */
#ifdef PPMD
#define XX_PPMD_FILE_TYPE XX_FILE_TYPE_PPMD
#else
#define XX_PPMD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PPMD_SIGNATURE UINT32_C(0x84ACAF8F)
#define PPMD_HEADER_SIZE 16U
#define PPMD_PAYLOAD_NAME "payload"
/* Room for a sanitized name plus a "_<n>" duplicate suffix. */
#define PPMD_OUT_NAME_CAP (XX_PPMD_MAX_NAME + 16U)

typedef struct ppmd_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;    /**< -1 when the stream end was not found. */
    uint64_t unpacked_size; /**< Valid when packed_size >= 0. */
    uint32_t attrib;
    uint32_t time;
    unsigned variant;
    unsigned order;
    unsigned memory_mb;
    unsigned restore;
    size_t name_length;
    char name[XX_PPMD_MAX_NAME + 1U];
} ppmd_member;

typedef struct ppmd_scan_s {
    ppmd_member *members;
    size_t count;
    size_t capacity;
    int64_t input_end;  /**< Device offset of the end of the input. */
    int64_t format_end; /**< Device offset one past the format. */
    bool sizes_known;
} ppmd_scan;

typedef struct ppmd_stream_s {
    ppmd_scan scan;
    size_t index;
    char **output_names; /**< NULL entry: the name is unsafe to create. */
    char **display_names;
} ppmd_stream;

/* ------------------------------------------------------------------------ */
/* Byte source shared with the stream decoders                              */
/* ------------------------------------------------------------------------ */

void xx_ppmdfile_source_init(xx_ppmdfile_source *source, xx_io_device *device, int64_t offset, int64_t limit, uint8_t *buffer)
{
    xx_mem_zero(source, sizeof(*source));
    source->device = device;
    source->next = offset;
    source->limit = limit;
    source->buffer = buffer;
}

uint8_t xx_ppmdfile_source_byte(xx_ppmdfile_source *source)
{
    size_t want, done = 0U;
    if (source->index < source->length) {
        ++source->consumed;
        return source->buffer[source->index++];
    }
    if (source->overrun || source->io_error) return 0;
    if (source->next >= source->limit) {
        source->overrun = true;
        return 0;
    }
    want = (source->limit - source->next) < (int64_t)XX_PPMDFILE_IN_BUFFER ? (size_t)(source->limit - source->next) : XX_PPMDFILE_IN_BUFFER;
    if (xx_io_seek64(source->device, source->next, SEEK_SET) != 0) {
        source->io_error = true;
        return 0;
    }
    while (done < want) {
        ssize_t amount = xx_io_read(source->device, source->buffer + done, want - done);
        if (amount <= 0 || (size_t)amount > want - done) {
            source->io_error = true;
            return 0;
        }
        done += (size_t)amount;
    }
    source->next += (int64_t)want;
    source->length = want;
    source->index = 1U;
    ++source->consumed;
    return source->buffer[0];
}

/* ------------------------------------------------------------------------ */
/* Header                                                                   */
/* ------------------------------------------------------------------------ */

static bool ppmd_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Parse and validate the member header at `offset`; the stream must have at
 * least its four initial code bytes before `end`. */
static bool ppmd_read_header(xx_io_device *device, int64_t offset, int64_t end, ppmd_member *member)
{
    uint8_t header[PPMD_HEADER_SIZE];
    uint8_t code[4];
    unsigned info, raw_length, index;
    if (offset < 0 || end - offset < (int64_t)PPMD_HEADER_SIZE + 4 || !ppmd_read_at(device, offset, header, sizeof(header))) return false;
    if (xx_data_get_u32(header, 4, 0, false) != PPMD_SIGNATURE) return false;
    info = xx_data_get_u16(header + 8U, 2, 0, false);
    raw_length = xx_data_get_u16(header + 10U, 2, 0, false);
    xx_mem_zero(member, sizeof(*member));
    member->variant = info >> 12U;
    member->order = (info & 0x0FU) + 1U;
    member->memory_mb = ((info >> 4U) & 0xFFU) + 1U;
    if (member->variant == XX_PPMD_METHOD_VAR_I) {
        member->restore = raw_length >> 14U;
        member->name_length = raw_length & 0x3FFFU;
        /* 2 (freeze) is not implemented by any decoder we can check. */
        if (member->restore > 1U) return false;
    } else if (member->variant == XX_PPMD_METHOD_VAR_H) {
        member->restore = 0U;
        member->name_length = raw_length;
    } else {
        return false;
    }
    if (member->order < 2U || member->name_length > XX_PPMD_MAX_NAME) return false;
    member->attrib = xx_data_get_u32(header + 4U, 4, 0, false);
    member->time = xx_data_get_u32(header + 12U, 4, 0, false);
    member->header_offset = offset;
    member->data_offset = offset + (int64_t)PPMD_HEADER_SIZE + (int64_t)member->name_length;
    member->packed_size = -1;
    if (end - member->data_offset < 4) return false;
    if (member->name_length != 0U && !ppmd_read_at(device, offset + (int64_t)PPMD_HEADER_SIZE, member->name, member->name_length)) return false;
    member->name[member->name_length] = 0;
    for (index = 0U; index < member->name_length; ++index) {
        unsigned char c = (unsigned char)member->name[index];
        if (c < 0x20U || c == 0x7FU) return false;
    }
    /* Both coders start with code = the first four bytes, and a code of
     * 0xFFFFFFFF cannot lie below any range. */
    if (!ppmd_read_at(device, member->data_offset, code, sizeof(code)) || xx_data_get_u32(code, 4, 0, false) == UINT32_C(0xFFFFFFFF)) return false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Decoding                                                                 */
/* ------------------------------------------------------------------------ */

static xx_ppmdfile_status ppmd_decode_member(xx_io_device *device, const ppmd_member *member, int64_t limit, xx_ppmdfile_write_fn write, void *write_context,
                                             uint64_t max_output, uint64_t *out_size, int64_t *out_packed, xx_pd_struct *pd)
{
    xx_ppmdfile_source source;
    xx_ppmdfile_status status;
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(XX_PPMDFILE_IN_BUFFER);
    uint32_t memory = (uint32_t)member->memory_mb << 20U;
    if (!buffer) return XX_PPMDFILE_NO_MEMORY;
    xx_ppmdfile_source_init(&source, device, member->data_offset, limit, buffer);
    if (member->variant == XX_PPMD_METHOD_VAR_H) status = xx_ppmdfile_decode_varh(&source, member->order, memory, write, write_context, max_output, out_size, pd);
    else status = xx_ppmdfile_decode_vari(&source, member->order, memory, member->restore, write, write_context, max_output, out_size, pd);
    if (out_packed) *out_packed = (int64_t)source.consumed;
    xx_mem_free(buffer);
    return status;
}

static void ppmd_scan_free(ppmd_scan *scan)
{
    if (scan->members) xx_mem_free(scan->members);
    scan->members = NULL;
    scan->count = scan->capacity = 0U;
}

static bool ppmd_scan_push(ppmd_scan *scan, const ppmd_member *member)
{
    if (scan->count == scan->capacity) {
        size_t grown = scan->capacity ? scan->capacity * 2U : 4U;
        ppmd_member *members;
        if (grown > XX_PPMD_MAX_MEMBERS) grown = XX_PPMD_MAX_MEMBERS;
        if (grown <= scan->count) return false;
        members = (ppmd_member *)(scan->members ? xx_mem_realloc(scan->members, grown * sizeof(*members)) : xx_mem_alloc(grown * sizeof(*members)));
        if (!members) return false;
        scan->members = members;
        scan->capacity = grown;
    }
    scan->members[scan->count++] = *member;
    return true;
}

/* Header check only (`measure` false), or the member walk. */
static bool ppmd_parse(Abstractformat *format, ppmd_scan *scan, bool measure, xx_pd_struct *pd)
{
    ppmd_member member;
    int64_t total, position;
    uint64_t output_budget = XX_PPMD_SCAN_MAX_OUTPUT;
    xx_mem_zero(scan, sizeof(*scan));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    if (!ppmd_read_header(format->device, format->base_address, total, &member)) return false;
    scan->input_end = total;
    scan->format_end = total;
    scan->sizes_known = false;
    if (!ppmd_scan_push(scan, &member)) return false;
    if (!measure) return true;
    if (total - member.data_offset > XX_PPMD_SCAN_MAX_PACKED) return true;

    for (;;) {
        ppmd_member *current = &scan->members[scan->count - 1U];
        uint64_t unpacked = 0U;
        int64_t packed = 0;
        xx_ppmdfile_status status;
        if (pd && xx_pd_is_stopped(pd)) break;
        status = ppmd_decode_member(format->device, current, total, NULL, NULL, output_budget + 1U, &unpacked, &packed, pd);
        /* Undecodable, or past the output budget: this member is published
         * as running to the end of the input. */
        if (status != XX_PPMDFILE_OK || unpacked > output_budget) break;
        current->packed_size = packed;
        current->unpacked_size = unpacked;
        output_budget -= unpacked;
        position = current->data_offset + packed;
        scan->format_end = position;
        if (position == total) {
            scan->sizes_known = true;
            break;
        }
        if (scan->count >= XX_PPMD_MAX_MEMBERS || !ppmd_read_header(format->device, position, total, &member)) {
            /* Trailing bytes that are not another member end the format. */
            scan->sizes_known = true;
            break;
        }
        if (!ppmd_scan_push(scan, &member)) break;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Names                                                                    */
/* ------------------------------------------------------------------------ */

static char ppmd_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ppmd_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || ppmd_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* One path component: not empty, not only dots and spaces, no reserved
 * punctuation, not a device name (CON, NUL, COM1, LPT1.TXT, CONIN$...). */
static bool ppmd_safe_component(const char *name, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' ||
            c == '/')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ppmd_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((ppmd_upper(name[0]) == 'C' && ppmd_upper(name[1]) == 'O' && ppmd_upper(name[2]) == 'M') ||
         (ppmd_upper(name[0]) == 'L' && ppmd_upper(name[1]) == 'P' && ppmd_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* The listed name: code-page bytes above 0x7E become '_', '\' becomes '/'. */
static void ppmd_display_name(const ppmd_member *member, char *out)
{
    size_t index;
    if (member->name_length == 0U) {
        xx_rt_memcpy(out, PPMD_PAYLOAD_NAME, sizeof(PPMD_PAYLOAD_NAME));
        return;
    }
    for (index = 0U; index < member->name_length; ++index) {
        unsigned char c = (unsigned char)member->name[index];
        out[index] = c > 0x7EU ? '_' : (c == '\\' ? '/' : (char)c);
    }
    out[member->name_length] = 0;
}

/* A relative path of safe components separated by single '/'. */
static bool ppmd_safe_path(const char *path)
{
    size_t start = 0U, index = 0U;
    if (!path[0] || path[0] == '/') return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!ppmd_safe_component(path + start, index - start)) return false;
            if (path[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

static bool ppmd_same_name(const char *a, const char *b)
{
    while (*a && *b) {
        if (ppmd_upper(*a) != ppmd_upper(*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void ppmd_append_number(char *out, size_t cap, size_t value)
{
    char digits[24];
    size_t count = 0U, length = xx_str_len(out);
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    if (length + 1U + count + 1U > cap) return;
    out[length++] = '_';
    while (count) out[length++] = digits[--count];
    out[length] = 0;
}

static void ppmd_stream_free(void *opaque)
{
    ppmd_stream *stream = (ppmd_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->scan.count; ++index) {
        if (stream->output_names && stream->output_names[index]) xx_mem_free(stream->output_names[index]);
        if (stream->display_names && stream->display_names[index]) xx_mem_free(stream->display_names[index]);
    }
    if (stream->output_names) xx_mem_free(stream->output_names);
    if (stream->display_names) xx_mem_free(stream->display_names);
    ppmd_scan_free(&stream->scan);
    xx_mem_free(stream);
}

static bool ppmd_build_names(ppmd_stream *stream)
{
    size_t count = stream->scan.count, index, other;
    stream->output_names = (char **)xx_mem_calloc(count, sizeof(char *));
    stream->display_names = (char **)xx_mem_calloc(count, sizeof(char *));
    if (!stream->output_names || !stream->display_names) return false;
    for (index = 0U; index < count; ++index) {
        char *display = (char *)xx_mem_alloc(PPMD_OUT_NAME_CAP);
        if (!display) return false;
        ppmd_display_name(&stream->scan.members[index], display);
        /* Keep members from overwriting each other. */
        for (other = 0U; other < index; ++other) {
            if (stream->display_names[other] && ppmd_same_name(stream->display_names[other], display)) {
                ppmd_append_number(display, PPMD_OUT_NAME_CAP, index + 1U);
                break;
            }
        }
        stream->display_names[index] = display;
        if (ppmd_safe_path(display)) {
            size_t length = xx_str_len(display);
            char *output = (char *)xx_mem_alloc(length + 1U);
            if (!output) return false;
            xx_rt_memcpy(output, display, length + 1U);
            stream->output_names[index] = output;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Options and records                                                      */
/* ------------------------------------------------------------------------ */

static bool ppmd_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *ppmd_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ppmd_set_record(xx_archive_record *record, const ppmd_stream *stream)
{
    const ppmd_member *member = &stream->scan.members[stream->index];
    bool known = member->packed_size >= 0;
    int64_t packed = known ? member->packed_size : stream->scan.input_end - member->data_offset;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)PPMD_HEADER_SIZE + (int64_t)member->name_length;
    record->data_offset = member->data_offset;
    record->compressed_size = packed;
    if (!xx_archive_record_set_original_name(record, stream->display_names[stream->index]) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->variant) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attrib) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE, member->time >> 16U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME, member->time & 0xFFFFU) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (known && (!xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)packed) ||
                  !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size)))
        return false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void xx_ppmd_init(xx_ppmd *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PPMD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ppmd");
    xx_format_set_extension(&archive->format, "pmd");
    archive->format.check_is_valid = xx_ppmd_check_is_valid;
    archive->format.handle_base_info = xx_ppmd_handle_base_info;
    archive->format.get_format_size = xx_ppmd_get_format_size;
    archive->format.get_number_of_archive_records = xx_ppmd_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_ppmd_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_ppmd_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_ppmd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_ppmd_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_ppmd_free_archive_records_reading;
}

xx_ppmd *xx_ppmd_create(xx_io_device *device, int64_t base_address)
{
    xx_ppmd *archive = (xx_ppmd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ppmd_init(archive, device, base_address);
    return archive;
}

void xx_ppmd_destroy(xx_ppmd *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ppmd_free(xx_ppmd *archive)
{
    if (!archive) return;
    xx_ppmd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ppmd_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    ppmd_scan scan;
    bool valid = ppmd_parse(format, &scan, false, pd);
    ppmd_scan_free(&scan);
    return valid;
}

bool xx_ppmd_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    ppmd_scan scan;
    xx_ppmd *archive;
    if (!format || !ppmd_parse(format, &scan, true, pd)) {
        if (format) ppmd_scan_free(&scan);
        return false;
    }
    archive = (xx_ppmd *)format;
    archive->number_of_records = scan.count;
    archive->variant = scan.members[0].variant;
    archive->order = scan.members[0].order;
    archive->memory_mb = scan.members[0].memory_mb;
    archive->restore = scan.members[0].restore;
    archive->sizes_known = scan.sizes_known;
    format->number_of_archive_records = scan.count;
    format->format_size = scan.format_end - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    ppmd_scan_free(&scan);
    return true;
}

int64_t xx_ppmd_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ppmd_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_ppmd_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ppmd_handle_base_info(format, pd)) ? ((xx_ppmd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ppmd_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    ppmd_stream *stream;
    xx_archive_record_state *state;
    stream = (ppmd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!ppmd_parse(format, &stream->scan, true, pd) || stream->scan.count == 0U || !ppmd_build_names(stream)) {
        ppmd_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ppmd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ppmd_stream_free;
    state->total_records = stream->scan.count;
    if (!ppmd_copy_options(&state->options, options) || !ppmd_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ppmd_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_ppmd_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ppmd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (ppmd_stream *)state->internal_state) || stream->index + 1U >= stream->scan.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!ppmd_set_record(&state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

static bool ppmd_write_device(void *context, const uint8_t *data, size_t size)
{
    xx_io_device *device = (xx_io_device *)context;
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Decode the current member; a measured member must end exactly where the
 * scan found its end. */
static bool ppmd_unpack_member(Abstractformat *format, const ppmd_stream *stream, xx_io_device *destination, xx_pd_struct *pd)
{
    const ppmd_member *member = &stream->scan.members[stream->index];
    int64_t limit = member->packed_size >= 0 ? member->data_offset + member->packed_size : stream->scan.input_end;
    uint64_t unpacked = 0U;
    int64_t packed = 0;
    xx_ppmdfile_status status = ppmd_decode_member(format->device, member, limit, destination ? ppmd_write_device : NULL, destination, 0U, &unpacked, &packed, pd);
    if (status != XX_PPMDFILE_OK) return false;
    if (member->packed_size >= 0 && (packed != member->packed_size || unpacked != member->unpacked_size)) return false;
    return true;
}

bool xx_ppmd_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ppmd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    const char *name;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ppmd_stream *)state->internal_state) || stream->index >= stream->scan.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = ppmd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ppmd_unpack_member(format, stream, NULL, pd);
    name = stream->output_names[stream->index];
    if (!name) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ppmd_unpack_member(format, stream, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ppmd_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

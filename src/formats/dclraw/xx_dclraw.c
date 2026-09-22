/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for a BARE PKWARE Data Compression Library stream -- the shape an
 * installer leaves behind when it strips every container field and keeps only
 * the compressed bytes (the FSB*.EX_ family and friends).  There is no header
 * beyond the two bytes the codec itself defines: byte 0 is the literal mode
 * (0 binary, 1 coded) and byte 1 the dictionary size in bits (4, 5 or 6).
 *
 * Two bytes of which one is nearly free is not a signature, so validity here
 * is decided by DECODING: the stream must run to its own end marker and must
 * consume the file exactly, to the last byte.  That is a far stronger test
 * than any header compare, and it is why the file is measured with
 * xx_dcl_scan_memory() before anything is allocated for the output.  Since
 * the format carries no name, the single record is named after the file.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dclraw/xx_dclraw.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef DCLRAW
#define XX_DCLRAW_FILE_TYPE XX_FILE_TYPE_DCLRAW
#else
#define XX_DCLRAW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DCLRAW_MAX_MEMBERS 1048576U

typedef struct dclraw_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;
    uint32_t crc;
    uint32_t dos_time;
    bool folder;
} dclraw_member;

typedef struct dclraw_stream_s {
    dclraw_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} dclraw_stream;

static uint16_t dclraw_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t dclraw_le32(const uint8_t *bytes) {
    return (uint32_t)dclraw_le16(bytes) | ((uint32_t)dclraw_le16(bytes + 2U) << 16U);
}

static bool dclraw_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Every name field in these containers is a fixed-width buffer whose tail is
 * uninitialised builder heap, so only the bytes before the first NUL are ever
 * surfaced, and separators and traversal components are made harmless. */
static char *dclraw_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U, limit = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    /* The field is fixed width and its tail is stale builder heap, so the
     * name ends at the first NUL and everything after it is discarded. */
    while (limit < size && bytes[limit] != 0U) ++limit;
    size = limit;
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
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
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

/* A member name that survives to the filesystem must be a plain relative
 * path; anything else makes the member invalid rather than renamed. */
static bool dclraw_safe_output_name(const char *name) {
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

/* The raw 8.3 fields of these DOS-era containers are the only evidence that a
 * candidate offset really is a header, so a byte that cannot appear in a name
 * rejects the file instead of being scrubbed. */
static bool dclraw_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void dclraw_stream_free(void *opaque) {
    dclraw_stream *stream = (dclraw_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool dclraw_add_member(dclraw_stream *stream, const dclraw_member *member) {
    dclraw_member *grown;
    if (!stream || !member || stream->count >= DCLRAW_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (dclraw_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define DCLRAW_MAX_STREAM (128 * 1024 * 1024)
#define DCLRAW_MAX_OUTPUT ((size_t)512U * 1024U * 1024U)

static bool dclraw_parse(Abstractformat *format, dclraw_stream **result) {
    uint8_t *packed = NULL;
    dclraw_stream *stream = NULL;
    dclraw_member member;
    int64_t total, size;
    size_t consumed = 0U, produced = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* Bound the whole file before it is read into memory: a raw stream has no
     * length field, so the file itself is the only bound there is. */
    if (size < 4 || size > DCLRAW_MAX_STREAM) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed) return false;
    if (!dclraw_read_at(format->device, format->base_address, packed,
                        (size_t)size) ||
        packed[0] > 1U || packed[1] < 4U || packed[1] > 6U)
        goto fail;
    /* The stream must decode to its end marker AND finish on the last byte of
     * the file; anything else is some other file that happens to start 00 06. */
    if (!xx_dcl_scan_memory(packed, (size_t)size, DCLRAW_MAX_OUTPUT, &consumed,
                            &produced) ||
        consumed != (size_t)size || produced == 0U)
        goto fail;
    xx_mem_free(packed);
    packed = NULL;
    stream = (dclraw_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = dclraw_normalize_name((const uint8_t *)"data", 4U);
    if (!member.name) {
        dclraw_stream_free(stream);
        return false;
    }
    member.header_offset = format->base_address;
    member.header_size = 2;
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = produced;
    if (!dclraw_add_member(stream, &member)) {
        xx_str_free(member.name);
        dclraw_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    return false;
}

static bool dclraw_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *dclraw_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dclraw_set_record(xx_archive_record *record,
                           const dclraw_member *member) {
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
                                          member->crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool dclraw_decode_member(Abstractformat *format,
                                 const dclraw_member *member, uint8_t **plain,
                                 size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U, output_size;
    if (!format || !member || !plain || !plain_size || member->packed_size < 2 ||
        member->unpacked_size == 0U || member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!packed || !output ||
        !dclraw_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size) ||
        !xx_dcl_decode_memory(packed, (size_t)member->packed_size, output,
                              output_size, &written) ||
        written != output_size)
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

void xx_dclraw_init(xx_dclraw *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DCLRAW_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pkware-dcl");
    xx_format_set_extension(&archive->format, "dcl");
    archive->format.check_is_valid = xx_dclraw_check_is_valid;
    archive->format.handle_base_info = xx_dclraw_handle_base_info;
    archive->format.get_format_size = xx_dclraw_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dclraw_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dclraw_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dclraw_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dclraw_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dclraw_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dclraw_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_dclraw *xx_dclraw_create(xx_io_device *device, int64_t base_address) {
    xx_dclraw *archive = (xx_dclraw *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dclraw_init(archive, device, base_address);
    return archive;
}

void xx_dclraw_destroy(xx_dclraw *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_dclraw_free(xx_dclraw *archive) {
    if (!archive) return;
    xx_dclraw_destroy(archive);
    xx_mem_free(archive);
}

bool xx_dclraw_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    dclraw_stream *stream;
    (void)pd;
    if (!dclraw_parse(format, &stream)) return false;
    dclraw_stream_free(stream);
    return true;
}

bool xx_dclraw_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    dclraw_stream *stream;
    xx_dclraw *archive;
    (void)pd;
    if (!format || !dclraw_parse(format, &stream)) return false;
    archive = (xx_dclraw *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    dclraw_stream_free(stream);
    return true;
}

int64_t xx_dclraw_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dclraw_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_dclraw_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dclraw_handle_base_info(format, pd))
               ? ((xx_dclraw *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_dclraw_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dclraw_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!dclraw_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dclraw_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dclraw_stream_free;
    state->total_records = stream->count;
    if (!dclraw_copy_options(&state->options, options) ||
        !dclraw_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_dclraw_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_dclraw_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    dclraw_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (dclraw_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = dclraw_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dclraw_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    dclraw_stream *stream;
    dclraw_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (dclraw_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!dclraw_safe_output_name(member->name) ||
        !dclraw_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = dclraw_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_dclraw_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

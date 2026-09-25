/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for the .SCF setup container shipped with the HSII/PrintPartner
 * installers.  The file opens with a uint32 whose value is 4 on every known
 * sample, then a chain of 26-byte member headers
 *   { char name[14]; uint32 packed; uint32 unpacked; uint16 dos_date;
 *     uint16 dos_time; }
 * each immediately followed by its packed bytes, with the next header sitting
 * exactly at header + 26 + packed.  Member payloads are raw PKWARE Data
 * Compression Library streams, so they start 00/01 followed by a 4..6
 * dictionary-size byte and are handed to the library's DCL decoder.  Layout
 * derived from the corpus in F:\ARC\ARC\SCF; the member chain has to close
 * exactly on the end of the file, which is what makes the weak leading uint32
 * safe to accept.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/scf/xx_scf.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef SCF
#define XX_SCF_FILE_TYPE XX_FILE_TYPE_SCF
#else
#define XX_SCF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SCF_MAX_MEMBERS 1048576U

typedef struct scf_member_s {
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
} scf_member;

typedef struct scf_stream_s {
    scf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} scf_stream;

static uint16_t scf_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t scf_le32(const uint8_t *bytes) {
    return (uint32_t)scf_le16(bytes) | ((uint32_t)scf_le16(bytes + 2U) << 16U);
}

static bool scf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *scf_normalize_name(const uint8_t *bytes, size_t size) {
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
static bool scf_safe_output_name(const char *name) {
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
static bool scf_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void scf_stream_free(void *opaque) {
    scf_stream *stream = (scf_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool scf_add_member(scf_stream *stream, const scf_member *member) {
    scf_member *grown;
    if (!stream || !member || stream->count >= SCF_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (scf_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define SCF_HEADER_SIZE 4U
#define SCF_ENTRY_SIZE 26U
#define SCF_NAME_SIZE 14U
#define SCF_SIGNATURE UINT32_C(4)

static bool scf_parse(Abstractformat *format, scf_stream **result) {
    uint8_t header[SCF_HEADER_SIZE];
    scf_stream *stream = NULL;
    int64_t total, size, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(SCF_HEADER_SIZE + SCF_ENTRY_SIZE) ||
        !scf_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        scf_le32(header) != SCF_SIGNATURE)
        return false;
    stream = (scf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = (int64_t)SCF_HEADER_SIZE;
    while (cursor < size) {
        uint8_t entry[SCF_ENTRY_SIZE];
        uint8_t probe[2];
        scf_member member;
        uint32_t packed, unpacked;
        if (size - cursor < (int64_t)SCF_ENTRY_SIZE ||
            !scf_read_at(format->device, format->base_address + cursor, entry,
                         sizeof(entry)))
            goto fail;
        if (!scf_plausible_raw_name(entry, SCF_NAME_SIZE)) goto fail;
        packed = scf_le32(entry + 14U);
        unpacked = scf_le32(entry + 18U);
        /* Bound the declared packed length against what is really left. */
        if ((int64_t)packed > size - cursor - (int64_t)SCF_ENTRY_SIZE)
            goto fail;
        if (packed < 2U) goto fail;
        if (!scf_read_at(format->device,
                         format->base_address + cursor + (int64_t)SCF_ENTRY_SIZE,
                         probe, sizeof(probe)))
            goto fail;
        /* Raw DCL prologue: literal mode 0/1, dictionary bits 4..6. */
        if (probe[0] > 1U || probe[1] < 4U || probe[1] > 6U) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = scf_normalize_name(entry, SCF_NAME_SIZE);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + cursor;
        member.header_size = (int64_t)SCF_ENTRY_SIZE;
        member.data_offset = member.header_offset + (int64_t)SCF_ENTRY_SIZE;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.dos_time = ((uint32_t)scf_le16(entry + 22U) << 16U) |
                          scf_le16(entry + 24U);
        if (!scf_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += (int64_t)SCF_ENTRY_SIZE + (int64_t)packed;
    }
    if (cursor != size || stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    scf_stream_free(stream);
    return false;
}

static bool scf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *scf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool scf_set_record(xx_archive_record *record,
                           const scf_member *member) {
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

static bool scf_decode_member(Abstractformat *format, const scf_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U, output_size;
    if (!format || !member || !plain || !plain_size || member->packed_size <= 0 ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        !scf_read_at(format->device, member->data_offset, packed,
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

void xx_scf_init(xx_scf *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SCF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-scf-setup");
    xx_format_set_extension(&archive->format, "scf");
    archive->format.check_is_valid = xx_scf_check_is_valid;
    archive->format.handle_base_info = xx_scf_handle_base_info;
    archive->format.get_format_size = xx_scf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_scf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_scf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_scf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_scf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_scf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_scf_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_scf *xx_scf_create(xx_io_device *device, int64_t base_address) {
    xx_scf *archive = (xx_scf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_scf_init(archive, device, base_address);
    return archive;
}

void xx_scf_destroy(xx_scf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_scf_free(xx_scf *archive) {
    if (!archive) return;
    xx_scf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_scf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    scf_stream *stream;
    (void)pd;
    if (!scf_parse(format, &stream)) return false;
    scf_stream_free(stream);
    return true;
}

bool xx_scf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    scf_stream *stream;
    xx_scf *archive;
    (void)pd;
    if (!format || !scf_parse(format, &stream)) return false;
    archive = (xx_scf *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    scf_stream_free(stream);
    return true;
}

int64_t xx_scf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_scf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_scf_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_scf_handle_base_info(format, pd))
               ? ((xx_scf *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_scf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    scf_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!scf_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        scf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = scf_stream_free;
    state->total_records = stream->count;
    if (!scf_copy_options(&state->options, options) ||
        !scf_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_scf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_scf_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    scf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (scf_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = scf_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_scf_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    scf_stream *stream;
    scf_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (scf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!scf_safe_output_name(member->name) ||
        !scf_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = scf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_scf_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

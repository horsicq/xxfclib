/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the GST Software installer container, as shipped with
 * Timeworks Publisher and the other GST DTP products.  The format carries no
 * directory: a file is a bare chain of 32-byte member headers, each one
 * immediately followed by its payload, and the chain has to land exactly on
 * the end of the file.
 *
 * The layout below was recovered from the sample corpus; no specification for
 * it is published.  Every field was cross-checked against 105 containers
 * (525 members):
 *
 *   0x00  u16  magic 0xC8E9 ('\xe9\xc8')
 *   0x02  u32  opaque stamp (constant per member, semantics unknown)
 *   0x06  u8   flags (0x00, 0x01 or 0x20 in the corpus)
 *   0x07  u8   method: 0 = stored, 1 = PKWARE DCL ("implode")
 *   0x08  u32  uncompressed size
 *   0x0C  u32  compressed size (payload bytes that follow the header)
 *   0x10  char name[12] (8.3, NUL padded, not terminated when full)
 *   0x1C  u32  CRC-32 of the decoded member
 *
 * The CRC-32 at 0x1C matched the decoded plaintext for all 525 members, which
 * is what pins both the codec and the member boundaries.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gst/xx_gst.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef GST
#define XX_GST_FILE_TYPE XX_FILE_TYPE_GST
#else
#define XX_GST_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GST_HEADER_SIZE 32U
#define GST_NAME_SIZE 12U
#define GST_MAX_MEMBERS 65536U
/* A 32-byte header must never be able to ask for an unbounded allocation. */
#define GST_MAX_UNPACKED_SIZE ((uint64_t)256U * 1024U * 1024U)

#define GST_METHOD_STORE 0U
#define GST_METHOD_DCL 1U

typedef struct gst_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t stamp;
    uint8_t flags;
    uint8_t method;
} gst_member;

typedef struct gst_stream_s {
    gst_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} gst_stream;

static uint16_t gst_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t gst_le32(const uint8_t *bytes) {
    return (uint32_t)gst_le16(bytes) | ((uint32_t)gst_le16(bytes + 2U) << 16U);
}

static bool gst_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* GST names are flat 8.3 ANSI strings; they never carry a path.  Keep the
 * bytes the packer wrote but make anything the filesystem would choke on
 * harmless, and refuse an entry that has no usable name left. */
static char *gst_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input, output = 0U;
    if (!bytes || size == 0U || size > SIZE_MAX - 1U) return NULL;
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c == 0U) break;
        if (c < 0x20U || c == '/' || c == '\\' || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) {
        xx_mem_free(name);
        return NULL;
    }
    name[output] = 0;
    return name;
}

static bool gst_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '.') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static void gst_stream_free(void *opaque) {
    gst_stream *stream = (gst_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool gst_add_member(gst_stream *stream, const gst_member *member) {
    gst_member *grown;
    if (!stream || !member || stream->count >= GST_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (gst_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool gst_parse(Abstractformat *format, gst_stream **result) {
    gst_stream *stream = NULL;
    int64_t total, size, cursor = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)GST_HEADER_SIZE) return false;
    stream = (gst_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (cursor < size) {
        uint8_t header[GST_HEADER_SIZE];
        gst_member member;
        uint32_t packed, unpacked;
        if (size - cursor < (int64_t)GST_HEADER_SIZE ||
            !gst_read_at(format->device, format->base_address + cursor, header,
                         sizeof(header)))
            goto fail;
        if (header[0] != 0xe9U || header[1] != 0xc8U) goto fail;
        unpacked = gst_le32(header + 8U);
        packed = gst_le32(header + 12U);
        /* Bound the declared payload against what the file actually holds
         * before it is used for anything at all. */
        if ((uint64_t)packed >
            (uint64_t)(size - cursor - (int64_t)GST_HEADER_SIZE))
            goto fail;
        if ((uint64_t)unpacked > GST_MAX_UNPACKED_SIZE) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.stamp = gst_le32(header + 2U);
        member.flags = header[6];
        member.method = header[7];
        member.unpacked_size = unpacked;
        member.packed_size = (int64_t)packed;
        member.crc32 = gst_le32(header + 28U);
        if (member.method != GST_METHOD_STORE &&
            member.method != GST_METHOD_DCL)
            goto fail;
        if (member.method == GST_METHOD_STORE &&
            (uint64_t)member.packed_size != member.unpacked_size)
            goto fail;
        member.name = gst_normalize_name(header + 16U, GST_NAME_SIZE);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + cursor;
        member.data_offset = member.header_offset + (int64_t)GST_HEADER_SIZE;
        if (!gst_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += (int64_t)GST_HEADER_SIZE + member.packed_size;
    }
    /* No trailer, no count: the chain landing exactly on EOF is the test. */
    if (cursor != size || stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    gst_stream_free(stream);
    return false;
}

static bool gst_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *gst_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool gst_set_record(xx_archive_record *record,
                           const gst_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)GST_HEADER_SIZE;
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
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->stamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool gst_decode_member(Abstractformat *format, const gst_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->packed_size < 0 ||
        member->unpacked_size > GST_MAX_UNPACKED_SIZE ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !gst_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)))
        goto fail;
    if (member->method == GST_METHOD_STORE) {
        if ((uint64_t)member->packed_size != member->unpacked_size) goto fail;
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else if (member->method == GST_METHOD_DCL) {
        decoded = xx_dcl_decode_memory(packed, (size_t)member->packed_size,
                                       output, output_size, &written);
    }
    /* The stored CRC-32 is the format's own assertion about the plaintext;
     * without it matching there is no way to tell a correct decode from a
     * plausible one. */
    if (!decoded || written != output_size ||
        xx_crc32_calc(0U, output, written) != member->crc32)
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

void xx_gst_init(xx_gst *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GST_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gst-archive");
    xx_format_set_extension(&archive->format, "gst");
    archive->format.check_is_valid = xx_gst_check_is_valid;
    archive->format.handle_base_info = xx_gst_handle_base_info;
    archive->format.get_format_size = xx_gst_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gst_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gst_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gst_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gst_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gst_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gst_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_gst *xx_gst_create(xx_io_device *device, int64_t base_address) {
    xx_gst *archive = (xx_gst *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gst_init(archive, device, base_address);
    return archive;
}

void xx_gst_destroy(xx_gst *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_gst_free(xx_gst *archive) {
    if (!archive) return;
    xx_gst_destroy(archive);
    xx_mem_free(archive);
}

bool xx_gst_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    gst_stream *stream;
    (void)pd;
    if (!gst_parse(format, &stream)) return false;
    gst_stream_free(stream);
    return true;
}

bool xx_gst_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    gst_stream *stream;
    xx_gst *archive;
    (void)pd;
    if (!format || !gst_parse(format, &stream)) return false;
    archive = (xx_gst *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    gst_stream_free(stream);
    return true;
}

int64_t xx_gst_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gst_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_gst_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gst_handle_base_info(format, pd))
               ? ((xx_gst *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_gst_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gst_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!gst_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gst_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gst_stream_free;
    state->total_records = stream->count;
    if (!gst_copy_options(&state->options, options) ||
        !gst_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_gst_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_gst_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    gst_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (gst_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = gst_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_gst_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    gst_stream *stream;
    gst_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gst_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!gst_safe_output_name(member->name) ||
        !gst_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = gst_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_gst_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Novell NetWare "BAGF" bag file: a 112-byte header naming one stored member,
 * then the member's bytes.  The signature and version test is U3's own
 * recognition predicate (FUN_006742a0); the field table and the "stored, not
 * compressed" finding are in xx_bagf.h.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bagf/xx_bagf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BAGF is registered there. */
#ifdef BAGF
#define XX_BAGF_FILE_TYPE XX_FILE_TYPE_BAGF
#else
#define XX_BAGF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct bagf_stream_s {
    char name[XX_BAGF_NAME_FIELD];
    char comment[XX_BAGF_COMMENT_FIELD];
    int64_t data_offset;
    int64_t data_size;
    uint32_t opaque_08;
    uint32_t opaque_20;
    uint32_t opaque_24;
    bool consumed;
} bagf_stream;

static uint32_t bagf_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool bagf_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/*
 * Read a length-prefixed string out of a fixed-width field.  The count byte
 * is bounded by the field, never trusted to run past it, and the result is
 * sanitised: the name becomes an output file name, so separators and the
 * dot-only shapes have to die here.
 */
static bool bagf_pascal(const uint8_t *field, size_t width, char *out,
                        bool as_name) {
    size_t length, index;
    if (width == 0U) return false;
    length = field[0];
    if (length == 0U || length > width - 1U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = field[1U + index];
        if (value < 0x20U || value >= 0x7fU) return false;
        if (as_name &&
            (value == '/' || value == '\\' || value == ':' || value == '<' ||
             value == '>' || value == '"' || value == '|' || value == '?' ||
             value == '*'))
            return false;
        out[index] = (char)value;
    }
    out[length] = 0;
    if (as_name && out[0] == '.' &&
        (length == 1U || (length == 2U && out[1] == '.')))
        return false;
    return true;
}

static void bagf_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* --------------------------------------------------------------- parse -- */

static bool bagf_parse(Abstractformat *format, bagf_stream **result,
                       xx_pd_struct *pd) {
    uint8_t header[XX_BAGF_HEADER_SIZE];
    bagf_stream *stream;
    int64_t total, span;
    uint32_t data_size;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < (int64_t)XX_BAGF_HEADER_SIZE) return false;
    if (!bagf_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, XX_BAGF_SIGNATURE, XX_BAGF_SIGNATURE_SIZE) != 0)
        return false;
    /* U3's version test, byte for byte. */
    if (header[4] != XX_BAGF_VERSION || header[5] != 0U) return false;
    if (bagf_le32(header + 8) == 0U ||
        (bagf_le32(header + 8) & 0x80000000U) != 0U)
        return false;

    data_size = bagf_le32(header + 0x28);
    /* The declared payload must fit in the real file before it is used. */
    if ((int64_t)data_size > span - (int64_t)XX_BAGF_HEADER_SIZE) return false;

    stream = (bagf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!bagf_pascal(header + XX_BAGF_NAME_OFFSET, XX_BAGF_NAME_FIELD,
                     stream->name, true)) {
        xx_mem_free(stream);
        return false;
    }
    /* The description is optional; an unusable one is simply not published. */
    if (!bagf_pascal(header + XX_BAGF_COMMENT_OFFSET, XX_BAGF_COMMENT_FIELD,
                     stream->comment, false))
        stream->comment[0] = 0;
    stream->data_offset = format->base_address + (int64_t)XX_BAGF_HEADER_SIZE;
    stream->data_size = (int64_t)data_size;
    stream->opaque_08 = bagf_le32(header + 8);
    stream->opaque_20 = bagf_le32(header + 0x20);
    stream->opaque_24 = bagf_le32(header + 0x24);
    stream->consumed = false;
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool bagf_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *bagf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool bagf_set_record(xx_archive_record *record,
                            const Abstractformat *format,
                            const bagf_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = XX_BAGF_HEADER_SIZE;
    record->data_offset = stream->data_offset;
    record->compressed_size = stream->data_size;
    if (!xx_archive_record_set_original_name(record, stream->name)) return false;
    if (stream->comment[0] &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        stream->comment))
        return false;
    return xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->data_size) &&
           /* Stored: the payload is the member, byte for byte. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_bagf_init(xx_bagf *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BAGF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-netware-bag");
    xx_format_set_extension(&archive->format, "xdc");
    xx_format_set_version(&archive->format, "2");
    archive->format.check_is_valid = xx_bagf_check_is_valid;
    archive->format.handle_base_info = xx_bagf_handle_base_info;
    archive->format.get_format_size = xx_bagf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bagf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bagf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bagf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bagf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bagf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bagf_free_archive_records_reading;
    archive->data_offset = -1;
    archive->data_size = -1;
}

xx_bagf *xx_bagf_create(xx_io_device *device, int64_t base_address) {
    xx_bagf *archive = (xx_bagf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bagf_init(archive, device, base_address);
    return archive;
}

void xx_bagf_destroy(xx_bagf *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bagf_free(xx_bagf *archive) {
    if (!archive) return;
    xx_bagf_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_bagf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    bagf_stream *stream;
    if (!bagf_parse(format, &stream, pd)) return false;
    bagf_stream_free(stream);
    return true;
}

bool xx_bagf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    bagf_stream *stream;
    xx_bagf *archive;
    int64_t total, archive_size;

    if (!format || !bagf_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_bagf *)format;
    archive->data_offset = stream->data_offset;
    archive->data_size = stream->data_size;
    archive->opaque_08 = stream->opaque_08;
    archive->opaque_20 = stream->opaque_20;
    archive->opaque_24 = stream->opaque_24;
    archive_size = (int64_t)XX_BAGF_HEADER_SIZE + stream->data_size;
    format->number_of_archive_records = 1U;
    format->format_size = archive_size;
    total = xx_io_total_size(format->device);
    if (total > format->base_address + archive_size) {
        format->overlay_offset = format->base_address + archive_size;
        format->overlay_size = total - format->overlay_offset;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    bagf_stream_free(stream);
    return true;
}

int64_t xx_bagf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bagf_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_bagf_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bagf_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_bagf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bagf_stream *stream;
    xx_archive_record_state *state;

    if (!bagf_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bagf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bagf_stream_free;
    state->total_records = 1;
    if (!bagf_copy_options(&state->options, options) ||
        !bagf_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bagf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bagf_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    bagf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bagf_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

bool xx_bagf_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    bagf_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (bagf_stream *)state->internal_state) || stream->consumed ||
        (pd && xx_pd_is_stopped(pd)))
        return false;

    plain = (uint8_t *)xx_mem_alloc(stream->data_size != 0
                                        ? (size_t)stream->data_size
                                        : 1U);
    if (!plain) goto done;
    if (stream->data_size != 0 &&
        !bagf_read_at(format->device, stream->data_offset, plain,
                      (size_t)stream->data_size))
        goto done;

    path_option = bagf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < (size_t)stream->data_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         (size_t)stream->data_size - written);
            if (amount <= 0 ||
                (size_t)amount > (size_t)stream->data_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bagf_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_bagf_get_data_offset(const xx_bagf *archive) {
    return archive ? archive->data_offset : -1;
}

int64_t xx_bagf_get_data_size(const xx_bagf *archive) {
    return archive ? archive->data_size : -1;
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The "DC"/"DL" single-member compressed-file container U3 labels LIF.  The
 * header test is U3's own recognition predicate (FUN_00555840), tightened
 * with the two structural identities that hold in the whole corpus.  The
 * payload codec (method 6) is NOT implemented: see xx_lif.h for what is and
 * is not known, and why the record is still published.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lif/xx_lif.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LIF is registered there. */
#ifdef LIF
#define XX_LIF_FILE_TYPE XX_FILE_TYPE_LIF
#else
#define XX_LIF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct lif_stream_s {
    char name[XX_LIF_NAME_SIZE + 1];
    int64_t packed_offset;
    int64_t packed_size;
    int64_t total_size;
    uint16_t dos_date;
    uint16_t dos_time;
    bool consumed;
} lif_stream;

static uint16_t lif_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t lif_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool lif_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* The member name reaches a caller as an output file name, so every
 * separator and both dot-only shapes have to die here. */
static bool lif_copy_name(const uint8_t *field, char *out) {
    size_t length = 0U, index;
    while (length < XX_LIF_NAME_SIZE && field[length] != 0U) ++length;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = field[index];
        if (value < 0x20U || value >= 0x7fU) return false;
        if (value == '/' || value == '\\' || value == ':' || value == '<' ||
            value == '>' || value == '"' || value == '|' || value == '?' ||
            value == '*')
            return false;
        out[index] = (char)value;
    }
    out[length] = 0;
    if (out[0] == '.' && (length == 1U || (length == 2U && out[1] == '.')))
        return false;
    return true;
}

static void lif_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* --------------------------------------------------------------- parse -- */

static bool lif_parse(Abstractformat *format, lif_stream **result,
                      xx_pd_struct *pd) {
    uint8_t header[XX_LIF_HEADER_SIZE];
    lif_stream *stream;
    int64_t total, span;
    uint32_t declared_total, method, packed_size;
    uint16_t magic;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span <= (int64_t)XX_LIF_HEADER_SIZE) return false;
    if (!lif_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;

    magic = lif_le16(header);
    if (magic != 0x4344U && magic != 0x4c44U) return false;
    if (lif_le16(header + 2) != XX_LIF_VERSION) return false;
    if (lif_le16(header + 4) == 0U) return false;

    declared_total = lif_le32(header + 0x15);
    method = lif_le32(header + 0x19);
    packed_size = lif_le32(header + 0x1d);
    if (declared_total == 0U || (declared_total & 0x80000000U) != 0U)
        return false;
    if (method != XX_LIF_METHOD) return false;
    if ((packed_size & 0x80000000U) != 0U) return false;
    if (lif_le32(header + 0x21) != 0U) return false;

    /* The two structural identities.  A two-byte magic is far too weak on its
     * own, and these are what make it safe: the declared total must cover the
     * whole of what was handed to the reader, and the packed extent must be
     * exactly what is left after the header. */
    if ((int64_t)declared_total != span) return false;
    if ((int64_t)packed_size != span - (int64_t)XX_LIF_HEADER_SIZE)
        return false;

    stream = (lif_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!lif_copy_name(header + 6, stream->name)) {
        xx_mem_free(stream);
        return false;
    }
    stream->packed_offset = format->base_address + (int64_t)XX_LIF_HEADER_SIZE;
    stream->packed_size = (int64_t)packed_size;
    stream->total_size = (int64_t)declared_total;
    stream->dos_date = lif_le16(header + 0x25);
    stream->dos_time = lif_le16(header + 0x27);
    stream->consumed = false;
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool lif_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool lif_set_record(xx_archive_record *record,
                           const Abstractformat *format,
                           const lif_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = XX_LIF_HEADER_SIZE;
    record->data_offset = stream->packed_offset;
    record->compressed_size = stream->packed_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->packed_size) &&
           /* The container stores no uncompressed size, and method 6 is not
            * decoded here, so none is published: reporting the packed size
            * would be a fabricated number. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_LIF_METHOD) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          stream->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          stream->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_lif_init(xx_lif *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LIF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "cmp");
    xx_format_set_version(&archive->format, "2");
    archive->format.check_is_valid = xx_lif_check_is_valid;
    archive->format.handle_base_info = xx_lif_handle_base_info;
    archive->format.get_format_size = xx_lif_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lif_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lif_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lif_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lif_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lif_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lif_free_archive_records_reading;
    archive->packed_offset = -1;
    archive->packed_size = -1;
}

xx_lif *xx_lif_create(xx_io_device *device, int64_t base_address) {
    xx_lif *archive = (xx_lif *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lif_init(archive, device, base_address);
    return archive;
}

void xx_lif_destroy(xx_lif *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lif_free(xx_lif *archive) {
    if (!archive) return;
    xx_lif_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_lif_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lif_stream *stream;
    if (!lif_parse(format, &stream, pd)) return false;
    lif_stream_free(stream);
    return true;
}

bool xx_lif_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lif_stream *stream;
    xx_lif *archive;

    if (!format || !lif_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_lif *)format;
    archive->packed_offset = stream->packed_offset;
    archive->packed_size = stream->packed_size;
    archive->dos_date = stream->dos_date;
    archive->dos_time = stream->dos_time;
    format->number_of_archive_records = 1U;
    /* The declared total was verified to equal what was handed to the reader,
     * so there is no overlay. */
    format->format_size = stream->total_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    lif_stream_free(stream);
    return true;
}

int64_t xx_lif_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_lif_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_lif_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lif_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_lif_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lif_stream *stream;
    xx_archive_record_state *state;

    if (!lif_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lif_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lif_stream_free;
    state->total_records = 1;
    if (!lif_copy_options(&state->options, options) ||
        !lif_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lif_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lif_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    lif_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lif_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

bool xx_lif_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    (void)format;
    (void)state;
    (void)pd;
    /* Method 6 is not identified.  A reader that refuses is worth more than
     * one that emits garbage, so this fails closed; the member's name, extent
     * and timestamp are still published by the record. */
    return false;
}

void xx_lif_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_lif_get_packed_offset(const xx_lif *archive) {
    return archive ? archive->packed_offset : -1;
}

int64_t xx_lif_get_packed_size(const xx_lif *archive) {
    return archive ? archive->packed_size : -1;
}

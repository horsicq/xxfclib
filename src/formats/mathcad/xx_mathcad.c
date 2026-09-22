/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MathSoft MathCAD packed worksheet.  A 15-byte ".MCDCOMPRESSION" banner
 * followed by one LZSS bit stream that runs to end of file.  Neither the
 * container nor the codec is documented; both were measured from the 593
 * corpus samples and the decoder was then verified byte-for-byte against all
 * of them.  xx_mathcad.h records the layout, the token encoding and the
 * evidence behind each field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mathcad/xx_mathcad.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as MATHCAD is registered there. */
#ifdef MATHCAD
#define XX_MATHCAD_FILE_TYPE XX_FILE_TYPE_MATHCAD
#else
#define XX_MATHCAD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The file stores no member name; one is synthesised for the single record. */
#define XX_MATHCAD_PAYLOAD_NAME "worksheet"
/* Size of the staging buffer used when flushing decoded bytes. */
#define XX_MATHCAD_FLUSH_SIZE 4096U

typedef struct xx_mathcad_parsed_s {
    int64_t total_size;    /**< Device size, the bound every field is checked
                            *   against. */
    int64_t packed_offset; /**< Absolute offset of the packed stream. */
    int64_t packed_size;   /**< Packed stream length, derived from the real
                            *   file size - the format declares none. */
} xx_mathcad_parsed;

static void xx_mathcad_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a worksheet can sit at an arbitrary
 * offset inside a larger dump, and long is 32-bit on Win64. */
static bool xx_mathcad_read_at(xx_io_device *device, int64_t offset,
                               void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_mathcad_write_all(xx_io_device *device, const void *data,
                                 size_t size, xx_pd_struct *pd) {
    size_t done = 0U;

    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------- lzss -- */

/* Most-significant-bit-first reader over the packed stream. */
typedef struct xx_mathcad_bits_s {
    const uint8_t *data;
    size_t size;
    size_t bit; /**< Next bit to consume, counted from the stream start. */
} xx_mathcad_bits;

/* Reads @p count bits, or reports exhaustion.  Every caller checks the
 * return: a truncated token is a broken stream, never a zero-filled one. */
static bool xx_mathcad_bits_get(xx_mathcad_bits *bits, unsigned count,
                                uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;

    if (!bits || !value || count > 32U) return false;
    if (bits->size > (size_t)-1 / 8U) return false;
    if (bits->bit + count > bits->size * 8U) return false;
    for (index = 0U; index < count; ++index) {
        uint8_t byte = bits->data[bits->bit >> 3];
        unsigned shift = 7U - (unsigned)(bits->bit & 7U);
        result = (result << 1) | (uint32_t)((byte >> shift) & 1U);
        ++bits->bit;
    }
    *value = result;
    return true;
}

/* Decodes the whole stream.  @p destination may be NULL to validate and
 * measure without producing output.  The 4096-byte ring is the only window a
 * match can reach, so the decoded bytes never have to be kept: output is
 * flushed as it is produced and the peak allocation is the packed stream plus
 * the ring. */
static bool xx_mathcad_lzss_decode(const uint8_t *packed, size_t packed_size,
                                   xx_io_device *destination,
                                   uint64_t limit, uint64_t *produced,
                                   xx_pd_struct *pd) {
    uint8_t ring[XX_MATHCAD_WINDOW_SIZE];
    uint8_t flush[XX_MATHCAD_FLUSH_SIZE];
    xx_mathcad_bits bits;
    size_t flush_used = 0U;
    /* The window write pointer starts at 1, not 0: measured, and the reason
     * a match position is one more than the output index it names. */
    uint32_t write = 1U;
    uint64_t total = 0U;
    bool ended = false;

    if (!packed || packed_size == 0U || !produced) return false;
    *produced = 0U;
    xx_mem_zero(ring, sizeof(ring));
    bits.data = packed;
    bits.size = packed_size;
    bits.bit = 0U;

    while (!ended) {
        uint32_t flag;
        uint32_t position;
        uint32_t length;
        uint32_t index;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_mathcad_bits_get(&bits, 1U, &flag)) break; /* padding ran out */
        if (flag != 0U) {
            uint32_t literal;
            if (!xx_mathcad_bits_get(&bits, 8U, &literal)) break;
            ring[write] = (uint8_t)literal;
            write = (write + 1U) & (XX_MATHCAD_WINDOW_SIZE - 1U);
            if (destination) flush[flush_used++] = (uint8_t)literal;
            ++total;
            if (total > limit) return false;
            if (flush_used == sizeof(flush)) {
                if (!xx_mathcad_write_all(destination, flush, flush_used, pd)) {
                    return false;
                }
                flush_used = 0U;
            }
            continue;
        }
        if (!xx_mathcad_bits_get(&bits, 12U, &position)) break;
        /* Position 0 is the end-of-stream marker and carries no length. */
        if (position == 0U) {
            ended = true;
            break;
        }
        if (!xx_mathcad_bits_get(&bits, 4U, &length)) break;
        length += 2U;
        for (index = 0U; index < length; ++index) {
            uint8_t byte = ring[(position + index) &
                                (XX_MATHCAD_WINDOW_SIZE - 1U)];
            ring[write] = byte;
            write = (write + 1U) & (XX_MATHCAD_WINDOW_SIZE - 1U);
            if (destination) flush[flush_used++] = byte;
            ++total;
            if (total > limit) return false;
            if (flush_used == sizeof(flush)) {
                if (!xx_mathcad_write_all(destination, flush, flush_used, pd)) {
                    return false;
                }
                flush_used = 0U;
            }
        }
    }

    if (!ended) return false; /* no end marker: truncated or not this codec */
    if (total == 0U) return false;
    if (destination && flush_used != 0U &&
        !xx_mathcad_write_all(destination, flush, flush_used, pd)) {
        return false;
    }
    *produced = total;
    return true;
}

/* Reads the packed extent and runs it through the decoder. */
static bool xx_mathcad_decode_stream(const xx_mathcad_parsed *parsed,
                                     xx_io_device *device,
                                     xx_io_device *destination,
                                     uint64_t *produced, xx_pd_struct *pd) {
    uint8_t *packed;
    uint64_t limit;
    bool result;

    if (!parsed || !device || !produced) return false;
    *produced = 0U;
    if (parsed->packed_size <= 0 ||
        parsed->packed_size > XX_MATHCAD_MAX_PACKED_SIZE ||
        (uint64_t)parsed->packed_size > (uint64_t)(size_t)-1) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)parsed->packed_size);
    if (!packed) return false;
    if (!xx_mathcad_read_at(device, parsed->packed_offset, packed,
                            (size_t)parsed->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    /* Ceiling derived from the coding, not guessed: no token produces more
     * than 8 bytes per input byte, so a small header cannot ask for a large
     * allocation or an unbounded loop. */
    limit = (uint64_t)parsed->packed_size * XX_MATHCAD_MAX_EXPANSION;
    result = xx_mathcad_lzss_decode(packed, (size_t)parsed->packed_size,
                                    destination, limit, produced, pd);
    xx_mem_free(packed);
    return result;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_mathcad_parse(Abstractformat *self, xx_mathcad_parsed *parsed,
                             xx_pd_struct *pd) {
    uint8_t header[XX_MATHCAD_HEADER_SIZE];
    int64_t span;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->total_size = -1;
        parsed->packed_offset = -1;
        parsed->packed_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }

    parsed->total_size = xx_io_total_size(self->device);
    if (parsed->total_size < 0 || parsed->total_size < self->base_address) {
        return false;
    }
    span = parsed->total_size - self->base_address;
    /* The banner plus at least one payload byte.  A worksheet with an empty
     * stream has never been seen and would decode to nothing. */
    if (span <= (int64_t)XX_MATHCAD_HEADER_SIZE) return false;

    if (!xx_mathcad_read_at(self->device, self->base_address, header,
                            sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, XX_MATHCAD_SIGNATURE,
                     XX_MATHCAD_SIGNATURE_SIZE) != 0) {
        return false;
    }

    parsed->packed_offset = self->base_address + (int64_t)XX_MATHCAD_HEADER_SIZE;
    /* Measured remainder of the file, not a declared field - the format
     * stores none.  span > HEADER_SIZE was checked above, so this is positive
     * and inside the device. */
    parsed->packed_size = span - (int64_t)XX_MATHCAD_HEADER_SIZE;
    return true;
}

/* ------------------------------------------------------------- options -- */

static bool xx_mathcad_copy_options(xx_list_s *destination,
                                    const xx_list_s *source) {
    size_t index;

    if (!destination || !source) return source == NULL;
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

static const xx_var *xx_mathcad_find_option(const xx_list_s *options,
                                            uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_mathcad_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_mathcad *document;

    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    document = (const xx_mathcad *)self;
    if (document->packed_offset < 0 || document->packed_size <= 0) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_MATHCAD_HEADER_SIZE;
    record->data_offset = document->packed_offset;
    record->compressed_size = document->packed_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_MATHCAD_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          document->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)document->packed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_mathcad_init(xx_mathcad *document, xx_io_device *device,
                     int64_t base_address) {
    if (!document) return;
    xx_mem_zero(document, sizeof(*document));
    xx_format_init(&document->format, device, base_address);
    document->format.endian = XX_ENDIAN_LITTLE;
    document->format.file_type = XX_MATHCAD_FILE_TYPE;
    document->format.format_type = XX_TYPE_ARCHIVE;
    document->format.is_archive = true;
    xx_format_set_mime_type(&document->format, "application/x-mathcad");
    xx_format_set_extension(&document->format, "mcd");
    document->format.check_is_valid = xx_mathcad_check_is_valid;
    document->format.handle_base_info = xx_mathcad_handle_base_info;
    document->format.get_format_size = xx_mathcad_get_format_size;
    document->format.get_number_of_archive_records =
        xx_mathcad_get_number_of_archive_records;
    document->format.create_archive_records_reading =
        xx_mathcad_create_archive_records_reading;
    document->format.get_current_archive_record =
        xx_mathcad_get_current_archive_record;
    document->format.unpack_current_archive_record =
        xx_mathcad_unpack_current_archive_record;
    document->format.archive_record_move_to_next =
        xx_mathcad_archive_record_move_to_next;
    document->format.free_archive_records_reading =
        xx_mathcad_free_archive_records_reading;
    document->format.destroy = xx_mathcad_vtable_destroy;
    document->packed_offset = -1;
    document->packed_size = -1;
    document->uncompressed_size = 0U;
    document->unpackable = false;
}

xx_mathcad *xx_mathcad_create(xx_io_device *device, int64_t base_address) {
    xx_mathcad *document = (xx_mathcad *)xx_mem_alloc(sizeof(*document));

    if (document) xx_mathcad_init(document, device, base_address);
    return document;
}

void xx_mathcad_destroy(xx_mathcad *document) {
    if (!document) return;
    if (document->format.close) document->format.close(&document->format);
    xx_format_cleanup_extra_parameters(&document->format);
    document->packed_offset = -1;
    document->packed_size = -1;
    document->uncompressed_size = 0U;
    document->unpackable = false;
}

static void xx_mathcad_vtable_destroy(Abstractformat *self) {
    xx_mathcad_destroy((xx_mathcad *)self);
}

void xx_mathcad_free(xx_mathcad *document) {
    if (!document) return;
    xx_mathcad_destroy(document);
    xx_mem_free(document);
}

/* -------------------------------------------------------------- format -- */

bool xx_mathcad_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mathcad_parsed parsed;

    return xx_mathcad_parse(self, &parsed, pd);
}

bool xx_mathcad_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mathcad *document = (xx_mathcad *)self;
    xx_mathcad_parsed parsed;
    uint64_t produced = 0U;

    if (!self) return false;
    if (!xx_mathcad_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    document->packed_offset = parsed.packed_offset;
    document->packed_size = parsed.packed_size;
    /* The stream declares no decoded length, so it is measured by decoding.
     * A stream that will not decode still leaves a recognised file: the
     * record is published with an unknown size and unpacking it fails. */
    document->unpackable = xx_mathcad_decode_stream(&parsed, self->device,
                                                    NULL, &produced, pd);
    document->uncompressed_size = document->unpackable ? produced : 0U;
    /* Banner plus payload, and the payload was measured to the end of the
     * device, so there is no overlay to report. */
    self->format_size = parsed.total_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_mathcad_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_mathcad_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

/* -------------------------------------------------------------- unpack -- */

bool xx_mathcad_unpack_to_device(xx_mathcad *document,
                                 xx_io_device *destination,
                                 xx_pd_struct *pd) {
    xx_mathcad_parsed parsed;
    uint64_t produced = 0U;

    if (!document ||
        (!document->format.base_info_handled &&
         !xx_format_handle_base_info(&document->format, pd)) ||
        !document->format.is_valid || !document->unpackable) {
        return false;
    }
    if (!xx_mathcad_parse(&document->format, &parsed, pd)) return false;
    if (!xx_mathcad_decode_stream(&parsed, document->format.device,
                                  destination, &produced, pd)) {
        return false;
    }
    return produced == document->uncompressed_size;
}

xx_archive_record_state *xx_mathcad_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_mathcad_copy_options(&state->options, options) ||
        !xx_mathcad_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_mathcad_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mathcad_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_mathcad_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_mathcad *document = (xx_mathcad *)self;
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    size_t path_length;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value =
        xx_mathcad_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify the stream decodes, produce nothing. */
        return xx_mathcad_unpack_to_device(document, NULL, pd);
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) return false;
    path_length = xx_str_len(base_path);
    if (path_length != 0U && base_path[path_length - 1U] != '/' &&
        base_path[path_length - 1U] != '\\') {
        destination_path =
            xx_str_concat3(base_path, "/", XX_MATHCAD_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_MATHCAD_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_mathcad_unpack_to_device(document, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_mathcad_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_mathcad_get_packed_offset(const xx_mathcad *document) {
    return document ? document->packed_offset : -1;
}

int64_t xx_mathcad_get_packed_size(const xx_mathcad *document) {
    return document ? document->packed_size : -1;
}

uint64_t xx_mathcad_get_uncompressed_size(const xx_mathcad *document) {
    return document ? document->uncompressed_size : 0U;
}

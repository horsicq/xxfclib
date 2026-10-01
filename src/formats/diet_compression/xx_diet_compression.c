/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DIET-compressed data files.  xx_diet_compression.h carries the layout.
 *
 * The header identification and the decoder's code tree follow Deark's
 * modules/diet.c (Copyright (C) 2023 Jason Summers, MIT license); the rest
 * (buffered input, streaming ring-buffer output, bounds, the archive API) is
 * original.
 *
 * Only the data-file variants are handled.  The DIET COM and EXE variants
 * are executable packers and are deliberately out of scope.
 *
 * Validation is a full trial decode: the stream has to reach the stop code
 * without a match reaching before the start of the output or overlapping
 * the bytes it produces (DIET never emits either), and for the 'dlz'
 * layouts the output has to be exactly the stored original length.  The
 * stored CRC covers the compressed bytes only; it is reported, not
 * enforced, like Deark does.
 *
 * Limits: the 'dlz' layouts bound the stream at 20 bits (1 MiB) and the
 * output at 22 bits (4 MiB); v1.00 stores neither, so its output is capped
 * at the same 4 MiB and the stream simply runs until the stop code.  Every
 * code produces at least one byte, so a decode is bounded by about 4 Mi
 * steps whatever the input.  Memory: an 8 KiB window, 4 KiB input and
 * 32 KiB output buffers.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/diet_compression/xx_diet_compression.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef DIET_COMPRESSION
#define XX_DIET_COMPRESSION_FILE_TYPE XX_FILE_TYPE_DIET_COMPRESSION
#else
#define XX_DIET_COMPRESSION_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DIET_PAYLOAD_NAME "payload"
#define DIET_WINDOW 8192U
#define DIET_MAX_OUTPUT ((uint32_t)4194304U)
#define DIET_IN_BUFFER 4096U
#define DIET_OUT_BUFFER 32768U
#define DIET_V100_HEADER 8
#define DIET_V102_HEADER 13
#define DIET_V144_HEADER 17
/* The smallest stream: one control word and the 0xFF of the stop code. */
#define DIET_MIN_STREAM 3

typedef struct diet_context_s {
    uint32_t version;
    int64_t header_size;
    int64_t stream_offset; /**< Absolute. */
    int64_t stream_end;    /**< Absolute read bound for the decoder. */
    int64_t stream_size;   /**< Declared ('dlz') or consumed (v1.00). */
    int64_t format_size;
    bool orig_known;
    uint32_t orig_len;
    uint16_t crc_stored;
    uint16_t crc_computed;
    uint64_t unpacked_size;
} diet_context;

typedef struct diet_stream_s {
    diet_context context;
    size_t index;
    size_t count;
} diet_stream;

/* ---- input ------------------------------------------------------------ */

typedef struct diet_input_s {
    xx_io_device *device;
    int64_t position; /**< Absolute offset of the next byte. */
    int64_t end;      /**< Absolute; no byte at or past it is read. */
    int64_t start;    /**< Absolute offset of buffer[0]. */
    size_t length;
    uint16_t crc;
    bool crc_on;
    uint8_t buffer[DIET_IN_BUFFER];
} diet_input;

static bool diet_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static uint16_t diet_crc16_arc(uint16_t crc, uint8_t value) {
    return xx_crc16_arc_calc(crc, &value, 1U);
}

static bool diet_input_byte(diet_input *in, uint8_t *out) {
    if (in->position >= in->end) return false;
    if (in->position < in->start ||
        in->position - in->start >= (int64_t)in->length) {
        int64_t available = in->end - in->position;
        size_t want = available < (int64_t)DIET_IN_BUFFER
                          ? (size_t)available : (size_t)DIET_IN_BUFFER;
        in->length = 0U;
        if (!diet_read_at(in->device, in->position, in->buffer, want))
            return false;
        in->start = in->position;
        in->length = want;
    }
    *out = in->buffer[(size_t)(in->position - in->start)];
    ++in->position;
    if (in->crc_on) in->crc = diet_crc16_arc(in->crc, *out);
    return true;
}

/* ---- decoder ---------------------------------------------------------- */

typedef struct diet_decoder_s {
    diet_input in;
    uint32_t bits;
    unsigned nbits;
    bool starved; /**< A control-word reload ran out of input. */
    bool failed;
    uint8_t window[DIET_WINDOW];
    uint32_t window_pos;
    uint64_t written;
    uint64_t limit; /**< Output above this is an error. */
    xx_io_device *sink;
    size_t out_length;
    uint8_t out[DIET_OUT_BUFFER];
} diet_decoder;

static bool diet_reload(diet_decoder *d) {
    uint8_t lo, hi;
    if (!diet_input_byte(&d->in, &lo) || !diet_input_byte(&d->in, &hi))
        return false;
    d->bits = (uint32_t)lo | ((uint32_t)hi << 8U);
    d->nbits = 16U;
    return true;
}

/* A control word is loaded the moment the previous one is used up, before
 * any inline byte that follows.  A reload that finds no input is only an
 * error if another bit or byte is then needed: a stream may end exactly on
 * a word boundary after its stop code. */
static unsigned diet_bit(diet_decoder *d) {
    unsigned value;
    if (d->failed) return 0U;
    if (d->starved || (d->nbits == 0U && !diet_reload(d))) {
        d->failed = true;
        return 0U;
    }
    value = d->bits & 1U;
    d->bits >>= 1U;
    if (--d->nbits == 0U && !diet_reload(d)) d->starved = true;
    return value;
}

static uint8_t diet_byte(diet_decoder *d) {
    uint8_t value = 0U;
    if (d->failed) return 0U;
    if (d->starved || !diet_input_byte(&d->in, &value)) {
        d->failed = true;
        return 0U;
    }
    return value;
}

static bool diet_flush(diet_decoder *d) {
    size_t done = 0U;
    if (!d->sink) {
        d->out_length = 0U;
        return true;
    }
    while (done < d->out_length) {
        ssize_t amount = xx_io_write(d->sink, d->out + done,
                                     d->out_length - done);
        if (amount <= 0 || (size_t)amount > d->out_length - done) return false;
        done += (size_t)amount;
    }
    d->out_length = 0U;
    return true;
}

static bool diet_emit(diet_decoder *d, uint8_t value) {
    if (d->written >= d->limit) return false;
    d->window[d->window_pos] = value;
    d->window_pos = (d->window_pos + 1U) & (DIET_WINDOW - 1U);
    ++d->written;
    if (d->sink) {
        d->out[d->out_length++] = value;
        if (d->out_length == DIET_OUT_BUFFER && !diet_flush(d)) return false;
    }
    return true;
}

static unsigned diet_match_length(diet_decoder *d) {
    unsigned count;
    unsigned x1, x2;
    for (count = 1U; count <= 4U; ++count)
        if (diet_bit(d)) return 2U + count;
    x1 = diet_bit(d);
    x2 = diet_bit(d);
    if (x1) return 7U + x2;
    if (!x2) {
        unsigned x3 = diet_bit(d);
        unsigned x4 = diet_bit(d);
        unsigned x5 = diet_bit(d);
        return 9U + 4U * x3 + 2U * x4 + x5;
    }
    return 17U + (unsigned)diet_byte(d);
}

/* Returns true at the stop code with every check passed. */
static bool diet_decode(diet_decoder *d, xx_pd_struct *pd) {
    uint64_t steps = 0U;
    for (;;) {
        unsigned position, length, v, a1, a2, a3, a4, a5, a6, a7, a8, i;
        uint32_t from;
        if (d->failed) return false;
        if ((++steps & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (diet_bit(d)) {
            uint8_t literal = diet_byte(d);
            if (d->failed || !diet_emit(d, literal)) return false;
            continue;
        }
        a1 = diet_bit(d); /* x2 */
        v = diet_byte(d);
        if (d->failed) return false;
        if (!a1) {
            a1 = diet_bit(d);
            if (a1) {
                a2 = diet_bit(d);
                a3 = diet_bit(d);
                a4 = diet_bit(d);
                position = 2303U - (1024U * a2 + 512U * a3 + 256U * a4 + v);
                length = 2U;
            } else if (v != 0xFFU) {
                position = 0xFFU - v;
                length = 2U;
            } else {
                /* 00 [FF] 0 is the stop code; 00 [FF] 1 is the EXE-only
                 * segment refresh, which a data file cannot contain. */
                a2 = diet_bit(d);
                if (d->failed || a2) return false;
                return true;
            }
        } else {
            a1 = diet_bit(d);
            a2 = diet_bit(d);
            if (a2) {
                position = 511U - (256U * a1 + v);
            } else if ((a3 = diet_bit(d)) != 0U) {
                position = 1023U - (256U * a1 + v);
            } else {
                a4 = diet_bit(d);
                a5 = diet_bit(d);
                if (a5) {
                    position = 2047U - (512U * a1 + 256U * a4 + v);
                } else {
                    a6 = diet_bit(d);
                    a7 = diet_bit(d);
                    if (a7) {
                        position = 4095U -
                                   (1024U * a1 + 512U * a4 + 256U * a6 + v);
                    } else {
                        a8 = diet_bit(d);
                        position = 8191U - (2048U * a1 + 1024U * a4 +
                                            512U * a6 + 256U * a8 + v);
                    }
                }
            }
            length = diet_match_length(d);
        }
        if (d->failed) return false;
        /* The distance is position + 1: it may not reach before the first
         * output byte, and DIET never overlaps a match with its own output. */
        if ((uint64_t)position + 1U > d->written || length > position + 1U)
            return false;
        from = (d->window_pos + DIET_WINDOW - 1U - position) &
               (DIET_WINDOW - 1U);
        for (i = 0U; i < length; ++i) {
            if (!diet_emit(d, d->window[(from + i) & (DIET_WINDOW - 1U)]))
                return false;
        }
    }
}

/* ---- header ----------------------------------------------------------- */

static const uint8_t diet_sig_int21[6] = {0xB4U, 0x4CU, 0xCDU, 0x21U,
                                          0x9DU, 0x89U};
static const uint8_t diet_sig_dlz[5] = {0x9DU, 0x89U, 'd', 'l', 'z'};

static bool diet_read_header(Abstractformat *format, diet_context *context) {
    uint8_t header[DIET_V144_HEADER];
    int64_t total, size;
    size_t have;
    const uint8_t *fields = NULL;
    xx_mem_zero(context, sizeof(*context));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < DIET_V100_HEADER + DIET_MIN_STREAM) return false;
    have = size < (int64_t)sizeof(header) ? (size_t)size : sizeof(header);
    if (!diet_read_at(format->device, format->base_address, header, have))
        return false;
    if (xx_rt_memcmp(header, diet_sig_dlz, sizeof(diet_sig_dlz)) == 0) {
        context->version = 102U;
        context->header_size = DIET_V102_HEADER;
        fields = header + 5;
    } else if (xx_rt_memcmp(header, diet_sig_int21, sizeof(diet_sig_int21)) ==
               0) {
        if (have >= 9U && header[6] == 'd' && header[7] == 'l' &&
            header[8] == 'z') {
            context->version = 144U;
            context->header_size = DIET_V144_HEADER;
            fields = header + 9;
        } else {
            context->version = 100U;
            context->header_size = DIET_V100_HEADER;
        }
    } else {
        return false;
    }
    if (size < context->header_size + DIET_MIN_STREAM) return false;
    context->stream_offset = format->base_address + context->header_size;
    if (fields) {
        int64_t declared;
        /* 0x80: "has following block" -- a multi-block file Deark does not
         * support either; the other flag bits concern EXE files. */
        if (fields[0] & 0x80U) return false;
        declared = ((int64_t)(fields[0] & 0x0FU) << 16) |
                   (int64_t)fields[1] | ((int64_t)fields[2] << 8);
        if (declared < DIET_MIN_STREAM ||
            declared > size - context->header_size)
            return false;
        context->stream_size = declared;
        context->stream_end = context->stream_offset + declared;
        context->crc_stored = (uint16_t)(fields[3] | (fields[4] << 8));
        context->orig_len = ((uint32_t)(fields[5] & 0xFCU) << 14) |
                            (uint32_t)fields[6] | ((uint32_t)fields[7] << 8);
        context->orig_known = true;
        context->format_size = context->header_size + declared;
    } else {
        context->crc_stored = (uint16_t)(header[6] | (header[7] << 8));
        context->stream_end = total;
    }
    return true;
}

/* Decode the stream once: to `sink` when given, else only to validate and
 * measure.  Fills in the consumed size (v1.00), the CRC and the output
 * length. */
static bool diet_run(Abstractformat *format, diet_context *context,
                     xx_io_device *sink, xx_pd_struct *pd) {
    diet_decoder *d;
    bool result = false;
    d = (diet_decoder *)xx_mem_alloc(sizeof(*d));
    if (!d) return false;
    xx_mem_zero(d, sizeof(*d));
    d->in.device = format->device;
    d->in.position = context->stream_offset;
    d->in.end = context->stream_end;
    d->in.crc_on = true;
    d->limit = context->orig_known ? context->orig_len : DIET_MAX_OUTPUT;
    d->sink = sink;
    if (!diet_decode(d, pd)) goto done;
    if (context->orig_known && d->written != (uint64_t)context->orig_len)
        goto done;
    if (!diet_flush(d)) goto done;
    if (!context->orig_known) {
        /* v1.00: the stream ends where the decoder stopped reading. */
        context->stream_size = d->in.position - context->stream_offset;
        context->format_size = context->header_size + context->stream_size;
        context->crc_computed = d->in.crc;
    } else {
        /* The CRC covers the whole declared stream. */
        uint8_t value;
        while (d->in.position < d->in.end)
            if (!diet_input_byte(&d->in, &value)) goto done;
        context->crc_computed = d->in.crc;
    }
    context->unpacked_size = d->written;
    result = true;
done:
    xx_mem_free(d);
    return result;
}

static bool diet_parse(Abstractformat *format, diet_context *out,
                       xx_pd_struct *pd) {
    diet_context context;
    if (!diet_read_header(format, &context) ||
        !diet_run(format, &context, NULL, pd))
        return false;
    *out = context;
    return true;
}

/* ---- archive API ------------------------------------------------------ */

static bool diet_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *diet_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool diet_set_record(xx_archive_record *record,
                            const diet_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset - context->header_size;
    record->header_size = context->header_size;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record, DIET_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void diet_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

void xx_diet_compression_init(xx_diet_compression *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DIET_COMPRESSION_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-diet");
    xx_format_set_extension(&archive->format, "dlz");
    archive->format.check_is_valid = xx_diet_compression_check_is_valid;
    archive->format.handle_base_info = xx_diet_compression_handle_base_info;
    archive->format.get_format_size = xx_diet_compression_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_diet_compression_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_diet_compression_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_diet_compression_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_diet_compression_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_diet_compression_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_diet_compression_free_archive_records_reading;
}

xx_diet_compression *xx_diet_compression_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_diet_compression *archive =
        (xx_diet_compression *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_diet_compression_init(archive, device, base_address);
    return archive;
}

void xx_diet_compression_destroy(xx_diet_compression *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_diet_compression_free(xx_diet_compression *archive) {
    if (!archive) return;
    xx_diet_compression_destroy(archive);
    xx_mem_free(archive);
}

bool xx_diet_compression_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    diet_context context;
    return diet_parse(format, &context, pd);
}

bool xx_diet_compression_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    diet_context context;
    xx_diet_compression *archive;
    if (!format || !diet_parse(format, &context, pd)) return false;
    archive = (xx_diet_compression *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->version = context.version;
    archive->crc_stored = context.crc_stored;
    archive->crc_computed = context.crc_computed;
    format->number_of_archive_records = 1U;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_diet_compression_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diet_compression_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_diet_compression_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diet_compression_handle_base_info(format, pd))
               ? ((xx_diet_compression *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_diet_compression_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    diet_stream *stream;
    xx_archive_record_state *state;
    diet_context context;
    if (!diet_parse(format, &context, pd)) return NULL;
    stream = (diet_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = diet_stream_free;
    state->total_records = 1U;
    if (!diet_copy_options(&state->options, options) ||
        !diet_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_diet_compression_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_diet_compression_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    diet_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (diet_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_diet_compression_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    diet_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (diet_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = diet_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the stream decodes and report that. */
        diet_context context;
        return diet_parse(format, &context, pd);
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
               ? xx_str_concat3(base, "/", DIET_PAYLOAD_NAME)
               : xx_str_concat(base, DIET_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        diet_context context = stream->context;
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = diet_run(format, &context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_diet_compression_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

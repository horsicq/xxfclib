/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quoted-Printable text (RFC 2045 section 6.7) without MIME headers.
 * xx_quoted_printable_encoded_fil.h carries the grammar and the acceptance
 * rules.
 *
 * Decoding follows RFC 2045: "=XY" is one byte, '=' followed by optional
 * transport whitespace and a line break is a soft line break, hard line
 * breaks are kept as written (LF stays LF, CRLF stays CRLF) and spaces or
 * tabs at the end of a line are dropped.  A malformed '=' sequence is kept
 * literally, as the RFC recommends.  None of the reference tools detects a
 * bare Quoted-Printable file (UUDeview only decodes it inside a MIME part
 * with "Content-Transfer-Encoding: quoted-printable"), so the detection
 * window and its rules are this reader's own.
 *
 * check_is_valid looks at no more than the first 64 KiB plus a 1 KiB padding
 * tail.  Binary input fails on its first byte, most prose and source code
 * on their first '=' that is not an escape, a trailing space or a line over
 * 78 characters.  The first 512 bytes are read into a stack buffer; only a
 * text still valid past them gets one heap buffer of
 * xx_get_file_buffer_size() bytes, freed before the scan returns.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/quoted_printable_encoded_fil/xx_quoted_printable_encoded_fil.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef QUOTED_PRINTABLE_ENCODED_FIL
#define XX_QUOTED_PRINTABLE_ENCODED_FIL_FILE_TYPE XX_FILE_TYPE_QUOTED_PRINTABLE_ENCODED_FIL
#else
#define XX_QUOTED_PRINTABLE_ENCODED_FIL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Bytes of text the acceptance rules are applied to. */
#define QP_WINDOW ((int64_t)64 * 1024)
/* 0x1A / 0x00 padding tolerated after a text that ends inside the window. */
#define QP_MAX_PAD ((int64_t)1024)
/* RFC 2045 caps encoded lines at 76 characters; Python's quopri and
 * binascii.b2a_qp write up to 78 when a line ends in an escaped space. */
#define QP_MAX_LINE 78U
/* Soft line breaks plus significant escapes the window must hold. */
#define QP_MIN_EVIDENCE 3U
#define QP_MIN_SIZE ((int64_t)8)
/* First chunk of a scan, read into a stack buffer. */
#define QP_FIRST_CHUNK 512U
/* Whitespace run held back while it may still turn out to be trailing. */
#define QP_WS_MAX 1024U
#define QP_PAYLOAD_NAME "payload"

typedef bool (*qp_sink)(void *context, const uint8_t *data, size_t size, int64_t offset);

static bool qp_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Hand the bytes of [start, limit) (relative to the base address) to `sink`
 * until it returns false.  False only on a read error, an allocation
 * failure or a stop request. */
static bool qp_iterate(Abstractformat *format, int64_t start, int64_t limit, qp_sink sink, void *context, xx_pd_struct *pd)
{
    uint8_t first[QP_FIRST_CHUNK];
    uint8_t *buffer = NULL;
    size_t capacity;
    int64_t position = start;
    bool result = true;
    if (position < 0 || limit < position) return false;
    if (position < (int64_t)QP_FIRST_CHUNK && position < limit) {
        int64_t end = limit < (int64_t)QP_FIRST_CHUNK ? limit : (int64_t)QP_FIRST_CHUNK;
        size_t want = (size_t)(end - position);
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!qp_read_at(format->device, format->base_address + position, first, want)) return false;
        if (!sink(context, first, want, position)) return true;
        position = end;
    }
    if (position >= limit) return true;
    capacity = xx_get_file_buffer_size();
    if (capacity == 0U) return false;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (position < limit) {
        int64_t left = limit - position;
        size_t want = (uint64_t)left < (uint64_t)capacity ? (size_t)left : capacity;
        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!qp_read_at(format->device, format->base_address + position, buffer, want)) {
            result = false;
            break;
        }
        if (!sink(context, buffer, want, position)) break;
        position += (int64_t)want;
    }
    xx_mem_free(buffer);
    return result;
}

static int qp_hex(uint8_t c, bool lower_ok)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (lower_ok && c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool qp_is_alnum(unsigned value)
{
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

/* ---------------------------------------------------------------------- */
/* Strict window check                                                     */

enum {
    QPC_TEXT = 0, /* inside a line */
    QPC_CR,       /* CR of a hard line break */
    QPC_EQ,       /* '=' seen */
    QPC_EQ_H1,    /* '=' and one hex digit seen */
    QPC_EQ_CR,    /* CR of a soft line break */
    QPC_PAD       /* 0x1A / 0x00 padding to EOF */
};

typedef struct qp_check_s {
    int state;
    unsigned line_length;
    unsigned high;
    uint64_t evidence;
    int64_t pad_start;  /**< Offset of the first padding byte, -1 = none. */
    bool last_ws;       /**< Last byte on the line was a space or tab. */
    bool escaped_ws;    /**< Last item on the line was "=20" or "=09". */
    bool eq_line_start; /**< The pending '=' opened its line. */
    bool rejected;
} qp_check;

static bool qp_check_reject(qp_check *check)
{
    check->rejected = true;
    return false;
}

static bool qp_check_line_end(qp_check *check)
{
    if (check->escaped_ws) ++check->evidence;
    check->escaped_ws = false;
    check->line_length = 0U;
    check->last_ws = false;
    return true;
}

static bool qp_check_feed(qp_check *check, uint8_t c, int64_t here)
{
    int value;
    switch (check->state) {
        case QPC_PAD: return (c == 0x1AU || c == 0x00U) ? true : qp_check_reject(check);
        case QPC_CR:
            if (c != '\n' || check->last_ws) return qp_check_reject(check);
            check->state = QPC_TEXT;
            return qp_check_line_end(check);
        case QPC_EQ_CR:
            if (c != '\n') return qp_check_reject(check);
            ++check->evidence;
            check->state = QPC_TEXT;
            return qp_check_line_end(check);
        case QPC_EQ:
            if (c == '\n') {
                ++check->evidence;
                check->state = QPC_TEXT;
                return qp_check_line_end(check);
            }
            if (c == '\r') {
                check->state = QPC_EQ_CR;
                return true;
            }
            value = qp_hex(c, false);
            if (value < 0) return qp_check_reject(check);
            check->high = (unsigned)value;
            if (++check->line_length > QP_MAX_LINE) return qp_check_reject(check);
            check->state = QPC_EQ_H1;
            return true;
        case QPC_EQ_H1: {
            unsigned byte;
            value = qp_hex(c, false);
            if (value < 0) return qp_check_reject(check);
            byte = (check->high << 4U) | (unsigned)value;
            if (qp_is_alnum(byte) && !(byte == 'F' && check->eq_line_start)) return qp_check_reject(check);
            if (++check->line_length > QP_MAX_LINE) return qp_check_reject(check);
            /* Evidence: '=', bytes 0x7F..0xFF, CR, LF; an escaped space or tab
             * only where it ends a line (decided on the next byte).  Other
             * control characters are neutral, so "X=10" in a key=value file
             * proves nothing. */
            if (byte == '=' || byte >= 0x7FU || byte == 0x0DU || byte == 0x0AU) ++check->evidence;
            check->escaped_ws = byte == 0x20U || byte == 0x09U;
            check->last_ws = false;
            check->state = QPC_TEXT;
            return true;
        }
        default: break;
    }
    /* QPC_TEXT */
    if (c == '\n') {
        if (check->last_ws) return qp_check_reject(check);
        return qp_check_line_end(check);
    }
    if (c == '\r') {
        check->state = QPC_CR;
        return true;
    }
    if (c == 0x1AU || c == 0x00U) {
        if (check->last_ws) return qp_check_reject(check);
        if (check->escaped_ws) ++check->evidence;
        check->escaped_ws = false;
        check->pad_start = here;
        check->state = QPC_PAD;
        return true;
    }
    if (c != '\t' && (c < 0x20U || c > 0x7EU)) return qp_check_reject(check);
    if (++check->line_length > QP_MAX_LINE) return qp_check_reject(check);
    check->escaped_ws = false;
    if (c == '=') {
        check->eq_line_start = check->line_length == 1U;
        check->last_ws = false;
        check->state = QPC_EQ;
        return true;
    }
    check->last_ws = c == ' ' || c == '\t';
    return true;
}

static bool qp_check_sink(void *context, const uint8_t *data, size_t size, int64_t offset)
{
    qp_check *check = (qp_check *)context;
    size_t index;
    for (index = 0U; index < size; ++index)
        if (!qp_check_feed(check, data[index], offset + (int64_t)index)) return false;
    return true;
}

/* The input ended right after the scanned bytes. */
static bool qp_check_eof(qp_check *check)
{
    switch (check->state) {
        case QPC_TEXT:
            if (check->escaped_ws) ++check->evidence;
            check->escaped_ws = false;
            return !check->last_ws;
        case QPC_EQ:
            ++check->evidence; /* soft line break at the end of the text */
            return true;
        case QPC_PAD: return true;
        default: return false;
    }
}

/* ---------------------------------------------------------------------- */
/* Text extent past the window                                             */

typedef struct qp_extent_s {
    int64_t end;
    bool found;
} qp_extent;

static bool qp_extent_sink(void *context, const uint8_t *data, size_t size, int64_t offset)
{
    qp_extent *extent = (qp_extent *)context;
    size_t index;
    for (index = 0U; index < size; ++index) {
        uint8_t c = data[index];
        if (c != '\t' && c != '\r' && c != '\n' && (c < 0x20U || c > 0x7EU)) {
            extent->end = offset + (int64_t)index;
            extent->found = true;
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Lenient decoder                                                         */

enum {
    QPD_TEXT = 0,
    QPD_CR,
    QPD_EQ,
    QPD_EQ_H1,
    QPD_EQ_WS,
    QPD_EQ_CR
};

typedef struct qp_decoder_s {
    int state;
    uint8_t high_char;
    uint8_t ws[QP_WS_MAX];
    size_t ws_count;
    uint8_t *output;
    size_t capacity;
    size_t used;
    xx_io_device *destination;
    uint64_t written;
    uint64_t escapes;
    uint64_t soft_breaks;
    bool failed;
} qp_decoder;

static bool qp_flush(qp_decoder *decoder)
{
    size_t done = 0U;
    if (decoder->destination) {
        while (done < decoder->used) {
            ssize_t amount = xx_io_write(decoder->destination, decoder->output + done, decoder->used - done);
            if (amount <= 0 || (size_t)amount > decoder->used - done) {
                decoder->failed = true;
                return false;
            }
            done += (size_t)amount;
        }
    }
    decoder->written += decoder->used;
    decoder->used = 0U;
    return true;
}

static bool qp_emit(qp_decoder *decoder, uint8_t byte)
{
    decoder->output[decoder->used++] = byte;
    return decoder->used < decoder->capacity || qp_flush(decoder);
}

static bool qp_emit_ws(qp_decoder *decoder)
{
    size_t index;
    for (index = 0U; index < decoder->ws_count; ++index)
        if (!qp_emit(decoder, decoder->ws[index])) return false;
    decoder->ws_count = 0U;
    return true;
}

static bool qp_decode_feed(qp_decoder *decoder, uint8_t c)
{
    int value;
    for (;;) {
        switch (decoder->state) {
            case QPD_CR:
                decoder->state = QPD_TEXT;
                if (c == '\n') {
                    decoder->ws_count = 0U;
                    return qp_emit(decoder, '\r') && qp_emit(decoder, '\n');
                }
                /* A lone CR is an ordinary character. */
                if (!qp_emit_ws(decoder) || !qp_emit(decoder, '\r')) return false;
                continue;
            case QPD_EQ:
                if (c == '\n') {
                    ++decoder->soft_breaks;
                    decoder->state = QPD_TEXT;
                    return true;
                }
                if (c == '\r') {
                    decoder->state = QPD_EQ_CR;
                    return true;
                }
                if (c == ' ' || c == '\t') {
                    decoder->ws[0] = c;
                    decoder->ws_count = 1U;
                    decoder->state = QPD_EQ_WS;
                    return true;
                }
                if (qp_hex(c, true) >= 0) {
                    decoder->high_char = c;
                    decoder->state = QPD_EQ_H1;
                    return true;
                }
                decoder->state = QPD_TEXT;
                if (!qp_emit(decoder, '=')) return false;
                continue;
            case QPD_EQ_H1:
                value = qp_hex(c, true);
                decoder->state = QPD_TEXT;
                if (value >= 0) {
                    ++decoder->escapes;
                    return qp_emit(decoder, (uint8_t)((qp_hex(decoder->high_char, true) << 4) | value));
                }
                if (!qp_emit(decoder, '=') || !qp_emit(decoder, decoder->high_char)) return false;
                continue;
            case QPD_EQ_WS:
                if (c == '\n') {
                    ++decoder->soft_breaks;
                    decoder->ws_count = 0U;
                    decoder->state = QPD_TEXT;
                    return true;
                }
                if (c == '\r') {
                    decoder->ws_count = 0U;
                    decoder->state = QPD_EQ_CR;
                    return true;
                }
                if ((c == ' ' || c == '\t') && decoder->ws_count < QP_WS_MAX) {
                    decoder->ws[decoder->ws_count++] = c;
                    return true;
                }
                /* Not a soft line break after all: '=' and the run are text. */
                decoder->state = QPD_TEXT;
                if (!qp_emit(decoder, '=') || !qp_emit_ws(decoder)) return false;
                continue;
            case QPD_EQ_CR:
                /* "=" CRLF, or "=" and a lone CR: a soft line break. */
                ++decoder->soft_breaks;
                decoder->state = QPD_TEXT;
                if (c == '\n') return true;
                continue;
            default: break;
        }
        /* QPD_TEXT */
        if (c == ' ' || c == '\t') {
            if (decoder->ws_count == QP_WS_MAX && !qp_emit_ws(decoder)) return false;
            decoder->ws[decoder->ws_count++] = c;
            return true;
        }
        if (c == '\n') {
            decoder->ws_count = 0U;
            return qp_emit(decoder, '\n');
        }
        if (c == '\r') {
            decoder->state = QPD_CR;
            return true;
        }
        if (!qp_emit_ws(decoder)) return false;
        if (c == '=') {
            decoder->state = QPD_EQ;
            return true;
        }
        return qp_emit(decoder, c);
    }
}

static bool qp_decode_sink(void *context, const uint8_t *data, size_t size, int64_t offset)
{
    qp_decoder *decoder = (qp_decoder *)context;
    size_t index;
    (void)offset;
    for (index = 0U; index < size; ++index)
        if (!qp_decode_feed(decoder, data[index])) return false;
    return true;
}

static bool qp_decode_eof(qp_decoder *decoder)
{
    switch (decoder->state) {
        case QPD_CR:
            /* Trailing whitespace before a final lone CR is dropped. */
            decoder->ws_count = 0U;
            if (!qp_emit(decoder, '\r')) return false;
            break;
        case QPD_EQ:
        case QPD_EQ_WS:
        case QPD_EQ_CR: ++decoder->soft_breaks; break;
        case QPD_EQ_H1:
            if (!qp_emit(decoder, '=') || !qp_emit(decoder, decoder->high_char)) return false;
            break;
        default: break;
    }
    decoder->ws_count = 0U;
    return decoder->used == 0U || qp_flush(decoder);
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */

typedef struct qp_context_s {
    int64_t text_size;
    int64_t format_size;
    uint64_t unpacked_size;
    uint64_t escapes;
    uint64_t soft_breaks;
} qp_context;

/* Decode [0, text_size) to `destination` (NULL: count only). */
static bool qp_decode(Abstractformat *format, int64_t text_size, xx_io_device *destination, qp_context *counts, xx_pd_struct *pd)
{
    qp_decoder *decoder;
    bool result;
    size_t capacity = xx_get_file_buffer_size();
    if (capacity == 0U) return false;
    decoder = (qp_decoder *)xx_mem_calloc(1U, sizeof(*decoder));
    if (!decoder) return false;
    decoder->output = (uint8_t *)xx_mem_alloc(capacity);
    if (!decoder->output) {
        xx_mem_free(decoder);
        return false;
    }
    decoder->capacity = capacity;
    decoder->destination = destination;
    result = qp_iterate(format, 0, text_size, qp_decode_sink, decoder, pd) && !decoder->failed && qp_decode_eof(decoder) && !decoder->failed;
    if (result && counts) {
        counts->unpacked_size = decoder->written;
        counts->escapes = decoder->escapes;
        counts->soft_breaks = decoder->soft_breaks;
    }
    xx_mem_free(decoder->output);
    xx_mem_free(decoder);
    return result;
}

/* Apply the window rules; with `measure`, also find the text's extent and
 * decoded size. */
static bool qp_parse(Abstractformat *format, qp_context *out, bool measure, xx_pd_struct *pd)
{
    qp_check check;
    qp_context context;
    int64_t total, size, window;
    uint8_t first;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < QP_MIN_SIZE) return false;
    /* Cheap early exit for the late probe. */
    if (!qp_read_at(format->device, format->base_address, &first, 1U) || (first != '\t' && first != '\r' && first != '\n' && (first < 0x20U || first > 0x7EU)))
        return false;

    xx_mem_zero(&check, sizeof(check));
    check.pad_start = -1;
    window = size < QP_WINDOW ? size : QP_WINDOW;
    if (!qp_iterate(format, 0, window, qp_check_sink, &check, pd) || check.rejected) return false;
    if (check.state == QPC_PAD && window < size) {
        /* The text ended inside the window: padding must reach EOF soon. */
        if (size - check.pad_start > QP_MAX_PAD) return false;
        if (!qp_iterate(format, window, size, qp_check_sink, &check, pd) || check.rejected) return false;
    }
    if (window == size || check.state == QPC_PAD) {
        if (!qp_check_eof(&check)) return false;
    }
    if (check.evidence < QP_MIN_EVIDENCE) return false;

    xx_mem_zero(&context, sizeof(context));
    if (measure) {
        if (check.state == QPC_PAD) {
            context.text_size = check.pad_start;
            context.format_size = size;
        } else if (window == size) {
            context.text_size = context.format_size = size;
        } else {
            qp_extent extent;
            extent.end = size;
            extent.found = false;
            if (!qp_iterate(format, window, size, qp_extent_sink, &extent, pd)) return false;
            context.text_size = context.format_size = extent.end;
        }
        if (context.text_size <= 0 || context.text_size > size || context.format_size < context.text_size || context.format_size > size) return false;
        if (!qp_decode(format, context.text_size, NULL, &context, pd)) return false;
    }
    *out = context;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct qp_stream_s {
    qp_context context;
    size_t index;
    size_t count;
} qp_stream;

static bool qp_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *qp_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool qp_set_record(Abstractformat *format, xx_archive_record *record, const qp_context *context)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = 0;
    record->data_offset = format->base_address;
    record->compressed_size = context->text_size;
    return xx_archive_record_set_original_name(record, QP_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)context->text_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void qp_stream_free(void *opaque)
{
    if (opaque) xx_mem_free(opaque);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_quoted_printable_encoded_fil_init(xx_quoted_printable_encoded_fil *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QUOTED_PRINTABLE_ENCODED_FIL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/plain");
    xx_format_set_extension(&archive->format, "qp");
    archive->format.check_is_valid = xx_quoted_printable_encoded_fil_check_is_valid;
    archive->format.handle_base_info = xx_quoted_printable_encoded_fil_handle_base_info;
    archive->format.get_format_size = xx_quoted_printable_encoded_fil_get_format_size;
    archive->format.get_number_of_archive_records = xx_quoted_printable_encoded_fil_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_quoted_printable_encoded_fil_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_quoted_printable_encoded_fil_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_quoted_printable_encoded_fil_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_quoted_printable_encoded_fil_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_quoted_printable_encoded_fil_free_archive_records_reading;
}

xx_quoted_printable_encoded_fil *xx_quoted_printable_encoded_fil_create(xx_io_device *device, int64_t base_address)
{
    xx_quoted_printable_encoded_fil *archive = (xx_quoted_printable_encoded_fil *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_quoted_printable_encoded_fil_init(archive, device, base_address);
    return archive;
}

void xx_quoted_printable_encoded_fil_destroy(xx_quoted_printable_encoded_fil *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_quoted_printable_encoded_fil_free(xx_quoted_printable_encoded_fil *archive)
{
    if (!archive) return;
    xx_quoted_printable_encoded_fil_destroy(archive);
    xx_mem_free(archive);
}

bool xx_quoted_printable_encoded_fil_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    qp_context context;
    return qp_parse(format, &context, false, pd);
}

bool xx_quoted_printable_encoded_fil_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    qp_context context;
    xx_quoted_printable_encoded_fil *archive;
    if (!format || !qp_parse(format, &context, true, pd)) return false;
    archive = (xx_quoted_printable_encoded_fil *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->escape_count = context.escapes;
    archive->soft_breaks = context.soft_breaks;
    archive->text_size = context.text_size;
    archive->format_size_all = context.format_size;
    xx_rt_memcpy(archive->name, QP_PAYLOAD_NAME, sizeof(QP_PAYLOAD_NAME));
    format->number_of_archive_records = 1U;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_quoted_printable_encoded_fil_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_quoted_printable_encoded_fil_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_quoted_printable_encoded_fil_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_quoted_printable_encoded_fil_handle_base_info(format, pd))
               ? ((xx_quoted_printable_encoded_fil *)format)->number_of_records
               : 0U;
}

uint64_t xx_quoted_printable_encoded_fil_get_unpacked_size(xx_quoted_printable_encoded_fil *archive)
{
    if (!archive) return 0U;
    if (!archive->format.base_info_handled && !xx_quoted_printable_encoded_fil_handle_base_info(&archive->format, NULL)) return 0U;
    return archive->unpacked_size;
}

bool xx_quoted_printable_encoded_fil_unpack_to_device(xx_quoted_printable_encoded_fil *archive, xx_io_device *destination, xx_pd_struct *pd)
{
    qp_context context;
    if (!archive || !destination || !qp_parse(&archive->format, &context, true, pd)) return false;
    return qp_decode(&archive->format, context.text_size, destination, NULL, pd);
}

xx_archive_record_state *xx_quoted_printable_encoded_fil_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    qp_stream *stream;
    xx_archive_record_state *state;
    qp_context context;
    if (!qp_parse(format, &context, true, pd)) return NULL;
    stream = (qp_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = qp_stream_free;
    state->total_records = 1U;
    if (!qp_copy_options(&state->options, options) || !qp_set_record(format, &state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_quoted_printable_encoded_fil_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_quoted_printable_encoded_fil_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    qp_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (qp_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_quoted_printable_encoded_fil_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    qp_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (qp_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = qp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the text still decodes. */
        return qp_decode(format, stream->context.text_size, NULL, NULL, pd);
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    {
        size_t length = xx_str_len(base);
        bool separator = length != 0U && base[length - 1U] != '/' && base[length - 1U] != '\\';
        path = separator ? xx_str_concat3(base, "/", QP_PAYLOAD_NAME) : xx_str_concat(base, QP_PAYLOAD_NAME);
    }
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = qp_decode(format, stream->context.text_size, destination, NULL, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_quoted_printable_encoded_fil_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

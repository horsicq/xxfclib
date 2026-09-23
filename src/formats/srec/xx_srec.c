/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Motorola S-record reader.  There is no codec: an S-record file is a text
 * encoding of a sparse binary image, so the whole format is line parsing plus
 * reassembly.  The parser runs twice - once to measure the addressed span and
 * once to materialise it - so the image buffer is never sized from a value
 * that has not already been bounded.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/srec/xx_srec.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_SREC exists in the enum. */
#ifdef SREC
#define XX_SREC_FILE_TYPE XX_FILE_TYPE_SREC
#else
#define XX_SREC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The single member is the reassembled binary image.  The S0 header text is
 * NOT used as its name: producers such as objcopy store the name of the
 * S-record file itself there ("prog.s19"), which would label raw binary as
 * S-record text, and the text is attacker controlled.  It is reported as a
 * comment and through xx_srec_get_header_text() instead. */
#define XX_SRECFMT_PAYLOAD_NAME "image.bin"

/* Absolute parse-time ceilings.
 *
 * The address of a data record is up to 32 bits wide and is entirely
 * attacker controlled.  Two S3 records, sixty bytes of text in total, can
 * name byte 0 and byte 0xFFFFFFFF; reassembling them naively materialises a
 * 4 GiB image.  Two independent rules stop that, and both are applied during
 * the measuring pass, before any buffer is allocated:
 *
 *   1. an absolute span ceiling (XX_SRECFMT_MAX_IMAGE_SIZE), and
 *   2. a density rule - above XX_SRECFMT_DENSITY_FLOOR the span may not
 *      exceed the payload actually carried by the file by more than
 *      XX_SRECFMT_MAX_DENSITY times.
 *
 * Rule 2 is what refuses "a 100 KB file forcing a 64 MiB allocation" even
 * when rule 1 alone would have allowed it. */
#define XX_SRECFMT_MAX_IMAGE_SIZE (UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
#define XX_SRECFMT_MAX_DENSITY UINT64_C(64)
#define XX_SRECFMT_DENSITY_FLOOR (UINT64_C(1024) * UINT64_C(1024))
/* Records accepted per file.  Generous enough for the largest image at one
 * data byte per record (objcopy's default is 16 per record, which already
 * needs 4.2 million records for a 64 MiB image); the walk itself is bounded by
 * the device size. */
#define XX_SRECFMT_MAX_LINES (XX_SRECFMT_MAX_IMAGE_SIZE + UINT64_C(1048576))
/* Leading blank lines the cheap probe tolerates before the first record. */
#define XX_SRECFMT_PROBE_BLANK_LINES 4U
/* The strict window.  check_is_valid() walks only the lines that start in the
 * first XX_SRECFMT_VALIDATE_WINDOW bytes of text, so the detector never walks
 * a 100 MB image.  handle_base_info() applies exactly the same strict rules to
 * the same lines, and past the window it keeps the longest prefix that obeys
 * every rule (the rest is overlay) instead of refusing the file.  Therefore
 * every file check_is_valid() accepts is also accepted by handle_base_info():
 * the detector never types a file SREC that cannot then be opened. */
#define XX_SRECFMT_VALIDATE_WINDOW (INT64_C(64) * INT64_C(1024))
/* Bytes filled into address ranges no record covers. */
#define XX_SRECFMT_FILL_BYTE UINT8_C(0x00)
/* Longest S0 header text kept. */
#define XX_SRECFMT_MAX_HEADER_TEXT 64U
/* DOS end-of-file marker. */
#define XX_SRECFMT_CTRL_Z 0x1AU
/* A run of Ctrl-Z padding up to this long at the end of the file is counted as
 * part of the text rather than reported as overlay. */
#define XX_SRECFMT_MAX_CTRL_Z_TAIL 4096

/** Buffered forward line reader over a device. */
typedef struct xx_srecfmt_lines_s {
    xx_io_device *device;
    int64_t position;      /**< Device offset just past the buffered bytes. */
    int64_t end;
    uint8_t buffer[4096];
    size_t fill;           /**< Valid bytes in buffer. */
    size_t cursor;         /**< Next unread byte in buffer. */
    bool exhausted;
    bool skip_lf;          /**< Last line ended in CR: swallow one LF. */
} xx_srecfmt_lines;

typedef enum xx_srecfmt_line_status_e {
    XX_SRECFMT_LINE_OK = 0,
    XX_SRECFMT_LINE_END,      /**< No more input. */
    XX_SRECFMT_LINE_OVERLONG, /**< Longer than XX_SREC_MAX_LINE_LENGTH. */
    XX_SRECFMT_LINE_MARKER    /**< Ctrl-Z at the start of a line. */
} xx_srecfmt_line_status;

/** One decoded S-record line. */
typedef struct xx_srecfmt_record_s {
    uint8_t type;          /**< 0 .. 9, never 4. */
    uint8_t address_width; /**< 2, 3 or 4 bytes. */
    uint32_t address;
    uint8_t data[255];
    size_t data_size;
} xx_srecfmt_record;

/** Running totals of the measuring pass.  Small enough to copy whole at every
 *  point where the text could end (a cut point). */
typedef struct xx_srecfmt_tally_s {
    uint64_t lines;
    uint64_t data_records;
    uint64_t data_bytes;
    uint64_t low;          /**< Lowest addressed byte; UINT64_MAX when none. */
    uint64_t high;         /**< Highest addressed byte. */
    uint64_t entry_point;
    uint64_t declared_record_count;
    bool has_entry_point;
    bool has_record_count;
    bool seen_terminator;
    bool has_header;       /**< The scan owns header_text from this prefix. */
    uint8_t address_width;
} xx_srecfmt_tally;

/** Everything the measuring pass learns. */
typedef struct xx_srecfmt_scan_s {
    uint64_t lines;
    uint64_t data_records;
    uint64_t data_bytes;
    uint64_t load_address;
    uint64_t end_address;
    uint64_t image_size;
    uint64_t entry_point;
    uint64_t declared_record_count;
    bool has_entry_point;
    bool has_record_count;
    bool is_contiguous;
    uint8_t address_width;
    int64_t stream_size;   /**< Bytes of text consumed from base_address. */
    char *header_text;     /**< Owned S0 text, or NULL. */
} xx_srecfmt_scan;

static void xx_srecfmt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Line reading                                                        */
/* ------------------------------------------------------------------ */

static void xx_srecfmt_lines_init(xx_srecfmt_lines *lines,
                                  xx_io_device *device, int64_t offset,
                                  int64_t end) {
    xx_mem_zero(lines, sizeof(*lines));
    lines->device = device;
    lines->position = offset;
    lines->end = end;
}

static bool xx_srecfmt_lines_fill(xx_srecfmt_lines *lines) {
    ssize_t amount;
    size_t want;
    if (lines->cursor < lines->fill) return true;
    if (lines->exhausted || lines->position >= lines->end) return false;
    want = (uint64_t)(lines->end - lines->position) > sizeof(lines->buffer)
               ? sizeof(lines->buffer)
               : (size_t)(lines->end - lines->position);
    amount = xx_io_read(lines->device, lines->buffer, want);
    if (amount <= 0 || (size_t)amount > want) {
        lines->exhausted = true;
        return false;
    }
    lines->position += amount;
    lines->fill = (size_t)amount;
    lines->cursor = 0U;
    return true;
}

/* Device offset of the next unread byte. */
static int64_t xx_srecfmt_lines_offset(const xx_srecfmt_lines *lines) {
    return lines->position - (int64_t)(lines->fill - lines->cursor);
}

/* Swallow the LF of a CR LF pair left pending by the previous line, so that
 * xx_srecfmt_lines_offset() is the first byte of the next line.  This does not
 * change what the next xx_srecfmt_lines_next() returns. */
static void xx_srecfmt_lines_settle(xx_srecfmt_lines *lines) {
    if (lines->skip_lf && xx_srecfmt_lines_fill(lines)) {
        lines->skip_lf = false;
        if (lines->buffer[lines->cursor] == (uint8_t)'\n') ++lines->cursor;
    }
}

/* Copy the next line, without its terminator, into out.  LF, CR and CR LF all
 * end a line.  A line longer than out_capacity is a hard stop rather than a
 * truncation, because an unterminated multi-megabyte "line" is exactly what a
 * hostile or binary file looks like.  A Ctrl-Z as the first byte of a line is
 * reported, unconsumed, as the DOS end-of-text marker.  *out_start gets the
 * device offset of the line's first byte. */
static xx_srecfmt_line_status xx_srecfmt_lines_next(xx_srecfmt_lines *lines,
                                                    char *out,
                                                    size_t out_capacity,
                                                    size_t *out_length,
                                                    int64_t *out_start) {
    size_t used = 0U;
    bool any = false;
    *out_length = 0U;
    *out_start = -1;
    for (;;) {
        uint8_t ch;
        if (!xx_srecfmt_lines_fill(lines)) break;
        ch = lines->buffer[lines->cursor];
        if (lines->skip_lf) {
            lines->skip_lf = false;
            if (ch == (uint8_t)'\n') {
                ++lines->cursor;
                continue;
            }
        }
        if (!any) {
            *out_start = xx_srecfmt_lines_offset(lines);
            if (ch == XX_SRECFMT_CTRL_Z) return XX_SRECFMT_LINE_MARKER;
            any = true;
        }
        ++lines->cursor;
        if (ch == (uint8_t)'\n' || ch == (uint8_t)'\r') {
            lines->skip_lf = ch == (uint8_t)'\r';
            *out_length = used;
            return XX_SRECFMT_LINE_OK;
        }
        if (used >= out_capacity) return XX_SRECFMT_LINE_OVERLONG;
        out[used++] = (char)ch;
    }
    if (!any) return XX_SRECFMT_LINE_END;
    *out_length = used;
    return XX_SRECFMT_LINE_OK;
}

/* True when every byte from the reader's position to the end of its range is
 * Ctrl-Z and there are at most XX_SRECFMT_MAX_CTRL_Z_TAIL of them. */
static bool xx_srecfmt_lines_tail_is_ctrl_z(xx_srecfmt_lines *lines) {
    if (lines->end - xx_srecfmt_lines_offset(lines) >
        (int64_t)XX_SRECFMT_MAX_CTRL_Z_TAIL) {
        return false;
    }
    while (xx_srecfmt_lines_fill(lines)) {
        if (lines->buffer[lines->cursor] != XX_SRECFMT_CTRL_Z) return false;
        ++lines->cursor;
    }
    return !lines->exhausted || lines->position >= lines->end;
}

/* ------------------------------------------------------------------ */
/* Line decoding                                                       */
/* ------------------------------------------------------------------ */

static bool xx_srecfmt_hex_digit(char ch, uint8_t *value) {
    if (ch >= '0' && ch <= '9') {
        *value = (uint8_t)(ch - '0');
    } else if (ch >= 'A' && ch <= 'F') {
        *value = (uint8_t)(ch - 'A' + 10);
    } else if (ch >= 'a' && ch <= 'f') {
        *value = (uint8_t)(ch - 'a' + 10);
    } else {
        return false;
    }
    return true;
}

static bool xx_srecfmt_hex_byte(const char *text, uint8_t *value) {
    uint8_t high;
    uint8_t low;
    if (!xx_srecfmt_hex_digit(text[0], &high) ||
        !xx_srecfmt_hex_digit(text[1], &low)) {
        return false;
    }
    *value = (uint8_t)((high << 4U) | low);
    return true;
}

static uint8_t xx_srecfmt_address_width(uint8_t type) {
    switch (type) {
        case 0U: return 2U;
        case 1U: return 2U;
        case 2U: return 3U;
        case 3U: return 4U;
        case 5U: return 2U;
        case 6U: return 3U;
        case 7U: return 4U;
        case 8U: return 3U;
        case 9U: return 2U;
        default: return 0U;  /* S4 is reserved and is refused. */
    }
}

static bool xx_srecfmt_is_space(char ch) {
    return ch == ' ' || ch == '\t';
}

/* Decode one line.  Every field is validated: the type, the hex alphabet, the
 * declared byte count against the real length, and the checksum. */
static bool xx_srecfmt_decode_line(const char *line, size_t length,
                                   xx_srecfmt_record *record) {
    uint8_t count;
    uint8_t type_digit;
    uint8_t width;
    uint8_t checksum;
    uint32_t sum;
    size_t payload;
    size_t index;
    /* Surrounding spaces and tabs are tolerated, and so is a DOS Ctrl-Z glued
     * to the end of the last line; nothing else is. */
    while (length != 0U && (xx_srecfmt_is_space(line[length - 1U]) ||
                            (uint8_t)line[length - 1U] == XX_SRECFMT_CTRL_Z)) {
        --length;
    }
    while (length != 0U && xx_srecfmt_is_space(line[0])) {
        ++line;
        --length;
    }
    if (length < 10U || length > XX_SREC_MAX_LINE_LENGTH ||
        (length & 1U) != 0U || line[0] != 'S') {
        return false;
    }
    if (!xx_srecfmt_hex_digit(line[1], &type_digit) || type_digit > 9U) {
        return false;
    }
    width = xx_srecfmt_address_width(type_digit);
    if (width == 0U) return false;
    if (!xx_srecfmt_hex_byte(line + 2, &count)) return false;
    /* The count covers the address bytes, the data and the checksum. */
    if (count < (uint8_t)(width + 1U)) return false;
    if (length != 4U + (size_t)count * 2U) return false;
    payload = (size_t)count - width - 1U;

    sum = count;
    record->type = type_digit;
    record->address_width = width;
    record->address = 0U;
    record->data_size = payload;
    for (index = 0U; index < (size_t)width; ++index) {
        uint8_t byte;
        if (!xx_srecfmt_hex_byte(line + 4U + index * 2U, &byte)) return false;
        record->address = (record->address << 8U) | byte;
        sum += byte;
    }
    for (index = 0U; index < payload; ++index) {
        uint8_t byte;
        if (!xx_srecfmt_hex_byte(line + 4U + ((size_t)width + index) * 2U,
                                 &byte)) {
            return false;
        }
        record->data[index] = byte;
        sum += byte;
    }
    if (!xx_srecfmt_hex_byte(line + 4U + ((size_t)width + payload) * 2U,
                             &checksum)) {
        return false;
    }
    /* One's complement of the low byte of the sum. */
    return (uint8_t)(~(uint8_t)(sum & 0xFFU)) == checksum;
}

static bool xx_srecfmt_line_is_blank(const char *line, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (!xx_srecfmt_is_space(line[index])) return false;
    }
    return true;
}

bool xx_srec_check_magic(const uint8_t *magic, size_t magic_size) {
    uint8_t type;
    uint8_t width;
    uint8_t count;
    size_t length;
    size_t limit;
    size_t index;
    if (!magic || magic_size < 10U || magic[0] != (uint8_t)'S' ||
        magic[1] < (uint8_t)'0' || magic[1] > (uint8_t)'9') {
        return false;
    }
    type = (uint8_t)(magic[1] - (uint8_t)'0');
    /* A file that opens with a termination record (S7/S8/S9) carries no data
     * record before it and so can never pass check_is_valid(). */
    if (type >= 7U) return false;
    width = xx_srecfmt_address_width(type);
    if (width == 0U ||
        !xx_srecfmt_hex_byte((const char *)magic + 2, &count) ||
        count < (uint8_t)(width + 1U)) {
        return false;
    }
    /* Every character of the first record that lies inside the window must
     * be a hex digit, and if the record ends inside the window it must be
     * followed by a line end (or trailing blank / Ctrl-Z). */
    length = 4U + (size_t)count * 2U;
    limit = length < magic_size ? length : magic_size;
    for (index = 4U; index < limit; ++index) {
        uint8_t value;
        if (!xx_srecfmt_hex_digit((char)magic[index], &value)) return false;
    }
    if (length < magic_size) {
        uint8_t ch = magic[length];
        if (ch != (uint8_t)'\r' && ch != (uint8_t)'\n' &&
            ch != (uint8_t)' ' && ch != (uint8_t)'\t' &&
            ch != XX_SRECFMT_CTRL_Z) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */

static void xx_srecfmt_scan_cleanup(xx_srecfmt_scan *scan) {
    if (!scan) return;
    if (scan->header_text) xx_mem_free(scan->header_text);
    xx_mem_zero(scan, sizeof(*scan));
    scan->stream_size = -1;
}

/* Keep the S0 text when it is short printable ASCII (NUL / space padding at
 * the end is dropped).  It is only ever used as metadata, never as a path. */
static char *xx_srecfmt_header_copy(const uint8_t *text, size_t length) {
    size_t index;
    char *copy;
    while (length != 0U &&
           (text[length - 1U] == 0U || text[length - 1U] == (uint8_t)' ')) {
        --length;
    }
    if (length == 0U || length > XX_SRECFMT_MAX_HEADER_TEXT) return NULL;
    for (index = 0U; index < length; ++index) {
        if (text[index] < 32U || text[index] >= 127U) return NULL;
    }
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* The span rules on a running tally: the absolute ceiling and, above the
 * density floor, the density rule. */
static bool xx_srecfmt_tally_span_ok(const xx_srecfmt_tally *tally) {
    uint64_t span;
    if (tally->data_bytes == 0U) return true;
    span = tally->high - tally->low + 1U;
    if (span > XX_SRECFMT_MAX_IMAGE_SIZE) return false;
    if (span > XX_SRECFMT_DENSITY_FLOOR &&
        (tally->data_bytes > UINT64_MAX / XX_SRECFMT_MAX_DENSITY ||
         span > tally->data_bytes * XX_SRECFMT_MAX_DENSITY)) {
        return false;
    }
    return true;
}

/* May the text end here?  Every rule a complete file must obey, applied to
 * the prefix read so far.  Detection (need_data) also demands at least one
 * data byte: a file of only S0 / S5 / termination records, or of empty data
 * records, is never auto-detected, because one checksummed S9 line in front
 * of anything would otherwise be enough to claim it.  Parsing a file that was
 * opened by name still accepts such a file as an empty image. */
static bool xx_srecfmt_tally_acceptable(const xx_srecfmt_tally *tally,
                                        bool need_data) {
    if (tally->lines == 0U) return false;
    if (need_data) {
        if (tally->data_bytes == 0U) return false;
    } else if (tally->data_records == 0U && !tally->seen_terminator) {
        /* A lone S0 header in front of binary matter is not S-record text. */
        return false;
    }
    return xx_srecfmt_tally_span_ok(tally);
}

/* Walk the text once.  No image is built here; the pass only measures the
 * addressed span so that the span can be refused before it is allocated.
 *
 * As in GNU BFD's reader, the termination record (S7/S8/S9) ends the image:
 * blank lines after it belong to the text, and the first non-blank byte after
 * it starts the overlay (a second concatenated block, trailing notes, padding).
 * A Ctrl-Z at the start of a line ends the text too; a run of Ctrl-Z to the end
 * of the device is counted as text.
 *
 * Lines that start in the first XX_SRECFMT_VALIDATE_WINDOW bytes are strict:
 * each non-blank one must be a well formed record and the span must stay under
 * the ceiling, or the file is refused.
 *
 * detect == true is the bounded walk check_is_valid() uses: it stops at the
 * window and then applies every end-of-text rule (span ceiling, density, at
 * least one data byte) to what it has read.
 *
 * detect == false is the full walk handle_base_info() uses.  Past the window it
 * never refuses: a malformed or overlong line, a record that would break the
 * span ceiling, or the record cap ends the text at that line.  If the text read
 * then breaks a rule (the density rule, say), the walk falls back to the last
 * cut point at which every rule held.  The cut taken at the window itself is
 * the very state check_is_valid() accepted, so whatever the detector accepts,
 * this walk accepts too, and anything after the chosen end is overlay. */
static bool xx_srecfmt_scan_run(Abstractformat *self, xx_srecfmt_scan *scan,
                                xx_pd_struct *pd, bool detect) {
    xx_srecfmt_lines lines;
    char line[XX_SREC_MAX_LINE_LENGTH];
    int64_t total_size;
    int64_t stream_end;
    int64_t cut_end = -1;
    xx_srecfmt_tally tally;
    xx_srecfmt_tally cut;
    bool have_cut = false;
    bool lenient = false;
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
        scan->stream_size = -1;
        scan->is_contiguous = true;
    }
    if (!self || !self->device || !scan || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_mem_zero(&tally, sizeof(tally));
    xx_mem_zero(&cut, sizeof(cut));
    tally.low = UINT64_MAX;
    total_size = xx_io_total_size(self->device);
    if (total_size <= self->base_address ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        goto fail;
    }
    stream_end = total_size;
    xx_srecfmt_lines_init(&lines, self->device, self->base_address,
                          total_size);
    for (;;) {
        size_t length = 0U;
        int64_t start = -1;
        int64_t offset;
        xx_srecfmt_line_status status;
        xx_srecfmt_record record;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        xx_srecfmt_lines_settle(&lines);
        offset = xx_srecfmt_lines_offset(&lines);
        if (!lenient &&
            offset - self->base_address >= XX_SRECFMT_VALIDATE_WINDOW) {
            if (detect) break;
            lenient = true;
        }
        if (lenient && xx_srecfmt_tally_acceptable(&tally, false)) {
            /* The text could end right here, before the next line. */
            xx_rt_memcpy(&cut, &tally, sizeof(cut));
            cut_end = offset;
            have_cut = true;
        }
        status = xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                       &start);
        if (status == XX_SRECFMT_LINE_END) break;
        if (status == XX_SRECFMT_LINE_MARKER) {
            if (!xx_srecfmt_lines_tail_is_ctrl_z(&lines)) stream_end = start;
            break;
        }
        if (status == XX_SRECFMT_LINE_OK &&
            xx_srecfmt_line_is_blank(line, length)) {
            continue;
        }
        if (tally.seen_terminator) {
            /* First non-blank matter after the termination record. */
            stream_end = start;
            break;
        }
        if (status != XX_SRECFMT_LINE_OK ||
            !xx_srecfmt_decode_line(line, length, &record) ||
            tally.lines >= XX_SRECFMT_MAX_LINES) {
            if (!lenient) goto fail;
            stream_end = start;  /* Past the window: the text ends here. */
            break;
        }
        switch (record.type) {
            case 0U:
                if (!scan->header_text && record.data_size != 0U) {
                    scan->header_text =
                        xx_srecfmt_header_copy(record.data, record.data_size);
                    tally.has_header = scan->header_text != NULL;
                }
                break;
            case 1U:
            case 2U:
            case 3U: {
                uint64_t first = record.address;
                uint64_t low = tally.low;
                uint64_t high = tally.high;
                if (record.data_size != 0U) {
                    uint64_t last = first + (uint64_t)record.data_size - 1U;
                    if (first < low) low = first;
                    if (last > high) high = last;
                    /* Bound the span as it grows so a hostile address cannot
                     * be accumulated and only noticed at the end. */
                    if (high - low >= XX_SRECFMT_MAX_IMAGE_SIZE) {
                        if (!lenient) goto fail;
                        stream_end = start;
                        goto walked;
                    }
                }
                ++tally.data_records;
                if (record.address_width > tally.address_width) {
                    tally.address_width = record.address_width;
                }
                tally.low = low;
                tally.high = high;
                tally.data_bytes += record.data_size;
                break;
            }
            case 5U:
            case 6U:
                /* The count is advisory: a mismatch (a producer counting
                 * differently, or a 16-bit count that wrapped) does not make
                 * the checksummed data any less valid, so it is recorded
                 * rather than enforced. */
                tally.declared_record_count = record.address;
                tally.has_record_count = true;
                break;
            case 7U:
            case 8U:
            case 9U:
                tally.entry_point = record.address;
                tally.has_entry_point = true;
                tally.seen_terminator = true;
                break;
            default:
                goto fail;
        }
        ++tally.lines;
    }
walked:
    if (!xx_srecfmt_tally_acceptable(&tally, detect)) {
        /* In the strict window (and in detection) this refuses the file.  Past
         * the window, fall back to the last prefix that obeyed every rule. */
        if (detect || !lenient || !have_cut) goto fail;
        xx_rt_memcpy(&tally, &cut, sizeof(tally));
        stream_end = cut_end;
        if (!tally.has_header && scan->header_text) {
            xx_mem_free(scan->header_text);
            scan->header_text = NULL;
        }
    }
    scan->lines = tally.lines;
    scan->data_records = tally.data_records;
    scan->data_bytes = tally.data_bytes;
    scan->entry_point = tally.entry_point;
    scan->has_entry_point = tally.has_entry_point;
    scan->declared_record_count = tally.declared_record_count;
    scan->has_record_count = tally.has_record_count;
    scan->address_width = tally.address_width;
    if (tally.data_bytes != 0U) {
        scan->load_address = tally.low;
        scan->end_address = tally.high;
        scan->image_size = tally.high - tally.low + 1U;
    }
    /* Records may overlap - EASy68K, for one, repeats the last data record -
     * and the later record wins when the image is built, as it does in
     * objcopy.  Overlap cannot inflate the image: the span is bounded by the
     * ceiling and density rules, and data_bytes by the text size. */
    scan->is_contiguous = scan->data_bytes >= scan->image_size;
    /* Absolute ceiling (already part of the tally rules; kept as a guard for
     * the allocation that follows). */
    if (scan->image_size > XX_SRECFMT_MAX_IMAGE_SIZE) goto fail;
    if (stream_end < self->base_address) goto fail;
    scan->stream_size = stream_end - self->base_address;
    return true;
fail:
    xx_srecfmt_scan_cleanup(scan);
    return false;
}

/* Second pass: allocate the already bounded span and place every data record
 * into it.  Gaps keep XX_SRECFMT_FILL_BYTE.  Only the text the first pass
 * accepted is read. */
static bool xx_srecfmt_build(Abstractformat *self,
                             const xx_srecfmt_scan *scan, uint8_t **out_image,
                             size_t *out_size, xx_pd_struct *pd) {
    xx_srecfmt_lines lines;
    char line[XX_SREC_MAX_LINE_LENGTH];
    uint8_t *image;
    if (out_image) *out_image = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !self->device || !scan || !out_image || !out_size ||
        scan->image_size == 0U ||
        scan->image_size > XX_SRECFMT_MAX_IMAGE_SIZE ||
        scan->image_size > (uint64_t)SIZE_MAX || scan->stream_size <= 0 ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        return false;
    }
    image = (uint8_t *)xx_mem_alloc((size_t)scan->image_size);
    if (!image) return false;
    xx_rt_memset(image, XX_SRECFMT_FILL_BYTE, (size_t)scan->image_size);
    xx_srecfmt_lines_init(&lines, self->device, self->base_address,
                          self->base_address + scan->stream_size);
    for (;;) {
        size_t length = 0U;
        int64_t start = -1;
        xx_srecfmt_line_status status;
        xx_srecfmt_record record;
        uint64_t offset;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        status = xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                       &start);
        /* The first pass fixed where the text ends; the same stops apply. */
        if (status == XX_SRECFMT_LINE_END ||
            status == XX_SRECFMT_LINE_MARKER) {
            break;
        }
        if (status != XX_SRECFMT_LINE_OK) goto fail;
        if (xx_srecfmt_line_is_blank(line, length)) continue;
        if (!xx_srecfmt_decode_line(line, length, &record)) goto fail;
        if (record.type >= 7U) break;  /* Termination record. */
        if (record.type > 3U || record.type == 0U || record.data_size == 0U) {
            continue;
        }
        if (record.address < scan->load_address) goto fail;
        offset = (uint64_t)record.address - scan->load_address;
        if (offset > scan->image_size ||
            (uint64_t)record.data_size > scan->image_size - offset) {
            goto fail;
        }
        xx_rt_memcpy(image + offset, record.data, record.data_size);
    }
    *out_image = image;
    *out_size = (size_t)scan->image_size;
    return true;
fail:
    xx_mem_free(image);
    return false;
}

/* ------------------------------------------------------------------ */
/* Probe                                                               */
/* ------------------------------------------------------------------ */

bool xx_srec_probe_device(xx_io_device *dev, int64_t base_address) {
    xx_srecfmt_lines lines;
    char line[XX_SREC_MAX_LINE_LENGTH];
    int64_t total_size;
    int64_t window;
    unsigned blanks = 0U;
    if (!dev || base_address < 0) return false;
    total_size = xx_io_total_size(dev);
    if (total_size <= base_address ||
        xx_io_seek64(dev, base_address, SEEK_SET) != 0) {
        return false;
    }
    /* A bounded window: enough for a few blank lines plus one record. */
    window = (int64_t)XX_SREC_MAX_LINE_LENGTH * 4;
    if (window > total_size - base_address) window = total_size - base_address;
    xx_srecfmt_lines_init(&lines, dev, base_address, base_address + window);
    for (;;) {
        size_t length = 0U;
        int64_t start = -1;
        xx_srecfmt_record record;
        if (xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                  &start) != XX_SRECFMT_LINE_OK) {
            return false;
        }
        if (xx_srecfmt_line_is_blank(line, length)) {
            if (++blanks > XX_SRECFMT_PROBE_BLANK_LINES) return false;
            continue;
        }
        return xx_srecfmt_decode_line(line, length, &record);
    }
}

/* ------------------------------------------------------------------ */
/* Options and records                                                 */
/* ------------------------------------------------------------------ */

static bool xx_srecfmt_copy_options(xx_list_s *destination,
                                    const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_srecfmt_find_option(const xx_list_s *options,
                                            uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static void xx_srecfmt_append(char *out, size_t capacity, size_t *used,
                              const char *text) {
    size_t index;
    for (index = 0U; text && text[index] != '\0'; ++index) {
        if (*used + 1U >= capacity) break;
        out[(*used)++] = text[index];
    }
    out[*used] = '\0';
}

/* Format "load=0x... entry=0x... header=..." without the CRT. */
static void xx_srecfmt_describe(const xx_srec *archive, char *out,
                                size_t capacity) {
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    unsigned pass;
    const char *header = xx_srec_get_header_text(archive);
    if (!out || capacity == 0U) return;
    out[0] = '\0';
    for (pass = 0U; pass < 2U; ++pass) {
        uint64_t value = pass == 0U ? archive->load_address
                                    : archive->entry_point;
        unsigned shift = 60U;
        bool started = false;
        if (pass == 1U && !archive->has_entry_point) break;
        xx_srecfmt_append(out, capacity, &used,
                          pass == 0U ? "load=0x" : " entry=0x");
        for (;;) {
            unsigned nibble = (unsigned)((value >> shift) & 0xFU);
            if (nibble != 0U || started || shift == 0U) {
                if (used + 1U >= capacity) return;
                out[used++] = digits[nibble];
                out[used] = '\0';
                started = true;
            }
            if (shift == 0U) break;
            shift -= 4U;
        }
    }
    if (header) {
        xx_srecfmt_append(out, capacity, &used, " header=");
        xx_srecfmt_append(out, capacity, &used, header);
    }
}

static bool xx_srecfmt_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_srec *archive;
    char description[160];
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_srec *)self;
    xx_srecfmt_describe(archive, description, sizeof(description));
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 0;
    record->data_offset = self->base_address;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_original_name(record,
                                               XX_SRECFMT_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           /* No dedicated load-address / entry-point meta id exists yet, so
            * both are reported as human readable text alongside the struct
            * accessors xx_srec_get_load_address()/xx_srec_get_entry_point(). */
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          description);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void xx_srec_init(xx_srec *archive, xx_io_device *dev, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    /* The addresses inside the text are big endian by convention; the file
     * itself has no binary byte order. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_SREC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-srec");
    xx_format_set_extension(&archive->format, "srec");
    archive->format.check_is_valid = xx_srec_check_is_valid;
    archive->format.handle_base_info = xx_srec_handle_base_info;
    archive->format.get_format_size = xx_srec_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_srec_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_srec_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_srec_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_srec_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_srec_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_srec_free_archive_records_reading;
    archive->format.destroy = xx_srecfmt_vtable_destroy;
    archive->stream_end = -1;
}

xx_srec *xx_srec_create(xx_io_device *dev, int64_t base_address) {
    xx_srec *archive = (xx_srec *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_srec_init(archive, dev, base_address);
    return archive;
}

static void xx_srecfmt_release_internal(xx_srec *archive) {
    if (archive && archive->internal) {
        xx_srecfmt_scan_cleanup((xx_srecfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
}

void xx_srec_destroy(xx_srec *archive) {
    if (!archive) return;
    xx_srecfmt_release_internal(archive);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_lines = 0U;
    archive->number_of_data_records = 0U;
    archive->data_bytes = 0U;
    archive->image_size = 0U;
    archive->stream_end = -1;
}

static void xx_srecfmt_vtable_destroy(Abstractformat *self) {
    xx_srec_destroy((xx_srec *)self);
}

void xx_srec_free(xx_srec *archive) {
    if (!archive) return;
    xx_srec_destroy(archive);
    xx_mem_free(archive);
}

/* ------------------------------------------------------------------ */
/* Abstractformat surface                                              */
/* ------------------------------------------------------------------ */

bool xx_srec_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_srecfmt_scan scan;
    bool result;
    /* Cheap bounded probe first so a binary file is rejected after at most
     * one line, then a bounded walk of the lines that start in the first
     * XX_SRECFMT_VALIDATE_WINDOW bytes of text.  The walk demands at least one
     * data byte and applies the span ceiling and the density rule to what it
     * read; handle_base_info() accepts every file this accepts. */
    if (!self || !self->device || !xx_srec_probe_device(self->device,
                                                        self->base_address)) {
        return false;
    }
    result = xx_srecfmt_scan_run(self, &scan, pd, true);
    xx_srecfmt_scan_cleanup(&scan);
    return result;
}

bool xx_srec_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_srecfmt_scan *scan;
    xx_srec *archive = (xx_srec *)self;
    int64_t total_size;
    if (!self) return false;
    scan = (xx_srecfmt_scan *)xx_mem_alloc(sizeof(*scan));
    if (!scan || !xx_srec_probe_device(self->device, self->base_address) ||
        !xx_srecfmt_scan_run(self, scan, pd, false)) {
        if (scan) xx_mem_free(scan);
        xx_srecfmt_release_internal(archive);
        archive->number_of_lines = 0U;
        archive->number_of_data_records = 0U;
        archive->data_bytes = 0U;
        archive->image_size = 0U;
        archive->stream_end = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    xx_srecfmt_release_internal(archive);
    archive->internal = scan;
    archive->number_of_lines = scan->lines;
    archive->number_of_data_records = scan->data_records;
    archive->data_bytes = scan->data_bytes;
    archive->image_size = scan->image_size;
    archive->load_address = scan->load_address;
    archive->end_address = scan->end_address;
    archive->entry_point = scan->entry_point;
    archive->declared_record_count = scan->declared_record_count;
    archive->has_entry_point = scan->has_entry_point;
    archive->has_record_count = scan->has_record_count;
    archive->is_contiguous = scan->is_contiguous;
    archive->address_width = scan->address_width;
    archive->stream_end = self->base_address + scan->stream_size;
    self->format_size = scan->stream_size;
    /* Whatever follows the text is reported as overlay: matter after an
     * end-of-text marker or a complete block, and, past the strict window,
     * the first line that broke a rule and everything after it. */
    total_size = xx_io_total_size(self->device);
    if (total_size > archive->stream_end) {
        self->overlay_offset = archive->stream_end;
        self->overlay_size = total_size - archive->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    /* An S-record file that carries no data bytes (header and termination
     * only) is valid and has no member. */
    self->number_of_archive_records = scan->image_size != 0U ? 1U : 0U;
    self->file_type = XX_SREC_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_srec_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_srec_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->number_of_archive_records;
}

bool xx_srec_unpack_to_device(xx_srec *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    const xx_srecfmt_scan *scan;
    uint8_t *image = NULL;
    size_t image_size = 0U;
    size_t done = 0U;
    bool result = true;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid) {
        return false;
    }
    scan = (const xx_srecfmt_scan *)archive->internal;
    if (!scan) return false;
    if (scan->image_size == 0U) return true;  /* Empty image. */
    if (!xx_srecfmt_build(&archive->format, scan, &image, &image_size, pd)) {
        return false;
    }
    while (done < image_size) {
        ssize_t amount = xx_io_write(destination, image + done,
                                     image_size - done);
        if (amount <= 0 || (size_t)amount > image_size - done) {
            result = false;
            break;
        }
        done += (size_t)amount;
    }
    xx_mem_free(image);
    return result;
}

xx_archive_record_state *xx_srec_create_archive_records_reading(
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
    if (!xx_srecfmt_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    if (self->number_of_archive_records == 0U) {
        /* Empty image: a valid reading session with no record. */
        state->has_record = false;
        state->total_records = 0;
        return state;
    }
    if (!xx_srecfmt_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_srec_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_srec_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* The reassembled image is the only record. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_srec_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    xx_srec *archive = (xx_srec *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = xx_srecfmt_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify the image really reassembles. */
        const xx_srecfmt_scan *scan = (const xx_srecfmt_scan *)archive->internal;
        uint8_t *image = NULL;
        size_t image_size = 0U;
        if (!scan || !xx_srecfmt_build(self, scan, &image, &image_size, pd)) {
            return false;
        }
        xx_mem_free(image);
        return (uint64_t)image_size == archive->image_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    /* The member name is the fixed XX_SRECFMT_PAYLOAD_NAME; nothing from the
     * file reaches the destination path. */
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path =
            xx_str_concat3(base_path, "/", XX_SRECFMT_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_SRECFMT_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_srec_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_srec_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

uint64_t xx_srec_get_number_of_lines(const xx_srec *archive) {
    return archive ? archive->number_of_lines : 0U;
}
uint64_t xx_srec_get_data_bytes(const xx_srec *archive) {
    return archive ? archive->data_bytes : 0U;
}
uint64_t xx_srec_get_image_size(const xx_srec *archive) {
    return archive ? archive->image_size : 0U;
}
uint64_t xx_srec_get_load_address(const xx_srec *archive) {
    return archive ? archive->load_address : 0U;
}
uint64_t xx_srec_get_entry_point(const xx_srec *archive) {
    return archive ? archive->entry_point : 0U;
}
bool xx_srec_get_has_entry_point(const xx_srec *archive) {
    return archive ? archive->has_entry_point : false;
}
uint8_t xx_srec_get_address_width(const xx_srec *archive) {
    return archive ? archive->address_width : 0U;
}
int64_t xx_srec_get_stream_end(const xx_srec *archive) {
    return archive ? archive->stream_end : -1;
}
const char *xx_srec_get_header_text(const xx_srec *archive) {
    const xx_srecfmt_scan *scan =
        archive ? (const xx_srecfmt_scan *)archive->internal : NULL;
    return scan ? scan->header_text : NULL;
}

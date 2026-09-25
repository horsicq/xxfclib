/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Intel HEX reader.  There is no codec: an Intel HEX file is a text encoding
 * of a sparse binary image, so the whole format is line parsing.  The parse
 * measures the blocks (runs of data records that continue one another) and
 * keeps where each one starts in the text; extraction decodes a block from
 * the text again and streams it out, so no buffer is ever sized from an
 * address the file supplies.
 *
 * Written from the published Intel HEX specification (Intel, "Hexadecimal
 * Object File Format Specification", rev. A, 1988).  7-Zip's IhexHandler.cpp
 * (LGPL) was used only to learn how 7-Zip splits the image into items, so
 * that its output can serve as the comparison oracle; no code was taken from
 * it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ihex/xx_ihex.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. */
#ifdef IHEX
#define XX_IHEX_FILE_TYPE XX_FILE_TYPE_IHEX
#else
#define XX_IHEX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Leading blank lines the probe tolerates before the first record. */
#define XX_IHEXFMT_PROBE_BLANK_LINES 4U
/* The strict window.  check_is_valid() walks only the lines that start in the
 * first XX_IHEXFMT_VALIDATE_WINDOW bytes of text, so the detector never walks
 * a 100 MB image.  handle_base_info() applies exactly the same strict rules to
 * the same lines, and past the window the first line that breaks a rule ends
 * the text (the rest is overlay) instead of refusing the file.  Every file
 * check_is_valid() accepts is therefore accepted by handle_base_info(). */
#define XX_IHEXFMT_VALIDATE_WINDOW (INT64_C(64) * INT64_C(1024))
/* Shortest legal record: ':' + count + address + type + checksum. */
#define XX_IHEXFMT_MIN_RECORD 11U
/* Longest legal record: 11 + 2 * 255. */
#define XX_IHEXFMT_MAX_RECORD 521U
/* DOS end-of-file marker. */
#define XX_IHEXFMT_CTRL_Z 0x1AU
/* A run of Ctrl-Z padding up to this long at the end of the file is counted as
 * part of the text rather than reported as overlay. */
#define XX_IHEXFMT_MAX_CTRL_Z_TAIL 4096
/* Initial block descriptor capacity. */
#define XX_IHEXFMT_INITIAL_BLOCKS 16U

#define XX_IHEXFMT_TYPE_DATA 0U
#define XX_IHEXFMT_TYPE_EOF 1U
#define XX_IHEXFMT_TYPE_SEGMENT 2U
#define XX_IHEXFMT_TYPE_START_SEGMENT 3U
#define XX_IHEXFMT_TYPE_LINEAR 4U
#define XX_IHEXFMT_TYPE_START_LINEAR 5U

/** Buffered forward line reader over a device. */
typedef struct xx_ihexfmt_lines_s {
    xx_io_device *device;
    int64_t position;      /**< Device offset just past the buffered bytes. */
    int64_t end;
    uint8_t buffer[4096];
    size_t fill;           /**< Valid bytes in buffer. */
    size_t cursor;         /**< Next unread byte in buffer. */
    bool exhausted;
    bool skip_lf;          /**< Last line ended in CR: swallow one LF. */
} xx_ihexfmt_lines;

typedef enum xx_ihexfmt_line_status_e {
    XX_IHEXFMT_LINE_OK = 0,
    XX_IHEXFMT_LINE_END,      /**< No more input. */
    XX_IHEXFMT_LINE_OVERLONG, /**< Longer than XX_IHEX_MAX_LINE_LENGTH. */
    XX_IHEXFMT_LINE_MARKER    /**< Ctrl-Z at the start of a line. */
} xx_ihexfmt_line_status;

/** One decoded record. */
typedef struct xx_ihexfmt_record_s {
    uint8_t type;
    uint8_t count;         /**< Data bytes. */
    uint16_t address;
    uint8_t data[255];
} xx_ihexfmt_record;

/** One run of data records, each starting where the previous one ended. */
typedef struct xx_ihexfmt_block_s {
    uint64_t address;      /**< Absolute address of the first byte. */
    uint64_t size;         /**< Bytes in the run. */
    int64_t text_offset;   /**< Device offset of the first record's line. */
    int64_t text_end;      /**< Device offset just past the last record. */
    uint32_t base;         /**< 02/04 base in effect at the first record. */
    bool renamed;          /**< An earlier block starts at the same address. */
} xx_ihexfmt_block;

/** Everything the parse learns. */
typedef struct xx_ihexfmt_scan_s {
    xx_ihexfmt_block *blocks;
    size_t block_count;
    size_t block_capacity;
    uint64_t lines;
    uint64_t data_records;
    uint64_t data_bytes;
    uint64_t low;          /**< Lowest addressed byte; UINT64_MAX when none. */
    uint64_t high;         /**< Highest addressed byte. */
    uint32_t entry_point;
    uint8_t entry_type;
    bool has_entry_point;
    bool seen_eof;
    bool has_segment;
    bool has_linear;
    int64_t stream_size;   /**< Bytes of text consumed from base_address. */
} xx_ihexfmt_scan;

static void xx_ihexfmt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Line reading                                                        */
/* ------------------------------------------------------------------ */

static void xx_ihexfmt_lines_init(xx_ihexfmt_lines *lines,
                                  xx_io_device *device, int64_t offset,
                                  int64_t end) {
    xx_mem_zero(lines, sizeof(*lines));
    lines->device = device;
    lines->position = offset;
    lines->end = end;
}

static bool xx_ihexfmt_lines_fill(xx_ihexfmt_lines *lines) {
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
static int64_t xx_ihexfmt_lines_offset(const xx_ihexfmt_lines *lines) {
    return lines->position - (int64_t)(lines->fill - lines->cursor);
}

/* Swallow the LF of a CR LF pair left pending by the previous line, so that
 * xx_ihexfmt_lines_offset() is the first byte of the next line. */
static void xx_ihexfmt_lines_settle(xx_ihexfmt_lines *lines) {
    if (lines->skip_lf && xx_ihexfmt_lines_fill(lines)) {
        lines->skip_lf = false;
        if (lines->buffer[lines->cursor] == (uint8_t)'\n') ++lines->cursor;
    }
}

/* Copy the next line, without its terminator, into out.  LF, CR and CR LF all
 * end a line.  A line longer than out_capacity is a hard stop rather than a
 * truncation, because an unterminated multi-megabyte "line" is exactly what a
 * binary file looks like.  A Ctrl-Z as the first byte of a line is reported,
 * unconsumed, as the DOS end-of-text marker.  *out_start gets the device
 * offset of the line's first byte and *out_length its length before the
 * terminator. */
static xx_ihexfmt_line_status xx_ihexfmt_lines_next(xx_ihexfmt_lines *lines,
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
        if (!xx_ihexfmt_lines_fill(lines)) break;
        ch = lines->buffer[lines->cursor];
        if (lines->skip_lf) {
            lines->skip_lf = false;
            if (ch == (uint8_t)'\n') {
                ++lines->cursor;
                continue;
            }
        }
        if (!any) {
            *out_start = xx_ihexfmt_lines_offset(lines);
            if (ch == XX_IHEXFMT_CTRL_Z) return XX_IHEXFMT_LINE_MARKER;
            any = true;
        }
        ++lines->cursor;
        if (ch == (uint8_t)'\n' || ch == (uint8_t)'\r') {
            lines->skip_lf = ch == (uint8_t)'\r';
            *out_length = used;
            return XX_IHEXFMT_LINE_OK;
        }
        if (used >= out_capacity) return XX_IHEXFMT_LINE_OVERLONG;
        out[used++] = (char)ch;
    }
    if (!any) return XX_IHEXFMT_LINE_END;
    *out_length = used;
    return XX_IHEXFMT_LINE_OK;
}

/* True when every byte from the reader's position to the end of its range is
 * Ctrl-Z and there are at most XX_IHEXFMT_MAX_CTRL_Z_TAIL of them. */
static bool xx_ihexfmt_lines_tail_is_ctrl_z(xx_ihexfmt_lines *lines) {
    if (lines->end - xx_ihexfmt_lines_offset(lines) >
        (int64_t)XX_IHEXFMT_MAX_CTRL_Z_TAIL) {
        return false;
    }
    while (xx_ihexfmt_lines_fill(lines)) {
        if (lines->buffer[lines->cursor] != XX_IHEXFMT_CTRL_Z) return false;
        ++lines->cursor;
    }
    return !lines->exhausted || lines->position >= lines->end;
}

/* ------------------------------------------------------------------ */
/* Record decoding                                                     */
/* ------------------------------------------------------------------ */

static bool xx_ihexfmt_hex_digit(uint8_t ch, uint8_t *value) {
    if (ch >= (uint8_t)'0' && ch <= (uint8_t)'9') {
        *value = (uint8_t)(ch - (uint8_t)'0');
    } else if (ch >= (uint8_t)'A' && ch <= (uint8_t)'F') {
        *value = (uint8_t)(ch - (uint8_t)'A' + 10U);
    } else if (ch >= (uint8_t)'a' && ch <= (uint8_t)'f') {
        *value = (uint8_t)(ch - (uint8_t)'a' + 10U);
    } else {
        return false;
    }
    return true;
}

static bool xx_ihexfmt_hex_byte(const uint8_t *text, uint8_t *value) {
    uint8_t high;
    uint8_t low;
    if (!xx_ihexfmt_hex_digit(text[0], &high) ||
        !xx_ihexfmt_hex_digit(text[1], &low)) {
        return false;
    }
    *value = (uint8_t)((high << 4U) | low);
    return true;
}

/* Does the declared data length fit the record type?  Types above 05 are
 * refused. */
static bool xx_ihexfmt_count_fits_type(uint8_t type, uint8_t count) {
    switch (type) {
        case XX_IHEXFMT_TYPE_DATA: return true;
        case XX_IHEXFMT_TYPE_EOF: return count == 0U;
        case XX_IHEXFMT_TYPE_SEGMENT:
        case XX_IHEXFMT_TYPE_LINEAR: return count == 2U;
        case XX_IHEXFMT_TYPE_START_SEGMENT:
        case XX_IHEXFMT_TYPE_START_LINEAR: return count == 4U;
        default: return false;
    }
}

static bool xx_ihexfmt_is_space(char ch) {
    return ch == ' ' || ch == '\t';
}

/* Decode one line.  Every field is validated: the colon, the hex alphabet,
 * the declared count against the real length and the type, and the
 * checksum. */
static bool xx_ihexfmt_decode_line(const char *line, size_t length,
                                   xx_ihexfmt_record *record) {
    const uint8_t *text;
    uint8_t count;
    uint8_t type;
    uint8_t high;
    uint8_t low;
    uint8_t checksum;
    uint32_t sum;
    size_t index;
    /* Surrounding spaces and tabs are tolerated, and so is a DOS Ctrl-Z glued
     * to the end of the last line; nothing else is. */
    while (length != 0U && (xx_ihexfmt_is_space(line[length - 1U]) ||
                            (uint8_t)line[length - 1U] == XX_IHEXFMT_CTRL_Z)) {
        --length;
    }
    while (length != 0U && xx_ihexfmt_is_space(line[0])) {
        ++line;
        --length;
    }
    if (length < XX_IHEXFMT_MIN_RECORD || length > XX_IHEXFMT_MAX_RECORD ||
        line[0] != ':') {
        return false;
    }
    text = (const uint8_t *)line;
    if (!xx_ihexfmt_hex_byte(text + 1, &count) ||
        length != XX_IHEXFMT_MIN_RECORD + (size_t)count * 2U ||
        !xx_ihexfmt_hex_byte(text + 3, &high) ||
        !xx_ihexfmt_hex_byte(text + 5, &low) ||
        !xx_ihexfmt_hex_byte(text + 7, &type) ||
        !xx_ihexfmt_count_fits_type(type, count)) {
        return false;
    }
    sum = (uint32_t)count + high + low + type;
    for (index = 0U; index < (size_t)count; ++index) {
        uint8_t byte;
        if (!xx_ihexfmt_hex_byte(text + 9U + index * 2U, &byte)) return false;
        record->data[index] = byte;
        sum += byte;
    }
    if (!xx_ihexfmt_hex_byte(text + 9U + (size_t)count * 2U, &checksum)) {
        return false;
    }
    record->type = type;
    record->count = count;
    record->address = (uint16_t)(((uint16_t)high << 8U) | low);
    /* Two's complement: every byte of the record sums to zero. */
    return ((sum + checksum) & 0xFFU) == 0U;
}

static bool xx_ihexfmt_line_is_blank(const char *line, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (!xx_ihexfmt_is_space(line[index])) return false;
    }
    return true;
}

static uint32_t xx_ihexfmt_be16(const uint8_t *data) {
    return ((uint32_t)data[0] << 8U) | data[1];
}

static uint32_t xx_ihexfmt_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | data[3];
}

bool xx_ihex_check_magic(const uint8_t *magic, size_t magic_size) {
    uint8_t count;
    uint8_t type;
    uint8_t value;
    size_t length;
    size_t limit;
    size_t index;
    uint32_t sum = 0U;
    if (!magic || magic_size < XX_IHEXFMT_MIN_RECORD ||
        magic[0] != (uint8_t)':' || !xx_ihexfmt_hex_byte(magic + 1, &count) ||
        !xx_ihexfmt_hex_byte(magic + 7, &type) ||
        !xx_ihexfmt_count_fits_type(type, count)) {
        return false;
    }
    /* A file that opens with the EOF record, or with an empty data record,
     * carries nothing before it and can never pass check_is_valid(). */
    if (type == XX_IHEXFMT_TYPE_EOF ||
        (type == XX_IHEXFMT_TYPE_DATA && count == 0U)) {
        return false;
    }
    /* Every character of the first record that lies inside the window must
     * be a hex digit.  If the record ends inside the window its checksum must
     * verify and a line end (or trailing blank / Ctrl-Z) must follow it. */
    length = XX_IHEXFMT_MIN_RECORD + (size_t)count * 2U;
    limit = length < magic_size ? length : magic_size;
    for (index = 1U; index < limit; ++index) {
        if (!xx_ihexfmt_hex_digit(magic[index], &value)) return false;
    }
    if (length > magic_size) return true;
    for (index = 1U; index + 1U < length; index += 2U) {
        if (!xx_ihexfmt_hex_byte(magic + index, &value)) return false;
        sum += value;
    }
    if ((sum & 0xFFU) != 0U) return false;
    if (length < magic_size) {
        uint8_t ch = magic[length];
        if (ch != (uint8_t)'\r' && ch != (uint8_t)'\n' &&
            ch != (uint8_t)' ' && ch != (uint8_t)'\t' &&
            ch != XX_IHEXFMT_CTRL_Z) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */

static void xx_ihexfmt_scan_cleanup(xx_ihexfmt_scan *scan) {
    if (!scan) return;
    if (scan->blocks) xx_mem_free(scan->blocks);
    xx_mem_zero(scan, sizeof(*scan));
    scan->low = UINT64_MAX;
    scan->stream_size = -1;
}

/* Room for one block more; false when the cap is reached or memory runs
 * out. */
static bool xx_ihexfmt_reserve_block(xx_ihexfmt_scan *scan) {
    size_t capacity;
    xx_ihexfmt_block *grown;
    if (scan->block_count < scan->block_capacity) return true;
    if (scan->block_count >= (size_t)XX_IHEX_MAX_BLOCKS) return false;
    capacity = scan->block_capacity == 0U ? (size_t)XX_IHEXFMT_INITIAL_BLOCKS
                                          : scan->block_capacity * 2U;
    if (capacity > (size_t)XX_IHEX_MAX_BLOCKS) {
        capacity = (size_t)XX_IHEX_MAX_BLOCKS;
    }
    grown = (xx_ihexfmt_block *)xx_mem_realloc(scan->blocks,
                                               capacity * sizeof(*grown));
    if (!grown) return false;
    scan->blocks = grown;
    scan->block_capacity = capacity;
    return true;
}

/* May the text end here?  Detection also demands at least one data byte, so
 * a file of only EOF / address / start records is never auto-detected.  A
 * file opened by name may hold an empty image, but some data record or the
 * EOF record must be present: a lone 04 record in front of other matter is
 * not Intel HEX text. */
static bool xx_ihexfmt_acceptable(const xx_ihexfmt_scan *scan, bool detect) {
    if (scan->lines == 0U) return false;
    if (detect) return scan->data_bytes != 0U;
    return scan->data_records != 0U || scan->seen_eof;
}

/* Walk the text once.
 *
 * The EOF record ends the image: blank lines after it belong to the text, and
 * the first non-blank byte after it starts the overlay.  A Ctrl-Z at the
 * start of a line ends the text too; a run of Ctrl-Z to the end of the device
 * is counted as text.  Without an EOF record the text runs to the end of the
 * device.
 *
 * Lines that start in the first XX_IHEXFMT_VALIDATE_WINDOW bytes are strict:
 * each non-blank one must be a well formed record, or the file is refused.
 *
 * detect == true is the bounded walk check_is_valid() uses: it stops at the
 * window, keeps no block list and demands at least one data byte.
 *
 * detect == false is the full walk handle_base_info() uses.  Past the window
 * it never refuses: a malformed or overlong line, or a record that would open
 * a block past XX_IHEX_MAX_BLOCKS, ends the text at that line.  The block cap
 * cannot be reached inside the window (a data record line is at least 12
 * bytes), so whatever the detector accepts, this walk accepts too. */
static bool xx_ihexfmt_scan_run(Abstractformat *self, xx_ihexfmt_scan *scan,
                                xx_pd_struct *pd, bool detect) {
    xx_ihexfmt_lines lines;
    char line[XX_IHEX_MAX_LINE_LENGTH];
    int64_t total_size;
    int64_t stream_end;
    uint32_t base = 0U;
    bool lenient = false;
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
        scan->low = UINT64_MAX;
        scan->stream_size = -1;
    }
    if (!self || !self->device || !scan || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size <= self->base_address ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        goto fail;
    }
    stream_end = total_size;
    xx_ihexfmt_lines_init(&lines, self->device, self->base_address,
                          total_size);
    for (;;) {
        size_t length = 0U;
        int64_t start = -1;
        int64_t offset;
        xx_ihexfmt_line_status status;
        xx_ihexfmt_record record;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        xx_ihexfmt_lines_settle(&lines);
        offset = xx_ihexfmt_lines_offset(&lines);
        if (!lenient &&
            offset - self->base_address >= XX_IHEXFMT_VALIDATE_WINDOW) {
            if (detect) break;
            lenient = true;
        }
        status = xx_ihexfmt_lines_next(&lines, line, sizeof(line), &length,
                                       &start);
        if (status == XX_IHEXFMT_LINE_END) break;
        if (status == XX_IHEXFMT_LINE_MARKER) {
            if (!xx_ihexfmt_lines_tail_is_ctrl_z(&lines)) stream_end = start;
            break;
        }
        if (status == XX_IHEXFMT_LINE_OK &&
            xx_ihexfmt_line_is_blank(line, length)) {
            continue;
        }
        if (scan->seen_eof) {
            /* First non-blank matter after the EOF record. */
            stream_end = start;
            break;
        }
        if (status != XX_IHEXFMT_LINE_OK ||
            !xx_ihexfmt_decode_line(line, length, &record)) {
            if (!lenient) goto fail;
            stream_end = start;  /* Past the window: the text ends here. */
            break;
        }
        switch (record.type) {
            case XX_IHEXFMT_TYPE_DATA: {
                uint64_t first = (uint64_t)base + record.address;
                uint64_t last;
                if (record.count == 0U) break;  /* Harmless no-op. */
                last = first + record.count - 1U;
                if (!detect) {
                    xx_ihexfmt_block *block =
                        scan->block_count != 0U
                            ? &scan->blocks[scan->block_count - 1U]
                            : NULL;
                    if (block && block->address + block->size == first) {
                        block->size += record.count;
                    } else {
                        if (!xx_ihexfmt_reserve_block(scan)) {
                            if (!lenient) goto fail;
                            stream_end = start;
                            goto walked;
                        }
                        block = &scan->blocks[scan->block_count++];
                        block->address = first;
                        block->size = record.count;
                        block->text_offset = start;
                        block->base = base;
                        block->renamed = false;
                    }
                    block->text_end = start + (int64_t)length;
                }
                if (first < scan->low) scan->low = first;
                if (last > scan->high) scan->high = last;
                ++scan->data_records;
                scan->data_bytes += record.count;
                break;
            }
            case XX_IHEXFMT_TYPE_EOF:
                scan->seen_eof = true;
                break;
            case XX_IHEXFMT_TYPE_SEGMENT:
                base = xx_ihexfmt_be16(record.data) << 4U;
                scan->has_segment = true;
                break;
            case XX_IHEXFMT_TYPE_LINEAR:
                base = xx_ihexfmt_be16(record.data) << 16U;
                scan->has_linear = true;
                break;
            case XX_IHEXFMT_TYPE_START_SEGMENT:
            case XX_IHEXFMT_TYPE_START_LINEAR:
                scan->entry_point = xx_ihexfmt_be32(record.data);
                scan->entry_type = record.type;
                scan->has_entry_point = true;
                break;
            default:
                goto fail;
        }
        ++scan->lines;
    }
walked:
    if (!xx_ihexfmt_acceptable(scan, detect)) goto fail;
    if (stream_end < self->base_address) goto fail;
    scan->stream_size = stream_end - self->base_address;
    return true;
fail:
    xx_ihexfmt_scan_cleanup(scan);
    return false;
}

/* Heap sort of block indices by (address, index), used once to find blocks
 * that start at the same address. */
static bool xx_ihexfmt_index_less(const xx_ihexfmt_block *blocks, uint32_t a,
                                  uint32_t b) {
    if (blocks[a].address != blocks[b].address) {
        return blocks[a].address < blocks[b].address;
    }
    return a < b;
}

static void xx_ihexfmt_sift_down(const xx_ihexfmt_block *blocks,
                                 uint32_t *order, size_t root, size_t count) {
    for (;;) {
        size_t child = root * 2U + 1U;
        uint32_t swap;
        if (child >= count) return;
        if (child + 1U < count &&
            xx_ihexfmt_index_less(blocks, order[child], order[child + 1U])) {
            ++child;
        }
        if (!xx_ihexfmt_index_less(blocks, order[root], order[child])) return;
        swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

/* Mark every block that starts where an earlier block starts; those get an
 * index suffix so that no two members share a name. */
static bool xx_ihexfmt_mark_duplicates(xx_ihexfmt_scan *scan) {
    uint32_t *order;
    size_t count = scan->block_count;
    size_t index;
    if (count < 2U) return true;
    order = (uint32_t *)xx_mem_alloc(count * sizeof(*order));
    if (!order) return false;
    for (index = 0U; index < count; ++index) order[index] = (uint32_t)index;
    for (index = count / 2U; index-- > 0U;) {
        xx_ihexfmt_sift_down(scan->blocks, order, index, count);
    }
    for (index = count - 1U; index > 0U; --index) {
        uint32_t swap = order[0];
        order[0] = order[index];
        order[index] = swap;
        xx_ihexfmt_sift_down(scan->blocks, order, 0U, index);
    }
    for (index = 1U; index < count; ++index) {
        if (scan->blocks[order[index]].address ==
            scan->blocks[order[index - 1U]].address) {
            scan->blocks[order[index]].renamed = true;
        }
    }
    xx_mem_free(order);
    return true;
}

/* Output buffer, so that a block is not written sixteen bytes at a time. */
#define XX_IHEXFMT_SINK_SIZE 65536U

typedef struct xx_ihexfmt_sink_s {
    xx_io_device *device;
    uint8_t *buffer;
    size_t used;
} xx_ihexfmt_sink;

static bool xx_ihexfmt_sink_flush(xx_ihexfmt_sink *sink) {
    size_t done = 0U;
    while (done < sink->used) {
        ssize_t amount = xx_io_write(sink->device, sink->buffer + done,
                                     sink->used - done);
        if (amount <= 0 || (size_t)amount > sink->used - done) return false;
        done += (size_t)amount;
    }
    sink->used = 0U;
    return true;
}

static bool xx_ihexfmt_sink_put(xx_ihexfmt_sink *sink, const uint8_t *data,
                                size_t size) {
    if (size > XX_IHEXFMT_SINK_SIZE - sink->used &&
        !xx_ihexfmt_sink_flush(sink)) {
        return false;
    }
    xx_rt_memcpy(sink->buffer + sink->used, data, size);
    sink->used += size;
    return true;
}

/* Decode one block from the text again, feeding its bytes to sink (or only
 * checking them when sink is NULL).  Only the block's own lines are read,
 * and every record must continue exactly where the parse said it would. */
static bool xx_ihexfmt_walk_records(Abstractformat *self,
                                    const xx_ihexfmt_block *block,
                                    xx_ihexfmt_sink *sink, xx_pd_struct *pd) {
    xx_ihexfmt_lines lines;
    char line[XX_IHEX_MAX_LINE_LENGTH];
    uint64_t expected;
    uint64_t remaining;
    uint32_t base;
    if (!self || !self->device || !block || block->text_offset < 0 ||
        block->text_end <= block->text_offset ||
        xx_io_seek64(self->device, block->text_offset, SEEK_SET) != 0) {
        return false;
    }
    xx_ihexfmt_lines_init(&lines, self->device, block->text_offset,
                          block->text_end);
    base = block->base;
    expected = block->address;
    remaining = block->size;
    while (remaining != 0U) {
        size_t length = 0U;
        int64_t start = -1;
        xx_ihexfmt_record record;
        uint64_t first;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (xx_ihexfmt_lines_next(&lines, line, sizeof(line), &length,
                                  &start) != XX_IHEXFMT_LINE_OK) {
            return false;
        }
        if (xx_ihexfmt_line_is_blank(line, length)) continue;
        if (!xx_ihexfmt_decode_line(line, length, &record)) return false;
        switch (record.type) {
            case XX_IHEXFMT_TYPE_DATA:
                if (record.count == 0U) break;
                first = (uint64_t)base + record.address;
                if (first != expected || record.count > remaining) {
                    return false;
                }
                if (sink && !xx_ihexfmt_sink_put(sink, record.data,
                                                 record.count)) {
                    return false;
                }
                expected += record.count;
                remaining -= record.count;
                break;
            case XX_IHEXFMT_TYPE_SEGMENT:
                base = xx_ihexfmt_be16(record.data) << 4U;
                break;
            case XX_IHEXFMT_TYPE_LINEAR:
                base = xx_ihexfmt_be16(record.data) << 16U;
                break;
            case XX_IHEXFMT_TYPE_START_SEGMENT:
            case XX_IHEXFMT_TYPE_START_LINEAR:
                break;
            default:
                return false;  /* EOF inside a block: the text changed. */
        }
    }
    return !sink || xx_ihexfmt_sink_flush(sink);
}

/* Write block to destination, or only verify it when destination is NULL. */
static bool xx_ihexfmt_walk_block(Abstractformat *self,
                                  const xx_ihexfmt_block *block,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    xx_ihexfmt_sink sink;
    bool result;
    if (!destination) return xx_ihexfmt_walk_records(self, block, NULL, pd);
    sink.device = destination;
    sink.used = 0U;
    sink.buffer = (uint8_t *)xx_mem_alloc(XX_IHEXFMT_SINK_SIZE);
    if (!sink.buffer) return false;
    result = xx_ihexfmt_walk_records(self, block, &sink, pd);
    xx_mem_free(sink.buffer);
    return result;
}

/* ------------------------------------------------------------------ */
/* Probe                                                               */
/* ------------------------------------------------------------------ */

bool xx_ihex_probe_device(xx_io_device *dev, int64_t base_address) {
    xx_ihexfmt_lines lines;
    char line[XX_IHEX_MAX_LINE_LENGTH];
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
    window = (int64_t)XX_IHEX_MAX_LINE_LENGTH * 4;
    if (window > total_size - base_address) window = total_size - base_address;
    xx_ihexfmt_lines_init(&lines, dev, base_address, base_address + window);
    for (;;) {
        size_t length = 0U;
        int64_t start = -1;
        xx_ihexfmt_record record;
        if (xx_ihexfmt_lines_next(&lines, line, sizeof(line), &length,
                                  &start) != XX_IHEXFMT_LINE_OK) {
            return false;
        }
        if (xx_ihexfmt_line_is_blank(line, length)) {
            if (++blanks > XX_IHEXFMT_PROBE_BLANK_LINES) return false;
            continue;
        }
        return xx_ihexfmt_decode_line(line, length, &record);
    }
}

/* ------------------------------------------------------------------ */
/* Options and records                                                 */
/* ------------------------------------------------------------------ */

static bool xx_ihexfmt_copy_options(xx_list_s *destination,
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

static const xx_var *xx_ihexfmt_find_option(const xx_list_s *options,
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

/* Largest member the caller allows, UINT64_MAX when unlimited. */
static uint64_t xx_ihexfmt_member_limit(const Abstractformat *self,
                                        const xx_list_s *options) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        self, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return UINT64_MAX;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : UINT64_MAX;
        }
        default: return UINT64_MAX;
    }
}

static void xx_ihexfmt_append(char *out, size_t capacity, size_t *used,
                              const char *text) {
    size_t index;
    for (index = 0U; text && text[index] != '\0'; ++index) {
        if (*used + 1U >= capacity) break;
        out[(*used)++] = text[index];
    }
    out[*used] = '\0';
}

/* Append value as hex, at least min_digits wide, without the CRT. */
static void xx_ihexfmt_append_hex(char *out, size_t capacity, size_t *used,
                                  uint64_t value, unsigned min_digits) {
    static const char digits[] = "0123456789ABCDEF";
    char text[17];
    unsigned count = 0U;
    unsigned index;
    do {
        text[count++] = digits[value & 0xFU];
        value >>= 4U;
    } while (value != 0U || count < min_digits);
    for (index = 0U; index < count / 2U; ++index) {
        char swap = text[index];
        text[index] = text[count - 1U - index];
        text[count - 1U - index] = swap;
    }
    text[count] = '\0';
    xx_ihexfmt_append(out, capacity, used, text);
}

static void xx_ihexfmt_append_dec(char *out, size_t capacity, size_t *used,
                                  uint64_t value) {
    char text[21];
    unsigned count = 0U;
    unsigned index;
    do {
        text[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (index = 0U; index < count / 2U; ++index) {
        char swap = text[index];
        text[index] = text[count - 1U - index];
        text[count - 1U - index] = swap;
    }
    text[count] = '\0';
    xx_ihexfmt_append(out, capacity, used, text);
}

/* "08000000.bin", or "08000000_<index>.bin" for a block that starts where an
 * earlier one starts.  Nothing from the text reaches the name except the
 * address, which is rendered as hex digits. */
static void xx_ihexfmt_block_name(const xx_ihexfmt_block *block, size_t index,
                                  char *out, size_t capacity) {
    size_t used = 0U;
    out[0] = '\0';
    xx_ihexfmt_append_hex(out, capacity, &used, block->address, 8U);
    if (block->renamed) {
        xx_ihexfmt_append(out, capacity, &used, "_");
        xx_ihexfmt_append_dec(out, capacity, &used, (uint64_t)index);
    }
    xx_ihexfmt_append(out, capacity, &used, ".bin");
}

static const xx_ihexfmt_block *xx_ihexfmt_get_block(const xx_ihex *archive,
                                                    uint64_t index) {
    const xx_ihexfmt_scan *scan =
        archive ? (const xx_ihexfmt_scan *)archive->internal : NULL;
    if (!scan || index >= (uint64_t)scan->block_count) return NULL;
    return &scan->blocks[index];
}

static bool xx_ihexfmt_populate_record(Abstractformat *self, size_t index,
                                       xx_archive_record *record) {
    const xx_ihex *archive;
    const xx_ihexfmt_block *block;
    char name[48];
    char comment[64];
    size_t used = 0U;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_ihex *)self;
    block = xx_ihexfmt_get_block(archive, (uint64_t)index);
    if (!block) return false;
    xx_ihexfmt_block_name(block, index, name, sizeof(name));
    comment[0] = '\0';
    xx_ihexfmt_append(comment, sizeof(comment), &used, "load=0x");
    xx_ihexfmt_append_hex(comment, sizeof(comment), &used, block->address,
                          8U);
    if (archive->has_entry_point) {
        xx_ihexfmt_append(comment, sizeof(comment), &used, " entry=0x");
        xx_ihexfmt_append_hex(comment, sizeof(comment), &used,
                              archive->entry_point, 8U);
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = block->text_offset;
    record->header_size = 0;
    record->data_offset = block->text_offset;
    record->compressed_size = block->text_end - block->text_offset;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          block->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(block->text_end - block->text_offset)) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          comment);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void xx_ihex_init(xx_ihex *archive, xx_io_device *dev, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    /* The addresses inside the text are big endian by convention; the file
     * itself has no binary byte order. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_IHEX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ihex");
    xx_format_set_extension(&archive->format, "hex");
    archive->format.check_is_valid = xx_ihex_check_is_valid;
    archive->format.handle_base_info = xx_ihex_handle_base_info;
    archive->format.get_format_size = xx_ihex_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ihex_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ihex_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ihex_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ihex_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ihex_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ihex_free_archive_records_reading;
    archive->format.destroy = xx_ihexfmt_vtable_destroy;
    archive->stream_end = -1;
}

xx_ihex *xx_ihex_create(xx_io_device *dev, int64_t base_address) {
    xx_ihex *archive = (xx_ihex *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ihex_init(archive, dev, base_address);
    return archive;
}

static void xx_ihexfmt_release_internal(xx_ihex *archive) {
    if (archive && archive->internal) {
        xx_ihexfmt_scan_cleanup((xx_ihexfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
}

static void xx_ihexfmt_reset_summary(xx_ihex *archive) {
    archive->number_of_lines = 0U;
    archive->number_of_data_records = 0U;
    archive->data_bytes = 0U;
    archive->number_of_blocks = 0U;
    archive->load_address = 0U;
    archive->end_address = 0U;
    archive->entry_point = 0U;
    archive->entry_type = 0U;
    archive->has_entry_point = false;
    archive->has_eof_record = false;
    archive->has_segment_records = false;
    archive->has_linear_records = false;
    archive->stream_end = -1;
}

void xx_ihex_destroy(xx_ihex *archive) {
    if (!archive) return;
    xx_ihexfmt_release_internal(archive);
    xx_format_cleanup_extra_parameters(&archive->format);
    xx_ihexfmt_reset_summary(archive);
}

static void xx_ihexfmt_vtable_destroy(Abstractformat *self) {
    xx_ihex_destroy((xx_ihex *)self);
}

void xx_ihex_free(xx_ihex *archive) {
    if (!archive) return;
    xx_ihex_destroy(archive);
    xx_mem_free(archive);
}

/* ------------------------------------------------------------------ */
/* Abstractformat surface                                              */
/* ------------------------------------------------------------------ */

bool xx_ihex_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ihexfmt_scan scan;
    bool result;
    /* Cheap bounded probe first so a binary file is rejected after at most
     * one line, then a bounded walk of the lines that start in the first
     * XX_IHEXFMT_VALIDATE_WINDOW bytes of text, which must carry at least one
     * data byte.  handle_base_info() accepts every file this accepts. */
    if (!self || !self->device ||
        !xx_ihex_probe_device(self->device, self->base_address)) {
        return false;
    }
    result = xx_ihexfmt_scan_run(self, &scan, pd, true);
    xx_ihexfmt_scan_cleanup(&scan);
    return result;
}

bool xx_ihex_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ihexfmt_scan *scan;
    xx_ihex *archive = (xx_ihex *)self;
    int64_t total_size;
    if (!self) return false;
    scan = (xx_ihexfmt_scan *)xx_mem_alloc(sizeof(*scan));
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
    }
    if (!scan || !xx_ihex_probe_device(self->device, self->base_address) ||
        !xx_ihexfmt_scan_run(self, scan, pd, false) ||
        !xx_ihexfmt_mark_duplicates(scan)) {
        if (scan) {
            xx_ihexfmt_scan_cleanup(scan);
            xx_mem_free(scan);
        }
        xx_ihexfmt_release_internal(archive);
        xx_ihexfmt_reset_summary(archive);
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    xx_ihexfmt_release_internal(archive);
    archive->internal = scan;
    archive->number_of_lines = scan->lines;
    archive->number_of_data_records = scan->data_records;
    archive->data_bytes = scan->data_bytes;
    archive->number_of_blocks = (uint64_t)scan->block_count;
    archive->load_address = scan->data_bytes != 0U ? scan->low : 0U;
    archive->end_address = scan->data_bytes != 0U ? scan->high : 0U;
    archive->entry_point = scan->entry_point;
    archive->entry_type = scan->entry_type;
    archive->has_entry_point = scan->has_entry_point;
    archive->has_eof_record = scan->seen_eof;
    archive->has_segment_records = scan->has_segment;
    archive->has_linear_records = scan->has_linear;
    archive->stream_end = self->base_address + scan->stream_size;
    self->format_size = scan->stream_size;
    /* Whatever follows the text is reported as overlay: matter after the EOF
     * record or an end-of-text marker, and, past the strict window, the first
     * line that broke a rule and everything after it. */
    total_size = xx_io_total_size(self->device);
    if (total_size > archive->stream_end) {
        self->overlay_offset = archive->stream_end;
        self->overlay_size = total_size - archive->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    /* A file that carries no data bytes (EOF record only) is valid and has
     * no member. */
    self->number_of_archive_records = (uint64_t)scan->block_count;
    self->file_type = XX_IHEX_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ihex_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ihex_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->number_of_archive_records;
}

bool xx_ihex_unpack_block_to_device(xx_ihex *archive, uint64_t index,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    const xx_ihexfmt_block *block;
    if (!archive ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid) {
        return false;
    }
    block = xx_ihexfmt_get_block(archive, index);
    return block && xx_ihexfmt_walk_block(&archive->format, block,
                                          destination, pd);
}

xx_archive_record_state *xx_ihex_create_archive_records_reading(
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
    if (!xx_ihexfmt_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->total_records = (int64_t)self->number_of_archive_records;
    if (self->number_of_archive_records == 0U) {
        /* Empty image: a valid reading session with no record. */
        state->has_record = false;
        return state;
    }
    if (!xx_ihexfmt_populate_record(self, 0U, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ihex_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ihex_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record ||
        state->current_index < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    next = state->current_index + 1;
    if ((uint64_t)next >= self->number_of_archive_records) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ihexfmt_populate_record(self, (size_t)next,
                                    &state->current_record)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

bool xx_ihex_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const xx_ihexfmt_block *block;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    char name[48];
    bool result;
    bool created = false;
    xx_ihex *archive = (xx_ihex *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        state->current_index < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    block = xx_ihexfmt_get_block(archive, (uint64_t)state->current_index);
    if (!block ||
        block->size > xx_ihexfmt_member_limit(self, &state->options)) {
        return false;
    }
    path_value = xx_ihexfmt_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify that the block decodes. */
        return xx_ihexfmt_walk_block(self, block, NULL, pd);
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
    /* The member name is built from the block's address alone (hex digits
     * plus a fixed suffix), so nothing else from the file reaches the
     * destination path. */
    xx_ihexfmt_block_name(block, (size_t)state->current_index, name,
                          sizeof(name));
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", name);
    } else {
        destination_path = xx_str_concat(base_path, name);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_ihexfmt_walk_block(self, block, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_ihex_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

uint64_t xx_ihex_get_number_of_lines(const xx_ihex *archive) {
    return archive ? archive->number_of_lines : 0U;
}
uint64_t xx_ihex_get_data_bytes(const xx_ihex *archive) {
    return archive ? archive->data_bytes : 0U;
}
uint64_t xx_ihex_get_number_of_blocks(const xx_ihex *archive) {
    return archive ? archive->number_of_blocks : 0U;
}
uint64_t xx_ihex_get_block_address(const xx_ihex *archive, uint64_t index) {
    const xx_ihexfmt_block *block = xx_ihexfmt_get_block(archive, index);
    return block ? block->address : 0U;
}
uint64_t xx_ihex_get_block_size(const xx_ihex *archive, uint64_t index) {
    const xx_ihexfmt_block *block = xx_ihexfmt_get_block(archive, index);
    return block ? block->size : 0U;
}
uint64_t xx_ihex_get_load_address(const xx_ihex *archive) {
    return archive ? archive->load_address : 0U;
}
uint32_t xx_ihex_get_entry_point(const xx_ihex *archive) {
    return archive ? archive->entry_point : 0U;
}
bool xx_ihex_get_has_entry_point(const xx_ihex *archive) {
    return archive ? archive->has_entry_point : false;
}
int64_t xx_ihex_get_stream_end(const xx_ihex *archive) {
    return archive ? archive->stream_end : -1;
}

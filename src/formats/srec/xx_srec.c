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
 *   2. a density rule - the span may not exceed the payload actually carried
 *      by the file by more than XX_SRECFMT_MAX_DENSITY times, with a small
 *      constant floor so genuinely small sparse images still load.
 *
 * Rule 2 is what refuses "a 100 KB file forcing a 100 MiB allocation" even
 * when rule 1 alone would have allowed it. */
#define XX_SRECFMT_MAX_IMAGE_SIZE (UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
#define XX_SRECFMT_MAX_DENSITY UINT64_C(64)
#define XX_SRECFMT_DENSITY_FLOOR (UINT64_C(64) * UINT64_C(1024))
#define XX_SRECFMT_MAX_LINES UINT64_C(4000000)
/* Consecutive non-record lines tolerated before the file is rejected. */
#define XX_SRECFMT_MAX_BLANK_LINES 64U
/* Bytes filled into address ranges no record covers. */
#define XX_SRECFMT_FILL_BYTE UINT8_C(0x00)
/* Longest S0 header text kept as a record name. */
#define XX_SRECFMT_MAX_HEADER_TEXT 64U

/** Buffered forward line reader over a device. */
typedef struct xx_srecfmt_lines_s {
    xx_io_device *device;
    int64_t position;      /**< Device offset of buffer[fill_start]. */
    int64_t end;
    uint8_t buffer[4096];
    size_t fill;           /**< Valid bytes in buffer. */
    size_t cursor;         /**< Next unread byte in buffer. */
    bool exhausted;
} xx_srecfmt_lines;

/** One decoded S-record line. */
typedef struct xx_srecfmt_record_s {
    uint8_t type;          /**< 0 .. 9, never 4. */
    uint8_t address_width; /**< 2, 3 or 4 bytes. */
    uint32_t address;
    uint8_t data[255];
    size_t data_size;
} xx_srecfmt_record;

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

/* Copy the next line, without its terminator, into out.  Returns false at end
 * of input or when the line is longer than XX_SREC_MAX_LINE_LENGTH - an
 * over-long line is a hard failure rather than a truncation, because an
 * unterminated multi-megabyte "line" is exactly what a hostile text file
 * looks like. */
static bool xx_srecfmt_lines_next(xx_srecfmt_lines *lines, char *out,
                                  size_t out_capacity, size_t *out_length,
                                  bool *out_overlong) {
    size_t used = 0U;
    bool any = false;
    if (out_overlong) *out_overlong = false;
    for (;;) {
        uint8_t ch;
        if (!xx_srecfmt_lines_fill(lines)) break;
        ch = lines->buffer[lines->cursor++];
        any = true;
        if (ch == (uint8_t)'\n') {
            if (out_length) *out_length = used;
            return true;
        }
        if (used >= out_capacity) {
            if (out_overlong) *out_overlong = true;
            return false;
        }
        out[used++] = (char)ch;
    }
    if (!any) return false;
    if (out_length) *out_length = used;
    return true;
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
    /* Trailing carriage return and spaces are tolerated; nothing else is. */
    while (length != 0U && (line[length - 1U] == '\r' ||
                            line[length - 1U] == ' ' ||
                            line[length - 1U] == '\t')) {
        --length;
    }
    if (length < 6U || length > XX_SREC_MAX_LINE_LENGTH ||
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
        char ch = line[index];
        if (ch != ' ' && ch != '\t' && ch != '\r') return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */

static void xx_srecfmt_scan_cleanup(xx_srecfmt_scan *scan) {
    if (!scan) return;
    if (scan->header_text) xx_str_free(scan->header_text);
    xx_mem_zero(scan, sizeof(*scan));
    scan->stream_size = -1;
}

/* S0 text is free-form vendor data.  It is kept as the record name only when
 * it looks like a single printable path component. */
static bool xx_srecfmt_plausible_name(const uint8_t *text, size_t length) {
    size_t index;
    if (!text || length == 0U || length > XX_SRECFMT_MAX_HEADER_TEXT) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t ch = text[index];
        if (ch < 32U || ch >= 127U || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' ||
            ch == '*' || ch == ' ') {
            return false;
        }
    }
    if (text[0] == '.' && (length == 1U || (length == 2U && text[1] == '.'))) {
        return false;
    }
    return text[length - 1U] != '.';
}

/* Walk the whole text once.  No image is built here; the pass only measures
 * the addressed span so that the span can be refused before it is allocated. */
static bool xx_srecfmt_scan_run(Abstractformat *self, xx_srecfmt_scan *scan,
                                xx_pd_struct *pd) {
    xx_srecfmt_lines lines;
    char line[XX_SREC_MAX_LINE_LENGTH];
    int64_t total_size;
    uint64_t low = UINT64_MAX;
    uint64_t high = 0U;
    unsigned blanks = 0U;
    bool seen_record = false;
    bool seen_terminator = false;
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
        scan->stream_size = -1;
        scan->is_contiguous = true;
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
    xx_srecfmt_lines_init(&lines, self->device, self->base_address,
                          total_size);
    for (;;) {
        size_t length = 0U;
        bool overlong = false;
        xx_srecfmt_record record;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                   &overlong)) {
            if (overlong) goto fail;
            break;
        }
        if (xx_srecfmt_line_is_blank(line, length)) {
            /* Blank padding is common, an unbounded run of it is not. */
            if (++blanks > XX_SRECFMT_MAX_BLANK_LINES) goto fail;
            continue;
        }
        blanks = 0U;
        if (!xx_srecfmt_decode_line(line, length, &record)) goto fail;
        if (++scan->lines > XX_SRECFMT_MAX_LINES) goto fail;
        seen_record = true;
        /* Records after a termination record are a malformed file. */
        if (seen_terminator) goto fail;
        switch (record.type) {
            case 0U:
                if (!scan->header_text && record.data_size != 0U &&
                    xx_srecfmt_plausible_name(record.data, record.data_size)) {
                    char *copy =
                        (char *)xx_mem_alloc(record.data_size + 1U);
                    if (!copy) goto fail;
                    xx_rt_memcpy(copy, record.data, record.data_size);
                    copy[record.data_size] = '\0';
                    scan->header_text = copy;
                }
                break;
            case 1U:
            case 2U:
            case 3U: {
                uint64_t start = record.address;
                uint64_t last;
                if (record.data_size == 0U) break;
                last = start + (uint64_t)record.data_size - 1U;
                if (record.address_width > scan->address_width) {
                    scan->address_width = record.address_width;
                }
                if (start < low) low = start;
                if (last > high) high = last;
                scan->data_bytes += record.data_size;
                ++scan->data_records;
                /* Bound the span as it grows so a hostile address cannot be
                 * accumulated and only noticed at the end. */
                if (high - low >= XX_SRECFMT_MAX_IMAGE_SIZE) goto fail;
                break;
            }
            case 5U:
            case 6U:
                scan->declared_record_count = record.address;
                scan->has_record_count = true;
                break;
            case 7U:
            case 8U:
            case 9U:
                scan->entry_point = record.address;
                scan->has_entry_point = true;
                seen_terminator = true;
                break;
            default:
                goto fail;
        }
    }
    if (!seen_record || scan->data_records == 0U || low > high) goto fail;
    scan->load_address = low;
    scan->end_address = high;
    scan->image_size = high - low + 1U;
    scan->is_contiguous = scan->image_size == scan->data_bytes;
    /* Absolute ceiling, then the density rule. */
    if (scan->image_size > XX_SRECFMT_MAX_IMAGE_SIZE) goto fail;
    if (scan->image_size > XX_SRECFMT_DENSITY_FLOOR &&
        (scan->data_bytes > UINT64_MAX / XX_SRECFMT_MAX_DENSITY ||
         scan->image_size > scan->data_bytes * XX_SRECFMT_MAX_DENSITY)) {
        goto fail;
    }
    /* Overlapping records would make data_bytes exceed the span; that is a
     * malformed file rather than a last-writer-wins merge. */
    if (scan->data_bytes > scan->image_size) goto fail;
    if (scan->has_record_count &&
        scan->declared_record_count != scan->data_records) {
        goto fail;
    }
    scan->stream_size = total_size - self->base_address;
    return true;
fail:
    xx_srecfmt_scan_cleanup(scan);
    return false;
}

/* Second pass: allocate the already bounded span and place every data record
 * into it.  Gaps keep XX_SRECFMT_FILL_BYTE. */
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
        scan->image_size > (uint64_t)SIZE_MAX ||
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
        bool overlong = false;
        xx_srecfmt_record record;
        uint64_t offset;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                   &overlong)) {
            if (overlong) goto fail;
            break;
        }
        if (xx_srecfmt_line_is_blank(line, length)) continue;
        if (!xx_srecfmt_decode_line(line, length, &record)) goto fail;
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
        bool overlong = false;
        xx_srecfmt_record record;
        if (!xx_srecfmt_lines_next(&lines, line, sizeof(line), &length,
                                   &overlong)) {
            return false;
        }
        if (xx_srecfmt_line_is_blank(line, length)) {
            if (++blanks > 4U) return false;
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

/* Format "load=0x... entry=0x..." without the CRT. */
static void xx_srecfmt_describe(const xx_srec *archive, char *out,
                                size_t capacity) {
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    unsigned pass;
    if (!out || capacity == 0U) return;
    out[0] = '\0';
    for (pass = 0U; pass < 2U; ++pass) {
        const char *label = pass == 0U ? "load=0x" : " entry=0x";
        uint64_t value = pass == 0U ? archive->load_address
                                    : archive->entry_point;
        unsigned shift = 60U;
        bool started = false;
        size_t index;
        if (pass == 1U && !archive->has_entry_point) break;
        for (index = 0U; label[index] != '\0'; ++index) {
            if (used + 1U >= capacity) return;
            out[used++] = label[index];
        }
        for (;;) {
            unsigned nibble = (unsigned)((value >> shift) & 0xFU);
            if (nibble != 0U || started || shift == 0U) {
                if (used + 1U >= capacity) return;
                out[used++] = digits[nibble];
                started = true;
            }
            if (shift == 0U) break;
            shift -= 4U;
        }
        out[used] = '\0';
    }
}

static const char *xx_srecfmt_record_name(const xx_srec *archive) {
    const xx_srecfmt_scan *scan =
        archive ? (const xx_srecfmt_scan *)archive->internal : NULL;
    return (scan && scan->header_text) ? scan->header_text
                                       : XX_SRECFMT_PAYLOAD_NAME;
}

static bool xx_srecfmt_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_srec *archive;
    char description[96];
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
    return xx_archive_record_set_original_name(
               record, xx_srecfmt_record_name(archive)) &&
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

static bool xx_srecfmt_safe_name(const char *name) {
    size_t length;
    size_t index;
    if (!name) return false;
    length = xx_str_len(name);
    if (length == 0U || length > XX_SRECFMT_MAX_HEADER_TEXT) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch >= 127U || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' ||
            ch == '*') {
            return false;
        }
    }
    if (name[0] == '.' && (length == 1U || (length == 2U && name[1] == '.'))) {
        return false;
    }
    return name[length - 1U] != ' ' && name[length - 1U] != '.';
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

void xx_srec_destroy(xx_srec *archive) {
    if (!archive) return;
    if (archive->internal) {
        xx_srecfmt_scan_cleanup((xx_srecfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
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
    /* Cheap bounded probe first so a binary file is rejected without a full
     * text walk. */
    if (!self || !self->device || !xx_srec_probe_device(self->device,
                                                        self->base_address)) {
        return false;
    }
    result = xx_srecfmt_scan_run(self, &scan, pd);
    xx_srecfmt_scan_cleanup(&scan);
    return result;
}

bool xx_srec_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_srecfmt_scan *scan;
    xx_srec *archive = (xx_srec *)self;
    if (!self) return false;
    scan = (xx_srecfmt_scan *)xx_mem_alloc(sizeof(*scan));
    if (!scan || !xx_srec_probe_device(self->device, self->base_address) ||
        !xx_srecfmt_scan_run(self, scan, pd)) {
        if (scan) xx_mem_free(scan);
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
    if (archive->internal) {
        xx_srecfmt_scan_cleanup((xx_srecfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
    }
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
    /* The parser consumes the text to the end of the device, so there is no
     * overlay to report. */
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
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
    return 1U;
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
    if (!scan || !xx_srecfmt_build(&archive->format, scan, &image, &image_size,
                                   pd)) {
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
    if (!xx_srecfmt_copy_options(&state->options, options) ||
        !xx_srecfmt_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
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
    const char *name;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    xx_srec *archive = (xx_srec *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!xx_srecfmt_safe_name(name)) name = XX_SRECFMT_PAYLOAD_NAME;
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

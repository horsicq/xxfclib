/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * yEnc text: one or more "=ybegin" ... "=yend" blocks; consecutive parts of
 * one file are joined into one member.  xx_yenc_encoded_file.h carries the
 * grammar and the acceptance rules.
 *
 * Format knowledge: the public yEnc 1.2 description (yenc.org).  XArchive's
 * XYEnc (transport/xyenc.cpp, MIT) was consulted for behaviour only; the
 * streaming block parser, the run joining and the member walk below are this
 * reader's own code.  The name sanitising follows xxfclib's own uue reader.
 *
 * Nothing is materialised: every pass streams through a 32 KiB read window
 * and a 4 KiB decode buffer.  check_is_valid reads 4 KiB windows, at most
 * the 32 KiB preamble plus the first block's first 64 KiB, and stops at the
 * first binary byte of the preamble.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/yenc_encoded_file/xx_yenc_encoded_file.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef YENC_ENCODED_FILE
#define XX_YENC_ENCODED_FILE_FILE_TYPE XX_FILE_TYPE_YENC_ENCODED_FILE
#else
#define XX_YENC_ENCODED_FILE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Text allowed before the first header. */
#define YE_PREAMBLE ((int64_t)32 * 1024)
/* Text allowed between two blocks. */
#define YE_MAX_GAP ((int64_t)64 * 1024)
/* Bytes of the first block's body the probe parses. */
#define YE_WINDOW ((int64_t)64 * 1024)
/* Longest control line (=ybegin, =ypart, =yend) kept. */
#define YE_CTRL_MAX 1024U
/* Longest data line, in encoded characters. */
#define YE_BODY_MAX 16384U
#define YE_READ_WINDOW ((size_t)32 * 1024)
#define YE_PROBE_READ ((size_t)4 * 1024)
#define YE_DECODE_BUFFER 4096U
#define YE_MAX_MEMBERS 65536U
#define YE_MAX_PARTS 65536U
/* Largest decimal attribute: 15 digits. */
#define YE_MAX_DIGITS 15U
#define YE_NAME_MAX 240U
#define YE_PAYLOAD_NAME "payload"

enum {
    YE_REJECT = 0,
    YE_CLEAN = 1,
    YE_FAILED = 2
};

/* ---------------------------------------------------------------------- */
/* Buffered byte reader                                                    */

typedef struct ye_line_s {
    int64_t start;
    int64_t next;  /**< Past the line break (unless runaway). */
    size_t length; /**< Bytes kept in text. */
    bool eof;      /**< start is at EOF. */
    bool has_eol;
    bool overlong; /**< Longer than YE_CTRL_MAX (next still valid). */
    bool runaway;  /**< No line break within the scan limit. */
    bool binary;   /**< A byte no text line holds. */
    uint8_t text[YE_CTRL_MAX];
} ye_line;

typedef struct ye_reader_s {
    Abstractformat *format;
    xx_pd_struct *pd;
    int64_t size;
    int64_t buffer_offset;
    size_t buffer_fill;
    bool failed;
    size_t capacity;
    uint8_t *buffer;
    ye_line line;
    ye_line scratch;
} ye_reader;

static bool ye_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
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

static bool ye_total(Abstractformat *format, int64_t *size)
{
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *size = total - format->base_address;
    return *size >= 16;
}

static ye_reader *ye_reader_create(Abstractformat *format, xx_pd_struct *pd)
{
    ye_reader *reader;
    int64_t size;
    if (!ye_total(format, &size)) return NULL;
    reader = (ye_reader *)xx_mem_calloc(1U, sizeof(*reader));
    if (!reader) return NULL;
    reader->capacity = YE_READ_WINDOW;
    reader->buffer = (uint8_t *)xx_mem_alloc(reader->capacity);
    if (!reader->buffer) {
        xx_mem_free(reader);
        return NULL;
    }
    reader->format = format;
    reader->pd = pd;
    reader->size = size;
    return reader;
}

static void ye_reader_free(ye_reader *reader)
{
    if (!reader) return;
    xx_mem_free(reader->buffer);
    xx_mem_free(reader);
}

/* The byte at `offset`, -1 at EOF or on failure (then `failed` is set). */
static int ye_byte(ye_reader *reader, int64_t offset)
{
    if (reader->failed || offset < 0 || offset >= reader->size) return -1;
    if (offset < reader->buffer_offset || offset >= reader->buffer_offset + (int64_t)reader->buffer_fill) {
        int64_t left = reader->size - offset;
        size_t want = (uint64_t)left < reader->capacity ? (size_t)left : reader->capacity;
        if ((reader->pd && xx_pd_is_stopped(reader->pd)) || !ye_read_at(reader->format->device, reader->format->base_address + offset, reader->buffer, want)) {
            reader->failed = true;
            reader->buffer_fill = 0U;
            return -1;
        }
        reader->buffer_offset = offset;
        reader->buffer_fill = want;
    }
    return reader->buffer[offset - reader->buffer_offset];
}

static bool ye_is_binary(int c)
{
    return c < 0x20 && c != '\t' && c != 0x0C && c != 0x1B;
}

/* Read the line at `offset`: a break is LF, or CRs with an optional LF.  The
 * first YE_CTRL_MAX bytes are kept; scanning stops after `scan_max` bytes
 * (runaway).  False only on a read failure. */
static bool ye_read_line(ye_reader *reader, int64_t offset, int64_t scan_max, ye_line *line)
{
    int64_t position = offset;
    int c;
    line->start = offset;
    line->next = offset;
    line->length = 0U;
    line->eof = false;
    line->has_eol = false;
    line->overlong = false;
    line->runaway = false;
    line->binary = false;
    if (offset >= reader->size) {
        line->eof = true;
        return !reader->failed;
    }
    for (;;) {
        c = ye_byte(reader, position);
        if (c < 0) {
            if (reader->failed) return false;
            line->next = position;
            return true;
        }
        if (c == '\n') {
            line->has_eol = true;
            line->next = position + 1;
            return true;
        }
        if (c == '\r') {
            ++position;
            while ((c = ye_byte(reader, position)) == '\r') ++position;
            if (reader->failed) return false;
            if (c == '\n') ++position;
            line->has_eol = true;
            line->next = position;
            return true;
        }
        if (position - offset >= scan_max) {
            line->runaway = true;
            return true;
        }
        if (ye_is_binary(c)) line->binary = true;
        if (line->length < YE_CTRL_MAX) line->text[line->length++] = (uint8_t)c;
        else line->overlong = true;
        ++position;
    }
}

static bool ye_starts(const ye_line *line, const char *prefix, size_t size)
{
    return line->length >= size && xx_rt_memcmp(line->text, prefix, size) == 0;
}

/* ---------------------------------------------------------------------- */
/* Attributes                                                              */

typedef struct ye_attrs_s {
    bool has_size, has_line, has_part, has_total, has_begin, has_end;
    bool has_pcrc, has_crc, has_name;
    uint64_t size, line, part, total, begin, end;
    uint32_t pcrc, crc;
    size_t name_start, name_length;
} ye_attrs;

static bool ye_key_is(const uint8_t *key, size_t size, const char *word)
{
    size_t index;
    for (index = 0U; index < size; ++index) {
        uint8_t c = key[index];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
        if (!word[index] || c != (uint8_t)word[index]) return false;
    }
    return word[size] == 0;
}

static bool ye_parse_dec(const uint8_t *text, size_t size, uint64_t *out)
{
    uint64_t value = 0U;
    size_t index;
    if (size == 0U || size > YE_MAX_DIGITS) return false;
    for (index = 0U; index < size; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        value = value * 10U + (uint64_t)(text[index] - '0');
    }
    *out = value;
    return true;
}

static bool ye_parse_hex(const uint8_t *text, size_t size, uint32_t *out)
{
    uint32_t value = 0U;
    size_t index;
    if (size == 0U || size > 8U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = text[index];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A' + 10);
        else return false;
        value = (value << 4U) | digit;
    }
    *out = value;
    return true;
}

/* key=value tokens after `prefix`; "name=" (when `with_name`) takes the
 * rest of the line.  Unknown keys are skipped; a repeated known key, a token
 * without '=' or a malformed number rejects the line. */
static bool ye_parse_attrs(const ye_line *line, size_t prefix, bool with_name, ye_attrs *attrs)
{
    const uint8_t *text = line->text;
    size_t length = line->length, index = prefix;
    xx_mem_zero(attrs, sizeof(*attrs));
    if (line->eof || line->overlong || line->runaway || prefix > length) return false;
    for (index = 0U; index < length; ++index)
        if (text[index] < 0x20U && text[index] != '\t') return false;
    index = prefix;
    while (index < length) {
        size_t key, key_size, value, value_size;
        bool ok = true;
        while (index < length && (text[index] == ' ' || text[index] == '\t')) ++index;
        if (index >= length) break;
        key = index;
        while (index < length && text[index] != '=' && text[index] != ' ' && text[index] != '\t') ++index;
        if (index >= length || text[index] != '=' || index == key) return false;
        key_size = index - key;
        value = ++index;
        if (with_name && ye_key_is(text + key, key_size, "name")) {
            attrs->has_name = true;
            attrs->name_start = value;
            attrs->name_length = length - value;
            break;
        }
        while (index < length && text[index] != ' ' && text[index] != '\t') ++index;
        value_size = index - value;
#define YE_DEC(field, word)                                         \
    if (ye_key_is(text + key, key_size, word)) {                    \
        if (attrs->has_##field) return false;                       \
        attrs->has_##field = true;                                  \
        ok = ye_parse_dec(text + value, value_size, &attrs->field); \
    } else
#define YE_HEX(field, word)                                         \
    if (ye_key_is(text + key, key_size, word)) {                    \
        if (attrs->has_##field) return false;                       \
        attrs->has_##field = true;                                  \
        ok = ye_parse_hex(text + value, value_size, &attrs->field); \
    } else
        YE_DEC(size, "size")
        YE_DEC(line, "line")
        YE_DEC(part, "part")
        YE_DEC(total, "total")
        YE_DEC(begin, "begin")
        YE_DEC(end, "end")
        YE_HEX(pcrc, "pcrc32")
        YE_HEX(crc, "crc32")
        { /* unknown key */
        }
#undef YE_DEC
#undef YE_HEX
        if (!ok) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static char ye_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ye_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || ye_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

static bool ye_safe_name(const char *name, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (!name || length == 0U || length > YE_NAME_MAX) return false;
    if (length == 1U && name[0] == '-') return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ye_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((ye_upper(name[0]) == 'C' && ye_upper(name[1]) == 'O' && ye_upper(name[2]) == 'M') ||
         (ye_upper(name[0]) == 'L' && ye_upper(name[1]) == 'P' && ye_upper(name[2]) == 'T')))
        return false;
    return true;
}

static void ye_set_name(char *target, const uint8_t *name, size_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    char out[YE_NAME_MAX + 1U];
    size_t start = 0U, index, used = 0U;
    bool ok = true;
    while (length > 0U && (name[length - 1U] == ' ' || name[length - 1U] == '\t')) --length;
    while (length > 0U && (name[0] == ' ' || name[0] == '\t')) {
        ++name;
        --length;
    }
    if (length >= 2U && name[0] == '"' && name[length - 1U] == '"') {
        ++name;
        length -= 2U;
    }
    for (index = 0U; index < length; ++index)
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':') start = index + 1U;
    for (index = start; index < length; ++index) {
        uint8_t c = name[index];
        if (c >= 0x80U || c == '%') {
            if (used + 3U > YE_NAME_MAX) {
                ok = false;
                break;
            }
            out[used++] = '%';
            out[used++] = hex[c >> 4U];
            out[used++] = hex[c & 15U];
        } else {
            if (used + 1U > YE_NAME_MAX) {
                ok = false;
                break;
            }
            out[used++] = (char)c;
        }
    }
    if (!ok || !ye_safe_name(out, used)) {
        used = sizeof(YE_PAYLOAD_NAME) - 1U;
        xx_rt_memcpy(out, YE_PAYLOAD_NAME, used);
    }
    xx_rt_memcpy(target, out, used);
    target[used] = 0;
}

/* ---------------------------------------------------------------------- */
/* Blocks                                                                  */

typedef struct ye_block_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t end;          /**< Past the =yend line. */
    uint64_t file_size;   /**< size= of =ybegin. */
    uint64_t begin, last; /**< 1-based inclusive part range. */
    uint64_t decoded;     /**< Bytes of this block. */
    uint64_t part, total;
    uint32_t crc; /**< CRC-32 of this block's bytes. */
    bool multipart;
    bool windowed; /**< The probe stopped at its window. */
    /* The run of joined parts this block ends. */
    bool continues;     /**< Joined to the block before it. */
    uint64_t run_first; /**< begin of the run's first part. */
    uint64_t run_size;
    uint32_t run_crc;
    uint32_t run_parts;
    bool run_has_file_crc;
    uint32_t run_file_crc;
    char name[YE_NAME_MAX + 1U];
} ye_block;

typedef struct ye_sink_s {
    xx_io_device *destination;
} ye_sink;

static bool ye_write_all(xx_io_device *destination, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Parse the block whose "=ybegin " line is `header`.  `previous` is the
 * block right before it (for joining), NULL for the first.  `window_end`
 * >= 0 stops the parse cleanly at the first body line starting there.
 * With `sink`, the decoded bytes are written. */
static int ye_parse_block(ye_reader *reader, const ye_line *header, const ye_block *previous, ye_block *block, int64_t window_end, ye_sink *sink)
{
    ye_line *line = &reader->scratch;
    ye_attrs attrs;
    uint8_t out[YE_DECODE_BUFFER];
    size_t used = 0U;
    uint64_t expected;
    int64_t position;
    uint32_t part_crc = 0U, run_crc;

    xx_mem_zero(block, sizeof(*block));
    if (!header->has_eol || !ye_starts(header, "=ybegin ", 8U) || !ye_parse_attrs(header, 8U, true, &attrs) || !attrs.has_size || !attrs.has_name) return YE_REJECT;
    if (attrs.has_line && (attrs.line == 0U || attrs.line > YE_BODY_MAX)) return YE_REJECT;
    block->header_offset = header->start;
    block->file_size = attrs.size;
    block->multipart = attrs.has_part;
    ye_set_name(block->name, header->text + attrs.name_start, attrs.name_length);
    position = header->next;
    if (block->multipart) {
        ye_attrs part;
        if (attrs.part == 0U || attrs.part > YE_MAX_PARTS || (attrs.has_total && (attrs.total == 0U || attrs.total > YE_MAX_PARTS || attrs.part > attrs.total)) ||
            attrs.size == 0U)
            return YE_REJECT;
        block->part = attrs.part;
        block->total = attrs.has_total ? attrs.total : 0U;
        if (!ye_read_line(reader, position, (int64_t)YE_CTRL_MAX + 1, line)) return YE_FAILED;
        if (!line->has_eol || !ye_starts(line, "=ypart ", 7U) || !ye_parse_attrs(line, 7U, false, &part) || !part.has_begin || !part.has_end || part.begin == 0U ||
            part.end < part.begin || part.end > attrs.size)
            return YE_REJECT;
        block->begin = part.begin;
        block->last = part.end;
        expected = part.end - part.begin + 1U;
        position = line->next;
    } else {
        if (attrs.has_total) return YE_REJECT;
        block->begin = 1U;
        block->last = attrs.size;
        expected = attrs.size;
    }
    block->data_offset = position;

    /* Joining: the same file's next part, contiguous. */
    if (previous && block->multipart && previous->multipart && previous->run_parts < YE_MAX_PARTS && previous->file_size == block->file_size &&
        previous->part + 1U == block->part && previous->last + 1U == block->begin && xx_str_cmp(previous->name, block->name) == 0) {
        block->continues = true;
        block->run_first = previous->run_first;
        block->run_size = previous->run_size;
        block->run_crc = previous->run_crc;
        block->run_parts = previous->run_parts;
        block->run_has_file_crc = previous->run_has_file_crc;
        block->run_file_crc = previous->run_file_crc;
    } else {
        block->run_first = block->begin;
    }
    run_crc = block->run_crc;

    for (;;) {
        int c;
        size_t line_length = 0U;
        if (window_end >= 0 && position >= window_end) {
            block->windowed = true;
            return YE_CLEAN;
        }
        c = ye_byte(reader, position);
        if (c < 0) return reader->failed ? YE_FAILED : YE_REJECT;
        if (c == '=' && ye_byte(reader, position + 1) == 'y') {
            if (!ye_read_line(reader, position, (int64_t)YE_CTRL_MAX + 1, line)) return YE_FAILED;
            if (ye_starts(line, "=ybegin ", 8U) || ye_starts(line, "=ypart ", 7U)) return YE_REJECT;
            if (ye_starts(line, "=yend", 5U) && (line->length == 5U || line->text[5] == ' ' || line->text[5] == '\t')) break;
        }
        if (reader->failed) return YE_FAILED;
        for (;;) {
            uint8_t value;
            c = ye_byte(reader, position);
            if (c < 0) return reader->failed ? YE_FAILED : YE_REJECT;
            if (c == '\n') {
                ++position;
                break;
            }
            if (c == '\r') {
                ++position;
                while ((c = ye_byte(reader, position)) == '\r') ++position;
                if (reader->failed) return YE_FAILED;
                if (c == '\n') ++position;
                break;
            }
            if (c == 0) return YE_REJECT;
            if (c == '=') {
                c = ye_byte(reader, position + 1);
                if (c < 0) return reader->failed ? YE_FAILED : YE_REJECT;
                if (c == '\n' || c == '\r' || c == 0) return YE_REJECT;
                value = (uint8_t)(c - 106);
                position += 2;
                line_length += 2U;
            } else {
                value = (uint8_t)(c - 42);
                ++position;
                ++line_length;
            }
            if (line_length > YE_BODY_MAX || block->decoded >= expected) return YE_REJECT;
            out[used++] = value;
            ++block->decoded;
            if (used == sizeof(out)) {
                part_crc = xx_crc32_calc(part_crc, out, used);
                run_crc = xx_crc32_calc(run_crc, out, used);
                if (sink && !ye_write_all(sink->destination, out, used)) return YE_FAILED;
                used = 0U;
            }
        }
    }
    if (used != 0U) {
        part_crc = xx_crc32_calc(part_crc, out, used);
        run_crc = xx_crc32_calc(run_crc, out, used);
        if (sink && !ye_write_all(sink->destination, out, used)) return YE_FAILED;
    }
    /* `line` holds the =yend line. */
    if (!line->has_eol && line->next != reader->size) return YE_REJECT;
    if (!ye_parse_attrs(line, 5U, false, &attrs) || !attrs.has_size || attrs.size != block->decoded || block->decoded != expected) return YE_REJECT;
    if (block->multipart && attrs.has_part && attrs.part != block->part) return YE_REJECT;
    if (attrs.has_pcrc && attrs.pcrc != part_crc) return YE_REJECT;
    block->crc = part_crc;
    block->end = line->next;
    block->run_crc = run_crc;
    block->run_size += block->decoded;
    ++block->run_parts;
    if (attrs.has_crc) {
        if (!block->multipart) {
            if (attrs.crc != part_crc) return YE_REJECT;
        } else {
            if (block->run_has_file_crc && block->run_file_crc != attrs.crc) return YE_REJECT;
            block->run_has_file_crc = true;
            block->run_file_crc = attrs.crc;
        }
    }
    /* A run that now covers the whole file must match its crc32. */
    if (block->multipart && block->run_first == 1U && block->last == block->file_size && block->run_has_file_crc && block->run_file_crc != run_crc) return YE_REJECT;
    return YE_CLEAN;
}

/* Find the next "=ybegin " line from `from`, skipping at most `limit` bytes
 * of text.  YE_CLEAN with reader->line holding it, YE_REJECT when none. */
static int ye_find_header(ye_reader *reader, int64_t from, int64_t limit)
{
    ye_line *line = &reader->line;
    int64_t position = from;
    for (;;) {
        int64_t left = limit - (position - from);
        if (left < 0) return YE_REJECT;
        if (!ye_read_line(reader, position, left + 1, line)) return YE_FAILED;
        if (line->eof || line->runaway || line->binary) return YE_REJECT;
        if (ye_starts(line, "=ybegin ", 8U)) return YE_CLEAN;
        if (!line->has_eol || line->next <= position) return YE_REJECT;
        position = line->next;
    }
}

/* ---------------------------------------------------------------------- */
/* Members                                                                 */

typedef struct ye_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t end;
    uint64_t size;
    uint32_t crc;
    uint32_t parts;
    char name[YE_NAME_MAX + 1U];
} ye_member;

typedef struct ye_cursor_s {
    ye_reader *reader;
    ye_block look;
    bool has_look;
} ye_cursor;

/* The first block (probe: only its window). */
static int ye_first(ye_cursor *cursor, bool probe)
{
    ye_reader *reader = cursor->reader;
    int64_t start = 0;
    int result;
    cursor->has_look = false;
    if (ye_byte(reader, 0) == 0xEF && ye_byte(reader, 1) == 0xBB && ye_byte(reader, 2) == 0xBF) start = 3;
    if (reader->failed) return YE_FAILED;
    result = ye_find_header(reader, start, YE_PREAMBLE);
    if (result != YE_CLEAN) return result;
    result = ye_parse_block(reader, &reader->line, NULL, &cursor->look, probe ? reader->line.next + YE_WINDOW : -1, NULL);
    cursor->has_look = result == YE_CLEAN;
    return result;
}

/* Parse the block after `previous` into the lookahead. */
static int ye_fetch_after(ye_cursor *cursor, const ye_block *previous)
{
    ye_reader *reader = cursor->reader;
    int result;
    cursor->has_look = false;
    result = ye_find_header(reader, previous->end, YE_MAX_GAP);
    if (result == YE_FAILED) return YE_FAILED;
    if (result != YE_CLEAN) return YE_CLEAN;
    result = ye_parse_block(reader, &reader->line, previous, &cursor->look, -1, NULL);
    if (result == YE_FAILED) return YE_FAILED;
    cursor->has_look = result == YE_CLEAN;
    return YE_CLEAN;
}

/* Take the member starting at the lookahead; the lookahead moves past it. */
static int ye_take_member(ye_cursor *cursor, ye_member *member)
{
    ye_block current;
    if (!cursor->has_look) return YE_REJECT;
    current = cursor->look;
    member->header_offset = current.header_offset;
    member->data_offset = current.data_offset;
    xx_rt_memcpy(member->name, current.name, sizeof(member->name));
    for (;;) {
        member->end = current.end;
        member->size = current.run_size;
        member->crc = current.run_crc;
        member->parts = current.run_parts;
        if (ye_fetch_after(cursor, &current) == YE_FAILED) return YE_FAILED;
        if (!cursor->has_look || !cursor->look.continues) return YE_CLEAN;
        current = cursor->look;
    }
}

typedef struct ye_summary_s {
    uint64_t count;
    uint64_t unpacked_size;
    int64_t text_size;
} ye_summary;

static bool ye_walk(Abstractformat *format, bool probe, ye_summary *summary, xx_pd_struct *pd)
{
    ye_cursor cursor;
    ye_member member;
    bool ok = false;
    xx_mem_zero(summary, sizeof(*summary));
    cursor.reader = ye_reader_create(format, pd);
    if (!cursor.reader) return false;
    /* The probe mostly meets binary files: read small windows. */
    if (probe) cursor.reader->capacity = YE_PROBE_READ;
    if (ye_first(&cursor, probe) != YE_CLEAN) goto done;
    if (probe) {
        ok = true;
        goto done;
    }
    while (cursor.has_look && summary->count < YE_MAX_MEMBERS) {
        if (ye_take_member(&cursor, &member) != YE_CLEAN) goto done;
        ++summary->count;
        summary->unpacked_size += member.size;
        summary->text_size = member.end;
    }
    ok = summary->count != 0U;
done:
    ye_reader_free(cursor.reader);
    return ok;
}

/* Decode `member` to `destination` (only check it when NULL). */
static bool ye_decode(Abstractformat *format, const ye_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    ye_reader *reader = ye_reader_create(format, pd);
    ye_block previous, block;
    ye_sink sink;
    uint32_t index;
    bool ok = false;
    if (!reader) return false;
    sink.destination = destination;
    if (!ye_read_line(reader, member->header_offset, (int64_t)YE_CTRL_MAX + 1, &reader->line) ||
        ye_parse_block(reader, &reader->line, NULL, &block, -1, destination ? &sink : NULL) != YE_CLEAN)
        goto done;
    for (index = 1U; index < member->parts; ++index) {
        previous = block;
        if (ye_find_header(reader, previous.end, YE_MAX_GAP) != YE_CLEAN ||
            ye_parse_block(reader, &reader->line, &previous, &block, -1, destination ? &sink : NULL) != YE_CLEAN || !block.continues)
            goto done;
    }
    ok = block.run_parts == member->parts && block.run_size == member->size && block.run_crc == member->crc && block.end == member->end;
done:
    ye_reader_free(reader);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct ye_stream_s {
    ye_cursor cursor;
    ye_member member;
    uint64_t index;
    uint64_t count;
    uint64_t *seen;
    size_t seen_mask;
    char name[YE_NAME_MAX + 24U];
} ye_stream;

static void ye_stream_free(void *opaque)
{
    ye_stream *stream = (ye_stream *)opaque;
    if (!stream) return;
    ye_reader_free(stream->cursor.reader);
    xx_mem_free(stream->seen);
    xx_mem_free(stream);
}

static uint64_t ye_name_hash(const char *name)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (; *name; ++name) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        hash ^= (uint8_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash ? hash : 1U;
}

static bool ye_seen_add(ye_stream *stream, uint64_t hash)
{
    size_t slot = (size_t)hash & stream->seen_mask, probes;
    for (probes = 0U; probes <= stream->seen_mask; ++probes) {
        if (stream->seen[slot] == 0U) {
            stream->seen[slot] = hash;
            return true;
        }
        if (stream->seen[slot] == hash) return false;
        slot = (slot + 1U) & stream->seen_mask;
    }
    return false;
}

static void ye_final_name(ye_stream *stream)
{
    const char *name = stream->member.name;
    size_t length = xx_str_len(name), dot = length, index;
    char digits[24];
    size_t digit_count = 0U;
    uint64_t number = stream->index + 1U;
    if (ye_seen_add(stream, ye_name_hash(name))) {
        xx_rt_memcpy(stream->name, name, length + 1U);
        return;
    }
    for (index = length; index > 1U; --index)
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (int)(number % 10U));
        number /= 10U;
    } while (number != 0U && digit_count < sizeof(digits));
    xx_rt_memcpy(stream->name, name, dot);
    stream->name[dot] = '%';
    stream->name[dot + 1U] = '_';
    for (index = 0U; index < digit_count; ++index) stream->name[dot + 2U + index] = digits[digit_count - 1U - index];
    xx_rt_memcpy(stream->name + dot + 2U + digit_count, name + dot, length - dot + 1U);
    (void)ye_seen_add(stream, ye_name_hash(stream->name));
}

static bool ye_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *ye_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ye_set_record(Abstractformat *format, xx_archive_record *record, ye_stream *stream)
{
    const ye_member *member = &stream->member;
    ye_final_name(stream);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->end - member->data_offset;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->parts) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, member->crc) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_yenc_encoded_file_init(xx_yenc_encoded_file *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_YENC_ENCODED_FILE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/x-yenc");
    xx_format_set_extension(&archive->format, "yenc");
    archive->format.check_is_valid = xx_yenc_encoded_file_check_is_valid;
    archive->format.handle_base_info = xx_yenc_encoded_file_handle_base_info;
    archive->format.get_format_size = xx_yenc_encoded_file_get_format_size;
    archive->format.get_number_of_archive_records = xx_yenc_encoded_file_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_yenc_encoded_file_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_yenc_encoded_file_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_yenc_encoded_file_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_yenc_encoded_file_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_yenc_encoded_file_free_archive_records_reading;
}

xx_yenc_encoded_file *xx_yenc_encoded_file_create(xx_io_device *device, int64_t base_address)
{
    xx_yenc_encoded_file *archive = (xx_yenc_encoded_file *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_yenc_encoded_file_init(archive, device, base_address);
    return archive;
}

void xx_yenc_encoded_file_destroy(xx_yenc_encoded_file *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_yenc_encoded_file_free(xx_yenc_encoded_file *archive)
{
    if (!archive) return;
    xx_yenc_encoded_file_destroy(archive);
    xx_mem_free(archive);
}

bool xx_yenc_encoded_file_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    ye_summary summary;
    return format && ye_walk(format, true, &summary, pd);
}

bool xx_yenc_encoded_file_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    ye_summary summary;
    xx_yenc_encoded_file *archive;
    if (!format || !ye_walk(format, false, &summary, pd)) return false;
    archive = (xx_yenc_encoded_file *)format;
    archive->number_of_records = summary.count;
    archive->unpacked_size = summary.unpacked_size;
    archive->text_size = summary.text_size;
    format->number_of_archive_records = summary.count;
    format->format_size = summary.text_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_yenc_encoded_file_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_yenc_encoded_file_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_yenc_encoded_file_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_yenc_encoded_file_handle_base_info(format, pd)) ? ((xx_yenc_encoded_file *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_yenc_encoded_file_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    ye_stream *stream;
    xx_archive_record_state *state;
    size_t slots = 16U;
    if (!format || (!format->base_info_handled && !xx_yenc_encoded_file_handle_base_info(format, pd))) return NULL;
    stream = (ye_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = ((xx_yenc_encoded_file *)format)->number_of_records;
    while (slots < 2U * stream->count) slots <<= 1U;
    stream->seen = (uint64_t *)xx_mem_calloc(slots, sizeof(uint64_t));
    stream->seen_mask = slots - 1U;
    stream->cursor.reader = ye_reader_create(format, NULL);
    if (!stream->seen || !stream->cursor.reader || stream->count == 0U || ye_first(&stream->cursor, false) != YE_CLEAN ||
        ye_take_member(&stream->cursor, &stream->member) != YE_CLEAN) {
        ye_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ye_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ye_stream_free;
    state->total_records = stream->count;
    if (!ye_copy_options(&state->options, options) || !ye_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_yenc_encoded_file_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_yenc_encoded_file_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ye_stream *stream;
    int result;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ye_stream *)state->internal_state) || stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    stream->cursor.reader->pd = pd;
    stream->cursor.reader->failed = false;
    result = ye_take_member(&stream->cursor, &stream->member);
    stream->cursor.reader->pd = NULL;
    if (result != YE_CLEAN) {
        state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!ye_set_record(format, &state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_yenc_encoded_file_unpack_current_to_device(Abstractformat *format, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    ye_stream *stream;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ye_stream *)state->internal_state) || !destination) return false;
    return ye_decode(format, &stream->member, destination, pd);
}

bool xx_yenc_encoded_file_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ye_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ye_stream *)state->internal_state) || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = ye_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ye_decode(format, &stream->member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->name)
                                                                                                  : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = ye_decode(format, &stream->member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_yenc_encoded_file_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

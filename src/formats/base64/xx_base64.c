/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Base64 / MIME text, bare or in a "begin-base64" envelope.  xx_base64.h
 * carries the grammar and the acceptance rules.
 *
 * Decoding follows the usual Base64 rules (RFC 4648 alphabet, CR / LF
 * ignored, a last group of two or three symbols gives one or two bytes, the
 * text stops at its '=' padding), which is also what Deark's base64 and
 * uuencode modules (deark-1.7.3/modules/xfer.c, MIT licence, Copyright (C)
 * 2016 Jason Summers) and 7-Zip's Base64 handler do.  The envelope
 * ("begin-base64 <mode> <name>" ... "====") is the one sharutils'
 * `uuencode -m` writes.  Neither reference auto-detects bare Base64 (7-Zip
 * opens it by the .b64 extension only, Deark needs -m base64); the detection
 * window and its rules below are this reader's own.
 *
 * check_is_valid looks at no more than the first 64 KiB plus a 1 KiB tail,
 * so the late probe is cheap: binary input fails on its first byte and prose
 * on its first space or punctuation mark.  handle_base_info applies the same
 * rules and then walks the rest of the text only to find where it ends; no
 * pass allocates more than a fixed read buffer.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/base64/xx_base64.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BASE64 is registered there. */
#ifdef BASE64
#define XX_BASE64_FILE_TYPE XX_FILE_TYPE_BASE64
#else
#define XX_BASE64_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Bytes of text the acceptance rules are applied to. */
#define BASE64_WINDOW ((int64_t)64 * 1024)
/* Bytes of whitespace / 0x1A / 0x00 tolerated past the window when the text
 * ends inside it. */
#define BASE64_MAX_TAIL ((int64_t)1024)
/* Bare text: fewest symbols, and narrowest line of a multi-line text. */
#define BASE64_MIN_SYMBOLS 16U
#define BASE64_MIN_WIDTH 16U
/* Different symbols due from a symbol count on.  Hex text has at most 22
 * (0-9 A-F a-f), so a hex dump with a few stray letters stays out, while
 * encoded real data has far more: over the first 48 KiB of 5,374 system and
 * library files the fewest was 35, at 268 symbols. */
#define BASE64_DIVERSE_FROM 256U
#define BASE64_MIN_DISTINCT 24U
#define BASE64_DIVERSE_MORE_FROM 1024U
#define BASE64_MIN_DISTINCT_MORE 32U
/* Below this many symbols a bare text also needs a digit, '+', '/' or '='
 * padding, so a lone word such as "ReadMeFirstPlzOK" is not claimed. */
#define BASE64_SHORT_TEXT 64U
/* Longest "begin-base64" header line, line break included. */
#define BASE64_HEADER_MAX 1024U
#define BASE64_PREFIX "begin-base64 "
#define BASE64_PREFIX_SIZE 13U
#define BASE64_NAME_MAX 255U
#define BASE64_CHUNK 8192U
#define BASE64_PAYLOAD_NAME "payload"

/* Scanner phases. */
enum {
    BASE64_DATA = 0, /* symbols and line breaks */
    BASE64_PAD2,     /* one '=' seen, a second one is due */
    BASE64_AFTER,    /* padding complete, the line must end */
    BASE64_TRAIL,    /* bare text over: whitespace to EOF */
    BASE64_ZTAIL,    /* bare text over: 0x1A / 0x00 to EOF */
    BASE64_WAIT,     /* envelope data over: blank lines, then "====" */
    BASE64_TERM,     /* inside the "====" line */
    BASE64_DONE      /* envelope closed */
};

/* Resumable forward scan, offsets relative to the base address. */
typedef struct base64_scan_s {
    int64_t position;     /**< Offset of the next byte. */
    int64_t data_end;     /**< Just past the last symbol or pad. */
    int64_t text_end;     /**< Just past the last byte of the format. */
    uint64_t symbols;
    uint64_t line_length; /**< Symbols and pads on the current line. */
    uint64_t width;       /**< Length of the first complete line, 0 = none. */
    uint64_t distinct;    /**< One bit per symbol value seen. */
    unsigned term_count;  /**< '=' of the terminator line seen so far. */
    int phase;
    bool wrapped;
    bool strict;          /**< Apply the window rules to this byte. */
    bool cr;              /**< Previous byte was CR. */
    bool short_line;      /**< A complete line shorter than `width` seen. */
    bool has_upper;
    bool has_lower;
    bool has_nonhex;
    bool padded;          /**< '=' padding seen. */
    bool terminated;
    bool finished;        /**< The text ended; later bytes are not read. */
    bool rejected;        /**< A window rule failed. */
} base64_scan;

typedef struct base64_context_s {
    int64_t data_offset;
    int64_t data_size;
    int64_t text_size;
    uint64_t symbol_count;
    uint64_t unpacked_size;
    uint64_t line_width;
    uint32_t mode;
    bool wrapped;
    bool terminated;
    char name[BASE64_NAME_MAX + 1U];
} base64_context;

typedef struct base64_stream_s {
    base64_context context;
    size_t index;
    size_t count;
} base64_stream;

static int base64_value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool base64_read_at(xx_io_device *device, int64_t offset,
                           void *buffer, size_t size) {
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

/* ---------------------------------------------------------------------- */
/* Scanner                                                                 */

/* A byte that breaks a window rule rejects the text inside the window and
 * merely ends it past the window. */
static bool base64_foreign(base64_scan *scan) {
    if (scan->strict) scan->rejected = true;
    scan->finished = true;
    return false;
}

/* Line rules of a bare text, for a symbol or pad about to be added to the
 * current line. */
static bool base64_line_ok(const base64_scan *scan) {
    if (!scan->strict || scan->wrapped || scan->width == 0U) return true;
    /* A line after the first one: the text is multi-line, so the first
     * line's width must be a real encoder's, and no short line may come
     * before this one. */
    if (scan->line_length == 0U &&
        (scan->short_line || (scan->width & 3U) != 0U ||
         scan->width < BASE64_MIN_WIDTH))
        return false;
    return scan->line_length < scan->width;
}

static bool base64_symbol(base64_scan *scan, int value, uint8_t c,
                          int64_t here) {
    if (!base64_line_ok(scan)) return base64_foreign(scan);
    if (c >= 'A' && c <= 'Z') {
        scan->has_upper = true;
        if (c > 'F') scan->has_nonhex = true;
    } else if (c >= 'a' && c <= 'z') {
        scan->has_lower = true;
        if (c > 'f') scan->has_nonhex = true;
    } else if (c == '+' || c == '/') {
        scan->has_nonhex = true;
    }
    scan->distinct |= (uint64_t)1U << (unsigned)value;
    ++scan->symbols;
    ++scan->line_length;
    scan->data_end = scan->text_end = here + 1;
    return true;
}

static bool base64_pad(base64_scan *scan, int64_t here) {
    if (!base64_line_ok(scan)) return base64_foreign(scan);
    ++scan->line_length;
    scan->padded = true;
    scan->data_end = scan->text_end = here + 1;
    return true;
}

/* LF inside the text (phase DATA or AFTER). */
static bool base64_line_end(base64_scan *scan, int64_t here) {
    if (scan->line_length == 0U) {
        /* Blank line: skipped in an envelope, the end of a bare text. */
        if (scan->wrapped) return true;
        if (scan->symbols == 0U) return base64_foreign(scan);
        /* Unpadded, so the text must end on a whole group. */
        if (scan->phase == BASE64_DATA && scan->strict &&
            (scan->symbols & 3U) != 0U)
            return base64_foreign(scan);
        scan->phase = BASE64_TRAIL;
        scan->text_end = here + 1;
        return true;
    }
    if (!scan->wrapped) {
        if (scan->width == 0U) scan->width = scan->line_length;
        else if (scan->line_length < scan->width) scan->short_line = true;
    }
    scan->line_length = 0U;
    scan->text_end = here + 1;
    return true;
}

/* Feed one byte at offset `here`.  False once the text has ended (finished)
 * or a window rule failed (rejected). */
static bool base64_feed(base64_scan *scan, uint8_t c, int64_t here) {
    int value;
    if (scan->cr) {
        scan->cr = false;
        /* A CR not followed by LF only passes outside the window. */
        if (c != '\n' && scan->strict) return base64_foreign(scan);
    }
    switch (scan->phase) {
    case BASE64_DATA:
        value = base64_value(c);
        if (value >= 0) return base64_symbol(scan, value, c, here);
        if (c == '=') {
            unsigned rest = (unsigned)(scan->symbols & 3U);
            if (scan->wrapped && scan->line_length == 0U) {
                scan->phase = BASE64_TERM;
                scan->term_count = 1U;
                return true;
            }
            if (rest < 2U) return base64_foreign(scan);
            if (!base64_pad(scan, here)) return false;
            scan->phase = rest == 2U ? BASE64_PAD2 : BASE64_AFTER;
            return true;
        }
        if (c == '\r') {
            scan->cr = true;
            return true;
        }
        if (c == '\n') return base64_line_end(scan, here);
        if (!scan->wrapped && scan->line_length == 0U && scan->symbols != 0U &&
            (c == ' ' || c == '\t' || c == 0x1AU || c == 0x00U)) {
            /* Whitespace or padding after the last line: the text ended
             * without '=', so it must end on a whole group. */
            if (scan->strict && (scan->symbols & 3U) != 0U)
                return base64_foreign(scan);
            scan->phase = BASE64_TRAIL;
            return base64_feed(scan, c, here);
        }
        return base64_foreign(scan);
    case BASE64_PAD2:
        if (c != '=') return base64_foreign(scan);
        if (!base64_pad(scan, here)) return false;
        scan->phase = BASE64_AFTER;
        return true;
    case BASE64_AFTER:
        if (c == '\r') {
            scan->cr = true;
            return true;
        }
        if (c == '\n') {
            if (!base64_line_end(scan, here)) return false;
            scan->phase = scan->wrapped ? BASE64_WAIT : BASE64_TRAIL;
            return true;
        }
        if (!scan->wrapped && !scan->strict && (c == ' ' || c == '\t')) {
            scan->phase = BASE64_TRAIL;
            scan->text_end = here + 1;
            return true;
        }
        return base64_foreign(scan);
    case BASE64_TRAIL:
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            scan->text_end = here + 1;
            return true;
        }
        if (scan->strict && (c == 0x1AU || c == 0x00U)) {
            scan->phase = BASE64_ZTAIL;
            return true;
        }
        return base64_foreign(scan);
    case BASE64_ZTAIL:
        if (c == 0x1AU || c == 0x00U) return true;
        return base64_foreign(scan);
    case BASE64_WAIT:
        if (c == '\r') {
            scan->cr = true;
            return true;
        }
        if (c == '\n') return true;
        if (c == '=') {
            scan->phase = BASE64_TERM;
            scan->term_count = 1U;
            return true;
        }
        return base64_foreign(scan);
    case BASE64_TERM:
        if (c == '=' && scan->term_count < 4U) {
            ++scan->term_count;
            return true;
        }
        if (scan->term_count == 4U && c == '\r') {
            scan->cr = true;
            return true;
        }
        if (scan->term_count == 4U && c == '\n') {
            scan->terminated = true;
            scan->text_end = here + 1;
            scan->phase = BASE64_DONE;
            scan->finished = true;
            return false;
        }
        return base64_foreign(scan);
    default:
        scan->finished = true;
        return false;
    }
}

/* Feed the bytes of [scan->position, limit) until the text ends.  False
 * only on a read error or a stop request. */
static bool base64_scan_run(Abstractformat *format, base64_scan *scan,
                            int64_t limit, xx_pd_struct *pd) {
    uint8_t buffer[BASE64_CHUNK];
    while (!scan->finished && scan->position < limit) {
        int64_t left = limit - scan->position;
        size_t want = left < (int64_t)sizeof(buffer) ? (size_t)left
                                                      : sizeof(buffer);
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!base64_read_at(format->device,
                            format->base_address + scan->position, buffer,
                            want))
            return false;
        for (index = 0U; index < want; ++index)
            if (!base64_feed(scan, buffer[index],
                             scan->position + (int64_t)index))
                break;
        /* The byte that ended the text is not consumed. */
        scan->position += (int64_t)index;
    }
    return true;
}

/* The input ended at scan->position.  False when a window rule fails. */
static bool base64_scan_eof(base64_scan *scan) {
    unsigned rest = (unsigned)(scan->symbols & 3U);
    switch (scan->phase) {
    case BASE64_DATA:
        if (!scan->strict) return true;
        /* A bare text must end on a whole group; an envelope that lost its
         * terminator only needs its last group to be decodable. */
        return scan->wrapped ? rest != 1U : rest == 0U;
    case BASE64_PAD2:
        return !scan->strict;
    case BASE64_TERM:
        if (scan->term_count == 4U) {
            scan->terminated = true;
            scan->text_end = scan->position;
            scan->phase = BASE64_DONE;
            return true;
        }
        return !scan->strict;
    default:
        return true;
    }
}

/* ---------------------------------------------------------------------- */
/* Envelope header                                                         */

static char base64_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* The first `stem` bytes of `name` spell `word` exactly, ignoring case. */
static bool base64_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || base64_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* A plain file name: printable ASCII, no separators or reserved
 * punctuation, not only dots and spaces, no trailing dot or space, and not
 * a Windows device name with or without an extension. */
static bool base64_safe_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (base64_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((base64_upper(name[0]) == 'C' && base64_upper(name[1]) == 'O' &&
          base64_upper(name[2]) == 'M') ||
         (base64_upper(name[0]) == 'L' && base64_upper(name[1]) == 'P' &&
          base64_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* The member name: the header name's last path component when that is a
 * safe plain file name ("-" is standard input), else the default. */
static void base64_set_name(base64_context *context, const uint8_t *name,
                            size_t length) {
    size_t start = 0U, index;
    for (index = 0U; index < length; ++index)
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':')
            start = index + 1U;
    name += start;
    length -= start;
    if (length == 0U || length > BASE64_NAME_MAX ||
        (length == 1U && name[0] == '-') ||
        !base64_safe_name((const char *)name, length)) {
        name = (const uint8_t *)BASE64_PAYLOAD_NAME;
        length = sizeof(BASE64_PAYLOAD_NAME) - 1U;
    }
    xx_rt_memcpy(context->name, name, length);
    context->name[length] = 0;
}

/* "begin-base64 <1-4 octal digits> <name>" + LF or CRLF at the start of
 * `line` (the first `length` bytes of the input).  sharutils prints the mode
 * with %o, so it has as many digits as it needs.  True with the offset of
 * the next line in `data_offset`. */
static bool base64_parse_header(const uint8_t *line, size_t length,
                                base64_context *context) {
    size_t position = BASE64_PREFIX_SIZE, mode_start, end = 0U, index;
    uint32_t mode = 0U;
    if (length < BASE64_PREFIX_SIZE + 4U ||
        xx_rt_memcmp(line, BASE64_PREFIX, BASE64_PREFIX_SIZE) != 0)
        return false;
    mode_start = position;
    while (position < length && position < mode_start + 4U &&
           line[position] >= '0' && line[position] <= '7') {
        mode = (mode << 3U) | (uint32_t)(line[position] - '0');
        ++position;
    }
    if (position == mode_start || position >= length || line[position] != ' ')
        return false;
    ++position;
    for (index = position; index < length; ++index) {
        if (line[index] == '\n') {
            end = index;
            break;
        }
    }
    if (index >= length) return false;
    if (end > position && line[end - 1U] == '\r') --end;
    if (end <= position) return false;
    for (index = position; index < end; ++index)
        if (line[index] < 0x20U || line[index] == 0x7FU) return false;
    context->mode = mode;
    context->wrapped = true;
    base64_set_name(context, line + position, end - position);
    /* The line after the header. */
    while (line[end] != '\n') ++end;
    context->data_offset = (int64_t)end + 1;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */

/* Apply the window rules; with `measure`, also find where the text ends. */
static bool base64_parse(Abstractformat *format, base64_context *out,
                         bool measure, xx_pd_struct *pd) {
    base64_scan scan;
    base64_context context;
    int64_t total, size, window;
    unsigned distinct = 0U;
    uint64_t bits;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)BASE64_MIN_SYMBOLS) return false;

    xx_mem_zero(&context, sizeof(context));
    xx_mem_zero(&scan, sizeof(scan));
    {
        uint8_t head[BASE64_HEADER_MAX];
        size_t length = size < (int64_t)sizeof(head) ? (size_t)size
                                                      : sizeof(head);
        /* Cheap early exit for the late probe: the first byte must start
         * either the envelope or a bare text. */
        if (!base64_read_at(format->device, format->base_address, head, 1U) ||
            (head[0] != 'b' && base64_value(head[0]) < 0))
            return false;
        if (head[0] == 'b' &&
            base64_read_at(format->device, format->base_address, head,
                           length) &&
            !base64_parse_header(head, length, &context))
            xx_mem_zero(&context, sizeof(context));
    }
    scan.wrapped = context.wrapped;
    scan.position = scan.data_end = scan.text_end = context.data_offset;
    scan.phase = BASE64_DATA;
    scan.strict = true;

    window = size < BASE64_WINDOW ? size : BASE64_WINDOW;
    if (!base64_scan_run(format, &scan, window, pd) || scan.rejected)
        return false;
    if (!scan.finished && scan.position < size &&
        (scan.phase == BASE64_AFTER || scan.phase == BASE64_TRAIL ||
         scan.phase == BASE64_ZTAIL)) {
        /* The bare text ended inside the window: its tail must reach EOF
         * soon. */
        if (size - scan.position > BASE64_MAX_TAIL) return false;
        if (!base64_scan_run(format, &scan, size, pd) || scan.rejected)
            return false;
    }
    if (!scan.finished && scan.position >= size && !base64_scan_eof(&scan))
        return false;

    if (scan.wrapped) {
        if (scan.symbols == 0U && !scan.terminated) return false;
    } else {
        for (bits = scan.distinct; bits != 0U; bits &= bits - 1U) ++distinct;
        if (scan.symbols < BASE64_MIN_SYMBOLS || !scan.has_upper ||
            !scan.has_lower || !scan.has_nonhex ||
            (scan.symbols >= BASE64_DIVERSE_FROM &&
             distinct < BASE64_MIN_DISTINCT) ||
            (scan.symbols >= BASE64_DIVERSE_MORE_FROM &&
             distinct < BASE64_MIN_DISTINCT_MORE) ||
            /* Symbol values 52..63 are the digits, '+' and '/'. */
            (scan.symbols < BASE64_SHORT_TEXT && !scan.padded &&
             (scan.distinct >> 52U) == 0U))
            return false;
    }

    if (measure) {
        scan.strict = false;
        if (!scan.finished && scan.position < size &&
            !base64_scan_run(format, &scan, size, pd))
            return false;
        if (!scan.finished && scan.position >= size)
            (void)base64_scan_eof(&scan);
        if (scan.data_end < context.data_offset ||
            scan.text_end < scan.data_end || scan.text_end > size)
            return false;
        context.data_size = scan.data_end - context.data_offset;
        context.text_size = scan.text_end;
        context.symbol_count = scan.symbols;
        context.unpacked_size = scan.symbols / 4U * 3U;
        if ((scan.symbols & 3U) >= 2U)
            context.unpacked_size += (scan.symbols & 3U) - 1U;
        context.line_width = scan.width;
        context.terminated = scan.terminated;
        if (!context.wrapped) {
            xx_rt_memcpy(context.name, BASE64_PAYLOAD_NAME,
                         sizeof(BASE64_PAYLOAD_NAME));
        }
    }
    *out = context;
    return true;
}

static bool base64_write_all(xx_io_device *destination, const uint8_t *data,
                             size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Decode the measured text to `destination` (or only count it when that is
 * NULL).  Every byte of the text was classified by the measuring pass, so a
 * foreign byte or a count mismatch means the input changed underneath. */
static bool base64_decode(Abstractformat *format,
                          const base64_context *context,
                          xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t input[BASE64_CHUNK];
    uint8_t output[BASE64_CHUNK];
    int64_t position = 0;
    uint64_t written = 0U;
    uint32_t group = 0U;
    unsigned count = 0U;
    bool padded = false;
    if (!format || !context || context->data_size < 0) return false;
    while (!padded && position < context->data_size) {
        int64_t left = context->data_size - position;
        size_t want = left < (int64_t)sizeof(input) ? (size_t)left
                                                     : sizeof(input);
        size_t index, produced = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!base64_read_at(format->device,
                            format->base_address + context->data_offset +
                                position,
                            input, want))
            return false;
        for (index = 0U; index < want; ++index) {
            uint8_t c = input[index];
            int value = base64_value(c);
            if (value < 0) {
                if (c == '\r' || c == '\n') continue;
                if (c == '=') {
                    padded = true;
                    break;
                }
                return false;
            }
            group = (group << 6U) | (uint32_t)value;
            if (++count == 4U) {
                output[produced++] = (uint8_t)(group >> 16U);
                output[produced++] = (uint8_t)(group >> 8U);
                output[produced++] = (uint8_t)group;
                group = 0U;
                count = 0U;
            }
        }
        /* One chunk of input yields at most 3/4 of its size. */
        if (produced != 0U) {
            if ((uint64_t)produced > context->unpacked_size - written ||
                (destination &&
                 !base64_write_all(destination, output, produced)))
                return false;
            written += (uint64_t)produced;
        }
        position += (int64_t)want;
    }
    if (count >= 2U) {
        uint8_t tail[2];
        size_t produced = count - 1U;
        group <<= 6U * (4U - count);
        tail[0] = (uint8_t)(group >> 16U);
        tail[1] = (uint8_t)(group >> 8U);
        if ((uint64_t)produced > context->unpacked_size - written ||
            (destination && !base64_write_all(destination, tail, produced)))
            return false;
        written += (uint64_t)produced;
    }
    return written == context->unpacked_size;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool base64_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
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

static const xx_var *base64_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool base64_set_record(Abstractformat *format,
                              xx_archive_record *record,
                              const base64_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = context->data_offset;
    record->data_offset = format->base_address + context->data_offset;
    record->compressed_size = context->data_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void base64_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_base64_init(xx_base64 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BASE64_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/plain");
    xx_format_set_extension(&archive->format, "b64");
    archive->format.check_is_valid = xx_base64_check_is_valid;
    archive->format.handle_base_info = xx_base64_handle_base_info;
    archive->format.get_format_size = xx_base64_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_base64_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_base64_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_base64_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_base64_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_base64_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_base64_free_archive_records_reading;
}

xx_base64 *xx_base64_create(xx_io_device *device, int64_t base_address) {
    xx_base64 *archive = (xx_base64 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_base64_init(archive, device, base_address);
    return archive;
}

void xx_base64_destroy(xx_base64 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_base64_free(xx_base64 *archive) {
    if (!archive) return;
    xx_base64_destroy(archive);
    xx_mem_free(archive);
}

bool xx_base64_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    base64_context context;
    return base64_parse(format, &context, false, pd);
}

bool xx_base64_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    base64_context context;
    xx_base64 *archive;
    size_t length;
    if (!format || !base64_parse(format, &context, true, pd)) return false;
    archive = (xx_base64 *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->symbol_count = context.symbol_count;
    archive->data_offset = context.data_offset;
    archive->data_size = context.data_size;
    archive->text_size = context.text_size;
    archive->line_width = context.line_width > UINT32_MAX
                              ? UINT32_MAX
                              : (uint32_t)context.line_width;
    archive->mode = context.mode;
    archive->is_wrapped = context.wrapped;
    archive->is_terminated = context.terminated;
    length = xx_str_len(context.name);
    if (length >= sizeof(archive->name)) length = sizeof(archive->name) - 1U;
    xx_rt_memcpy(archive->name, context.name, length);
    archive->name[length] = 0;
    format->number_of_archive_records = 1U;
    format->format_size = context.text_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_base64_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_base64_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_base64_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_base64_handle_base_info(format, pd))
               ? ((xx_base64 *)format)->number_of_records : 0U;
}

uint64_t xx_base64_get_unpacked_size(xx_base64 *archive) {
    if (!archive) return 0U;
    if (!archive->format.base_info_handled &&
        !xx_base64_handle_base_info(&archive->format, NULL))
        return 0U;
    return archive->unpacked_size;
}

bool xx_base64_unpack_to_device(xx_base64 *archive, xx_io_device *destination,
                                xx_pd_struct *pd) {
    base64_context context;
    if (!archive || !destination ||
        !base64_parse(&archive->format, &context, true, pd))
        return false;
    return base64_decode(&archive->format, &context, destination, pd);
}

xx_archive_record_state *xx_base64_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    base64_stream *stream;
    xx_archive_record_state *state;
    base64_context context;
    if (!base64_parse(format, &context, true, pd)) return NULL;
    stream = (base64_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = base64_stream_free;
    state->total_records = 1U;
    if (!base64_copy_options(&state->options, options) ||
        !base64_set_record(format, &state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_base64_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_base64_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    base64_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (base64_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_base64_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    base64_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (base64_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = base64_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the text still decodes and report that. */
        return base64_decode(format, &stream->context, NULL, pd);
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
               ? xx_str_concat3(base, "/", stream->context.name)
               : xx_str_concat(base, stream->context.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = base64_decode(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_base64_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

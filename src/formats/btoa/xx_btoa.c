/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * btoa / xbtoa and Adobe Ascii85.  xx_btoa.h carries the grammar and the
 * acceptance rules.
 *
 * Written from the format.  The btoa 5.2 sources (atob.c / btoa.c /
 * chksum.h by Paul Rutter, Joe Orost and Stefan Parmark) and Deark's
 * ascii85 module (deark-1.7.3/modules/xfer.c, MIT licence, Copyright (C)
 * 2016 Jason Summers) were read to pin down the checksum rules, the 5.x
 * per-line check character and the 'y' shorthand; no code was taken from
 * either.  The readings of the DOS "b2a" shorthands ('w' = 256 x 0xFF,
 * 'y' = 4 x 0xFF) were measured on real files: they are the only readings
 * under which those files' own E / S / R trailers match.
 *
 * A segment is only accepted once its trailer's length and all three
 * checksums match what the body decodes to, so check_is_valid decodes the
 * first segment completely (without writing anything).  Nothing is
 * buffered beyond one text line, one read window and one write window: a
 * file of any length costs about 40 KiB (plus ~0.3 KiB per listed record),
 * and the decoded size is bounded by the text itself and by
 * BTOA_MAX_DECODED / BTOA_MAX_TOTAL, never by a declared length.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/btoa/xx_btoa.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BTOA is registered there. */
#ifdef BTOA
#define XX_BTOA_FILE_TYPE XX_FILE_TYPE_BTOA
#else
#define XX_BTOA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Longest text line accepted, line terminator excluded.  btoa writes 78
 * characters (atob's own buffer holds 98); Deark gives up at 1023. */
#define BTOA_LINE_MAX 4096U
#define BTOA_READ_CHUNK 16384U
#define BTOA_WRITE_CHUNK 16384U
/* Begin..End segments listed per file. */
#define BTOA_MAX_SEGMENTS 1024U
/* Blank lines tolerated between two segments. */
#define BTOA_MAX_GAP_LINES 256U
/* Longest 5.x header name kept; a longer one falls back to the default. */
#define BTOA_NAME_MAX 255U
/* Room for the name plus a "_<n>" suffix that keeps it unique. */
#define BTOA_NAME_BUFFER (BTOA_NAME_MAX + 16U)
/* The 5.x header's line width: one data character plus the check. */
#define BTOA_MIN_WIDTH 2U
#define BTOA_MAX_WIDTH 1024U
/* Decoded bytes per segment, and over all listed segments.  The b2a 'w'
 * turns one character into 256 checksummed bytes, so without a ceiling a
 * 100 MB file of 'w' would cost minutes of checksum work at detection time;
 * at these ceilings the worst case stays near a second.  A real btoa file
 * of 256 MiB would be 320 MB of text. */
#define BTOA_MAX_DECODED ((uint64_t)256U * 1024U * 1024U)
#define BTOA_MAX_TOTAL ((uint64_t)1024U * 1024U * 1024U)
#define BTOA_W_RUN 256U
#define BTOA_PAYLOAD_NAME "payload"

typedef struct btoa_sums_s {
    uint64_t eor;
    uint64_t sum;
    uint64_t rot;
} btoa_sums;

typedef struct btoa_segment_s {
    int64_t offset;      /**< Absolute: header line, or "<~". */
    int64_t body_offset; /**< Absolute: first body byte. */
    int64_t body_size;   /**< Up to the trailer line, or to "~>". */
    int64_t end;         /**< Absolute: just past the segment. */
    uint64_t size;       /**< Decoded length (N for btoa). */
    uint32_t variant;    /**< XX_BTOA_VARIANT_*. */
    uint8_t y_byte;      /**< Reading of 'y' the checksum confirmed. */
    char name[BTOA_NAME_BUFFER];
} btoa_segment;

typedef struct btoa_list_s {
    btoa_segment *items;
    size_t count;
    size_t capacity;
} btoa_list;

typedef struct btoa_work_s {
    /* Input window. */
    xx_io_device *device;
    int64_t end;
    int64_t position;
    int64_t window_start;
    size_t window_length;
    bool io_failed;
    /* Decoder. */
    uint32_t variant;
    uint8_t y_byte;
    unsigned digits;
    uint64_t group;
    uint64_t produced;
    btoa_sums sums[2]; /**< Old format: [0] 'y' = 0x20, [1] 'y' = 0xFF. */
    bool y_seen;       /**< sums[1] is only kept apart once a 'y' came. */
    /* Output. */
    xx_io_device *sink;
    uint64_t limit;
    uint64_t written;
    size_t out_length;
    bool out_failed;
    size_t line_length;
    uint8_t line[BTOA_LINE_MAX + 1U];
    uint8_t window[BTOA_READ_CHUNK];
    uint8_t out[BTOA_WRITE_CHUNK];
} btoa_work;

typedef struct btoa_stream_s {
    btoa_list list;
    size_t index;
} btoa_stream;

/* ---------------------------------------------------------------------- */
/* Input                                                                   */

static int btoa_getc(btoa_work *w) {
    int64_t offset;
    if (w->io_failed || w->position < 0 || w->position >= w->end) return -1;
    offset = w->position - w->window_start;
    if (w->window_length == 0U || offset < 0 ||
        (uint64_t)offset >= (uint64_t)w->window_length) {
        int64_t available = w->end - w->position;
        size_t want = available < (int64_t)BTOA_READ_CHUNK
                          ? (size_t)available : (size_t)BTOA_READ_CHUNK;
        size_t done = 0U;
        w->window_length = 0U;
        if (xx_io_seek64(w->device, w->position, SEEK_SET) != 0) {
            w->io_failed = true;
            return -1;
        }
        while (done < want) {
            ssize_t amount = xx_io_read(w->device, w->window + done,
                                        want - done);
            if (amount <= 0 || (size_t)amount > want - done) {
                w->io_failed = true;
                return -1;
            }
            done += (size_t)amount;
        }
        w->window_start = w->position;
        w->window_length = want;
        offset = 0;
    }
    ++w->position;
    return (int)w->window[(size_t)offset];
}

/* One line into w->line, without its LF and without one CR before it.
 * 1: a line (possibly empty, possibly ended by the end of data),
 * 0: no byte left, -1: read failure or a line longer than BTOA_LINE_MAX. */
static int btoa_read_line(btoa_work *w) {
    size_t length = 0U;
    bool any = false;
    for (;;) {
        int c = btoa_getc(w);
        if (c < 0) {
            if (w->io_failed) return -1;
            if (!any) return 0;
            break;
        }
        any = true;
        if (c == '\n') break;
        if (length >= BTOA_LINE_MAX) return -1;
        w->line[length++] = (uint8_t)c;
    }
    if (length > 0U && w->line[length - 1U] == '\r') --length;
    w->line[length] = 0U;
    w->line_length = length;
    return 1;
}

static bool btoa_is_blank(uint8_t c) { return c == ' ' || c == '\t'; }

/* ---------------------------------------------------------------------- */
/* Checksums and output                                                    */

static void btoa_sum(btoa_sums *sums, uint32_t item) {
    uint64_t carry = (sums->rot >> 31U) & 1U;
    sums->eor ^= (uint64_t)item;
    sums->sum += (uint64_t)item + 1U;
    sums->rot = (sums->rot << 1U) + carry + (uint64_t)item;
}

/* S and R are printed from a C long: 32-bit builds wrap them, LP64 builds
 * print 64 bits whose low half is the 32-bit value. */
static bool btoa_sums_match(const btoa_sums *sums, uint64_t eor, uint64_t sum,
                            uint64_t rot) {
    return eor == sums->eor &&
           (sum == sums->sum || sum == (sums->sum & UINT64_C(0xFFFFFFFF))) &&
           (rot == sums->rot || rot == (sums->rot & UINT64_C(0xFFFFFFFF)));
}

static bool btoa_flush(btoa_work *w) {
    size_t done = 0U;
    if (!w->sink || w->out_failed) {
        w->out_length = 0U;
        return !w->out_failed;
    }
    while (done < w->out_length) {
        ssize_t amount = xx_io_write(w->sink, w->out + done,
                                     w->out_length - done);
        if (amount <= 0 || (size_t)amount > w->out_length - done) {
            w->out_failed = true;
            w->out_length = 0U;
            return false;
        }
        done += (size_t)amount;
    }
    w->out_length = 0U;
    return true;
}

/* Output stops at `limit`: the padding of the last group is never written. */
static void btoa_put(btoa_work *w, uint8_t byte) {
    if (!w->sink || w->out_failed || w->written >= w->limit) return;
    w->out[w->out_length++] = byte;
    ++w->written;
    if (w->out_length == BTOA_WRITE_CHUNK) (void)btoa_flush(w);
}

static bool btoa_count(btoa_work *w, uint64_t amount) {
    if (w->produced > BTOA_MAX_DECODED - amount) return false;
    w->produced += amount;
    return true;
}

/* `count` copies of one item.  E and S have closed forms; R does not (the
 * carry out of bit 31 feeds back), so it is stepped in a tight loop. */
static void btoa_sum_run(btoa_sums *sums, uint32_t item, unsigned count) {
    uint64_t rot = sums->rot;
    unsigned index;
    for (index = 0U; index < count; ++index)
        rot = (rot << 1U) + ((rot >> 31U) & 1U) + (uint64_t)item;
    sums->rot = rot;
    sums->sum += (uint64_t)count * ((uint64_t)item + 1U);
    if (count & 1U) sums->eor ^= (uint64_t)item;
}

/* A decoded byte: old-format checksums (both readings) and output.  The two
 * readings only differ once a 'y' has been seen. */
static void btoa_byte(btoa_work *w, uint8_t byte) {
    if (w->variant == XX_BTOA_VARIANT_OLD) {
        btoa_sum(&w->sums[0], byte);
        if (w->y_seen) btoa_sum(&w->sums[1], byte);
    }
    btoa_put(w, byte);
}

static bool btoa_word(btoa_work *w, uint32_t value) {
    if (!btoa_count(w, 4U)) return false;
    btoa_byte(w, (uint8_t)(value >> 24U));
    btoa_byte(w, (uint8_t)(value >> 16U));
    btoa_byte(w, (uint8_t)(value >> 8U));
    btoa_byte(w, (uint8_t)value);
    return true;
}

static bool btoa_y(btoa_work *w) {
    unsigned index;
    if (!btoa_count(w, 4U)) return false;
    if (w->variant == XX_BTOA_VARIANT_OLD) {
        if (!w->y_seen) {
            w->sums[1] = w->sums[0];
            w->y_seen = true;
        }
        btoa_sum_run(&w->sums[0], 0x20U, 4U);
        btoa_sum_run(&w->sums[1], 0xFFU, 4U);
    }
    for (index = 0U; index < 4U; ++index) btoa_put(w, w->y_byte);
    return true;
}

/* b2a: 256 bytes 0xFF (old format only). */
static bool btoa_w(btoa_work *w) {
    unsigned index;
    if (!btoa_count(w, BTOA_W_RUN)) return false;
    btoa_sum_run(&w->sums[0], 0xFFU, BTOA_W_RUN);
    if (w->y_seen) btoa_sum_run(&w->sums[1], 0xFFU, BTOA_W_RUN);
    if (w->sink && w->written < w->limit)
        for (index = 0U; index < BTOA_W_RUN; ++index) btoa_put(w, 0xFFU);
    return true;
}

/* One data character: a digit or a shorthand of the current variant. */
static bool btoa_data(btoa_work *w, uint8_t c) {
    if (c >= '!' && c <= 'u') {
        w->group = w->group * 85U + (uint64_t)(c - '!');
        if (++w->digits == 5U) {
            if (w->group > UINT64_C(0xFFFFFFFF) ||
                !btoa_word(w, (uint32_t)w->group)) return false;
            w->group = 0U;
            w->digits = 0U;
        }
        return true;
    }
    if (w->digits != 0U) return false;
    if (c == 'z') return btoa_word(w, 0U);
    if (c == 'y' && w->variant != XX_BTOA_VARIANT_ADOBE) return btoa_y(w);
    if (c == 'w' && w->variant == XX_BTOA_VARIANT_OLD) return btoa_w(w);
    return false;
}

static void btoa_decoder_reset(btoa_work *w, uint32_t variant, uint8_t y_byte,
                               xx_io_device *sink, uint64_t limit) {
    w->io_failed = false;
    w->variant = variant;
    w->y_byte = y_byte;
    w->digits = 0U;
    w->group = 0U;
    w->produced = 0U;
    xx_mem_zero(w->sums, sizeof(w->sums));
    w->y_seen = false;
    w->sink = sink;
    w->limit = limit;
    w->written = 0U;
    w->out_length = 0U;
    w->out_failed = false;
}

/* ---------------------------------------------------------------------- */
/* Header and trailer lines                                                */

/* "xbtoa Begin", or "xbtoa5 <width> <name> Begin".  `name` receives the
 * raw name (empty when it does not fit). */
static bool btoa_parse_header(const uint8_t *line, size_t length,
                              uint32_t *variant, uint32_t *width, char *name) {
    size_t position, name_start, name_end, digits = 0U;
    uint32_t value = 0U;
    while (length > 0U && btoa_is_blank(line[length - 1U])) --length;
    name[0] = 0;
    *width = 0U;
    if (length == 11U && xx_rt_memcmp(line, "xbtoa Begin", 11U) == 0) {
        *variant = XX_BTOA_VARIANT_OLD;
        return true;
    }
    if (length < 7U || xx_rt_memcmp(line, "xbtoa5", 6U) != 0 ||
        !btoa_is_blank(line[6])) return false;
    position = 6U;
    while (position < length && btoa_is_blank(line[position])) ++position;
    while (position < length && line[position] >= '0' &&
           line[position] <= '9') {
        if (++digits > 4U) return false;
        value = value * 10U + (uint32_t)(line[position] - '0');
        ++position;
    }
    if (digits == 0U || value < BTOA_MIN_WIDTH || value > BTOA_MAX_WIDTH ||
        position >= length || !btoa_is_blank(line[position])) return false;
    while (position < length && btoa_is_blank(line[position])) ++position;
    name_start = position;
    if (length < name_start + 7U ||
        xx_rt_memcmp(line + length - 5U, "Begin", 5U) != 0 ||
        !btoa_is_blank(line[length - 6U])) return false;
    name_end = length - 6U;
    while (name_end > name_start && btoa_is_blank(line[name_end - 1U]))
        --name_end;
    if (name_end == name_start) return false;
    if (name_end - name_start <= BTOA_NAME_MAX) {
        xx_rt_memcpy(name, line + name_start, name_end - name_start);
        name[name_end - name_start] = 0;
    }
    *variant = XX_BTOA_VARIANT_NEW;
    *width = value;
    return true;
}

/* At least one blank, then 1..max_digits digits in `base`. */
static bool btoa_field(const uint8_t *line, size_t length, size_t *position,
                       unsigned base, unsigned max_digits, uint64_t *value) {
    size_t at = *position;
    unsigned digits = 0U;
    uint64_t result = 0U;
    if (at >= length || !btoa_is_blank(line[at])) return false;
    while (at < length && btoa_is_blank(line[at])) ++at;
    while (at < length) {
        uint8_t c = line[at];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (base == 16U && c >= 'a' && c <= 'f')
            digit = (unsigned)(c - 'a') + 10U;
        else if (base == 16U && c >= 'A' && c <= 'F')
            digit = (unsigned)(c - 'A') + 10U;
        else break;
        if (++digits > max_digits) return false;
        result = result * base + digit;
        ++at;
    }
    if (digits == 0U) return false;
    *value = result;
    *position = at;
    return true;
}

static bool btoa_label(const uint8_t *line, size_t length, size_t *position,
                       uint8_t letter) {
    size_t at = *position;
    if (at >= length || !btoa_is_blank(line[at])) return false;
    while (at < length && btoa_is_blank(line[at])) ++at;
    if (at >= length || line[at] != letter) return false;
    *position = at + 1U;
    return true;
}

/* "xbtoa End N <dec> <hex> E <hex> S <hex> R <hex>"; both lengths agree. */
static bool btoa_parse_trailer(const uint8_t *line, size_t length,
                               uint64_t *size, uint64_t *eor, uint64_t *sum,
                               uint64_t *rot) {
    size_t position = 11U;
    uint64_t size_hex = 0U;
    while (length > 0U && btoa_is_blank(line[length - 1U])) --length;
    if (length < 11U || xx_rt_memcmp(line, "xbtoa End N", 11U) != 0)
        return false;
    return btoa_field(line, length, &position, 10U, 19U, size) &&
           btoa_field(line, length, &position, 16U, 16U, &size_hex) &&
           btoa_label(line, length, &position, 'E') &&
           btoa_field(line, length, &position, 16U, 16U, eor) &&
           btoa_label(line, length, &position, 'S') &&
           btoa_field(line, length, &position, 16U, 16U, sum) &&
           btoa_label(line, length, &position, 'R') &&
           btoa_field(line, length, &position, 16U, 16U, rot) &&
           position == length && *size == size_hex;
}

/* ---------------------------------------------------------------------- */
/* Segments                                                                */

/* A btoa segment at found->offset.  Validation (sink NULL, limit 0) fills
 * `found`; extraction passes the validated segment's size and 'y' reading. */
static bool btoa_run_btoa(btoa_work *w, btoa_segment *found,
                          xx_io_device *sink, uint64_t limit, uint8_t y_byte,
                          xx_pd_struct *pd) {
    uint32_t variant = 0U, width = 0U;
    uint64_t size = 0U, eor = 0U, sum = 0U, rot = 0U;
    uint64_t lines = 0U;
    int64_t trailer = 0;
    bool short_line = false;
    w->io_failed = false;
    w->position = found->offset;
    if (btoa_read_line(w) <= 0 ||
        !btoa_parse_header(w->line, w->line_length, &variant, &width,
                           found->name)) return false;
    found->variant = variant;
    found->body_offset = w->position;
    btoa_decoder_reset(w, variant, y_byte, sink, limit);
    for (;; ++lines) {
        int64_t line_start = w->position;
        size_t index;
        if (pd && (lines & 0xFFU) == 0U && xx_pd_is_stopped(pd)) return false;
        if (btoa_read_line(w) <= 0) return false;
        if (w->line_length > 0U && w->line[0] == 'x') {
            trailer = line_start;
            break;
        }
        if (variant == XX_BTOA_VARIANT_OLD) {
            for (index = 0U; index < w->line_length; ++index) {
                uint8_t c = w->line[index];
                if (c == ' ' || c == '\t' || c == '\r') continue;
                if (!btoa_data(w, c)) return false;
            }
            continue;
        }
        /* 5.x: data characters, then one check character.  Only the last
         * line of the body may be shorter than the declared width. */
        if (w->line_length == 0U) continue;
        if (short_line || w->line_length < 2U ||
            w->line_length > (size_t)width) return false;
        if (w->line_length != (size_t)width) short_line = true;
        for (index = 0U; index + 1U < w->line_length; ++index) {
            uint8_t c = w->line[index];
            btoa_sum(&w->sums[0], c);
            if (!btoa_data(w, c)) return false;
        }
    }
    /* Every encoder writes whole groups: N is the real part of them. */
    if (!btoa_parse_trailer(w->line, w->line_length, &size, &eor, &sum,
                            &rot) ||
        w->digits != 0U || size > w->produced || w->produced - size > 3U)
        return false;
    if (variant == XX_BTOA_VARIANT_OLD) {
        if (btoa_sums_match(&w->sums[0], eor, sum, rot))
            found->y_byte = 0x20U;
        else if (w->y_seen && btoa_sums_match(&w->sums[1], eor, sum, rot))
            found->y_byte = 0xFFU;
        else
            return false;
    } else {
        if (!btoa_sums_match(&w->sums[0], eor, sum, rot)) return false;
        found->y_byte = 0x20U;
    }
    found->body_size = trailer - found->body_offset;
    found->end = w->position;
    found->size = size;
    return true;
}

static bool btoa_run_adobe(btoa_work *w, btoa_segment *found,
                           xx_io_device *sink, uint64_t limit,
                           xx_pd_struct *pd) {
    uint64_t steps = 0U;
    int64_t tilde = 0;
    w->io_failed = false;
    w->position = found->offset;
    if (btoa_getc(w) != '<' || btoa_getc(w) != '~') return false;
    found->variant = XX_BTOA_VARIANT_ADOBE;
    found->body_offset = w->position;
    found->name[0] = 0;
    btoa_decoder_reset(w, XX_BTOA_VARIANT_ADOBE, 0U, sink, limit);
    for (;;) {
        int c = btoa_getc(w);
        if (c < 0) return false;
        if ((++steps & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (c == '~') {
            tilde = w->position - 1;
            if (btoa_getc(w) != '>') return false;
            break;
        }
        if (c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' ||
            c == ' ') continue;
        if (!btoa_data(w, (uint8_t)c)) return false;
    }
    if (w->digits == 1U) return false;
    if (w->digits > 1U) {
        /* A final group of k digits is completed with 'u' and gives k-1
         * bytes. */
        unsigned kept = w->digits - 1U, index;
        uint64_t group = w->group;
        unsigned digits = w->digits;
        while (digits < 5U) {
            group = group * 85U + 84U;
            ++digits;
        }
        if (group > UINT64_C(0xFFFFFFFF) || !btoa_count(w, kept)) return false;
        for (index = 0U; index < kept; ++index)
            btoa_put(w, (uint8_t)(group >> (24U - 8U * index)));
        w->digits = 0U;
    }
    if (w->produced == 0U) return false;
    found->y_byte = 0U;
    found->body_size = tilde - found->body_offset;
    found->end = w->position;
    found->size = w->produced;
    return true;
}

static bool btoa_run(btoa_work *w, btoa_segment *found, xx_io_device *sink,
                     uint64_t limit, uint8_t y_byte, xx_pd_struct *pd) {
    int first;
    w->io_failed = false;
    w->position = found->offset;
    first = btoa_getc(w);
    if (first == '<')
        return btoa_run_adobe(w, found, sink, limit, pd);
    if (first == 'x')
        return btoa_run_btoa(w, found, sink, limit, y_byte, pd);
    return false;
}

static bool btoa_validate(btoa_work *w, btoa_segment *segment,
                          xx_pd_struct *pd) {
    return btoa_run(w, segment, NULL, 0U, 0x20U, pd);
}

/* Decode a validated segment to `sink` (NULL: decode and verify only). */
static bool btoa_extract(btoa_work *w, const btoa_segment *segment,
                         xx_io_device *sink, xx_pd_struct *pd) {
    btoa_segment check;
    bool result;
    xx_mem_zero(&check, sizeof(check));
    check.offset = segment->offset;
    result = btoa_run(w, &check, sink, segment->size, segment->y_byte, pd);
    if (!btoa_flush(w)) result = false;
    return result && check.variant == segment->variant &&
           check.size == segment->size && check.end == segment->end &&
           check.y_byte == segment->y_byte && !w->out_failed &&
           (!sink || w->written == segment->size);
}

static bool btoa_list_append(btoa_list *list, const btoa_segment *segment) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2U : 4U;
        btoa_segment *grown;
        if (capacity > BTOA_MAX_SEGMENTS) capacity = BTOA_MAX_SEGMENTS;
        if (capacity <= list->count) return false;
        grown = (btoa_segment *)(list->items
                                     ? xx_mem_realloc(list->items,
                                                      capacity * sizeof(*grown))
                                     : xx_mem_alloc(capacity * sizeof(*grown)));
        if (!grown) return false;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = *segment;
    return true;
}

static void btoa_list_free(btoa_list *list) {
    if (list->items) xx_mem_free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0U;
}

/* After a segment: blank lines, then another btoa header line, or not. */
static int64_t btoa_next_candidate(btoa_work *w, int64_t position) {
    unsigned gap;
    w->io_failed = false;
    w->position = position;
    for (gap = 0U; gap < BTOA_MAX_GAP_LINES; ++gap) {
        int64_t line_start = w->position;
        size_t index;
        bool blank = true;
        if (btoa_read_line(w) <= 0) return -1;
        for (index = 0U; index < w->line_length; ++index)
            if (!btoa_is_blank(w->line[index])) blank = false;
        if (blank) continue;
        return (w->line_length >= 7U &&
                xx_rt_memcmp(w->line, "xbtoa", 5U) == 0)
                   ? line_start : -1;
    }
    return -1;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char btoa_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool btoa_same_name(const char *left, const char *right) {
    while (*left && *right && btoa_upper(*left) == btoa_upper(*right)) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

/* The first `stem` bytes of `name` spell `word` exactly, ignoring case. */
static bool btoa_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || btoa_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* A plain file name: printable ASCII, no separators or reserved
 * punctuation, not only dots and spaces, no trailing dot or space, and not
 * a Windows device name with or without an extension. */
static bool btoa_safe_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
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
        if (btoa_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((btoa_upper(name[0]) == 'C' && btoa_upper(name[1]) == 'O' &&
          btoa_upper(name[2]) == 'M') ||
         (btoa_upper(name[0]) == 'L' && btoa_upper(name[1]) == 'P' &&
          btoa_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool btoa_name_taken(const btoa_list *list, size_t before,
                            const char *name) {
    size_t earlier;
    for (earlier = 0U; earlier < before; ++earlier)
        if (btoa_same_name(list->items[earlier].name, name)) return true;
    return false;
}

/* The header name's last path component when it is a safe file name
 * ("-" is btoa's standard input), else the default.  A name already used
 * (ignoring case) by an earlier record gets "_<record number>"; only when a
 * header literally named that too does the suffix count further up. */
static void btoa_assign_names(btoa_list *list) {
    size_t index;
    for (index = 0U; index < list->count; ++index) {
        btoa_segment *segment = &list->items[index];
        char base[BTOA_NAME_BUFFER];
        const char *leaf = segment->name, *scan;
        size_t length;
        unsigned suffix;
        for (scan = segment->name; *scan; ++scan)
            if (*scan == '/' || *scan == '\\' || *scan == ':') leaf = scan + 1;
        if (!leaf[0] || (leaf[0] == '-' && !leaf[1]) || !btoa_safe_name(leaf))
            leaf = BTOA_PAYLOAD_NAME;
        length = xx_str_len(leaf);
        if (length > BTOA_NAME_MAX) length = BTOA_NAME_MAX;
        xx_rt_memcpy(base, leaf, length);
        base[length] = 0;
        xx_rt_memcpy(segment->name, base, length + 1U);
        /* Record numbers are unique, so the first suffix tried almost
         * always settles it; the walk is bounded by the record count. */
        for (suffix = (unsigned)index + 1U;
             btoa_name_taken(list, index, segment->name) &&
             suffix <= (unsigned)(index + 1U + list->count);
             ++suffix)
            (void)xx_rt_snprintf(segment->name, sizeof(segment->name),
                                 "%s_%u", base, suffix);
    }
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */

/* The segment at the base address, and (unless `first_only`) every
 * segment that follows it. */
static bool btoa_collect(Abstractformat *format, btoa_list *list,
                         bool first_only, xx_pd_struct *pd) {
    btoa_work *w;
    btoa_segment segment;
    int64_t total;
    bool result = false;
    if (!format || !format->device || !list || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total <= format->base_address) return false;
    w = (btoa_work *)xx_mem_calloc(1U, sizeof(*w));
    if (!w) return false;
    w->device = format->device;
    w->end = total;
    xx_mem_zero(&segment, sizeof(segment));
    segment.offset = format->base_address;
    if (!btoa_validate(w, &segment, pd) || !btoa_list_append(list, &segment))
        goto done;
    if (!first_only && segment.variant != XX_BTOA_VARIANT_ADOBE) {
        uint64_t total_decoded = segment.size;
        /* A later segment that fails, or that would take the listing past
         * BTOA_MAX_TOTAL, ends the format there. */
        while (list->count < BTOA_MAX_SEGMENTS &&
               total_decoded < BTOA_MAX_TOTAL) {
            int64_t next = btoa_next_candidate(
                w, list->items[list->count - 1U].end);
            if (next < 0) break;
            xx_mem_zero(&segment, sizeof(segment));
            segment.offset = next;
            if (!btoa_validate(w, &segment, pd) ||
                segment.size > BTOA_MAX_TOTAL - total_decoded ||
                !btoa_list_append(list, &segment)) break;
            total_decoded += segment.size;
        }
        if (pd && xx_pd_is_stopped(pd)) goto done;
        btoa_assign_names(list);
    } else if (!first_only) {
        btoa_assign_names(list);
    }
    result = true;
done:
    xx_mem_free(w);
    if (!result) btoa_list_free(list);
    return result;
}

static bool btoa_copy_options(xx_list_s *destination,
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

static const xx_var *btoa_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool btoa_set_record(xx_archive_record *record,
                            const btoa_segment *segment) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = segment->offset;
    record->header_size = segment->body_offset - segment->offset;
    record->data_offset = segment->body_offset;
    record->compressed_size = segment->body_size;
    return xx_archive_record_set_original_name(record, segment->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)segment->body_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          segment->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void btoa_stream_free(void *opaque) {
    btoa_stream *stream = (btoa_stream *)opaque;
    if (!stream) return;
    btoa_list_free(&stream->list);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_btoa_init(xx_btoa *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BTOA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/plain");
    xx_format_set_extension(&archive->format, "btoa");
    archive->format.check_is_valid = xx_btoa_check_is_valid;
    archive->format.handle_base_info = xx_btoa_handle_base_info;
    archive->format.get_format_size = xx_btoa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_btoa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_btoa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_btoa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_btoa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_btoa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_btoa_free_archive_records_reading;
}

xx_btoa *xx_btoa_create(xx_io_device *device, int64_t base_address) {
    xx_btoa *archive = (xx_btoa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_btoa_init(archive, device, base_address);
    return archive;
}

void xx_btoa_destroy(xx_btoa *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_btoa_free(xx_btoa *archive) {
    if (!archive) return;
    xx_btoa_destroy(archive);
    xx_mem_free(archive);
}

bool xx_btoa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    btoa_list list;
    bool result;
    xx_mem_zero(&list, sizeof(list));
    result = btoa_collect(format, &list, true, pd);
    btoa_list_free(&list);
    return result;
}

bool xx_btoa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    btoa_list list;
    xx_btoa *archive;
    uint64_t total = 0U;
    size_t index;
    xx_mem_zero(&list, sizeof(list));
    if (!format || !btoa_collect(format, &list, false, pd)) return false;
    archive = (xx_btoa *)format;
    for (index = 0U; index < list.count; ++index)
        total += list.items[index].size;
    archive->number_of_records = (uint64_t)list.count;
    archive->unpacked_size = total;
    archive->variant = list.items[0].variant;
    format->number_of_archive_records = (uint64_t)list.count;
    format->format_size = list.items[list.count - 1U].end - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    btoa_list_free(&list);
    return true;
}

int64_t xx_btoa_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_btoa_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_btoa_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_btoa_handle_base_info(format, pd))
               ? ((xx_btoa *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_btoa_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    btoa_stream *stream;
    xx_archive_record_state *state;
    stream = (btoa_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!btoa_collect(format, &stream->list, false, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        btoa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = btoa_stream_free;
    state->total_records = (uint64_t)stream->list.count;
    if (!btoa_copy_options(&state->options, options) ||
        !btoa_set_record(&state->current_record, &stream->list.items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_btoa_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_btoa_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    btoa_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (btoa_stream *)state->internal_state) ||
        stream->index + 1U >= stream->list.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!btoa_set_record(&state->current_record,
                         &stream->list.items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

static bool btoa_decode_segment(Abstractformat *format,
                                const btoa_segment *segment,
                                xx_io_device *destination, xx_pd_struct *pd) {
    btoa_work *w;
    bool result;
    int64_t total = xx_io_total_size(format->device);
    if (total < segment->end) return false;
    w = (btoa_work *)xx_mem_calloc(1U, sizeof(*w));
    if (!w) return false;
    w->device = format->device;
    w->end = total;
    result = btoa_extract(w, segment, destination, pd);
    xx_mem_free(w);
    return result;
}

bool xx_btoa_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    btoa_stream *stream;
    const btoa_segment *segment;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(stream = (btoa_stream *)state->internal_state) ||
        stream->index >= stream->list.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    segment = &stream->list.items[stream->index];
    path_option = btoa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return btoa_decode_segment(format, segment, NULL, pd);
    /* Names were reduced to safe leaf names when the list was built; this
     * guards the invariant. */
    if (!btoa_safe_name(segment->name)) return false;
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
               ? xx_str_concat3(base, "/", segment->name)
               : xx_str_concat(base, segment->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = btoa_decode_segment(format, segment, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_btoa_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

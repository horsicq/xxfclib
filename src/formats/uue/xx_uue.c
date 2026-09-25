/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * UUencode / XXencode text: one or more "begin <mode> <name>" ... "end"
 * blocks, each decoding to one member.  xx_uue.h carries the grammar and the
 * acceptance rules.
 *
 * The decoding rules are the classic ones (length character, then four
 * 6-bit symbols per three bytes); Deark's uuencode / xxencode modules
 * (deark-1.7.3/modules/xfer.c, MIT licence, Copyright (C) 2016 Jason
 * Summers) and XArchive's XUU (MIT) were consulted for behaviour only - the
 * line grammar, the per-block alphabet choice, the detection window and the
 * multi-block walk below are this reader's own code.
 *
 * Everything is read through a fixed 8 KiB line reader; no pass allocates
 * more than that, except the name table of a listing (8 bytes per member,
 * at most 65,536 members).  check_is_valid parses no more than the first
 * block's first 64 KiB, twice at worst (once per alphabet).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uue/xx_uue.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as UUE is registered there. */
#ifdef UUE
#define XX_UUE_FILE_TYPE XX_FILE_TYPE_UUE
#else
#define XX_UUE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Bytes of a block, from its header, whose lines must all parse. */
#define UUE_WINDOW ((int64_t)64 * 1024)
/* Most bytes of other text skipped between two blocks. */
#define UUE_MAX_GAP ((int64_t)64 * 1024)
/* Longest header line (without its line break). */
#define UUE_HEADER_MAX 1024U
/* Longest body line kept: a data line is at most 1 + 84 + 2 characters. */
#define UUE_BODY_MAX 128U
/* Longest "end" line (trailing blanks allowed). */
#define UUE_END_MAX 32U
/* Blank lines tolerated between the zero-length line and "end". */
#define UUE_MAX_BLANKS 256U
#define UUE_MAX_BLOCKS 65536U
#define UUE_MAX_MODE_DIGITS 6U
#define UUE_NAME_MAX 240U
#define UUE_CHUNK 8192U
#define UUE_PAYLOAD_NAME "payload"

static const char uue_xx_alphabet[] =
    "+-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* ---------------------------------------------------------------------- */
/* Buffered line reader                                                    */

typedef struct uue_reader_s {
    Abstractformat *format;
    xx_pd_struct *pd;
    int64_t size;          /**< Bytes from the base address to EOF. */
    int64_t buffer_offset; /**< Offset of buffer[0], relative to the base. */
    size_t buffer_fill;
    bool failed;           /**< Read error or stop request. */
    uint8_t buffer[UUE_CHUNK];
} uue_reader;

typedef struct uue_line_s {
    int64_t start;  /**< Offset of the first byte. */
    int64_t next;   /**< Offset past the line break (valid unless overlong). */
    size_t length;  /**< Content bytes, without the line break. */
    bool eof;       /**< No line at all: start is at EOF. */
    bool has_eol;
    bool overlong;  /**< More than `limit` content bytes. */
    bool binary;    /**< Holds 0x00 or 0x1A. */
    uint8_t text[UUE_HEADER_MAX];
} uue_line;

static bool uue_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static void uue_reader_init(uue_reader *reader, Abstractformat *format,
                            int64_t size, xx_pd_struct *pd) {
    reader->format = format;
    reader->pd = pd;
    reader->size = size;
    reader->buffer_offset = 0;
    reader->buffer_fill = 0U;
    reader->failed = false;
}

/* The byte at `offset`, -1 at EOF or on failure. */
static int uue_byte(uue_reader *reader, int64_t offset) {
    if (reader->failed || offset < 0 || offset >= reader->size) return -1;
    if (offset < reader->buffer_offset ||
        offset >= reader->buffer_offset + (int64_t)reader->buffer_fill) {
        int64_t left = reader->size - offset;
        size_t want = left < (int64_t)UUE_CHUNK ? (size_t)left : UUE_CHUNK;
        if ((reader->pd && xx_pd_is_stopped(reader->pd)) ||
            !uue_read_at(reader->format->device,
                         reader->format->base_address + offset,
                         reader->buffer, want)) {
            reader->failed = true;
            reader->buffer_fill = 0U;
            return -1;
        }
        reader->buffer_offset = offset;
        reader->buffer_fill = want;
    }
    return reader->buffer[offset - reader->buffer_offset];
}

/* Read the line at `offset`.  A line break is LF, or a run of CRs with an
 * optional LF after it.  At most `limit` (<= UUE_HEADER_MAX) content bytes
 * are kept; a longer line is flagged overlong and not measured further.
 * False only on a read failure. */
static bool uue_read_line(uue_reader *reader, int64_t offset, size_t limit,
                          uue_line *line) {
    int64_t position = offset;
    int c;
    line->start = offset;
    line->next = offset;
    line->length = 0U;
    line->eof = false;
    line->has_eol = false;
    line->overlong = false;
    line->binary = false;
    if (offset >= reader->size) {
        line->eof = true;
        return !reader->failed;
    }
    for (;;) {
        c = uue_byte(reader, position);
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
            while ((c = uue_byte(reader, position)) == '\r') ++position;
            if (reader->failed) return false;
            if (c == '\n') ++position;
            line->has_eol = true;
            line->next = position;
            return true;
        }
        if (line->length >= limit) {
            line->overlong = true;
            return true;
        }
        if (c == 0x00 || c == 0x1A) line->binary = true;
        line->text[line->length++] = (uint8_t)c;
        ++position;
    }
}

/* ---------------------------------------------------------------------- */
/* Alphabets                                                               */

/* Symbol value of `c`, -1 when it is not a symbol. */
static int uue_value(unsigned method, uint8_t c) {
    if (method == XX_UUE_METHOD_XX) {
        if (c == '+') return 0;
        if (c == '-') return 1;
        if (c >= '0' && c <= '9') return c - '0' + 2;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 12;
        if (c >= 'a' && c <= 'z') return c - 'a' + 38;
        return -1;
    }
    if (c < 0x20U || c > 0x60U) return -1;
    return (c - 0x20) & 0x3F;
}

/* "end" with nothing but blanks after it. */
static bool uue_is_end_line(const uue_line *line) {
    size_t index;
    if (line->eof || line->overlong || line->length < 3U ||
        line->length > UUE_END_MAX || line->text[0] != 'e' ||
        line->text[1] != 'n' || line->text[2] != 'd')
        return false;
    for (index = 3U; index < line->length; ++index)
        if (line->text[index] != ' ' && line->text[index] != '\t')
            return false;
    return true;
}

/* A data line: its decoded byte count, 0 for a zero-length line, -1 when it
 * is not a data line.  `is_short` reports a uuencoded line whose trailing
 * blanks are missing. */
static int uue_data_line(unsigned method, const uue_line *line,
                         bool *is_short) {
    int count;
    size_t need, have, index;
    *is_short = false;
    if (line->eof || line->overlong) return -1;
    if (line->length == 0U) return 0;
    count = uue_value(method, line->text[0]);
    if (count <= 0) return count;
    need = (size_t)(count + 2) / 3U * 4U;
    have = line->length - 1U;
    if (have > need + 2U) return -1;
    for (index = 0U; index < have; ++index) {
        uint8_t c = line->text[1U + index];
        if (uue_value(method, c) >= 0) continue;
        /* Past the groups: a checksum character or trailing blanks. */
        if (index >= need && (c == ' ' || c == '\t')) continue;
        return -1;
    }
    if (have < need) {
        if (method != XX_UUE_METHOD_UU) return -1;
        *is_short = true;
    }
    return count;
}

/* ---------------------------------------------------------------------- */
/* Header and names                                                        */

typedef struct uue_block_s {
    int64_t header_offset; /**< Offsets relative to the base address. */
    int64_t data_offset;
    int64_t data_end;      /**< Past the last data line. */
    int64_t end;           /**< Past the block's last line. */
    uint64_t unpacked_size;
    uint64_t data_lines;
    uint64_t short_lines;
    uint32_t mode;
    unsigned method;
    bool terminated;
    bool has_end;
    char name[UUE_NAME_MAX + 1U]; /**< Safe name, before de-duplication. */
} uue_block;

static char uue_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* The first `stem` bytes of `name` spell `word` exactly, ignoring case. */
static bool uue_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || uue_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* A plain ASCII file name that no host reads as a path, a stream or a
 * device. */
static bool uue_safe_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (!name || length == 0U || length > UUE_NAME_MAX) return false;
    if (length == 1U && name[0] == '-') return false;
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
        if (uue_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((uue_upper(name[0]) == 'C' && uue_upper(name[1]) == 'O' &&
          uue_upper(name[2]) == 'M') ||
         (uue_upper(name[0]) == 'L' && uue_upper(name[1]) == 'P' &&
          uue_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* Last path component, '%' and bytes >= 0x80 escaped as "%XX", or the
 * default name when the result is not safe. */
static void uue_set_name(uue_block *block, const uint8_t *name,
                         size_t length) {
    static const char hex[] = "0123456789ABCDEF";
    char out[UUE_NAME_MAX + 1U];
    size_t start = 0U, index, used = 0U;
    bool ok = true;
    for (index = 0U; index < length; ++index)
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':')
            start = index + 1U;
    for (index = start; index < length && ok; ++index) {
        uint8_t c = name[index];
        if (c >= 0x80U || c == '%') {
            if (used + 3U > UUE_NAME_MAX) {
                ok = false;
                break;
            }
            out[used++] = '%';
            out[used++] = hex[c >> 4U];
            out[used++] = hex[c & 15U];
        } else {
            if (used + 1U > UUE_NAME_MAX) {
                ok = false;
                break;
            }
            out[used++] = (char)c;
        }
    }
    if (!ok || !uue_safe_name(out, used)) {
        used = sizeof(UUE_PAYLOAD_NAME) - 1U;
        xx_rt_memcpy(out, UUE_PAYLOAD_NAME, used);
    }
    xx_rt_memcpy(block->name, out, used);
    block->name[used] = 0;
}

/* "begin <1-6 octal digits> <name>".  Fills mode and name. */
static bool uue_parse_header(const uue_line *line, uue_block *block) {
    size_t position = 6U, digits = 0U, end, index;
    uint32_t mode = 0U;
    if (line->eof || line->overlong || line->length < 9U ||
        xx_rt_memcmp(line->text, "begin ", 6U) != 0)
        return false;
    while (position < line->length && digits < UUE_MAX_MODE_DIGITS &&
           line->text[position] >= '0' && line->text[position] <= '7') {
        mode = (mode << 3U) | (uint32_t)(line->text[position] - '0');
        ++position;
        ++digits;
    }
    if (digits == 0U || position >= line->length ||
        line->text[position] != ' ')
        return false;
    ++position;
    for (index = 0U; index < line->length; ++index) {
        uint8_t c = line->text[index];
        if ((c < 0x20U && c != '\t') || c == 0x7FU) return false;
    }
    end = line->length;
    while (end > position &&
           (line->text[end - 1U] == ' ' || line->text[end - 1U] == '\t'))
        --end;
    if (end <= position) return false;
    block->mode = mode & 07777U;
    uue_set_name(block, line->text + position, end - position);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Blocks                                                                  */

enum { UUE_REJECT = 0, UUE_CLEAN = 1, UUE_FAILED = 2 };

/* Parse the body that starts at block->data_offset with `method`.  With
 * `whole` false the parse stops (clean) at the first line that starts past
 * the block's window; with `whole` true it runs to the block's end, a bad
 * line past the window ending the block unterminated. */
static int uue_parse_body(uue_reader *reader, uue_block *block,
                          unsigned method, bool whole, uue_line *line) {
    int64_t window_end = block->header_offset + UUE_WINDOW;
    int64_t position = block->data_offset;
    unsigned blanks = 0U;
    bool zero = false;
    block->method = method;
    block->data_end = block->end = block->data_offset;
    block->unpacked_size = 0U;
    block->data_lines = 0U;
    block->short_lines = 0U;
    block->terminated = false;
    block->has_end = false;
    for (;;) {
        bool is_short = false;
        int count;
        if (!uue_read_line(reader, position, UUE_BODY_MAX, line))
            return UUE_FAILED;
        if (line->eof) return UUE_CLEAN;
        if (!whole && line->start >= window_end) return UUE_CLEAN;
        if (zero) {
            /* After the zero-length line: blank lines, then maybe "end". */
            if (uue_is_end_line(line)) {
                block->has_end = true;
                block->end = line->next;
                return UUE_CLEAN;
            }
            if (line->length == 0U && line->has_eol &&
                ++blanks <= UUE_MAX_BLANKS) {
                position = line->next;
                continue;
            }
            return UUE_CLEAN;
        }
        if (uue_is_end_line(line)) {
            block->terminated = true;
            block->has_end = true;
            block->end = line->next;
            return UUE_CLEAN;
        }
        count = uue_data_line(method, line, &is_short);
        if (count == 0) {
            block->terminated = true;
            block->end = line->next;
            zero = true;
            position = line->next;
            continue;
        }
        if (count < 0) {
            /* A bad line inside the window rejects the block, unless it is
             * the unfinished last line of a cut-off file. */
            if (line->start < window_end && line->has_eol) return UUE_REJECT;
            return UUE_CLEAN;
        }
        ++block->data_lines;
        if (is_short) ++block->short_lines;
        block->unpacked_size += (uint64_t)count;
        block->data_end = block->end = line->next;
        position = line->next;
    }
}

/* Parse the block whose header line is `header`: choose its alphabet over
 * the window, then (with `whole`) measure it.  UUE_CLEAN when it is a
 * block. */
static int uue_parse_block(uue_reader *reader, const uue_line *header,
                           uue_block *block, bool whole, uue_line *scratch) {
    uue_block xx;
    int uu_result, xx_result = UUE_REJECT;
    unsigned method;
    xx_mem_zero(block, sizeof(*block));
    if (!header->has_eol || !uue_parse_header(header, block)) return UUE_REJECT;
    block->header_offset = header->start;
    block->data_offset = header->next;
    xx = *block;
    uu_result = uue_parse_body(reader, block, XX_UUE_METHOD_UU, false, scratch);
    if (uu_result == UUE_FAILED) return UUE_FAILED;
    if (uu_result != UUE_CLEAN || block->short_lines != 0U) {
        xx_result = uue_parse_body(reader, &xx, XX_UUE_METHOD_XX, false,
                                   scratch);
        if (xx_result == UUE_FAILED) return UUE_FAILED;
    }
    if (xx_result == UUE_CLEAN &&
        (uu_result != UUE_CLEAN || xx.short_lines < block->short_lines)) {
        *block = xx;
        method = XX_UUE_METHOD_XX;
    } else if (uu_result == UUE_CLEAN) {
        method = XX_UUE_METHOD_UU;
    } else {
        return UUE_REJECT;
    }
    if (whole) {
        int result = uue_parse_body(reader, block, method, true, scratch);
        if (result != UUE_CLEAN) return result;
    }
    /* An empty member needs its "end" line. */
    if (block->data_lines == 0U && !block->has_end) return UUE_REJECT;
    return UUE_CLEAN;
}

/* Offset of the first header (past a UTF-8 BOM). */
static int64_t uue_first_header(uue_reader *reader) {
    return (uue_byte(reader, 0) == 0xEF && uue_byte(reader, 1) == 0xBB &&
            uue_byte(reader, 2) == 0xBF)
               ? 3
               : 0;
}

/* The first block.  UUE_CLEAN when the input starts with one. */
static int uue_first_block(uue_reader *reader, uue_block *block, bool whole,
                           uue_line *line, uue_line *scratch) {
    int64_t offset = uue_first_header(reader);
    if (reader->failed) return UUE_FAILED;
    if (uue_byte(reader, offset) != 'b') return UUE_REJECT;
    if (!uue_read_line(reader, offset, UUE_HEADER_MAX, line))
        return UUE_FAILED;
    return uue_parse_block(reader, line, block, whole, scratch);
}

/* The block after `previous`, which ended terminated: skip other text up to
 * the next header.  UUE_REJECT when there is none. */
static int uue_next_block(uue_reader *reader, const uue_block *previous,
                          uue_block *block, uue_line *line,
                          uue_line *scratch) {
    int64_t position = previous->end;
    if (!previous->terminated) return UUE_REJECT;
    while (position - previous->end <= UUE_MAX_GAP) {
        if (!uue_read_line(reader, position, UUE_HEADER_MAX, line))
            return UUE_FAILED;
        if (line->eof || line->overlong || line->binary) return UUE_REJECT;
        /* A line that only starts like a header ("begin the next part") is
         * text; a real header whose block does not parse ends the walk. */
        if (line->has_eol && line->length >= 9U && line->text[0] == 'b' &&
            uue_parse_header(line, block)) {
            uue_block candidate;
            int result = uue_parse_block(reader, line, &candidate, true,
                                         scratch);
            if (result == UUE_CLEAN) *block = candidate;
            return result;
        }
        if (line->next <= position) return UUE_REJECT;
        position = line->next;
    }
    return UUE_REJECT;
}

/* ---------------------------------------------------------------------- */
/* Whole-format walk                                                       */

typedef struct uue_summary_s {
    uint64_t count;
    uint64_t unpacked_size;
    int64_t text_size;
    bool terminated;
} uue_summary;

static bool uue_size(Abstractformat *format, int64_t *size) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *size = total - format->base_address;
    return *size >= 9;
}

/* Walk every block.  With `whole` false only the first block's window is
 * checked (the detection probe). */
static bool uue_walk(Abstractformat *format, bool whole, uue_summary *out,
                     xx_pd_struct *pd) {
    uue_reader *reader;
    uue_line *line, *scratch;
    uue_block block, next;
    uue_summary summary;
    int64_t size;
    int result;
    bool ok = false;
    if (!out || !uue_size(format, &size)) return false;
    reader = (uue_reader *)xx_mem_alloc(sizeof(*reader) + 2U * sizeof(uue_line));
    if (!reader) return false;
    line = (uue_line *)(void *)(reader + 1);
    scratch = line + 1;
    uue_reader_init(reader, format, size, pd);
    xx_mem_zero(&summary, sizeof(summary));
    result = uue_first_block(reader, &block, whole, line, scratch);
    if (result != UUE_CLEAN) goto done;
    summary.count = 1U;
    if (whole) {
        summary.unpacked_size = block.unpacked_size;
        while (summary.count < UUE_MAX_BLOCKS) {
            result = uue_next_block(reader, &block, &next, line, scratch);
            if (result == UUE_FAILED) goto done;
            if (result != UUE_CLEAN) break;
            block = next;
            ++summary.count;
            summary.unpacked_size += block.unpacked_size;
        }
        summary.text_size = block.end;
        summary.terminated = block.terminated;
    }
    *out = summary;
    ok = true;
done:
    xx_mem_free(reader);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Decoding                                                                */

static bool uue_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Decode `block` to `destination` (or only check it when that is NULL).
 * Every line was classified by the measuring pass, so a line that no longer
 * parses or a size mismatch means the input changed underneath. */
static bool uue_decode(Abstractformat *format, const uue_block *block,
                       xx_io_device *destination, xx_pd_struct *pd) {
    uue_reader *reader;
    uue_line *line;
    uint8_t output[UUE_CHUNK];
    size_t used = 0U;
    uint64_t written = 0U;
    int64_t position, size;
    bool ok = false;
    if (!block || !uue_size(format, &size)) return false;
    reader = (uue_reader *)xx_mem_alloc(sizeof(*reader) + sizeof(uue_line));
    if (!reader) return false;
    line = (uue_line *)(void *)(reader + 1);
    uue_reader_init(reader, format, size, pd);
    position = block->data_offset;
    while (position < block->data_end) {
        bool is_short;
        int count, group;
        size_t have;
        if (!uue_read_line(reader, position, UUE_BODY_MAX, line)) goto done;
        count = uue_data_line(block->method, line, &is_short);
        if (count <= 0 || line->next > block->data_end) goto done;
        have = line->length - 1U;
        for (group = 0; group < count; group += 3) {
            uint32_t value = 0U;
            size_t index, take = (size_t)(count - group < 3 ? count - group
                                                             : 3);
            for (index = 0U; index < 4U; ++index) {
                size_t at = (size_t)group / 3U * 4U + index;
                int symbol = at < have ? uue_value(block->method,
                                                   line->text[1U + at])
                                       : 0;
                if (symbol < 0) goto done;
                value = (value << 6U) | (uint32_t)symbol;
            }
            for (index = 0U; index < take; ++index)
                output[used++] = (uint8_t)(value >> (16U - 8U * index));
        }
        if (used > sizeof(output) - 64U) {
            if (destination && !uue_write_all(destination, output, used))
                goto done;
            written += used;
            used = 0U;
        }
        position = line->next;
    }
    if (used != 0U) {
        if (destination && !uue_write_all(destination, output, used))
            goto done;
        written += used;
    }
    ok = written == block->unpacked_size;
done:
    xx_mem_free(reader);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct uue_stream_s {
    uue_reader *reader;
    uue_line *line;
    uue_line *scratch;
    uue_block block;
    uint64_t index;
    uint64_t count;
    uint64_t *seen; /**< Open-addressed set of name hashes, 0 = empty. */
    size_t seen_mask;
    char name[UUE_NAME_MAX + 24U]; /**< The current member's final name. */
} uue_stream;

static void uue_stream_free(void *opaque) {
    uue_stream *stream = (uue_stream *)opaque;
    if (!stream) return;
    if (stream->reader) xx_mem_free(stream->reader);
    if (stream->seen) xx_mem_free(stream->seen);
    xx_mem_free(stream);
}

static uint64_t uue_name_hash(const char *name) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (; *name; ++name) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        hash ^= (uint8_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash ? hash : 1U;
}

/* Add `hash`; false when it was already there. */
static bool uue_seen_add(uue_stream *stream, uint64_t hash) {
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

/* The member name: the block's safe name, or, when an earlier member took
 * it already, the same with "%_<block number>" before its extension. */
static void uue_final_name(uue_stream *stream) {
    const char *name = stream->block.name;
    size_t length = xx_str_len(name), dot = length, index;
    char digits[24];
    size_t digit_count = 0U;
    uint64_t number = stream->index + 1U;
    if (uue_seen_add(stream, uue_name_hash(name))) {
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
    for (index = 0U; index < digit_count; ++index)
        stream->name[dot + 2U + index] = digits[digit_count - 1U - index];
    xx_rt_memcpy(stream->name + dot + 2U + digit_count, name + dot,
                 length - dot + 1U);
    (void)uue_seen_add(stream, uue_name_hash(stream->name));
}

static bool uue_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *uue_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool uue_set_record(Abstractformat *format, xx_archive_record *record,
                           uue_stream *stream) {
    const uue_block *block = &stream->block;
    uue_final_name(stream);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + block->header_offset;
    record->header_size = block->data_offset - block->header_offset;
    record->data_offset = format->base_address + block->data_offset;
    record->compressed_size = block->data_end - block->data_offset;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          block->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          block->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          block->mode) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_uue_init(xx_uue *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_UUE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/x-uuencode");
    xx_format_set_extension(&archive->format, "uue");
    archive->format.check_is_valid = xx_uue_check_is_valid;
    archive->format.handle_base_info = xx_uue_handle_base_info;
    archive->format.get_format_size = xx_uue_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_uue_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_uue_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_uue_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_uue_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_uue_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_uue_free_archive_records_reading;
}

xx_uue *xx_uue_create(xx_io_device *device, int64_t base_address) {
    xx_uue *archive = (xx_uue *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_uue_init(archive, device, base_address);
    return archive;
}

void xx_uue_destroy(xx_uue *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_uue_free(xx_uue *archive) {
    if (!archive) return;
    xx_uue_destroy(archive);
    xx_mem_free(archive);
}

bool xx_uue_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uue_summary summary;
    return uue_walk(format, false, &summary, pd);
}

bool xx_uue_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    uue_summary summary;
    xx_uue *archive;
    if (!format || !uue_walk(format, true, &summary, pd)) return false;
    archive = (xx_uue *)format;
    archive->number_of_records = summary.count;
    archive->unpacked_size = summary.unpacked_size;
    archive->text_size = summary.text_size;
    archive->is_terminated = summary.terminated;
    format->number_of_archive_records = summary.count;
    format->format_size = summary.text_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_uue_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_uue_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_uue_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_uue_handle_base_info(format, pd))
               ? ((xx_uue *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_uue_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    uue_stream *stream;
    xx_archive_record_state *state;
    size_t slots = 16U;
    int64_t size;
    if (!format ||
        (!format->base_info_handled && !xx_uue_handle_base_info(format, pd)) ||
        !uue_size(format, &size))
        return NULL;
    stream = (uue_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = ((xx_uue *)format)->number_of_records;
    while (slots < 2U * stream->count) slots <<= 1U;
    stream->seen = (uint64_t *)xx_mem_calloc(slots, sizeof(uint64_t));
    stream->seen_mask = slots - 1U;
    stream->reader =
        (uue_reader *)xx_mem_alloc(sizeof(uue_reader) + 2U * sizeof(uue_line));
    if (!stream->seen || !stream->reader || stream->count == 0U) {
        uue_stream_free(stream);
        return NULL;
    }
    stream->line = (uue_line *)(void *)(stream->reader + 1);
    stream->scratch = stream->line + 1;
    uue_reader_init(stream->reader, format, size, NULL);
    if (uue_first_block(stream->reader, &stream->block, true, stream->line,
                        stream->scratch) != UUE_CLEAN) {
        uue_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        uue_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = uue_stream_free;
    state->total_records = stream->count;
    if (!uue_copy_options(&state->options, options) ||
        !uue_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_uue_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_uue_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    uue_stream *stream;
    uue_block next;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (uue_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    stream->reader->pd = pd;
    stream->reader->failed = false;
    if (uue_next_block(stream->reader, &stream->block, &next, stream->line,
                       stream->scratch) != UUE_CLEAN) {
        stream->reader->pd = NULL;
        state->has_record = false;
        return false;
    }
    stream->reader->pd = NULL;
    stream->block = next;
    ++stream->index;
    if (!uue_set_record(format, &state->current_record, stream)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_uue_unpack_current_to_device(Abstractformat *format,
                                     xx_archive_record_state *state,
                                     xx_io_device *destination,
                                     xx_pd_struct *pd) {
    uue_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (uue_stream *)state->internal_state) || !destination)
        return false;
    return uue_decode(format, &stream->block, destination, pd);
}

bool xx_uue_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    uue_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (uue_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = uue_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the block still decodes and report that. */
        return uue_decode(format, &stream->block, NULL, pd);
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = uue_decode(format, &stream->block, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_uue_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

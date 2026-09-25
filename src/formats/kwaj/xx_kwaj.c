/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The KWAJ member of the MS-DOS installation compression family.
 * xx_kwaj.h carries the header table.
 *
 * The decoders are written from the format's structure.  Where the format
 * leaves behaviour open (how a method-3 stream ends, what an unassigned
 * Huffman code yields, when an MSZIP chain stops) this reader follows Deark
 * 1.7.3, modules/mscompress.c (MIT licence, Copyright (C) 2017 Jason
 * Summers), which is the reference its output is compared against:
 *
 *  - method 2 is the LZSS of SZ/QBasic: a 4 KiB window of spaces written
 *    from position 4096-18, one flag byte per eight tokens (bit set: a
 *    literal byte; clear: a 12-bit window position and a 4-bit length - 3);
 *  - method 3 reads its five Huffman tables and then its tokens MSB-first;
 *    a code prefix that no table entry continues decodes as symbol 0, and
 *    a token cut short by the end of the input ends the stream;
 *  - method 4 is a chain of [u16 length]["CK"][raw Deflate] blocks sharing
 *    a 32 KiB history; a block of fewer than 32768 bytes, a zero length
 *    word, or fewer than four bytes left ends it.
 *
 * Only method 4 has a natural end.  The other methods stop at the declared
 * length when the header has one and otherwise run to the end of the file,
 * so their format size is then the whole input.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/kwaj/xx_kwaj.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef KWAJ
#define XX_KWAJ_FILE_TYPE XX_FILE_TYPE_KWAJ
#else
#define XX_KWAJ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define KWAJ_HEADER_SIZE 14
#define KWAJ_METHOD_STORED 0U
#define KWAJ_METHOD_XOR 1U
#define KWAJ_METHOD_LZSS 2U
#define KWAJ_METHOD_LZH 3U
#define KWAJ_METHOD_MSZIP 4U

#define KWAJ_FLAG_LENGTH 0x0001U
#define KWAJ_FLAG_UNKNOWN1 0x0002U
#define KWAJ_FLAG_UNKNOWN2 0x0004U
#define KWAJ_FLAG_NAME 0x0008U
#define KWAJ_FLAG_EXTENSION 0x0010U

/* Output ceiling for one member, declared or not: DOS-era installation
 * files are far smaller, and it bounds what a hostile stream can write. */
#define KWAJ_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)
/* Above this much compressed data the base-info size pass is skipped and
 * the sizes are taken from the header alone. */
#define KWAJ_MAX_SIZE_PASS ((int64_t)64 * 1024 * 1024)

#define KWAJ_IN_BUFFER 65536U
#define KWAJ_OUT_BUFFER 65536U
#define KWAJ_WINDOW 4096U
#define KWAJ_LZSS_START (KWAJ_WINDOW - 18U)

#define KWAJ_MSZIP_BLOCK 32768U
/* A stored Deflate block header: one byte (BFINAL 0, BTYPE 00, padding)
 * and LEN/NLEN.  It carries the previous block into the next one. */
#define KWAJ_MSZIP_PREFIX 5U
#define KWAJ_MSZIP_DATA (KWAJ_MSZIP_PREFIX + KWAJ_MSZIP_BLOCK)
#define KWAJ_MSZIP_INPUT (KWAJ_MSZIP_DATA + 65535U)
#define KWAJ_MSZIP_OUTPUT (2U * KWAJ_MSZIP_BLOCK)

/* Deark's Huffman builder refuses codes longer than 48 bits; a table entry
 * longer than that is simply absent. */
#define KWAJ_HUFF_MAX_BITS 48U
#define KWAJ_TREES 5U

#define KWAJ_NAME_BASE_MAX 8U
#define KWAJ_NAME_EXT_MAX 3U
/* Each stored byte becomes at most two UTF-8 bytes. */
#define KWAJ_NAME_CAPACITY (2U * (KWAJ_NAME_BASE_MAX + 1U + KWAJ_NAME_EXT_MAX) + 1U)
#define KWAJ_PAYLOAD_NAME "payload"

static const uint8_t kwaj_magic[8] = {'K', 'W', 'A', 'J',
                                      0x88U, 0xf0U, 0x27U, 0xd1U};

typedef struct kwaj_header_s {
    uint32_t method;
    uint32_t flags;
    int64_t data_offset; /**< Relative to the base address. */
    bool has_length;
    uint32_t length;
    bool has_name;
    bool name_safe;
    char name[KWAJ_NAME_CAPACITY]; /**< UTF-8. */
} kwaj_header;

typedef struct kwaj_context_s {
    kwaj_header header;
    int64_t input_size;  /**< Bytes from the base address to end of device. */
    int64_t stream_size; /**< Format size: header plus the compressed data. */
    uint64_t unpacked_size;
    bool unpacked_size_known;
} kwaj_context;

typedef struct kwaj_stream_s {
    kwaj_context context;
    size_t index;
    size_t count;
} kwaj_stream;

static uint16_t kwaj_le16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t kwaj_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool kwaj_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- member name ------------------------------------------------------- */

static char kwaj_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool kwaj_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || kwaj_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Extraction writes <base>/<name>.  Refused: control bytes (C0, DEL, C1),
 * separators, drive colons and the other characters Windows reserves,
 * names made only of dots and spaces ("." / ".."), and device names such
 * as CON, NUL.TXT, COM1 or CONIN$ in any case, with or without extension. */
static bool kwaj_name_is_safe(const uint8_t *raw, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    const char *name = (const char *)raw;
    size_t stem = 0U, index;
    bool meaningful = false;
    if (!raw || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || (c >= 0x7fU && c <= 0x9fU) || c == '/' ||
            c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' ||
            c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (kwaj_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((kwaj_upper(name[0]) == 'C' && kwaj_upper(name[1]) == 'O' &&
          kwaj_upper(name[2]) == 'M') ||
         (kwaj_upper(name[0]) == 'L' && kwaj_upper(name[1]) == 'P' &&
          kwaj_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* The stored bytes are read as Latin-1.  Bytes the listing cannot show
 * (controls) are listed as '_'; the name is then unsafe anyway. */
static void kwaj_name_to_utf8(const uint8_t *raw, size_t length, char *out) {
    size_t index, used = 0U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || (c >= 0x7fU && c <= 0x9fU)) {
            out[used++] = '_';
        } else if (c < 0x80U) {
            out[used++] = (char)c;
        } else {
            out[used++] = (char)(0xc0U | (c >> 6U));
            out[used++] = (char)(0x80U | (c & 0x3fU));
        }
    }
    out[used] = '\0';
}

/* A NUL-terminated field of at most @p limit characters, entirely before
 * the data offset.  Returns the length, or -1 when it is not terminated in
 * time (the header's names are then ignored, as Deark does). */
static int kwaj_read_field(xx_io_device *device, int64_t base,
                           int64_t position, int64_t data_offset,
                           size_t limit, uint8_t *out) {
    uint8_t bytes[KWAJ_NAME_BASE_MAX + 1U];
    size_t available, index;
    if (position >= data_offset) return -1;
    available = (size_t)((data_offset - position) < (int64_t)(limit + 1U)
                             ? (data_offset - position)
                             : (int64_t)(limit + 1U));
    if (!kwaj_read_at(device, base + position, bytes, available)) return -1;
    for (index = 0U; index < available; ++index) {
        if (bytes[index] == 0U) {
            xx_rt_memcpy(out, bytes, index);
            return (int)index;
        }
    }
    return -1;
}

/* ---- header ------------------------------------------------------------ */

static bool kwaj_parse_header(xx_io_device *device, int64_t base,
                              int64_t size, kwaj_header *out) {
    uint8_t raw[KWAJ_HEADER_SIZE];
    uint8_t word[4];
    uint8_t base_name[KWAJ_NAME_BASE_MAX + 1U];
    uint8_t extension[KWAJ_NAME_EXT_MAX + 1U];
    uint8_t joined[KWAJ_NAME_BASE_MAX + 1U + KWAJ_NAME_EXT_MAX];
    kwaj_header header;
    int64_t position;
    int base_length = -1, extension_length = -1;
    if (!device || !out || base < 0 || size < KWAJ_HEADER_SIZE ||
        !kwaj_read_at(device, base, raw, sizeof(raw)) ||
        xx_rt_memcmp(raw, kwaj_magic, sizeof(kwaj_magic)) != 0)
        return false;
    xx_mem_zero(&header, sizeof(header));
    header.method = kwaj_le16(raw + 8U);
    header.data_offset = (int64_t)kwaj_le16(raw + 10U);
    header.flags = kwaj_le16(raw + 12U);
    /* The data cannot overlap the fixed header, and it must be present. */
    if (header.method > KWAJ_METHOD_MSZIP ||
        header.data_offset < KWAJ_HEADER_SIZE || header.data_offset > size)
        return false;
    position = KWAJ_HEADER_SIZE;
    if (header.flags & KWAJ_FLAG_LENGTH) {
        if (header.data_offset - position < 4 ||
            !kwaj_read_at(device, base + position, word, 4U))
            return false;
        header.has_length = true;
        header.length = kwaj_le32(word);
        if ((uint64_t)header.length > KWAJ_MAX_OUTPUT) return false;
        position += 4;
    }
    /* The remaining fields only carry the name.  The data offset is stored
     * explicitly, so a field that does not fit merely loses the name. */
    if (header.flags & KWAJ_FLAG_UNKNOWN1) position += 2;
    if (header.flags & KWAJ_FLAG_UNKNOWN2) {
        if (header.data_offset - position < 2 ||
            !kwaj_read_at(device, base + position, word, 2U))
            goto names_done;
        position += 2 + (int64_t)kwaj_le16(word);
    }
    if (header.flags & KWAJ_FLAG_NAME) {
        base_length = kwaj_read_field(device, base, position,
                                      header.data_offset, KWAJ_NAME_BASE_MAX,
                                      base_name);
        if (base_length < 0) goto names_done;
        position += base_length + 1;
    }
    if (header.flags & KWAJ_FLAG_EXTENSION) {
        extension_length = kwaj_read_field(device, base, position,
                                           header.data_offset,
                                           KWAJ_NAME_EXT_MAX, extension);
    }
names_done:
    if (base_length > 0) {
        size_t length = (size_t)base_length;
        xx_rt_memcpy(joined, base_name, length);
        if (extension_length > 0) {
            joined[length++] = '.';
            xx_rt_memcpy(joined + length, extension, (size_t)extension_length);
            length += (size_t)extension_length;
        }
        header.has_name = true;
        header.name_safe = kwaj_name_is_safe(joined, length);
        kwaj_name_to_utf8(joined, length, header.name);
    } else {
        header.name_safe = true;
        xx_rt_memcpy(header.name, KWAJ_PAYLOAD_NAME,
                     sizeof(KWAJ_PAYLOAD_NAME));
    }
    *out = header;
    return true;
}

/* ---- buffered input / output ------------------------------------------ */

typedef struct kwaj_input_s {
    xx_io_device *device;
    int64_t next; /**< Device offset of the next byte to buffer. */
    int64_t end;  /**< Device offset one past the input. */
    size_t length;
    size_t index;
    bool failed;
    uint8_t buffer[KWAJ_IN_BUFFER];
} kwaj_input;

static int kwaj_input_byte(kwaj_input *in) {
    if (in->index >= in->length) {
        int64_t left = in->end - in->next;
        size_t want;
        if (in->failed || left <= 0) return -1;
        want = left < (int64_t)KWAJ_IN_BUFFER ? (size_t)left
                                              : (size_t)KWAJ_IN_BUFFER;
        if (!kwaj_read_at(in->device, in->next, in->buffer, want)) {
            in->failed = true;
            return -1;
        }
        in->next += (int64_t)want;
        in->length = want;
        in->index = 0U;
    }
    return in->buffer[in->index++];
}

/* Device offset just past the last byte handed out. */
static int64_t kwaj_input_position(const kwaj_input *in) {
    return in->next - (int64_t)(in->length - in->index);
}

typedef struct kwaj_output_s {
    xx_io_device *device; /**< NULL: count only. */
    xx_pd_struct *pd;
    uint64_t total;
    uint64_t limit; /**< Declared length, or one past the ceiling. */
    bool failed;
    size_t used;
    uint8_t buffer[KWAJ_OUT_BUFFER];
} kwaj_output;

static bool kwaj_output_flush(kwaj_output *out) {
    size_t done = 0U;
    if (out->pd && xx_pd_is_stopped(out->pd)) out->failed = true;
    while (!out->failed && out->device && done < out->used) {
        ssize_t amount = xx_io_write(out->device, out->buffer + done,
                                     out->used - done);
        if (amount <= 0 || (size_t)amount > out->used - done)
            out->failed = true;
        else
            done += (size_t)amount;
    }
    out->used = 0U;
    return !out->failed;
}

static bool kwaj_output_full(const kwaj_output *out) {
    return out->failed || out->total >= out->limit;
}

/* False once no further byte is wanted: the limit is reached, a write
 * failed, or the caller cancelled. */
static bool kwaj_output_byte(kwaj_output *out, uint8_t value) {
    if (kwaj_output_full(out)) return false;
    out->buffer[out->used++] = value;
    ++out->total;
    if (out->used == KWAJ_OUT_BUFFER && !kwaj_output_flush(out)) return false;
    return out->total < out->limit;
}

/* ---- methods 0 and 1 ---------------------------------------------------- */

static void kwaj_decode_copy(kwaj_input *in, kwaj_output *out, uint8_t mask) {
    while (!kwaj_output_full(out)) {
        int value = kwaj_input_byte(in);
        if (value < 0 || !kwaj_output_byte(out, (uint8_t)value ^ mask)) break;
    }
}

/* ---- method 2: LZSS ------------------------------------------------------ */

static void kwaj_decode_lzss(kwaj_input *in, kwaj_output *out,
                             uint8_t *window) {
    unsigned position = KWAJ_LZSS_START;
    xx_rt_memset(window, 0x20, KWAJ_WINDOW);
    for (;;) {
        int flags = kwaj_input_byte(in);
        unsigned bit;
        if (flags < 0) return;
        for (bit = 0U; bit < 8U; ++bit) {
            if (kwaj_output_full(out)) return;
            if ((unsigned)flags & (1U << bit)) {
                int value = kwaj_input_byte(in);
                if (value < 0) return;
                window[position] = (uint8_t)value;
                position = (position + 1U) & (KWAJ_WINDOW - 1U);
                if (!kwaj_output_byte(out, (uint8_t)value)) return;
            } else {
                int low = kwaj_input_byte(in), high;
                unsigned from, count, index;
                if (low < 0 || (high = kwaj_input_byte(in)) < 0) return;
                from = (unsigned)low | (((unsigned)high & 0xf0U) << 4U);
                count = ((unsigned)high & 0x0fU) + 3U;
                for (index = 0U; index < count; ++index) {
                    uint8_t value = window[(from + index) & (KWAJ_WINDOW - 1U)];
                    window[position] = value;
                    position = (position + 1U) & (KWAJ_WINDOW - 1U);
                    if (!kwaj_output_byte(out, value)) return;
                }
            }
        }
    }
}

/* ---- method 3: LZ + Huffman --------------------------------------------- */

typedef struct kwaj_bits_s {
    kwaj_input *in;
    uint32_t buffer;
    unsigned count;
    bool eof;
} kwaj_bits;

/* MSB-first.  At the end of the input it latches `eof` and yields 0. */
static unsigned kwaj_bits_get(kwaj_bits *bits, unsigned width) {
    if (bits->eof) return 0U;
    while (bits->count < width) {
        int value = kwaj_input_byte(bits->in);
        if (value < 0) {
            bits->eof = true;
            return 0U;
        }
        bits->buffer = (bits->buffer << 8U) | (uint32_t)value;
        bits->count += 8U;
    }
    bits->count -= width;
    return (unsigned)(bits->buffer >> bits->count) & ((1U << width) - 1U);
}

typedef struct kwaj_huff_s {
    unsigned max_bits;
    uint64_t kraft; /**< sum of count[l] << (max_bits - l). */
    uint16_t count[KWAJ_HUFF_MAX_BITS + 1U];
    uint16_t symbol[256];
} kwaj_huff;

/* Canonical code from per-symbol lengths (0 = absent), shortest codes
 * first and, within a length, in symbol order.  An over-subscribed set is
 * corrupt; an incomplete one is allowed. */
static bool kwaj_huff_build(kwaj_huff *huff, const uint8_t *lengths,
                            unsigned symbols) {
    uint16_t offset[KWAJ_HUFF_MAX_BITS + 2U];
    unsigned index, bits;
    xx_mem_zero(huff, sizeof(*huff));
    for (index = 0U; index < symbols; ++index) {
        unsigned length = lengths[index];
        if (length == 0U || length > KWAJ_HUFF_MAX_BITS) continue;
        ++huff->count[length];
        if (length > huff->max_bits) huff->max_bits = length;
    }
    for (bits = 1U; bits <= huff->max_bits; ++bits)
        huff->kraft += (uint64_t)huff->count[bits]
                       << (huff->max_bits - bits);
    if (huff->max_bits != 0U &&
        huff->kraft > ((uint64_t)1U << huff->max_bits))
        return false;
    offset[1] = 0U;
    for (bits = 1U; bits <= KWAJ_HUFF_MAX_BITS; ++bits)
        offset[bits + 1U] = (uint16_t)(offset[bits] + huff->count[bits]);
    for (index = 0U; index < symbols; ++index) {
        unsigned length = lengths[index];
        if (length == 0U || length > KWAJ_HUFF_MAX_BITS) continue;
        huff->symbol[offset[length]++] = (uint16_t)index;
    }
    return true;
}

/* One symbol, or -1 at the end of the input.  A prefix that no code
 * continues yields symbol 0 after the bits read so far, as in Deark. */
static int kwaj_huff_decode(const kwaj_huff *huff, kwaj_bits *bits) {
    uint64_t code = 0U, first = 0U;
    unsigned length, index = 0U;
    if (huff->max_bits == 0U) {
        (void)kwaj_bits_get(bits, 1U);
        return bits->eof ? -1 : 0;
    }
    for (length = 1U; length <= huff->max_bits; ++length) {
        unsigned shift = huff->max_bits - length;
        uint64_t used;
        code = (code << 1U) | kwaj_bits_get(bits, 1U);
        if (bits->eof) return -1;
        if (code - first < huff->count[length])
            return huff->symbol[index + (unsigned)(code - first)];
        index += huff->count[length];
        first = (first + huff->count[length]) << 1U;
        used = (huff->kraft + (((uint64_t)1U << shift) - 1U)) >> shift;
        if (code >= used) return 0;
    }
    return 0;
}

/* Table encodings 0-3.  Lengths are 8-bit and wrap as in the reference. */
static bool kwaj_huff_read(kwaj_huff *huff, kwaj_bits *bits, unsigned type,
                           unsigned symbols) {
    uint8_t lengths[256];
    uint8_t previous;
    unsigned index;
    switch (type) {
    case 0: {
        uint8_t width = symbols <= 16U ? 4U : symbols <= 32U ? 5U
                        : symbols <= 64U ? 6U : 8U;
        for (index = 0U; index < symbols; ++index) lengths[index] = width;
        break;
    }
    case 1:
        lengths[0] = (uint8_t)kwaj_bits_get(bits, 4U);
        previous = lengths[0];
        for (index = 1U; index < symbols && !bits->eof; ++index) {
            if (kwaj_bits_get(bits, 1U) != 0U) {
                if (kwaj_bits_get(bits, 1U) == 0U)
                    previous = (uint8_t)(previous + 1U);
                else
                    previous = (uint8_t)kwaj_bits_get(bits, 4U);
            }
            lengths[index] = previous;
        }
        break;
    case 2:
        lengths[0] = (uint8_t)kwaj_bits_get(bits, 4U);
        previous = lengths[0];
        for (index = 1U; index < symbols && !bits->eof; ++index) {
            unsigned step = kwaj_bits_get(bits, 2U);
            if (step == 3U)
                previous = (uint8_t)kwaj_bits_get(bits, 4U);
            else
                previous = (uint8_t)(previous + step - 1U);
            lengths[index] = previous;
        }
        break;
    case 3:
        for (index = 0U; index < symbols && !bits->eof; ++index)
            lengths[index] = (uint8_t)kwaj_bits_get(bits, 4U);
        break;
    default:
        return false;
    }
    if (bits->eof) return false;
    return kwaj_huff_build(huff, lengths, symbols);
}

typedef struct kwaj_lzh_s {
    kwaj_huff tree[KWAJ_TREES];
    uint8_t window[KWAJ_WINDOW];
} kwaj_lzh;

enum { KWAJ_T_MATCH = 0, KWAJ_T_MATCH2, KWAJ_T_LITLEN, KWAJ_T_OFFSET,
       KWAJ_T_LITERAL };

/* False only when the table header is damaged; the token stream itself
 * ends wherever the input ends. */
static bool kwaj_decode_lzh(kwaj_input *in, kwaj_output *out, kwaj_lzh *lzh) {
    static const unsigned symbols[KWAJ_TREES] = {16U, 16U, 32U, 64U, 256U};
    unsigned types[KWAJ_TREES];
    kwaj_bits bits;
    const kwaj_huff *lengths_table;
    unsigned index, position = 0U;
    xx_mem_zero(&bits, sizeof(bits));
    bits.in = in;
    for (index = 0U; index < KWAJ_TREES; ++index)
        types[index] = kwaj_bits_get(&bits, 4U);
    (void)kwaj_bits_get(&bits, 4U);
    if (bits.eof) return false;
    for (index = 0U; index < KWAJ_TREES; ++index)
        if (!kwaj_huff_read(&lzh->tree[index], &bits, types[index],
                            symbols[index]))
            return false;
    xx_rt_memset(lzh->window, 0x20, sizeof(lzh->window));
    lengths_table = &lzh->tree[KWAJ_T_MATCH];
    for (;;) {
        int value;
        if (kwaj_output_full(out)) break;
        value = kwaj_huff_decode(lengths_table, &bits);
        if (value < 0) break;
        if (value != 0) {
            unsigned count = (unsigned)value + 2U, from;
            int high = kwaj_huff_decode(&lzh->tree[KWAJ_T_OFFSET], &bits);
            unsigned low;
            if (high < 0) break;
            low = kwaj_bits_get(&bits, 6U);
            if (bits.eof) break;
            from = (position - (((unsigned)high << 6U) | low)) &
                   (KWAJ_WINDOW - 1U);
            lengths_table = &lzh->tree[KWAJ_T_MATCH];
            for (index = 0U; index < count; ++index) {
                uint8_t byte = lzh->window[from];
                from = (from + 1U) & (KWAJ_WINDOW - 1U);
                lzh->window[position] = byte;
                position = (position + 1U) & (KWAJ_WINDOW - 1U);
                if (!kwaj_output_byte(out, byte)) return true;
            }
        } else {
            int run = kwaj_huff_decode(&lzh->tree[KWAJ_T_LITLEN], &bits);
            if (run < 0) break;
            /* A run shorter than 32 is followed by a match, coded with the
             * second length table; a full run keeps the current table. */
            if (run != 31) lengths_table = &lzh->tree[KWAJ_T_MATCH2];
            for (index = 0U; index <= (unsigned)run; ++index) {
                int byte = kwaj_huff_decode(&lzh->tree[KWAJ_T_LITERAL], &bits);
                if (byte < 0) return true;
                lzh->window[position] = (uint8_t)byte;
                position = (position + 1U) & (KWAJ_WINDOW - 1U);
                if (!kwaj_output_byte(out, (uint8_t)byte)) return true;
            }
        }
    }
    return true;
}

/* ---- method 4: MSZIP ----------------------------------------------------- */

typedef struct kwaj_mszip_s {
    uint8_t input[KWAJ_MSZIP_INPUT];
    uint8_t output[KWAJ_MSZIP_OUTPUT];
} kwaj_mszip;

/* Decode the block chain from device offset @p start to @p end.  Each
 * block is inflated on its own with the previous block in front of it as a
 * stored Deflate block, which makes the shared 32 KiB history visible to
 * the public raw-Deflate decoder.  @p stream_end receives the offset just
 * past the chain. */
static bool kwaj_decode_mszip(xx_io_device *device, int64_t start,
                              int64_t end, kwaj_output *out,
                              kwaj_mszip *work, int64_t *stream_end) {
    int64_t position = start;
    size_t history = 0U;
    for (;;) {
        uint8_t head[4];
        int64_t remaining = end - position;
        size_t packed, written = 0U;
        uint8_t *source;
        size_t source_size, produced, index;
        if (kwaj_output_full(out) || remaining < 2) break;
        if (!kwaj_read_at(device, position, head, 2U)) return false;
        if (kwaj_le16(head) == 0U) {
            position += 2;
            break;
        }
        if (remaining < 4) break;
        if (!kwaj_read_at(device, position + 2, head + 2, 2U) ||
            head[2] != 'C' || head[3] != 'K')
            return false;
        packed = kwaj_le16(head);
        if (packed <= 2U || (int64_t)packed - 2 > remaining - 4) return false;
        packed -= 2U;
        if (!kwaj_read_at(device, position + 4, work->input + KWAJ_MSZIP_DATA,
                          packed))
            return false;
        if (history != 0U) {
            uint8_t *prefix = work->input + KWAJ_MSZIP_DATA - history -
                              KWAJ_MSZIP_PREFIX;
            prefix[0] = 0U;
            prefix[1] = (uint8_t)(history & 0xffU);
            prefix[2] = (uint8_t)(history >> 8U);
            prefix[3] = (uint8_t)~prefix[1];
            prefix[4] = (uint8_t)~prefix[2];
            source = prefix;
        } else {
            source = work->input + KWAJ_MSZIP_DATA;
        }
        source_size = (size_t)(work->input + KWAJ_MSZIP_DATA - source) + packed;
        if (!xx_deflate_decompress_memory(source, source_size, work->output,
                                          history + KWAJ_MSZIP_BLOCK,
                                          &written, false) ||
            written < history)
            return false;
        produced = written - history;
        for (index = 0U; index < produced; ++index)
            if (!kwaj_output_byte(out, work->output[history + index])) break;
        if (out->failed) return false;
        position += 4 + (int64_t)packed;
        if (produced < KWAJ_MSZIP_BLOCK) break;
        xx_rt_memcpy(work->input + KWAJ_MSZIP_PREFIX,
                     work->output + history, KWAJ_MSZIP_BLOCK);
        history = KWAJ_MSZIP_BLOCK;
    }
    *stream_end = position;
    return true;
}

/* ---- driver ---------------------------------------------------------------- */

/* Decode the member.  @p destination may be NULL to measure only.  On
 * success @p produced is the member's size and @p stream_size the format
 * size (header included). */
static bool kwaj_run(xx_io_device *device, int64_t base, int64_t input_size,
                     const kwaj_header *header, xx_io_device *destination,
                     xx_pd_struct *pd, uint64_t *produced,
                     int64_t *stream_size) {
    kwaj_output *out;
    int64_t start = base + header->data_offset;
    int64_t end = base + input_size;
    int64_t stream_end = end;
    bool result = false;
    out = (kwaj_output *)xx_mem_alloc(sizeof(*out));
    if (!out) return false;
    xx_mem_zero(out, sizeof(*out));
    out->device = destination;
    out->pd = pd;
    out->limit = header->has_length ? (uint64_t)header->length
                                    : KWAJ_MAX_OUTPUT + 1U;
    if (header->method == KWAJ_METHOD_MSZIP) {
        kwaj_mszip *work = (kwaj_mszip *)xx_mem_alloc(sizeof(*work));
        if (!work) goto done;
        result = kwaj_decode_mszip(device, start, end, out, work, &stream_end);
        xx_mem_free(work);
    } else {
        kwaj_input *in = (kwaj_input *)xx_mem_alloc(sizeof(*in));
        kwaj_lzh *lzh = NULL;
        if (!in) goto done;
        xx_mem_zero(in, sizeof(*in));
        in->device = device;
        in->next = start;
        in->end = end;
        result = true;
        switch (header->method) {
        case KWAJ_METHOD_STORED: kwaj_decode_copy(in, out, 0x00U); break;
        case KWAJ_METHOD_XOR: kwaj_decode_copy(in, out, 0xffU); break;
        case KWAJ_METHOD_LZSS:
            lzh = (kwaj_lzh *)xx_mem_alloc(sizeof(*lzh));
            if (!lzh) result = false;
            else kwaj_decode_lzss(in, out, lzh->window);
            break;
        default:
            lzh = (kwaj_lzh *)xx_mem_alloc(sizeof(*lzh));
            result = lzh && kwaj_decode_lzh(in, out, lzh);
            break;
        }
        if (in->failed) result = false;
        /* With a declared length the stream ends at its last used byte;
         * without one it owns the rest of the input. */
        if (header->has_length) stream_end = kwaj_input_position(in);
        if (lzh) xx_mem_free(lzh);
        xx_mem_free(in);
    }
    if (result && !kwaj_output_flush(out)) result = false;
    if (result) {
        if (header->has_length ? out->total != (uint64_t)header->length
                               : out->total > KWAJ_MAX_OUTPUT)
            result = false;
    }
    if (result) {
        *produced = out->total;
        *stream_size = stream_end - base;
    }
done:
    xx_mem_free(out);
    return result;
}

static bool kwaj_parse(Abstractformat *format, kwaj_context *out,
                       bool measure, xx_pd_struct *pd) {
    kwaj_context context;
    int64_t total;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&context, sizeof(context));
    context.input_size = total - format->base_address;
    if (!kwaj_parse_header(format->device, format->base_address,
                           context.input_size, &context.header))
        return false;
    context.stream_size = context.input_size;
    if (context.header.has_length) {
        context.unpacked_size = context.header.length;
        context.unpacked_size_known = true;
        if (context.header.method <= KWAJ_METHOD_XOR) {
            if ((int64_t)context.header.length >
                context.input_size - context.header.data_offset)
                return false;
            context.stream_size =
                context.header.data_offset + (int64_t)context.header.length;
        }
    }
    if (measure &&
        context.input_size - context.header.data_offset <= KWAJ_MAX_SIZE_PASS) {
        uint64_t produced = 0U;
        int64_t stream_size = 0;
        if (!kwaj_run(format->device, format->base_address,
                      context.input_size, &context.header, NULL, pd,
                      &produced, &stream_size))
            return false;
        context.unpacked_size = produced;
        context.unpacked_size_known = true;
        context.stream_size = stream_size;
    }
    *out = context;
    return true;
}

static void kwaj_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool kwaj_copy_options(xx_list_s *destination,
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

static const xx_var *kwaj_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool kwaj_set_record(xx_archive_record *record, int64_t base,
                            const kwaj_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base;
    record->header_size = context->header.data_offset;
    record->data_offset = base + context->header.data_offset;
    record->compressed_size =
        context->stream_size - context->header.data_offset;
    return xx_archive_record_set_original_name(record, context->header.name) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(context->stream_size -
                          context->header.data_offset)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          context->header.method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_kwaj_init(xx_kwaj *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_KWAJ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ms-compress-kwaj");
    xx_format_set_extension(&archive->format, "kwaj");
    archive->format.check_is_valid = xx_kwaj_check_is_valid;
    archive->format.handle_base_info = xx_kwaj_handle_base_info;
    archive->format.get_format_size = xx_kwaj_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_kwaj_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_kwaj_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_kwaj_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_kwaj_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_kwaj_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_kwaj_free_archive_records_reading;
}

xx_kwaj *xx_kwaj_create(xx_io_device *device, int64_t base_address) {
    xx_kwaj *archive = (xx_kwaj *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_kwaj_init(archive, device, base_address);
    return archive;
}

void xx_kwaj_destroy(xx_kwaj *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_kwaj_free(xx_kwaj *archive) {
    if (!archive) return;
    xx_kwaj_destroy(archive);
    xx_mem_free(archive);
}

/* The header alone: magic, method, data offset and, when flagged, a length
 * field that fits before the data.  Cheap enough for the detector. */
bool xx_kwaj_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    kwaj_context context;
    return kwaj_parse(format, &context, false, pd);
}

bool xx_kwaj_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    kwaj_context context;
    xx_kwaj *archive;
    if (!format || !kwaj_parse(format, &context, true, pd)) return false;
    archive = (xx_kwaj *)format;
    archive->number_of_records = 1U;
    archive->method = context.header.method;
    archive->header_flags = context.header.flags;
    archive->data_offset = context.header.data_offset;
    archive->has_length = context.header.has_length;
    archive->unpacked_size = context.unpacked_size;
    archive->unpacked_size_known = context.unpacked_size_known;
    format->number_of_archive_records = 1U;
    format->format_size = context.stream_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_kwaj_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kwaj_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_kwaj_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kwaj_handle_base_info(format, pd))
               ? ((xx_kwaj *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_kwaj_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    kwaj_stream *stream;
    xx_archive_record_state *state;
    kwaj_context context;
    if (!kwaj_parse(format, &context, true, pd)) return NULL;
    stream = (kwaj_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = kwaj_stream_free;
    state->total_records = 1U;
    if (!kwaj_copy_options(&state->options, options) ||
        !kwaj_set_record(&state->current_record, format->base_address,
                         &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_kwaj_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_kwaj_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    kwaj_stream *stream;
    (void)pd;
    if (state && state->format == format &&
        (stream = (kwaj_stream *)state->internal_state) != NULL)
        stream->index = stream->count;
    if (state) state->has_record = false;
    return false;
}

bool xx_kwaj_unpack_to_device(xx_kwaj *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    kwaj_context context;
    uint64_t produced = 0U;
    int64_t stream_size = 0;
    if (!archive || !destination ||
        !kwaj_parse(&archive->format, &context, false, pd))
        return false;
    return kwaj_run(archive->format.device, archive->format.base_address,
                    context.input_size, &context.header, destination, pd,
                    &produced, &stream_size);
}

bool xx_kwaj_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    kwaj_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    uint64_t produced = 0U;
    int64_t stream_size = 0;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (kwaj_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = kwaj_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the member decodes. */
        return kwaj_run(format->device, format->base_address,
                        stream->context.input_size, &stream->context.header,
                        NULL, pd, &produced, &stream_size);
    }
    if (!stream->context.header.name_safe) return false;
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
               ? xx_str_concat3(base, "/", stream->context.header.name)
               : xx_str_concat(base, stream->context.header.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = kwaj_run(format->device, format->base_address,
                          stream->context.input_size, &stream->context.header,
                          destination, pd, &produced, &stream_size);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_kwaj_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

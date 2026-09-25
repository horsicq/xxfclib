/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stunts / 4D Sports Driving "DSI" compressed resources (".PES" in the corpus,
 * also ".PVS", ".P3S" and ".PRE").  This is a single-payload wrapper, not a
 * multi-member archive, so the record list always holds exactly one entry.
 *
 *   header, 4 bytes at offset 0:
 *     0x00   1  u8       0x80 | pass count (1..8)
 *     0x01   3  u24 LE   length of the final plaintext
 *
 *   then, for each pass, a pass header followed by that pass's coded bytes:
 *     +0x00  1  u8       codec: 1 = RLE, 2 = variable-length codes
 *     +0x01  3  u24 LE   length this pass produces
 *
 * Passes are chained: each one's output is the next one's input, header and
 * all, and the last pass must produce exactly the length the file header
 * declares.  A two-pass file (VLE then RLE) is what every corpus sample is.
 *
 * Both codecs are ported from XArchive's core/xdecompress.cpp
 * (decStuntsVLE / decStuntsRLE / decStuntsDSI).
 *
 *   RLE pass: a 3-byte source length, a reserved zero byte, then an escape
 *   header whose low seven bits give an escape-byte count (1..10) and whose
 *   high bit, when clear, means the coded bytes are themselves wrapped in a
 *   "repeat this byte sequence" layer keyed on the second escape byte.  In the
 *   run layer escape #1 takes an 8-bit count and a byte, escape #3 a 16-bit
 *   count and a byte, and escape #n (n = 2, 4..10) means n - 1 copies of the
 *   byte that follows.
 *
 *   VLE pass: a canonical variable-length code.  A levels header gives the
 *   number of code widths (1..15) and, in its high bit, whether symbols are
 *   stored as deltas against the previous output byte; then one symbol count
 *   per width, then the alphabet.  Widths of eight bits and under are resolved
 *   by a direct 256-entry table; wider codes fall through to the escape
 *   base/limit recurrences built from the same width distribution.  Stunts 1.0
 *   and earlier store each coded byte with its bits reversed, so a VLE pass
 *   that fails - or that does not yield a valid pass header for the next pass -
 *   is retried in the reversed bit order, exactly as the reference does.
 *
 * THERE IS NO MAGIC.  The 24-bit declared length is the format's only
 * integrity statement, so parse runs the complete decode and requires it to
 * produce exactly that many bytes; the structural gate (high bit set, pass
 * count 1..8, first codec byte 1 or 2) is only a cheap pre-filter in front of
 * it.  Verified byte for byte against U3's output for all 20 samples in
 * F:\ARC\ARC\STUNTS_FT.
 *
 * A stream without the high bit in byte 0 declares no plaintext length at all
 * and therefore cannot be validated; this reader rejects it rather than
 * guessing, which matches the reference implementation's own prefilter.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stunts/xx_stunts.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef STUNTS
#define XX_STUNTS_FILE_TYPE XX_FILE_TYPE_STUNTS
#else
#define XX_STUNTS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_STUNTS_HEADER_SIZE 4
#define XX_STUNTS_MIN_SIZE 12
#define XX_STUNTS_MAX_PASSES 8
#define XX_STUNTS_METHOD_DSI 1U
/* The declared plaintext length is a 24-bit field, so it can never exceed
 * this; stating it makes the output allocation's bound explicit. */
#define XX_STUNTS_MAX_DECODED 0xffffff
/* A DSI stream expands, so a packed file larger than the widest possible
 * plaintext by a wide margin is not one.  This also caps the single read of
 * the whole file that the decoder needs. */
#define XX_STUNTS_MAX_PACKED (2 * XX_STUNTS_MAX_DECODED)
/* The container stores no file name - the packer replaces the file in place -
 * and this reader cannot see the container's own name, so the single record
 * gets a fixed placeholder. */
#define XX_STUNTS_PLACEHOLDER_NAME "stunts_data"

typedef struct xx_stunts_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;             /* packed bytes behind the 4-byte header */
    int64_t uncompressed_size;
} xx_stunts_member;

typedef struct xx_stunts_stream_s {
    xx_stunts_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_stunts_stream;

static void xx_stunts_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_stunts_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_stunts_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The container carries no name; the single record always gets the same
 * placeholder.  It is kept as a function so the record-building code below
 * looks like every other reader's. */
static char *xx_stunts_make_name(void) {
    return xx_str_dup(XX_STUNTS_PLACEHOLDER_NAME);
}

static void xx_stunts_stream_free(void *pointer) {
    xx_stunts_stream *stream = (xx_stunts_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

/* --------------------------------------------------------------- codec -- */

/* The 24-bit little-endian length that prefixes every pass and the file. */
static bool xx_stunts_read_u24(const uint8_t *data, size_t size, size_t *pos,
                               int64_t *value) {
    if (*pos + 3U > size) return false;
    *value = (int64_t)data[*pos] | ((int64_t)data[*pos + 1U] << 8) |
             ((int64_t)data[*pos + 2U] << 16);
    *pos += 3U;
    return true;
}

typedef struct xx_stunts_bits_s {
    const uint8_t *source;
    size_t size;
    size_t position;
    unsigned padding;
    bool reverse;
} xx_stunts_bits;

/* The original 16-bit decoder always fetches a look-ahead byte, even when the
 * last code already completed the requested output.  At most two such bytes
 * are treated as zero padding - never as an unbounded source for a truncated
 * stream. */
static bool xx_stunts_next_byte(xx_stunts_bits *bits, uint8_t *value) {
    uint8_t byte = 0U;

    if (bits->position < bits->size) {
        byte = bits->source[bits->position++];
    } else {
        if (bits->padding >= 2U) return false;
        ++bits->padding;
        ++bits->position;
    }
    if (bits->reverse) {
        byte = (uint8_t)(((byte & 0x55U) << 1) | ((byte >> 1) & 0x55U));
        byte = (uint8_t)(((byte & 0x33U) << 2) | ((byte >> 2) & 0x33U));
        byte = (uint8_t)((byte << 4) | (byte >> 4));
    }
    *value = byte;
    return true;
}

/* Decode DSI's canonical variable-length codes into @p output, which the
 * caller sized to @p output_size from a length the stream itself declared.
 * The decoder never writes past it: the loop is bounded by output_size and
 * every table index is range-checked before use. */
static bool xx_stunts_vle(const uint8_t *source, size_t size, size_t offset,
                          int64_t output_size, uint8_t *output,
                          bool reverse_bits, xx_pd_struct *pd) {
    uint8_t distribution[15];
    uint16_t escape_base[16];
    uint16_t escape_limit[16];
    uint8_t symbols[256];
    uint8_t widths[256];
    xx_stunts_bits bits;
    const uint8_t *alphabet;
    size_t position = offset;
    int32_t increment = 0;
    int32_t alphabet_size = 0;
    int32_t table_index = 0;
    int32_t alphabet_index = 0;
    int32_t repetitions = 0x80;
    int32_t direct_widths;
    int32_t width;
    int32_t index;
    int64_t written = 0;
    uint8_t levels_header;
    uint8_t widths_length;
    uint8_t current_width;
    uint8_t next_width;
    uint8_t first_byte;
    uint8_t second_byte;
    uint8_t previous_output = 0U;
    uint16_t current_word;
    bool delta_symbols;

    if (output_size < 0 || position >= size) return false;
    levels_header = source[position++];
    delta_symbols = (levels_header & 0x80U) != 0U;
    widths_length = (uint8_t)(levels_header & 0x7fU);
    if (widths_length == 0U || widths_length > 15U) return false;
    if (position + widths_length > size) return false;

    xx_rt_memset(escape_base, 0, sizeof(escape_base));
    xx_rt_memset(escape_limit, 0, sizeof(escape_limit));
    for (index = 0; index < (int32_t)widths_length; ++index) {
        uint8_t count;

        increment *= 2;
        escape_base[index] = (uint16_t)(alphabet_size - increment);
        count = source[position++];
        distribution[index] = count;
        increment += (int32_t)count;
        alphabet_size += (int32_t)count;
        escape_limit[index] = (uint16_t)increment;
        if (alphabet_size > 256 || increment > 0xffff) return false;
    }
    if (alphabet_size < 2) return false;
    if (position + (size_t)alphabet_size + 2U > size) return false;
    alphabet = source + position;
    position += (size_t)alphabet_size;

    xx_rt_memset(symbols, 0, sizeof(symbols));
    xx_rt_memset(widths, 0x40, sizeof(widths));
    direct_widths = widths_length < 8U ? (int32_t)widths_length : 8;
    for (width = 1; width <= direct_widths; ++width, repetitions >>= 1) {
        int32_t groups = (int32_t)distribution[width - 1];
        int32_t group;

        for (group = 0; group < groups; ++group) {
            int32_t j;

            if (alphabet_index >= alphabet_size ||
                table_index > 256 - repetitions) {
                return false;
            }
            for (j = 0; j < repetitions; ++j) {
                symbols[table_index] = alphabet[alphabet_index];
                widths[table_index++] = (uint8_t)width;
            }
            ++alphabet_index;
        }
    }

    bits.source = source;
    bits.size = size;
    bits.position = position;
    bits.padding = 0U;
    bits.reverse = reverse_bits;

    current_width = 8U;
    next_width = 0U;
    if (!xx_stunts_next_byte(&bits, &first_byte) ||
        !xx_stunts_next_byte(&bits, &second_byte)) {
        return false;
    }
    current_word = (uint16_t)(((uint16_t)first_byte << 8) | second_byte);

    while (written < output_size) {
        uint8_t code;
        uint8_t value;

        if ((written & 0x3fff) == 0 && pd && xx_pd_is_stopped(pd)) return false;
        code = (uint8_t)(current_word >> 8);
        next_width = widths[code];
        if (next_width > 8U) {
            uint8_t following;
            bool found = false;

            /* 0x40 is the "not a direct code" filler the table was primed
             * with; any other wide value means the table is corrupt. */
            if (next_width != 0x40U) return false;
            code = (uint8_t)current_word;
            current_word = (uint16_t)(current_word >> 8);
            index = 7;
            while (!found) {
                if (current_width == 0U) {
                    if (!xx_stunts_next_byte(&bits, &code)) return false;
                    current_width = 8U;
                }
                current_word = (uint16_t)((uint32_t)(current_word << 1) |
                                          ((code & 0x80U) ? 1U : 0U));
                code = (uint8_t)(code << 1);
                --current_width;
                ++index;
                if (index >= (int32_t)widths_length || index >= 16) {
                    return false;
                }
                if (current_word < escape_limit[index]) {
                    current_word =
                        (uint16_t)(current_word + escape_base[index]);
                    if ((int32_t)current_word >= alphabet_size) return false;
                    value = alphabet[current_word];
                    if (delta_symbols) {
                        value = (uint8_t)(previous_output + value);
                    }
                    previous_output = value;
                    output[written++] = value;
                    found = true;
                }
            }
            if (!xx_stunts_next_byte(&bits, &following)) return false;
            current_word =
                (uint16_t)(((uint16_t)code << current_width) | following);
            next_width = (uint8_t)(8U - current_width);
            current_width = 8U;
        } else {
            if (next_width == 0U) return false;
            value = symbols[code];
            if (delta_symbols) value = (uint8_t)(previous_output + value);
            previous_output = value;
            output[written++] = value;
            if (current_width < next_width) {
                uint8_t following;

                current_word = (uint16_t)(current_word << current_width);
                next_width = (uint8_t)(next_width - current_width);
                current_width = 8U;
                if (!xx_stunts_next_byte(&bits, &following)) return false;
                current_word = (uint16_t)(current_word | following);
            }
        }
        current_word = (uint16_t)(current_word << next_width);
        current_width = (uint8_t)(current_width - next_width);
    }
    return true;
}

/* Decode a DSI run-length pass into @p output, sized by the caller to
 * @p output_size.  Both layers - the optional byte-sequence repeat wrapper and
 * the run layer itself - refuse to emit a byte once output_size is reached, so
 * neither buffer can be overrun by a hostile count. */
static bool xx_stunts_rle(const uint8_t *source, size_t size, size_t offset,
                          int64_t output_size, uint8_t *output,
                          xx_pd_struct *pd) {
    uint8_t lookup[256];
    const uint8_t *escapes;
    const uint8_t *final_source;
    uint8_t *expanded = NULL;
    size_t final_size;
    size_t final_position;
    size_t position = offset;
    int64_t declared = 0;
    int32_t escape_count;
    int32_t index;
    int64_t written = 0;
    uint8_t reserved;
    uint8_t escape_header;
    bool result = false;

    if (output_size < 0) return false;
    if (!xx_stunts_read_u24(source, size, &position, &declared)) return false;
    if (declared < 1 || position + 2U > size) return false;
    reserved = source[position++];
    escape_header = source[position++];
    escape_count = (int32_t)(escape_header & 0x7fU);
    if (reserved != 0U || escape_count < 1 || escape_count > 10) return false;
    if (position + (size_t)escape_count > size) return false;
    escapes = source + position;
    position += (size_t)escape_count;

    final_source = source;
    final_size = size;
    final_position = position;

    if ((escape_header & 0x80U) == 0U) {
        /* The coded bytes are themselves wrapped: a marker byte brackets a
         * literal sequence, which a following count then repeats. */
        uint8_t sequence_escape;
        size_t produced = 0U;

        if (escape_count < 2) return false;
        sequence_escape = escapes[1];
        expanded = (uint8_t *)xx_mem_alloc((size_t)output_size);
        if (!expanded) return false;
        while (position < size) {
            uint8_t value;
            size_t sequence_start;
            size_t sequence_length;
            int32_t repeat;

            if ((produced & 0x3fffU) == 0U && pd && xx_pd_is_stopped(pd)) {
                goto done;
            }
            value = source[position++];
            if (value != sequence_escape) {
                if ((int64_t)produced >= output_size) goto done;
                expanded[produced++] = value;
                continue;
            }
            sequence_start = position;
            while (position < size && source[position] != sequence_escape) {
                if ((int64_t)produced >= output_size) goto done;
                expanded[produced++] = source[position++];
            }
            if (position >= size) goto done;
            sequence_length = position - sequence_start;
            ++position;
            if (position >= size) goto done;
            repeat = (int32_t)source[position++] - 1;
            if (sequence_length < 1U ||
                (int64_t)repeat * (int64_t)sequence_length >
                    output_size - (int64_t)produced) {
                goto done;
            }
            while (repeat-- > 0) {
                xx_rt_memcpy(expanded + produced, source + sequence_start,
                             sequence_length);
                produced += sequence_length;
            }
        }
        final_source = expanded;
        final_size = produced;
        final_position = 0U;
    }

    xx_rt_memset(lookup, 0, sizeof(lookup));
    for (index = 0; index < escape_count; ++index) {
        lookup[escapes[index]] = (uint8_t)(index + 1);
    }
    while (written < output_size) {
        uint8_t value;
        int32_t escape_index;
        int64_t repeat;

        if ((written & 0x3fff) == 0 && pd && xx_pd_is_stopped(pd)) goto done;
        if (final_position >= final_size) goto done;
        value = final_source[final_position++];
        escape_index = (int32_t)lookup[value];
        if (escape_index == 0) {
            output[written++] = value;
            continue;
        }
        if (escape_index == 1) {
            if (final_position + 2U > final_size) goto done;
            repeat = (int64_t)final_source[final_position++];
            value = final_source[final_position++];
        } else if (escape_index == 3) {
            if (final_position + 3U > final_size) goto done;
            repeat = (int64_t)final_source[final_position] |
                     ((int64_t)final_source[final_position + 1U] << 8);
            final_position += 2U;
            value = final_source[final_position++];
        } else {
            if (final_position >= final_size) goto done;
            repeat = (int64_t)escape_index - 1;
            value = final_source[final_position++];
        }
        if (repeat < 0 || repeat > output_size - written) goto done;
        if (repeat != 0) {
            xx_rt_memset(output + written, (int)value, (size_t)repeat);
            written += repeat;
        }
    }
    result = true;

done:
    if (expanded) xx_mem_free(expanded);
    return result;
}

/* Run the whole chain.  @p packed is the complete file; the plaintext comes
 * back in @p out / @p out_size and is always exactly the length the file
 * header declared, which is the format's only integrity statement. */
static bool xx_stunts_dsi(const uint8_t *packed, size_t packed_size,
                          uint8_t **out, int64_t *out_size,
                          xx_pd_struct *pd) {
    const uint8_t *current = packed;
    uint8_t *owned = NULL;
    size_t current_size = packed_size;
    size_t position = 0U;
    int64_t expected = 0;
    int32_t passes;
    int32_t pass;

    *out = NULL;
    *out_size = 0;
    if (packed_size < XX_STUNTS_HEADER_SIZE) return false;
    /* No high bit means no declared plaintext length, and with no length
     * there is nothing to verify a decode against: refuse rather than guess. */
    if ((packed[0] & 0x80U) == 0U) return false;
    passes = (int32_t)(packed[0] & 0x7fU);
    if (passes < 1 || passes > XX_STUNTS_MAX_PASSES) return false;
    position = 1U;
    if (!xx_stunts_read_u24(packed, packed_size, &position, &expected)) {
        return false;
    }
    if (expected < 1 || expected > XX_STUNTS_MAX_DECODED) return false;

    for (pass = 0; pass < passes; ++pass) {
        uint8_t *decoded;
        int64_t pass_size = 0;
        uint8_t type;
        bool ok;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (position >= current_size) goto fail;
        type = current[position++];
        if (type != 1U && type != 2U) goto fail;
        if (!xx_stunts_read_u24(current, current_size, &position, &pass_size)) {
            goto fail;
        }
        /* Every pass length is bounded before it is allocated, and the last
         * one must agree with the file header exactly. */
        if (pass_size < 1 || pass_size > XX_STUNTS_MAX_DECODED) goto fail;
        if (pass + 1 == passes && pass_size != expected) goto fail;
        decoded = (uint8_t *)xx_mem_alloc((size_t)pass_size);
        if (!decoded) goto fail;
        if (type == 1U) {
            ok = xx_stunts_rle(current, current_size, position, pass_size,
                               decoded, pd);
        } else {
            ok = xx_stunts_vle(current, current_size, position, pass_size,
                               decoded, false, pd);
            /* Stunts 1.0 and earlier store each coded byte bit-reversed.
             * Prefer the later order, but retry when the decode failed or
             * could not produce the pass header the next pass needs. */
            if (!ok || (pass + 1 < passes &&
                        (pass_size < 4 ||
                         (decoded[0] != 1U && decoded[0] != 2U)))) {
                ok = xx_stunts_vle(current, current_size, position, pass_size,
                                   decoded, true, pd);
            }
        }
        if (!ok) {
            xx_mem_free(decoded);
            goto fail;
        }
        if (owned) xx_mem_free(owned);
        owned = decoded;
        current = decoded;
        current_size = (size_t)pass_size;
        position = 0U;
    }
    if (!owned || (int64_t)current_size != expected) goto fail;
    *out = owned;
    *out_size = expected;
    return true;

fail:
    if (owned) xx_mem_free(owned);
    return false;
}

/* Read the whole container.  The single read is bounded by the packed-size
 * ceiling above, so a large unrelated file is refused before it is read. */
static uint8_t *xx_stunts_load(Abstractformat *self, int64_t span) {
    uint8_t *packed;

    if (span < XX_STUNTS_MIN_SIZE || span > XX_STUNTS_MAX_PACKED) return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!packed) return NULL;
    if (!xx_stunts_read_at(self, self->base_address, packed, (size_t)span)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

static xx_stunts_stream *xx_stunts_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_stunts_stream *stream = NULL;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    int64_t plain_size = 0;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    packed = xx_stunts_load(self, span);
    if (!packed) return NULL;

    /* Cheap structural pre-filter before the decode: the high bit, a sane
     * pass count and a codec byte this reader implements. */
    if ((packed[0] & 0x80U) == 0U ||
        (packed[0] & 0x7fU) < 1U ||
        (packed[0] & 0x7fU) > XX_STUNTS_MAX_PASSES ||
        (packed[4] != 1U && packed[4] != 2U)) {
        goto fail;
    }

    /* The decode IS the validation: with no magic and no checksum, producing
     * exactly the declared number of plaintext bytes is the only evidence the
     * file is what it claims to be. */
    if (!xx_stunts_dsi(packed, (size_t)span, &plain, &plain_size, pd)) {
        goto fail;
    }
    xx_mem_free(plain);
    plain = NULL;

    stream = (xx_stunts_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_stunts_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items));

    stream->items[0].name = xx_stunts_make_name();
    if (!stream->items[0].name) goto fail;
    stream->items[0].header_offset = self->base_address;
    stream->items[0].header_size = XX_STUNTS_HEADER_SIZE;
    stream->items[0].data_offset = self->base_address + XX_STUNTS_HEADER_SIZE;
    stream->items[0].size = span - XX_STUNTS_HEADER_SIZE;
    stream->items[0].uncompressed_size = plain_size;
    stream->count = 1U;

    xx_mem_free(packed);
    stream->archive_size = span;
    return stream;

fail:
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    xx_stunts_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* Decoding re-reads the container and runs the same chain parse verified, so
 * the plaintext length is guaranteed to match what the record published. */
static bool xx_stunts_decode(Abstractformat *self,
                             const xx_stunts_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain = NULL;
    int64_t plain_size = 0;
    int64_t span;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    span = member->size + XX_STUNTS_HEADER_SIZE;
    packed = xx_stunts_load(self, span);
    if (!packed) return false;
    if (!xx_stunts_dsi(packed, (size_t)span, &plain, &plain_size, pd)) {
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    if (plain_size != member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = (size_t)plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_stunts_init(xx_stunts *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_STUNTS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stunts-resource");
    xx_format_set_extension(&archive->format, "pes");
    archive->format.check_is_valid = xx_stunts_check_is_valid;
    archive->format.handle_base_info = xx_stunts_handle_base_info;
    archive->format.get_format_size = xx_stunts_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stunts_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stunts_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stunts_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stunts_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stunts_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stunts_free_archive_records_reading;
    archive->format.destroy = xx_stunts_vtable_destroy;
}

xx_stunts *xx_stunts_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_stunts *archive = (xx_stunts *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_stunts_init(archive, device, base_address);
    return archive;
}

void xx_stunts_destroy(xx_stunts *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_stunts_free(xx_stunts *archive) {
    if (!archive) return;
    xx_stunts_destroy(archive);
    xx_mem_free(archive);
}

static void xx_stunts_vtable_destroy(Abstractformat *self) {
    xx_stunts_destroy((xx_stunts *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_stunts_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_stunts_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_stunts_parse(self, pd);
    if (!stream) return false;
    xx_stunts_stream_free(stream);
    return true;
}

bool xx_stunts_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_stunts *archive = (xx_stunts *)self;
    xx_stunts_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_stunts_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_stunts_stream_free(stream);
    return true;
}

int64_t xx_stunts_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_stunts_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_stunts *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_stunts_set_record(xx_archive_record *record,
                                     const xx_stunts_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_STUNTS_METHOD_DSI) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_stunts_copy_options(xx_list_s *target,
                                       const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_stunts_get_option(const xx_list_s *options,
                                              uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_stunts_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_stunts_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_stunts_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_stunts_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_stunts_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_stunts_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_stunts_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stunts_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stunts_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_stunts_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_stunts_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_stunts_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stunts_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_stunts_stream *stream;
    const xx_stunts_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_stunts_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_stunts_path_safe(member->name)) return false;

    path_option = xx_stunts_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_stunts_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_stunts_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_stunts_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

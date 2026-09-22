/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Three Electronic Arts codecs, ported from the XArchive reference decoders:
 *
 *   xx_ea_decode_memory          <- Algos/xealzwdecoder.cpp   (EA .PEA method 1)
 *   xx_ea_lib_decode_memory      <- Algos/xealibdecoder.cpp   (EALIB method 1)
 *   xx_ea_refpack_*_memory       <- Algos/xearefpackdecoder.cpp (RefPack / QFS)
 *
 * They share a vendor and nothing else; no state, no tables and no helpers are
 * common between them.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/ea/xx_ea.h"

/* ------------------------------------------------------------------ */
/* EA .PEA method 1: 12-bit LZW                                        */
/* ------------------------------------------------------------------ */

#define EA_LZW_MAX_BITS 12
#define EA_LZW_CLEAR 0x100U
#define EA_LZW_END 0x101U
#define EA_LZW_FIRST_CODE 0x102U
#define EA_LZW_MAX_CODES (1U << EA_LZW_MAX_BITS)

typedef struct ea_lzw_tables {
    uint16_t prefix[EA_LZW_MAX_CODES];
    uint8_t suffix[EA_LZW_MAX_CODES];
    /* One slot per possible code plus the final literal pushed after the
     * chain walk. */
    uint8_t stack[EA_LZW_MAX_CODES + 1U];
} ea_lzw_tables;

typedef struct ea_bits {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint32_t buffer;
    unsigned count;
} ea_bits;

/* LSB-first: bytes enter the accumulator at the current bit height and the
 * requested width is taken off the bottom. */
static bool ea_lzw_read(ea_bits *reader, unsigned width, uint32_t *value)
{
    while (reader->count < width) {
        if (reader->offset >= reader->input_size) return false;
        reader->buffer |= (uint32_t)reader->input[reader->offset++]
                          << reader->count;
        reader->count += 8U;
    }
    *value = reader->buffer & ((1U << width) - 1U);
    reader->buffer >>= width;
    reader->count -= width;
    return true;
}

bool xx_ea_decode_memory(const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t output_size, size_t *written)
{
    ea_lzw_tables *tables;
    ea_bits reader;
    size_t output_at = 0U;
    unsigned width = 9U;
    uint32_t max_code = 0x1ffU;
    uint32_t next_free = EA_LZW_FIRST_CODE;
    uint32_t previous = 0U;
    uint32_t first_char = 0U;
    bool started = false;
    bool ended = false;
    bool failed = false;
    unsigned index;

    if (written) *written = 0U;
    if (!input || !output || input_size == 0U || output_size == 0U) {
        return false;
    }

    tables = (ea_lzw_tables *)xx_mem_alloc(sizeof(ea_lzw_tables));
    if (!tables) return false;
    xx_rt_memset(tables, 0, sizeof(*tables));
    for (index = 0U; index < 256U; ++index) {
        tables->suffix[index] = (uint8_t)index;
    }

    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.input = input;
    reader.input_size = input_size;

    while (!ended && !failed) {
        uint32_t raw = 0U;
        uint32_t code;

        /* The width is bumped BEFORE the fetch, and only while it is still
         * below the 12-bit ceiling.  At width 12 max_code becomes 4096
         * exactly -- one past the largest assignable code -- so the table
         * stops growing instead of widening to 13.  Deliberate: this is the
         * "no early change" behaviour the EA packer produces. */
        if (max_code < next_free) {
            ++width;
            max_code = (width == EA_LZW_MAX_BITS) ? EA_LZW_MAX_CODES
                                                  : ((1U << width) - 1U);
        }

        if (!ea_lzw_read(&reader, width, &raw)) {
            /* No whole code left: the packer pads the last byte, so this is
             * the natural end of a well-formed stream. */
            ended = true;
            break;
        }
        code = raw;

        if (code == EA_LZW_END) {
            ended = true;
            break;
        }

        if (code == EA_LZW_CLEAR) {
            uint32_t seed = 0U;
            width = 9U;
            max_code = 0x1ffU;
            next_free = EA_LZW_FIRST_CODE;
            /* No byte alignment after a CLEAR: the next code follows in the
             * very next bits. */
            if (!ea_lzw_read(&reader, width, &seed)) {
                /* Mid-token end of input. */
                failed = true;
                break;
            }
            if (seed == EA_LZW_END) {
                ended = true;
                break;
            }
            /* A CLEAR must be followed by a bare literal: that literal is what
             * seeds the previous-code register.  The reference reads an
             * uninitialised register otherwise, so refuse instead. */
            if (seed >= 0x100U) {
                failed = true;
                break;
            }
            if (output_at >= output_size) {
                failed = true;
                break;
            }
            previous = seed;
            first_char = seed;
            output[output_at++] = (uint8_t)seed;
            started = true;
            continue;
        }

        /* A stream that does not open with CLEAR has no defined previous
         * code. */
        if (!started) {
            failed = true;
            break;
        }

        {
            size_t stack_size = 0U;
            uint32_t current = code;

            if (code >= next_free) {
                /* KwKwK. */
                tables->stack[stack_size++] = (uint8_t)first_char;
                current = previous;
            }
            while (current > 0xffU) {
                if ((current >= EA_LZW_MAX_CODES) ||
                    (stack_size >= EA_LZW_MAX_CODES)) {
                    failed = true;
                    break;
                }
                tables->stack[stack_size++] = tables->suffix[current];
                current = tables->prefix[current];
            }
            if (failed) break;
            tables->stack[stack_size++] = (uint8_t)(current & 0xffU);
            first_char = current & 0xffU;

            if (stack_size > (output_size - output_at)) {
                /* Out of output capacity is a failure, never a truncation. */
                failed = true;
                break;
            }
            while (stack_size > 0U) {
                --stack_size;
                output[output_at++] = tables->stack[stack_size];
            }

            if (next_free < EA_LZW_MAX_CODES) {
                tables->prefix[next_free] = (uint16_t)previous;
                tables->suffix[next_free] = (uint8_t)first_char;
                ++next_free;
            }
            previous = code;
        }
    }

    xx_mem_free(tables);

    if (failed || !ended || !started) return false;

    if (written) *written = output_at;

    return true;
}

/* ------------------------------------------------------------------ */
/* EALIB method 1: Okumura LZSS                                        */
/* ------------------------------------------------------------------ */

#define EALIB_RING_SIZE 4096U
#define EALIB_RING_MASK (EALIB_RING_SIZE - 1U)
#define EALIB_LZSS_F 18U

bool xx_ea_lib_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written)
{
    /* The producer clears the WHOLE ring, so a reference into the pre-history
     * yields 0x00 bytes -- not 0x20 as the classic Okumura code would give.
     * Both variants emit the right number of bytes, so getting this wrong is
     * silent corruption rather than a failure. */
    uint8_t ring[EALIB_RING_SIZE];
    size_t in_at = 0U;
    size_t out_at = 0U;
    uint32_t ring_at = EALIB_RING_SIZE - EALIB_LZSS_F;
    uint32_t flags = 0U;

    if (written) *written = 0U;
    if (!input || !output || input_size == 0U || output_size == 0U) {
        return false;
    }

    xx_rt_memset(ring, 0, sizeof(ring));

    while (out_at < output_size) {
        flags >>= 1;
        if ((flags & 0x100U) == 0U) {
            if (in_at >= input_size) return false;
            flags = (uint32_t)input[in_at++] | 0xff00U;
        }

        if (flags & 1U) {
            uint8_t byte;
            if (in_at >= input_size) return false;
            byte = input[in_at++];
            output[out_at++] = byte;
            ring[ring_at] = byte;
            ring_at = (ring_at + 1U) & EALIB_RING_MASK;
        } else {
            uint32_t first, second, source, length, k;
            if ((in_at + 1U) >= input_size) return false;
            first = input[in_at];
            second = input[in_at + 1U];
            in_at += 2U;
            source = (first | ((second & 0xf0U) << 4)) & EALIB_RING_MASK;
            length = (second & 0x0fU) + 3U;
            for (k = 0U; k < length; ++k) {
                uint8_t byte;
                /* Deliberate, and matching the reference: the final match of a
                 * member may run past the declared size, and the surplus is
                 * simply dropped.  The declared plaintext is still produced in
                 * full -- this is the end of the stream, not a short decode. */
                if (out_at >= output_size) break;
                byte = ring[source];
                source = (source + 1U) & EALIB_RING_MASK;
                output[out_at++] = byte;
                ring[ring_at] = byte;
                ring_at = (ring_at + 1U) & EALIB_RING_MASK;
            }
        }
    }

    if (out_at != output_size) return false;

    if (written) *written = out_at;

    return true;
}

/* ------------------------------------------------------------------ */
/* EA RefPack (QFS)                                                    */
/* ------------------------------------------------------------------ */

#define REFPACK_MAGIC_LOW 0xfbU   /* second signature byte, constant */
#define REFPACK_FLAG_LARGE 0x01U  /* size fields are four bytes wide */
#define REFPACK_FLAG_PACKED 0x80U /* packed size precedes unpacked size */
#define REFPACK_FLAG_BASE 0x10U
/* Bits 0x02..0x40 are unassigned; requiring them to be clear is what keeps a
 * stray 0xFB byte in arbitrary data from being read as a header. */
#define REFPACK_FLAG_MASK 0x7eU

typedef struct refpack_header {
    size_t header_size;
    size_t unpacked_size;
} refpack_header;

typedef struct refpack_command {
    unsigned command_size;
    unsigned literals;
    unsigned copy;
    size_t distance;
    bool terminator;
} refpack_command;

static bool refpack_read_header(const uint8_t *input, size_t input_size,
                                refpack_header *header)
{
    size_t at = 2U;
    size_t field = 3U;
    size_t value = 0U;
    unsigned i;

    if (input_size < 2U) return false;
    if (input[1] != REFPACK_MAGIC_LOW) return false;
    if ((input[0] & REFPACK_FLAG_MASK) != REFPACK_FLAG_BASE) return false;

    if (input[0] & REFPACK_FLAG_LARGE) field = 4U;

    if (input[0] & REFPACK_FLAG_PACKED) {
        if ((input_size - at) < field) return false;
        /* The packed-size field is read and skipped; the grammar itself is
         * what delimits the stream. */
        at += field;
    }

    if ((input_size - at) < field) return false;
    for (i = 0U; i < field; ++i) {
        value = (value << 8) | (size_t)input[at];
        ++at;
    }

    /* A zero-length payload cannot be encoded (the shortest stream is still a
     * terminator command), so it is a false positive rather than an empty
     * file. */
    if (value == 0U) return false;

    header->header_size = at;
    header->unpacked_size = value;

    return true;
}

/* Decodes the command at input[at].  Fails when the command runs past the end
 * of the buffer: RefPack always ends on an explicit 0xFC..0xFF terminator, so
 * running out mid-command is a broken stream, never a normal end. */
static bool refpack_read_command(const uint8_t *input, size_t input_size,
                                 size_t at, refpack_command *command)
{
    uint8_t b0;

    if (at >= input_size) return false;
    b0 = input[at];

    command->command_size = 0U;
    command->literals = 0U;
    command->copy = 0U;
    command->distance = 0U;
    command->terminator = false;

    if (b0 < 0x80U) {
        uint8_t b1;
        if ((input_size - at) < 2U) return false;
        b1 = input[at + 1U];
        command->command_size = 2U;
        command->literals = (unsigned)(b0 & 0x03U);
        command->copy = (unsigned)((b0 & 0x1cU) >> 2) + 3U;
        command->distance = (size_t)(((uint32_t)(b0 & 0x60U) << 3) +
                                     (uint32_t)b1) + 1U;
    } else if (b0 < 0xc0U) {
        uint8_t b1, b2;
        if ((input_size - at) < 3U) return false;
        b1 = input[at + 1U];
        b2 = input[at + 2U];
        command->command_size = 3U;
        command->literals = (unsigned)((b1 >> 6) & 0x03U);
        command->copy = (unsigned)(b0 & 0x3fU) + 4U;
        command->distance = (size_t)(((uint32_t)(b1 & 0x3fU) << 8) +
                                     (uint32_t)b2) + 1U;
    } else if (b0 < 0xe0U) {
        uint8_t b1, b2, b3;
        if ((input_size - at) < 4U) return false;
        b1 = input[at + 1U];
        b2 = input[at + 2U];
        b3 = input[at + 3U];
        command->command_size = 4U;
        command->literals = (unsigned)(b0 & 0x03U);
        command->copy = (unsigned)(((uint32_t)(b0 & 0x0cU) << 6) +
                                   (uint32_t)b3) + 5U;
        command->distance = (size_t)(((uint32_t)(b0 & 0x10U) << 12) +
                                     ((uint32_t)b1 << 8) + (uint32_t)b2) + 1U;
    } else if (b0 < 0xfcU) {
        command->command_size = 1U;
        /* The literal-run count is always a multiple of four, so this command
         * can never express a tail of 1..3 bytes -- which is why the
         * terminator carries its own short literal count. */
        command->literals = (unsigned)((uint32_t)(b0 & 0x1fU) << 2) + 4U;
    } else {
        command->command_size = 1U;
        command->literals = (unsigned)(b0 & 0x03U);
        command->terminator = true;
    }

    return true;
}

/*
 * The command walk, shared by both RefPack entry points.
 *
 * With @p output non-NULL the bytes go there; with @p output NULL nothing is
 * kept and only the output length is counted, so the grammar can be used as a
 * cheap validity probe.  Nothing but the store differs between the two, so the
 * measure and the decode cannot disagree.
 *
 * No window is needed for the measuring pass: the only thing a copy has to
 * satisfy is that its distance does not reach in front of the output, and that
 * is a length comparison.  RefPack has no pre-filled window, which is exactly
 * what makes the grammar a usable probe.
 */
static bool refpack_run(const uint8_t *input, size_t input_size,
                        uint8_t *output, size_t limit, size_t *produced,
                        size_t *consumed)
{
    refpack_header header;
    refpack_command command;
    size_t at;
    size_t out_at = 0U;
    bool terminated = false;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;

    if (!input || input_size < 5U || limit == 0U) return false;
    if (!refpack_read_header(input, input_size, &header)) return false;
    if (header.unpacked_size > limit) return false;
    if (input_size <= header.header_size) return false;

    at = header.header_size;

    while (at < input_size) {
        if (!refpack_read_command(input, input_size, at, &command)) {
            return false;
        }
        at += command.command_size;

        if ((input_size - at) < (size_t)command.literals) return false;
        if (((size_t)command.literals + (size_t)command.copy) >
            (header.unpacked_size - out_at)) {
            return false;
        }

        if (command.literals > 0U) {
            if (output) {
                xx_rt_memcpy(output + out_at, input + at, command.literals);
            }
            at += command.literals;
            out_at += command.literals;
        }

        if (command.copy > 0U) {
            /* No pre-filled window: a back reference in front of the output is
             * a malformed stream, not something to clamp. */
            if ((command.distance == 0U) || (command.distance > out_at)) {
                return false;
            }
            if (output) {
                unsigned i;
                /* Byte by byte: overlapping copies (distance < length) are how
                 * the encoder expresses runs, so a block move would be wrong
                 * here. */
                for (i = 0U; i < command.copy; ++i) {
                    output[out_at] = output[out_at - command.distance];
                    ++out_at;
                }
            } else {
                out_at += command.copy;
            }
        }

        if (command.terminator) {
            terminated = true;
            break;
        }
    }

    if (!terminated) return false;
    if (out_at != header.unpacked_size) return false;

    if (produced) *produced = out_at;
    if (consumed) *consumed = at;

    return true;
}

bool xx_ea_refpack_decode_memory(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size,
                                 size_t *written)
{
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!refpack_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (written) *written = produced;

    return true;
}

bool xx_ea_refpack_scan_memory(const uint8_t *input, size_t input_size,
                               size_t max_output, size_t *consumed,
                               size_t *produced)
{
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;

    return refpack_run(input, input_size, NULL, max_output, produced,
                       consumed);
}

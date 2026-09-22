/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Amiga MI10 backward LZ decoder.  Ported one-for-one from the XArchive
 * reference decoder (XArchive/Algos/xmi10decoder.cpp).  Everything the
 * reference does inside its decode loop -- token shapes, the short-form
 * distance that is NOT biased by one, the "distance must stay inside the
 * already-produced tail" test applied after each output step, and the demand
 * that the input cursor end exactly on offset 2 -- is reproduced here.  The
 * container-level parts of the reference (device I/O, memory reservations and
 * the optional "MI10 byte sum" checksum, which lives in the archive record and
 * not in the stream) belong to the caller and are not part of this codec.
 */
#include "xxfclib/algo/mi10/xx_mi10.h"

/* Bytes 0 and 1 are the fixed zero marker and the per-block escape byte, so
 * offset 2 is the floor of the backward token cursor. */
#define MI10_FLOOR ((size_t)2)

/* Steps the backward cursor down one byte.  Returns -1 when the cursor would
 * move below the two header bytes, exactly like the reference helper. */
static int32_t mi10_read_backward(const uint8_t *input, size_t input_size,
                                  size_t *position)
{
    if ((*position <= MI10_FLOOR) || (*position > input_size)) return -1;
    --(*position);
    return (int32_t)input[*position];
}

bool xx_mi10_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    size_t input_position;
    size_t output_position;
    uint8_t escape;

    if (written) *written = 0;
    if (!input || !output) return false;
    if (input_size < 3) return false;
    if (output_size == 0) return false;
    if (input[0] != 0) return false;

    escape = input[1];
    input_position = input_size;
    output_position = output_size;

    while (output_position > 0) {
        int32_t value;
        int32_t control;
        size_t distance;
        size_t length;
        size_t i;

        value = mi10_read_backward(input, input_size, &input_position);
        if (value < 0) return false;
        if (value != (int32_t)escape) {
            --output_position;
            output[output_position] = (uint8_t)value;
            continue;
        }

        control = mi10_read_backward(input, input_size, &input_position);
        if (control < 0) return false;
        if (control == 0) {
            /* ESC,00 represents one literal occurrence of the escape byte. */
            --output_position;
            output[output_position] = escape;
            continue;
        }

        distance = (size_t)control;
        length = 3;
        if (control >= 0x80) {
            int32_t length_distance =
                mi10_read_backward(input, input_size, &input_position);
            if (length_distance < 0) return false;
            /* Deliberate: only the long form biases the distance by one. */
            distance = (size_t)((((uint32_t)control & 0x7fu) << 4) |
                                ((uint32_t)length_distance & 0x0fu)) + 1;
            if (length_distance < 0x80) {
                length = (size_t)(length_distance >> 4) + 4;
            } else {
                int32_t length_low =
                    mi10_read_backward(input, input_size, &input_position);
                if (length_low < 0) return false;
                length = (size_t)((((uint32_t)length_distance & 0x70u) << 4) |
                                  (uint32_t)length_low) + 12;
            }
        }

        if ((length == 0) || (length > output_position)) return false;
        /* distance is always >= 1 here: the short form rejected control == 0
         * above and the long form adds one, but the reference tests it and so
         * does this port. */
        if (distance == 0) return false;

        for (i = 0; i < length; ++i) {
            --output_position;
            /* The source must lie inside the bytes already produced; a
             * reference past the end of the plaintext is malformed. */
            if (distance >= (output_size - output_position)) return false;
            output[output_position] = output[output_position + distance];
        }
    }

    /* The reference requires the token stream to be consumed to the byte. */
    if (input_position != MI10_FLOOR) return false;

    if (written) *written = output_size;
    return true;
}

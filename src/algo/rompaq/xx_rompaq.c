/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive/Algos/xrompaqdecoder.cpp (XRomPaqDecoder::decode).
 * The reference holds no codec maths of its own: it walks the bank headers
 * and hands each bank's payload to XDclDecoder, so this port walks the bank
 * headers and hands each bank's payload to xx_dcl.
 *
 * DEVIATION, provably output-equivalent: a bank's plaintext length is stored
 * nowhere, and the reference discovers it by decoding into a growable
 * QByteArray.  A caller buffer cannot grow, so this port runs the same bank
 * twice through xx_dcl's single core routine - xx_dcl_scan_memory with the
 * remaining budget (the same value the reference passes as maxOutputSize),
 * then xx_dcl_decode_memory for exactly the length that scan reported.  The
 * measure and the decode are the same code path, so they cannot disagree.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/rompaq/xx_rompaq.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#define ROMPAQ_HEADER_SIZE 0x48U
#define ROMPAQ_OFFSET_VERSION 0x0aU
#define ROMPAQ_OFFSET_METHOD 0x3cU
#define ROMPAQ_OFFSET_PARTSIZE 0x3fU

#define ROMPAQ_VERSION_100 0x0100U
#define ROMPAQ_VERSION_101 0x0101U

#define ROMPAQ_METHOD_STORED 1U
#define ROMPAQ_METHOD_IMPLODE 2U

/* The reference stores the image size in a qint32 and refuses anything wider,
 * so a 64-bit caller cannot ask for more than this either. */
#define ROMPAQ_MAX_IMAGE_SIZE 0x7fffffffU

static uint16_t rompaq_read16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint32_t rompaq_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

bool xx_rompaq_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written) {
    size_t offset = 0U;
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!input || !output) return false;
    if ((output_size == 0U) || (output_size > ROMPAQ_MAX_IMAGE_SIZE)) {
        return false;
    }
    if (input_size < ROMPAQ_HEADER_SIZE) return false;

    while (offset < input_size) {
        uint32_t part_raw;
        size_t part_size;
        size_t payload;
        unsigned version;
        unsigned method;

        if (offset > input_size - ROMPAQ_HEADER_SIZE) return false;

        version = rompaq_read16(input + offset + ROMPAQ_OFFSET_VERSION);
        if ((version != ROMPAQ_VERSION_100) &&
            (version != ROMPAQ_VERSION_101)) {
            return false;
        }

        method = input[offset + ROMPAQ_OFFSET_METHOD];
        part_raw = rompaq_read32(input + offset + ROMPAQ_OFFSET_PARTSIZE);
        /* The reference keeps the part size in a qint32 and rejects a
         * negative one in both method branches (`nPartSize <= 0` for implode,
         * `nPartSize < 0` for stored), so hoisting the sign test here only
         * changes which `false` is returned, never whether one is. */
        if (part_raw > ROMPAQ_MAX_IMAGE_SIZE) return false;
        part_size = (size_t)part_raw;

        payload = offset + ROMPAQ_HEADER_SIZE;

        if (method == ROMPAQ_METHOD_IMPLODE) {
            size_t remaining;
            size_t bank_out = 0U;
            /* A bank whose stream begins with a zero word carries a two-byte
             * pad before its PKWARE DCL header; a non-zero word IS the DCL
             * header (00 04/05/06) and must be kept.  Deliberate: do not
             * "fix" this into an unconditional skip. */
            if (payload > input_size - 2U) return false;
            if (rompaq_read16(input + payload) == 0U) payload += 2U;
            if ((part_size == 0U) || (part_size > input_size - payload)) {
                return false;
            }
            remaining = output_size - produced;
            if (remaining == 0U) return false;
            if (!xx_dcl_scan_memory(input + payload, part_size, remaining,
                                    NULL, &bank_out)) {
                return false;
            }
            /* Matching the reference: XDclDecoder refuses a stream that
             * produces nothing (`output.size < 1`). */
            if (bank_out == 0U) return false;
            {
                size_t bank_written = 0U;
                if (!xx_dcl_decode_memory(input + payload, part_size,
                                          output + produced, bank_out,
                                          &bank_written) ||
                    (bank_written != bank_out)) {
                    return false;
                }
            }
            produced += bank_out;
        } else if (method == ROMPAQ_METHOD_STORED) {
            if ((part_size > input_size - payload) ||
                (part_size > output_size - produced)) {
                return false;
            }
            if (part_size > 0U) {
                xx_rt_memcpy(output + produced, input + payload, part_size);
                produced += part_size;
            }
        } else {
            return false;
        }

        /* A stored bank may declare a zero part size; the walk still advances
         * by the bank header, so the loop always makes progress. */
        offset = payload + part_size;
    }

    if ((offset != input_size) || (produced != output_size)) return false;

    if (written) *written = produced;
    return true;
}

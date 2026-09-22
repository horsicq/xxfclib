/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Prefix-then-copy, ported from the HANDLE_METHOD_SCL_SECTORS arm of
 * XDecompress::decompress (core/xdecompress.cpp):
 *
 *     QByteArray baOut = baProperty;
 *     baOut.append(packed);
 *     *pbResult = (baOut.size() == nUncompressedSize);
 *
 * The size equality is the whole of the validation, so it is reproduced here
 * as "both halves must fit, and *written reports exactly their sum" - the
 * caller compares that against its declared uncompressed size.
 */

#include "xxfclib/algo/sclsectors/xx_sclsectors.h"
#include "xxfclib/rt/xx_rt.h"

bool xx_sclsectors_decode_memory_ex(const uint8_t *prefix, size_t prefix_size, const uint8_t *input, size_t input_size, uint8_t *output,
                                    size_t output_size, size_t *written)
{
    size_t total = 0;

    if (written) *written = 0;

    if (!prefix && (prefix_size > 0)) return false;
    if (!input && (input_size > 0)) return false;

    /* Guard the addition before it can wrap on a 32-bit size_t. */
    if (prefix_size > (((size_t)0 - (size_t)1) - input_size)) return false;
    total = prefix_size + input_size;

    if (total > output_size) return false;
    if ((total > 0) && !output) return false;

    if (prefix_size > 0) xx_rt_memcpy(output, prefix, prefix_size);
    if (input_size > 0) xx_rt_memcpy(output + prefix_size, input, input_size);

    if (written) *written = total;

    return true;
}

bool xx_sclsectors_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written)
{
    return xx_sclsectors_decode_memory_ex(NULL, 0, input, input_size, output, output_size, written);
}

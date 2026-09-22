/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Pocket Soft RTPatch codecs.
 *
 * Codec design and reference implementation:
 * Copyright (c) 2026 Sandy Carter, https://github.com/bwrsandman/rtptool
 * (MIT License).  Ported from the bounded C++ adaptation in
 * XArchive/Algos/xrtpatchdecoder.cpp.
 */
#ifndef XXFCLIB_ALGO_RTPATCH_H
#define XXFCLIB_ALGO_RTPATCH_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an RTPatch adaptive-Huffman/LZSS stream.
 *
 * The stream opens with a bit-packed header (magic 0xb59c, a raw-literal
 * flag, a reserved 0xff byte, two 12-bit rescale periods and a 4-bit window
 * selector) and then alternates single flag bits: 0 introduces a literal, 1
 * introduces a match.  Both RTPatch containers in this project (the .rta
 * archive and the .rtp patch package) store the decoded length in their
 * record headers, so no measuring entry point is provided: @p output_size is
 * that stored length and the stream must decode to exactly that many bytes.
 *
 * @param input       Compressed bytes, header included.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size The container's stored decoded length.
 * @param written     Receives the byte count produced; 0 on every failure.
 * @return true only when exactly @p output_size bytes were decoded.
 */
XXFC_API bool xx_rtpatch_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

/**
 * @brief Expand an RTPatch banner/comment block.
 *
 * Not a compressor: the block is a 16-bit little-endian line count followed
 * by that many counted, NUL-terminated strings.  Each line is emitted without
 * its NUL and followed by CR LF.  The block must end exactly at
 * @p input_size and produce exactly @p output_size bytes, which the container
 * computes from the same string table while scanning.
 *
 * @param input       The counted-string block, count word included.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size The container's computed decoded length.
 * @param written     Receives the byte count produced; 0 on every failure.
 * @return true only when the block was consumed whole.
 */
XXFC_API bool xx_rtpatch_text_decode_memory(const uint8_t *input,
                                            size_t input_size,
                                            uint8_t *output,
                                            size_t output_size,
                                            size_t *written);

#ifdef __cplusplus
}
#endif
#endif

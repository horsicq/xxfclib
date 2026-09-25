/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Chunk decoders for the Windows Imaging Format: XPRESS (Huffman variant),
 * LZX (WIM variant) and LZMS.  Private to the wim reader.
 *
 * Every WIM chunk is an independent stream: nothing carries over from one
 * chunk to the next, so a decoder is reset per call and only its workspace is
 * reused.  A call either fills exactly out_size bytes or fails.
 */
#ifndef XXFCLIB_FORMAT_WIM_CODEC_H
#define XXFCLIB_FORMAT_WIM_CODEC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The numbering is the one a solid resource header stores. */
#define XX_WIM_CODEC_NONE 0U
#define XX_WIM_CODEC_XPRESS 1U
#define XX_WIM_CODEC_LZX 2U
#define XX_WIM_CODEC_LZMS 3U

/* Largest chunk any decoder accepts.  LZX windows stop at 2^21; LZMS solid
 * chunks are 2^26 in every image Microsoft's tools write. */
#define XX_WIM_CODEC_MAX_CHUNK (1U << 27)

typedef struct xx_wim_codec xx_wim_codec;

xx_wim_codec *xx_wim_codec_create(unsigned method);
void xx_wim_codec_free(xx_wim_codec *codec);

/* Decode one chunk.  For LZX, window_size is the resource chunk size (a power
 * of two from 2^15 to 2^21); the other codecs ignore it. */
bool xx_wim_codec_decode(xx_wim_codec *codec, const uint8_t *in,
                         size_t in_size, uint8_t *out, size_t out_size,
                         uint32_t window_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WIM_CODEC_H */

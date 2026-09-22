/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM GTU member body.  Ported one-for-one from the reference arm
 * XBinary::HANDLE_METHOD_GTU in XArchive/core/xdecompress.cpp.
 *
 * GTU introduces no codec of its own: every frame is one complete Okumura
 * LZARI stream, the same codec AMPK method 1 carries, so this module is the
 * frame walk on top of xx_ampk_lzari_decode_memory() exactly as the reference
 * sits on top of XAMPKDecoder::decodeLZARI().
 *
 * The frame chain is
 *
 *     [i32 rawSize][i32 packedSize][packedSize bytes of LZARI]
 *
 * repeated until the declared plaintext size has been produced.  The format
 * reader (XGTU) publishes the member starting at its first output-producing
 * frame, so the walk begins at offset 0 of `input`; frames belonging to a
 * previous volume were already stepped over by the reader, never by this
 * decoder.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/gtu/xx_gtu.h"
#include "xxfclib/algo/ampk/xx_ampk.h"

#define GTU_FRAME_PRELUDE 8U

/* Little-endian i32, read byte by byte. */
static int32_t gtu_read_i32(const uint8_t *p)
{
    uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    /* Reference reads a quint32 and casts to qint32; reproduce that without
     * relying on implementation-defined conversion of an out-of-range value. */
    if (value >= UINT32_C(0x80000000))
        return -(int32_t)(UINT32_C(0xffffffff) - value) - 1;
    return (int32_t)value;
}

bool xx_gtu_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written)
{
    size_t frame_offset = 0U;
    size_t left = output_size;
    size_t produced = 0U;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (output_size == 0U) return true;

    while (left > 0U) {
        int32_t raw_size;
        int32_t packed_size;
        size_t frame_raw;
        size_t frame_packed;
        size_t frame_written = 0U;

        /* Reference: `if (nGtuFrameOffset > packed.size() - 8) fail` in signed
         * 64-bit arithmetic, so a member shorter than a prelude fails too. */
        if ((input_size < GTU_FRAME_PRELUDE) ||
            (frame_offset > input_size - GTU_FRAME_PRELUDE))
            return false;

        raw_size = gtu_read_i32(input + frame_offset);
        packed_size = gtu_read_i32(input + frame_offset + 4U);
        frame_offset += GTU_FRAME_PRELUDE;

        if (raw_size <= 0 || packed_size <= 0) return false;
        frame_raw = (size_t)raw_size;
        frame_packed = (size_t)packed_size;
        if (frame_raw > left) return false;
        if (frame_packed > input_size - frame_offset) return false;

        /* One complete LZARI stream per frame: the model, the ring buffer and
         * the arithmetic interval all restart at every frame.  The AMPK entry
         * point returns true only when it produced exactly frame_raw bytes,
         * which is the reference's `baGtuFrame.size() != nGtuRawSize` check. */
        if (!xx_ampk_lzari_decode_memory(input + frame_offset, frame_packed,
                                         output + produced, frame_raw,
                                         &frame_written))
            return false;
        if (frame_written != frame_raw) return false;

        produced += frame_raw;
        frame_offset += frame_packed;
        left -= frame_raw;
    }

    if (produced != output_size) return false;
    if (written) *written = produced;
    return true;
}

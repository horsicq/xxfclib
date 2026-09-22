/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_TPS_H
#define XXFCLIB_ALGO_TPS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TPS (Clarion/TopSpeed database) member codec, ported from
 * XArchive/Algos/xtpsdecoder.cpp.
 *
 * THE FRAMING IS NOT PART OF THE COMPRESSED STREAM.  Every payload byte on
 * disk is stored XOR 0x80, and the payload is cut into 0x4000-byte blocks,
 * each followed by ONE PLAIN check byte - a mod-256 additive checksum of the
 * de-XORed block, not a flag.  Reading it as a flag, or feeding it to the
 * codec, desynchronises the Huffman tree a couple of hundred bytes in and
 * yields plausible garbage rather than an error.
 *
 * The block counter starts at 4, charged for the uncompressed-size dword that
 * sits at directory offset +0x12, so block 0 carries only 0x3ffc data bytes
 * while every later block carries 0x4000.  Those four size bytes are also the
 * first four bytes fed to block 0's checksum, which is why the decoded length
 * has to be known before the framing can be verified.  After the last block's
 * check byte comes one plain 0x01 end-of-member marker.
 *
 * Underneath the framing is plain Yoshizaki LZHUF in the parameter set that
 * xx_lzhuf_decode_memory() implements - distance variant 1, F = 0x3C,
 * THRESHOLD = 2 (N_CHAR 314), MAX_FREQ 0x8000 with the textbook halve-and-
 * rebuild, an 0x2000-byte ring preset to 0x20, no end symbol - i.e. LHA
 * "-lh1-" with a 4 KiB match window.  This module is the framing only and
 * delegates the codec, exactly as the reference delegates to XLZHUFDecoder.
 *
 * NOTE: despite the shared Clarion/TopSpeed lineage this is NOT the TopSpeed
 * installer codec (xx_topspeed_decode_memory), which is blocked 12-bit LZW.
 * The two share no algorithm and no code.
 */

/**
 * @brief Decode a complete TPS member.
 *
 * The container stores the decoded length in its directory, so there is no
 * measuring entry point - and there could not usefully be one, because the
 * length is an INPUT to the framing check (it seeds block 0's checksum).
 *
 * @param input       The member payload exactly as stored, including the
 *                    check bytes and the trailing 0x01 marker.
 * @param input_size  Length of @p input; the whole of it must be consumed.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size, from the directory entry.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when the framing verified and exactly @p output_size
 *         bytes were produced.
 */
XXFC_API bool xx_tps_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif

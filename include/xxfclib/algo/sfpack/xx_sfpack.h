/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_SFPACK_H
#define XXFCLIB_ALGO_SFPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rebuild the .sf2 held in an SFPack (.sfpack) file.
 *
 * SFPACK IS NOT AN ARCHIVE.  It does not hold files: it holds the pieces of
 * ONE SoundFont 2 (RIFF/sfbk) and the extractor REBUILDS that single file.
 * The reassembly is part of the codec, not a courtesy: the sample data is laid
 * out afresh, so the dwStart / dwEnd / dwStartloop / dwEndloop fields of every
 * shdr record in the decompressed pdta have to be rewritten and 46 zero bytes
 * are appended after every sample.  Emitting the pieces separately would
 * produce something no synthesiser can load.
 *
 * @p input is the WHOLE .sfpack file, not a sub-stream: the sample streams are
 * reached through a table of absolute file offsets.
 *
 * Container
 *   +0x00  4    "SFPK"
 *   +0x04  u16  version 0x0100
 *   +0x06  u16  flags.  Bit 2 means ENCRYPTED and is refused; bits 0/1
 *                announce an embedded .txt/.lic blob in the skipped region.
 *   +0x08  i32  the size of the .sf2 that will be produced.  DO NOT TRUST IT:
 *                most corpus files declare a few hundred bytes more than what
 *                is actually written, which is why xx_sfpack_scan_memory()
 *                exists.
 *   +0x0c  u32  zero
 *   +0x10       chunk("INFO"), chunk("pdta"), i32 skipLen + skipLen bytes,
 *               i32 tableBytes, tableBytes/4 * i32 absolute sample offsets.
 *
 * Two codecs are involved.  Codec 1 (the INFO and pdta chunks) is textbook LZW
 * with NO control codes at all, MSB-first bits out of a one-byte refill, a
 * width step one code EARLY and a full table reset at code 0xfff.  Codec 2
 * (the samples) is a lossless predictive audio coder whose bit reader refills
 * from 32-bit LITTLE-ENDIAN words and hands out bits from bit 31 down.
 *
 * @param input       The whole .sfpack file.
 * @param input_size  Its length.
 * @param output      Destination buffer for the rebuilt .sf2.
 * @param output_size Capacity; the rebuild must fit or the call fails.
 * @param written     Receives the rebuilt .sf2 size (0 on failure).
 * @return true only on a complete rebuild.
 */
XXFC_API bool xx_sfpack_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Measure the .sf2 that xx_sfpack_decode_memory() would produce.
 *
 * The size stored at +0x08 is unreliable (see above), so a reader cannot
 * allocate from it.  This decompresses only the pdta chunk -- the sample
 * streams are not touched -- and returns the exact output size, exactly as the
 * reference reader's measure() step does.  Because the samples are not walked,
 * a successful measure does not promise a successful decode.
 *
 * @param input       The whole .sfpack file.
 * @param input_size  Its length.
 * @param max_output  Refuse a file that would rebuild to more than this.
 *                    0 means "no caller ceiling" (an internal 1 GiB cap still
 *                    applies, as in the reference).
 * @param consumed    Receives the input bytes the container occupies, which is
 *                    always @p input_size: the sample table holds absolute
 *                    file offsets, so the codec owns the whole file and no
 *                    trailing member can be distinguished.  May be NULL.
 * @param produced    Receives the rebuilt .sf2 size. May be NULL.
 * @return true when the header and the pdta chunk describe a rebuildable file.
 */
XXFC_API bool xx_sfpack_scan_memory(const uint8_t *input, size_t input_size,
                                    size_t max_output, size_t *consumed,
                                    size_t *produced);

#ifdef __cplusplus
}
#endif
#endif

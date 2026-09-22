/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZW15V_H
#define XXFCLIB_ALGO_LZW15V_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "LZW15V" - Mark Nelson's variable-width LZW (9..15 bits) as it appears as a
 * COMPLETELY HEADERLESS stream in DOS-era truncated-extension install files
 * (.EX_, .DL_, .WO_, .HL_, .DA_, .SY_) and as the member payload of GLU
 * containers.  There is no magic, no size field and no checksum: the stream
 * starts with the first 9-bit code.
 *
 * BIT ORDER IS MSB-FIRST.  A byte is consumed whole and drained from bit 7
 * downwards; a code's first-read bit is its most significant one.
 *
 * Control codes (this is where the dialect differs from Unix compress / GIF):
 *
 *   0x100  END    stop; the only end-of-stream signal
 *   0x101  BUMP   widen the code width by one, capped at 15 bits
 *   0x102  CLEAR  reset: next assignable code back to 0x103, width back to 9,
 *                 and the code that FOLLOWS is a fresh 9-bit literal
 *
 * The width changes ONLY on an explicit BUMP - a decoder that widens when the
 * next code reaches (1 << width), as compress and GIF do, desynchronises
 * immediately here.  First assignable code is 0x103, the table stops growing
 * at 0x8000.
 */

#define XX_LZW15V_CODE_END 0x100U
#define XX_LZW15V_CODE_BUMP 0x101U
#define XX_LZW15V_CODE_CLEAR 0x102U
#define XX_LZW15V_FIRST_CODE 0x103U
#define XX_LZW15V_MAX_CODES 0x8000U
#define XX_LZW15V_MIN_CODE_BITS 9
#define XX_LZW15V_MAX_CODE_BITS 15

/**
 * @brief Decode a whole LZW15V stream into @p output.
 *
 * Faithful to the reference decoder: no grammar checks beyond memory safety.
 * Succeeds only when exactly @p output_size bytes come out, which is also the
 * containers' own success test.
 *
 * @param input       Compressed bytes (the stream starts at input[0]).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer of @p output_size bytes.
 * @param output_size Expected plaintext size; both a cap and a requirement.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode that filled @p output exactly.
 */
XXFC_API bool xx_lzw15v_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Measure an LZW15V stream, which stores no plaintext length.
 *
 * Neither container using this codec records a decoded size, so a reader
 * cannot allocate without this.  It runs the SAME core routine with no output
 * buffer and adds the strict grammar that headerless detection needs: the
 * opening code of every segment is a literal (< 0x100), BUMP never fires past
 * 15 bits, a used code is never more than one past the next assignable one,
 * the next assignable code never outruns the current width, and the stream
 * ends on an explicit END.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input (may extend past the stream, as in a
 *                    GLU container where members are concatenated).
 * @param max_output  Refuse a stream decoding to more than this.
 * @param consumed    Receives the input bytes the stream occupies - i.e. where
 *                    the next member begins.  May be NULL.
 * @param produced    Receives the decoded size.  May be NULL.
 * @return true when the stream reached its END code within the limit.
 */
XXFC_API bool xx_lzw15v_scan_memory(const uint8_t *input, size_t input_size,
                                    size_t max_output, size_t *consumed,
                                    size_t *produced);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_LZW15V_H */

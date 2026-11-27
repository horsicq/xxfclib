/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZX_GATED_H
#define XXFCLIB_ALGO_LZX_GATED_H

/* Library-internal variant of xx_lzx_cab_decode for callers that load the input and consume the output while the
   decoder runs on another thread (the CAB reader). Not part of the public API. */

#include "xxfclib/algo/lzx/xx_lzx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the decoder, on the decoding thread, before it touches CFDATA block `entry` (0 .. block_count - 1, in
   order, once each).
   - `settled_bytes`: the decoder will neither read nor write output[0 .. settled_bytes) any more. It never
     decreases and trails the decoder by one window (by one more frame when the decoder translates E8 calls itself).
   - Return true once blocks[entry], block_sizes[entry] and plain_sizes[entry] are valid; they and the block's bytes
     must then stay unchanged until the decoder returns. Entries above `entry` are not read before their own call, so
     another thread may still be filling them. The decoder writes output only from the start of block `entry` to
     the end of that block plus 32 bytes (never past output_size).
   - Return false to abort: the decoder returns false. */
typedef bool (*xx_lzx_cab_gate_fn)(void *context, size_t entry, size_t settled_bytes);

/* As xx_lzx_cab_decode when gate and translation_size are NULL.
   With a gate, the per-block arguments are validated block by block after the gate returned, instead of all before
   decoding; the result (success or failure, output bytes) is the same.
   With translation_size, the decoder leaves the E8 call translation to the caller: it stores the stream's
   translation size (0: none) there before it calls the gate for block 1 and does not store to it again. The output
   is the same as xx_lzx_cab_decode's once the caller has run xx_lzx_cab_translate over all of it. */
bool xx_lzx_cab_decode_gated(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, unsigned window_bits,
                             uint8_t *output, size_t output_size, size_t *written, xx_lzx_cab_gate_fn gate, void *context, int32_t *translation_size);

/* Internal status variant. Sets *out_of_memory only for decoder-state allocation
   failure. It belongs to the decoding thread until that thread is joined. */
bool xx_lzx_cab_decode_gated_status(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, unsigned window_bits,
                                    uint8_t *output, size_t output_size, size_t *written, xx_lzx_cab_gate_fn gate, void *context, int32_t *translation_size,
                                    bool *out_of_memory);

/* E8 call translation of output[from .. to) of a stream decoded with translation_size. `from` must be a multiple of
   32768, `to` a multiple of 32768 or the output size; every byte must be translated exactly once, and only when the
   decoder is done with it (settled_bytes, or after the decoder returned). Does nothing when translation_size is 0. */
void xx_lzx_cab_translate(uint8_t *output, size_t from, size_t to, int32_t translation_size);

#ifdef __cplusplus
}
#endif
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sqx.h
 * @brief SQX (SQX-Software / Michael Bierhoff) member codec, methods 1..4.
 *
 * Facts that are easy to get wrong:
 *
 *   - the dictionary is 1 << (((flags >> 8) & 0xF) + 15), capped at 4 MiB;
 *   - bits are MSB-first INSIDE LITTLE-ENDIAN 32-BIT WORDS -- a plain
 *     MSB-first byte reader desyncs immediately;
 *   - there are THREE distance tables (48, 50 and 52 usable symbols) and the
 *     one in force is chosen by the dictionary size, not by the stream;
 *   - a block header opens with one bit: 0 selects the Huffman coder
 *     implemented here, 1 selects a second "direct" coder that is not
 *     implemented.  Methods 5 and up, and the "b0" preprocessor filter, are
 *     not implemented either; all of those fail instead of emitting
 *     plausible garbage.
 *
 * SOLIDITY.  Flag 0x04 means a member continues the previous member's LZ
 * state: the 4 MiB window, the write cursor, the four recent distances and
 * the last match are all carried over.  Solidity therefore cannot be
 * expressed by a plain one-buffer-in/one-buffer-out call: the caller hands
 * over the whole archive image plus the table of coded members that precede
 * (and include) the wanted one, and the decoder replays them to rebuild the
 * window.  Stored members are absent from the table because they never touch
 * the LZ state.
 *
 * This is why xx_sqx_decode_memory carries two extra parameters beyond the
 * usual (input, input_size, output, output_size, written) shape.  A
 * signature without them could not tell which member is wanted, nor which
 * dictionary size the stream was coded with, and would have to guess -- and
 * a wrong dictionary decodes to plausible garbage rather than to an error.
 *
 * The containers that use this codec DO store the plaintext length (it is in
 * the SQX member header), so no measuring entry point is provided.
 */

#ifndef XX_SQX_H
#define XX_SQX_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Largest member this decoder will produce (1 GiB), as in the reference. */
#define XX_SQX_MAX_UNCOMPRESSED_SIZE 0x40000000

/** One coded member of an SQX archive, in archive order. */
typedef struct xx_sqx_member {
    uint64_t data_offset;    /**< offset of the packed bytes inside `input` */
    uint64_t packed_size;    /**< packed byte count */
    uint64_t unpacked_size;  /**< decoded byte count (from the member header) */
    uint16_t flags;          /**< member flags; bit 2 = solid, bits 8..11 = dict */
    uint8_t filter;          /**< "b0" preprocessor selector; only 0 is supported */
    uint8_t method;          /**< method byte; only 1..4 are supported */
} xx_sqx_member;

/**
 * @brief Decode one member of an SQX archive.
 *
 * Members [0 .. target_index] are replayed in order so that the LZ window is
 * correct for a solid target; only the target's bytes are materialised.  A
 * member before the target that fails to decode is not fatal -- the
 * reference keeps the state as the failure left it and carries on -- but the
 * target itself must decode completely.
 *
 * @param input         The archive image the member offsets refer to.
 * @param input_size    Length of @p input.
 * @param members       Table of coded members, archive order.
 * @param member_count  Number of entries in @p members.
 * @param target_index  Index into @p members of the wanted member.
 * @param output        Receives the target member's plaintext.
 * @param output_size   Capacity of @p output; must be at least the target's
 *                      unpacked_size.
 * @param written       Receives the byte count produced; set on every path.
 * @return true only when the target member decoded completely.
 */
XXFC_API bool xx_sqx_decode_memory(const uint8_t *input, size_t input_size,
                                   const xx_sqx_member *members,
                                   size_t member_count, size_t target_index,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XX_SQX_H */

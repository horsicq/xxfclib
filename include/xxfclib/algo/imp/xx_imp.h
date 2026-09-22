/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_imp.h
 * @brief IMP (Technelysium "IMP\n") member codecs.
 *
 * This is NOT the ZIP Implode method (that one lives in
 * xxfclib/algo/implode) and it is not the PKWARE DCL explode format
 * (xxfclib/algo/dcl) either.  IMP is the container written by the
 * Technelysium IMP archiver, and its coder is a BLOCK DRIVER over one SOLID
 * stream: each block opens with a 4-bit method, a 1-bit last flag, a 20-bit
 * unpacked size and a 20-bit packed size, all LSB-first, and the next block
 * starts `packed` bytes further on.  The methods are
 *
 *   0  stored (the bytes start at the reader's next unconsumed byte)
 *   1  a Deflate relative: LZ77 with LZX-style repeated offsets (distance
 *      symbol 0 reuses r0, symbol 1 swaps in r1) and canonical Huffman
 *      tables whose code lengths are delta coded against the previous table
 *      row, plus a run/zero encoding with doubling multipliers.  Symbol
 *      0x11F is not a match at all: it records a delta post-filter span,
 *      applied after the block and un-applied over whatever window the next
 *      block keeps.
 *   2  bzip2-shaped: BWT + MTF + RLE with per-group Huffman tables and a
 *      unary-coded, move-to-front selector.
 *   3  the same coder as 1.
 *
 * Because the stream is solid, a member is a slice of the decoded stream at
 * a byte offset, and the x86 branch converter that may have to be undone is
 * selected per member.  That is why xx_imp_decode_memory carries two extra
 * parameters beyond the usual (input, input_size, output, output_size,
 * written) shape: without them the call could not say which member is
 * wanted.
 *
 * The container DOES store each member's plaintext length (it is in the
 * directory record), so no measuring entry point is provided.
 *
 * The directory is itself compressed with the same LZ77 coder, so the reader
 * needs xx_imp_decode_directory to parse the archive at all.
 */

#ifndef XX_IMP_H
#define XX_IMP_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Largest member this decoder will produce (1 GiB), as in the reference. */
#define XX_IMP_MAX_UNCOMPRESSED_SIZE 0x40000000

/** Size of one directory record. */
#define XX_IMP_DIRECTORY_RECORD_SIZE 0x26

/**
 * @brief Decode one member out of an IMP solid stream.
 *
 * @param input         The member stream, starting at its own "IMPLH\0"
 *                      signature; the first block header sits at +6.
 * @param input_size    Length of @p input.
 * @param stream_offset The member's offset inside the DECODED solid stream.
 *                      An 11-byte in-stream header (u32 size, u16 name
 *                      length, u16 version, u16 crc16, u8 attributes) plus
 *                      that many name bytes sit there; the payload follows.
 * @param attributes    The member attribute byte.  Bits 1..2 select the x86
 *                      branch converter: 2 -> 16 bit, 4 -> 32 bit.
 * @param output        Receives the member's payload.
 * @param output_size   Exactly the member's decoded size, as published in
 *                      the directory record.
 * @param written       Receives the byte count produced; set on every path.
 * @return true only when the whole member decoded.
 */
XXFC_API bool xx_imp_decode_memory(const uint8_t *input, size_t input_size,
                                   uint64_t stream_offset, uint8_t attributes,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief Decode the chained "IMPDE\0" directory chunks.
 *
 * A member record never straddles a chunk: when the tail of a chunk is too
 * short for one, the walk moves to the next chunk.  The chunk boundaries
 * therefore matter, so they are reported alongside the bytes instead of
 * being flattened away.
 *
 * @param directory       Bytes starting at the first "IMPDE\0".
 * @param directory_size  Length of @p directory.
 * @param records         Record count from the archive header; the walk
 *                        stops once that many records' worth of bytes exist.
 * @param output          Receives the decoded chunks, back to back.
 * @param output_size     Capacity of @p output.
 * @param chunk_sizes     Receives one decoded size per chunk, in order.
 * @param chunk_capacity  Number of entries @p chunk_sizes can hold.
 * @param chunk_count     Receives the number of chunks. May be NULL.
 * @param written         Receives the total bytes written to @p output.
 * @return true when at least one chunk decoded and everything fit.
 */
XXFC_API bool xx_imp_decode_directory(const uint8_t *directory,
                                      size_t directory_size, uint32_t records,
                                      uint8_t *output, size_t output_size,
                                      uint32_t *chunk_sizes,
                                      size_t chunk_capacity,
                                      size_t *chunk_count, size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XX_IMP_H */

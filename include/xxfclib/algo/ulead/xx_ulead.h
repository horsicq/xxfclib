/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ulead.h
 * @brief ULEAD ("U_LEAD CORP.") single-member container, ported from
 *        XArchive's XULEADDecoder plus the XSharedLZWDecoder
 *        parameterisation it selects.
 *
 * The member is split into 0x4000-byte blocks, each compressed
 * independently, and a u16 PACKED size per block precedes the payloads.  A
 * block whose packed size equals its plain size is STORED - that is the only
 * marker, there is no per-block method byte, so a block that happens to
 * compress to exactly its own length would be read as stored.  The producer
 * evidently makes sure that cannot happen by storing it instead.
 *
 * The codec is the shared parameterised LZW at 12 bits with a clear code, an
 * end code, MSB-first codes and the one-code-early width step - the same
 * option set xx_cmp's method 1 uses at 16 bits, so the two differ only in
 * code width.  Each block restarts the dictionary.
 *
 * The plaintext length IS stored: it is (blockCount - 1) * 0x4000 +
 * lastBlockSize, taken from the header, so no measuring entry point is
 * required.  xx_ulead_parse_header() hands a reader that number (and the
 * stored file name) before it allocates.
 *
 * The WHOLE FILE is handed to the decoder, not just the payload region,
 * because the block table lives in the header.
 */

#ifndef XXFCLIB_ALGO_ULEAD_H
#define XXFCLIB_ALGO_ULEAD_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Plain size of every block but the last. */
#define XX_ULEAD_BLOCK_SIZE 0x4000

/** Parsed ULEAD header. */
typedef struct xx_ulead_header_s {
    uint64_t data_offset;       /**< first block payload                   */
    uint64_t table_offset;      /**< the u16-per-block packed sizes        */
    uint64_t uncompressed_size; /**< the member's plaintext length         */
    uint32_t block_count;
    uint32_t last_block_size;
    uint32_t layout;            /**< 1 or 2                                */
    /** Latin-1, NUL-terminated; empty for the layout that stores no name. */
    char file_name[17];
} xx_ulead_header;

/**
 * @brief Parse and validate a ULEAD header.
 *
 * @param input       The start of the file; at least 0x2c bytes.
 * @param input_size  How much of the file @p input holds.
 * @param file_size   The whole file's size (may exceed @p input_size when
 *                    only the header has been read).
 * @param header      Receives the parsed header.
 * @return true when the header is a well-formed ULEAD header.
 */
XXFC_API bool xx_ulead_parse_header(const uint8_t *input, size_t input_size,
                                    uint64_t file_size,
                                    xx_ulead_header *header);

/**
 * @brief Decode a ULEAD member.
 *
 * @param input       The WHOLE file, header included.
 * @param input_size  Length of @p input.
 * @param output      Destination, at least the header's uncompressed size.
 * @param output_size Capacity of @p output.  Must be at least the header's
 *                    uncompressed size; anything past it is left untouched.
 * @param written     Receives the produced byte count.  Set on every path.
 * @return true only when the whole member decoded.
 */
XXFC_API bool xx_ulead_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_ULEAD_H */

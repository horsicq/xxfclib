/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_PCSECURE_H
#define XXFCLIB_ALGO_PCSECURE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Central Point PCSECURE (PC Tools 5.x / 6.x / 7.x) protected files.
 *
 * ENCRYPTION, NOT COMPRESSION - but the key is NOT a secret the caller has to
 * supply in the common case.  PCSECURE ships four built-in product keys for
 * files saved without a user password, and a file saved WITH one carries its
 * key in the 8-byte password verifier at header offset 60, wrapped under one
 * more fixed key.  Both are recoverable from the file alone, so this module is
 * self-contained: hand it the whole file and it finds the key itself, exactly
 * as XPCSecure::parseContext does.  A file whose verifier is non-zero and
 * whose unwrapped key does not open the header is genuinely user-protected;
 * that is reported as a failure, not guessed at.
 *
 * The payload is DES-ECB, then optionally LZW UNDER the encryption, so the
 * order out is decrypt first, inflate second.  Two deviations from textbook
 * DES, both load-bearing (see xx_pcsecure.c):
 *   * the round count is a header field (0..16), not 16;
 *   * a round count BELOW 3 skips IP and FP entirely.
 */

enum {
    XX_PCSECURE_HEADER_SIZE = 68,
    XX_PCSECURE_BLOCK_SIZE = 8,
    XX_PCSECURE_MAX_ROUNDS = 16,
    XX_PCSECURE_MAX_OUTPUT = 0x20000000 /* 512 MiB sanity cap */
};

/** Everything the encrypted header yields once a key opens it. */
typedef struct xx_pcsecure_info {
    uint8_t key[8];           /**< File key, MSB-first as the schedule sees it */
    uint32_t signature;       /**< "PCT5" / "PCT6" / "PCT7" / "AfoS", LE u32   */
    uint32_t flags;           /**< bit 0 set: the payload is LZW-compressed    */
    int32_t rounds;           /**< DES rounds for the PAYLOAD, 0..16           */
    uint64_t uncompressed_size;
    uint64_t compressed_size;
    uint32_t dos_time;        /**< meaningful for PCT7 only                    */
    uint8_t name_extension[4];/**< stored file extension, with its dot         */
    bool user_password;       /**< the verifier at +60 was non-zero            */
} xx_pcsecure_info;

/**
 * @brief Find the key and read the header of a whole PCSECURE file.
 *
 * @p input must start at file offset 0 and be longer than the 68-byte header.
 * Returns false when the signature is unknown, no candidate key produces the
 * "SeaHawks" verifier, or the header's declared sizes are impossible.
 */
XXFC_API bool xx_pcsecure_parse_header(const uint8_t *input, size_t input_size,
                                       xx_pcsecure_info *info);

/**
 * @brief Decrypt (and inflate) a whole PCSECURE file.
 *
 * @p input is the WHOLE FILE from offset 0; the result is the single member's
 * plaintext, exactly @c info.uncompressed_size bytes.
 */
XXFC_API bool xx_pcsecure_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

/**
 * @brief Report the member's decoded size without producing it.
 *
 * The plaintext length IS stored - at header offset 0x18 - but that field is
 * under the encryption, so a reader cannot size its allocation without doing
 * the key search first.  This does the search and reports the declared size;
 * unlike a sliding-window scan it does not decode, because there is nothing to
 * measure that the header does not already state.  @p consumed is the whole
 * file, which is what the member occupies.
 */
XXFC_API bool xx_pcsecure_scan_memory(const uint8_t *input, size_t input_size,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

/**
 * @brief The payload-only entry point, mirroring XPCSecureDecoder::decode.
 *
 * @p payload is the raw member stream starting at file offset 68 and @p key /
 * @p rounds / @p flags / @p compressed_size are the fields the container
 * publishes as its 14-byte compress property (key MSB-first, then rounds,
 * flags, and the compressed size as a little-endian u32).  Use this when the
 * reader has already parsed the header; use xx_pcsecure_decode_memory when it
 * has not.
 */
XXFC_API bool xx_pcsecure_decode_payload(const uint8_t *payload,
                                         size_t payload_size,
                                         const uint8_t key[8], int32_t rounds,
                                         uint8_t flags,
                                         uint64_t compressed_size,
                                         uint64_t uncompressed_size,
                                         uint8_t *output, size_t output_size,
                                         size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_PCSECURE_H */

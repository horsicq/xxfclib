/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ZIE_H
#define XXFCLIB_ALGO_ZIE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ZIE -- the "ProtectIt/2" (OS/2) encrypted ZIP wrapper.  There is no
 * compression at all: strip the 0x118-byte header, undo a 16-byte repeating
 * XOR, and what is left is an ordinary ZIP.  Plaintext length == payload
 * length, so no measuring entry point is needed.
 *
 * HEADER (0x118 bytes, then the encrypted payload)
 *   +0x000  u32       0x32544950 = "PIT2"
 *   +0x004  16 bytes  obfuscated key
 *   +0x014  13 bytes  original file name, NUL padded, validated as an 8.3-ish
 *                     name (the reference's own check, ported verbatim)
 *   +0x024 .. 0x118   zero padding
 *
 * THE KEY.  The reference XORs the blob at +4 with two 16-byte tables held in
 * its data section.  Both XORs land on the same 16 bytes, so the pair is just
 * a split constant, and the constant is the product name:
 *
 *     key[i] = header[4 + i] ^ "ProtectIt/2 OS/2"[i]      for i in 0..15
 *
 * THE CIPHER is a 16-byte repeating XOR whose PHASE IS DERIVED FROM THE
 * PAYLOAD LENGTH:
 *
 *     size = payload rounded DOWN to a multiple of 4
 *     rot  = (0x10 - (size & 0xf)) & 0xf
 *     rot += 8 for the first 0xC0000 bytes when size >= 0xC0000
 *     plain[pos] = cipher[pos] ^ key[(pos + rot) & 0xf]
 *
 * and the last (payload % 4) bytes are left IN THE CLEAR.
 *
 * TRUNCATED ARCHIVES are why the phase is resolved by known plaintext rather
 * than by the length rule alone: a truncated .zie has a length the encryptor
 * never saw, and its true phase is 0.  The archive underneath must start with
 * "PK" 03 04; the length-derived phase is tried first and phase 0 is the
 * fallback.  That reproduces the reference on intact files and recovers
 * truncated ones the reference extracts nothing from.
 */

#define XX_ZIE_HEADER_SIZE ((size_t)0x118U)
#define XX_ZIE_LARGE_PAYLOAD_SIZE ((size_t)0xC0000U)
#define XX_ZIE_NAME_OFFSET ((size_t)0x14U)
#define XX_ZIE_NAME_SIZE ((size_t)0x0DU)

typedef struct xx_zie_method {
    uint8_t key[16];
    uint32_t base;  /* 0..15, the phase the payload was encrypted with */
    bool recovered; /* true when the length rule failed and phase 0 was used */
} xx_zie_method;

/**
 * @brief Magic plus the reference's 8.3-ish name check on +0x14.
 * @param header      At least XX_ZIE_HEADER_SIZE bytes of the file.
 * @param header_size Bytes available at @p header.
 */
XXFC_API bool xx_zie_is_valid_header(const uint8_t *header, size_t header_size);

/**
 * @brief Copy the stored original file name out of the header.
 *
 * The field is 13 bytes, NUL padded; the name is everything before the first
 * NUL. No code page conversion is applied.
 *
 * @param output      Destination; a NUL terminator is always appended.
 * @param output_size Capacity of @p output; must be at least 14 bytes.
 * @param length      Receives the name length without the terminator. May be
 *                    NULL.
 */
XXFC_API bool xx_zie_file_name(const uint8_t *header, size_t header_size,
                               char *output, size_t output_size,
                               size_t *length);

/**
 * @brief Derive the key and resolve the XOR phase.
 *
 * @param probe        The first four payload bytes (file offset 0x118).
 * @param probe_size   Bytes available at @p probe; four are required.
 * @param payload_size Size of the whole payload, i.e. file size - 0x118.
 * @return true when the header is a ZIE header and one of the two candidate
 *         phases yields the "PK" 03 04 local file header signature.
 */
XXFC_API bool xx_zie_resolve_method(const uint8_t *header, size_t header_size,
                                    const uint8_t *probe, size_t probe_size,
                                    uint64_t payload_size,
                                    xx_zie_method *method);

/**
 * @brief Decrypt a payload with an already resolved method.
 *
 * @p input is the payload, i.e. everything from file offset 0x118 on, and the
 * result has exactly the same length. @p output may be the same address as
 * @p input for in-place work; partial overlap is not supported.
 */
XXFC_API bool xx_zie_decode_method(const uint8_t *input, size_t input_size,
                                   const xx_zie_method *method,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief Decrypt a complete ZIE file.
 *
 * @p input is the whole file, header included; the header is validated, the
 * method resolved from it and from the first payload bytes, and the decrypted
 * ZIP is written to @p output. @p written receives input_size - 0x118.
 */
XXFC_API bool xx_zie_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_ZIE_H */

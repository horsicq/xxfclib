/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gpgsigned.h @brief GnuPG "signed file" (OpenPGP ZIP compressed
 * data packet) reader. */

/* What binwalk calls a "GPG signed file" is the binary output of
 * `gpg --sign --compress-algo zip`: one OpenPGP Compressed Data packet
 * (RFC 4880 section 5.6) written with an old-format header and an
 * indeterminate length, so the packet runs to the end of the data:
 *
 *   +0x00  0xA3   old-format CTB: bit 7 set, bit 6 clear, tag 8
 *                 (Compressed Data), length type 3 (indeterminate)
 *   +0x01  0x01   compression algorithm 1 = ZIP, i.e. raw RFC 1951 Deflate
 *   +0x02  ...    raw Deflate stream, ending with its final block
 *
 * The decompressed bytes are themselves a sequence of OpenPGP packets -
 * normally a one-pass signature, a literal data packet carrying the signed
 * file and the signature - and are published as the single archive record,
 * the same "decompressed.bin" binwalk's gpg extractor writes.
 *
 * Source: binwalk's src/signatures/gpg.rs (the two-byte magic and the
 * requirement that the Deflate stream decodes) and src/extractors/gpg.rs +
 * src/extractors/inflate.rs (size = 2 + Deflate bytes consumed through the
 * final block; at least one byte must decompress).  This reader is stricter:
 * the decompressed data must also be a well-formed OpenPGP packet sequence.
 */

#ifndef XXFCLIB_FORMAT_GPGSIGNED_H
#define XXFCLIB_FORMAT_GPGSIGNED_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_GPGSIGNED_CTB 0xA3U           /**< old format, tag 8, indeterminate */
#define XX_GPGSIGNED_ALGO_ZIP 0x01U      /**< RFC 4880 9.3: 1 = ZIP (RFC 1951) */
#define XX_GPGSIGNED_HEADER_SIZE 2U
/** Decompressed output is capped; a larger stream is rejected. */
#define XX_GPGSIGNED_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)

typedef struct xx_gpgsigned xx_gpgsigned;
typedef struct xx_gpgsigned xx_gpgsigned_t;
typedef struct xx_gpgsigned XGpgsigned;

struct xx_gpgsigned {
    Abstractformat format;
    uint64_t uncompressed_size; /**< Bytes of the decompressed packet stream. */
    uint64_t packet_count;      /**< Top-level OpenPGP packets inside it. */
    int64_t stream_end;         /**< Absolute end of the Deflate stream, or -1. */
    uint32_t crc32;             /**< CRC-32 of the decompressed stream. */
    uint8_t first_packet_tag;   /**< Tag of the first inner packet. */
};

XXFC_API void xx_gpgsigned_init(xx_gpgsigned *gpg, xx_io_device *dev,
                                int64_t base_address);
XXFC_API xx_gpgsigned *xx_gpgsigned_create(xx_io_device *dev,
                                           int64_t base_address);
XXFC_API void xx_gpgsigned_destroy(xx_gpgsigned *gpg);
XXFC_API void xx_gpgsigned_free(xx_gpgsigned *gpg);

XXFC_API bool xx_gpgsigned_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_gpgsigned_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_gpgsigned_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_gpgsigned_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the packet stream to a caller-provided device.  The result is
 * checked against the size and CRC-32 recorded by handle_base_info. */
XXFC_API bool xx_gpgsigned_unpack_to_device(xx_gpgsigned *gpg,
                                            xx_io_device *destination,
                                            xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gpgsigned_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gpgsigned_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gpgsigned_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gpgsigned_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gpgsigned_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_gpgsigned_get_uncompressed_size(const xx_gpgsigned *gpg);
XXFC_API uint64_t xx_gpgsigned_get_packet_count(const xx_gpgsigned *gpg);
XXFC_API int64_t xx_gpgsigned_get_stream_end(const xx_gpgsigned *gpg);
XXFC_API uint32_t xx_gpgsigned_get_crc32(const xx_gpgsigned *gpg);

static inline Abstractformat *xx_gpgsigned_to_format(xx_gpgsigned *gpg) {
    return gpg ? &gpg->format : NULL;
}
static inline void XGpgsigned_init(xx_gpgsigned *gpg, xx_io_device *dev,
                                   int64_t base_address) {
    xx_gpgsigned_init(gpg, dev, base_address);
}
static inline xx_gpgsigned *XGpgsigned_create(xx_io_device *dev,
                                              int64_t base_address) {
    return xx_gpgsigned_create(dev, base_address);
}
static inline void XGpgsigned_free(xx_gpgsigned *gpg) {
    xx_gpgsigned_free(gpg);
}
static inline bool XGpgsigned_is_valid(xx_gpgsigned *gpg, xx_pd_struct *pd) {
    return gpg ? xx_format_is_valid(&gpg->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GPGSIGNED_H */

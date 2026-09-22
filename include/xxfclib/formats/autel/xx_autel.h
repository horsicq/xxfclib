/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_autel.h @brief Autel ECC obfuscated firmware reader. */

/* Autel ship the firmware for their drones and their EV chargers wrapped in a
 * 32 byte "ECC" header followed by a lightly obfuscated payload.  The payload
 * is not encrypted in any meaningful sense: it is run through a fixed 256
 * entry add/xor table that repeats every 256 bytes, and underneath it is an
 * ordinary image - a squashfs, a uImage, a gzip blob - that other readers in
 * this library decode.
 *
 * Everything in the header is LITTLE endian.
 *
 *   header (0x20 bytes)
 *     +0x00  8 bytes   magic, the ASCII "ECC0101" plus a NUL
 *     +0x08  u32       data size, the obfuscated payload length
 *     +0x0C  u32       header size, always 0x20
 *     +0x10  16 bytes  copyright string, "Copyright Autel" plus a NUL
 *     +0x20            obfuscated payload begins
 *
 * The de-obfuscation is byte-wise and position-dependent within each 256 byte
 * block:  plain[i] = ((cipher[i] + add[i % 256]) ^ xor[i % 256]) & 0xFF.
 * A short final block simply uses the first entries of the table.  Because
 * the transform is position-dependent, the payload bytes ON THE DEVICE are
 * not the plaintext: the archive record this reader publishes names the
 * obfuscated span, and it is xx_autel_unpack_current_archive_record() that
 * writes the decoded stream.  A caller that wants to recurse has to unpack
 * first; reading the record's range straight off the device yields ciphertext.
 *
 * Sources: binwalk src/structures/autel.rs (header layout and the checks on
 * header size and copyright string) and src/extractors/autel.rs (the block
 * size and the full add/xor table, which that file in turn credits to
 * https://gist.github.com/sector7-nl/3fc815cd2497817ad461bfbd393294cb).
 */

#ifndef XXFCLIB_FORMAT_AUTEL_H
#define XXFCLIB_FORMAT_AUTEL_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_AUTEL_HEADER_SIZE 0x20U
#define XX_AUTEL_MAGIC_SIZE 8U
#define XX_AUTEL_COPYRIGHT_OFFSET 0x10U
#define XX_AUTEL_COPYRIGHT_SIZE 16U
/** The obfuscation table repeats every this many payload bytes. */
#define XX_AUTEL_BLOCK_SIZE 256U

typedef struct xx_autel xx_autel;
typedef struct xx_autel xx_autel_t;
typedef struct xx_autel XAutel;

struct xx_autel {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t data_size;   /**< Obfuscated payload length from +0x08. */
    uint32_t header_size; /**< Always XX_AUTEL_HEADER_SIZE. */
    int64_t archive_end;  /**< base_address + header + payload, or -1. */
    void *internal;
};

XXFC_API void xx_autel_init(xx_autel *autel, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_autel *xx_autel_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_autel_destroy(xx_autel *autel);
XXFC_API void xx_autel_free(xx_autel *autel);

XXFC_API bool xx_autel_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_autel_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_autel_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_autel_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_autel_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_autel_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_autel_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_autel_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_autel_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_autel_get_number_of_records(const xx_autel *autel);
XXFC_API uint64_t xx_autel_get_number_of_members(const xx_autel *autel);
XXFC_API uint32_t xx_autel_get_data_size(const xx_autel *autel);
XXFC_API int64_t xx_autel_get_archive_end(const xx_autel *autel);

/**
 * @brief De-obfuscate @p size payload bytes in place.
 *
 * @param data          the obfuscated bytes.
 * @param size          how many bytes @p data holds.
 * @param block_offset  the offset of @p data within the payload, used to pick
 *                      the table entries; only block_offset % 256 matters.
 *
 * Exposed so a caller can decode a span it has already read without going
 * through the unpack path.
 */
XXFC_API void xx_autel_deobfuscate(void *data, size_t size,
                                   uint64_t block_offset);

static inline Abstractformat *xx_autel_to_format(xx_autel *autel) {
    return autel ? &autel->format : NULL;
}
static inline void XAutel_init(xx_autel *autel, xx_io_device *dev,
                               int64_t base_address) {
    xx_autel_init(autel, dev, base_address);
}
static inline xx_autel *XAutel_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_autel_create(dev, base_address);
}
static inline void XAutel_free(xx_autel *autel) { xx_autel_free(autel); }
static inline bool XAutel_is_valid(xx_autel *autel, xx_pd_struct *pd) {
    return autel ? xx_format_is_valid(&autel->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AUTEL_H */

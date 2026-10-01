/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xiaomi_hdr1.h @brief Xiaomi "HDR1" router firmware container. */

/*
 * Xiaomi (MiWiFi) router firmware images begin with a 48-byte little-endian
 * "HDR1" header that points at up to eight blobs and at a trailing RSA
 * signature block.  The layout is the one used by unblob's hdr1 handler
 * (unblob/handlers/archive/xiaomi/hdr.py, MIT) and OpenWrt's
 * firmware-utils/src/xiaomifw.c:
 *
 *   header, at the start of the image
 *   +0x00  char  magic[4]         "HDR1"
 *   +0x04  u32   signature_offset from the start of the image
 *   +0x08  u32   crc32            see below
 *   +0x0C  u16   unused
 *   +0x0E  u16   device_id
 *   +0x10  u32   blob_offsets[8]  from the start of the image; the list ends
 *                                 at the first zero entry
 *
 *   blob header, at every blob offset, followed by the blob's data
 *   +0x00  u32   magic            0x0000BABE in real images (not checked,
 *                                 as in unblob)
 *   +0x04  u32   flash_offset
 *   +0x08  u32   size             of the data that follows this header
 *   +0x0C  u16   type
 *   +0x0E  u16   unused
 *   +0x10  char  name[32]         NUL padded
 *
 *   signature block, at signature_offset
 *   +0x00  u32   signature_size
 *   +0x04  u32   padding[3]
 *   +0x10  u8    content[0x100]
 *
 * The image ends right after the 272-byte signature block.  The crc32 field
 * holds the bitwise complement of the ordinary CRC-32 (CRC-32/JAMCRC) of the
 * bytes from +0x0C to that end.  The reader requires the magic, a signature
 * offset past the header, a non-zero first blob offset, the whole image
 * inside the device and a matching CRC.  Every listed blob must be non-empty
 * and lie between the header and the signature.
 */

#ifndef XXFCLIB_FORMAT_XIAOMI_HDR1_H
#define XXFCLIB_FORMAT_XIAOMI_HDR1_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_XIAOMI_HDR1_MAX_BLOBS 8

typedef struct xx_xiaomi_hdr1 xx_xiaomi_hdr1;
typedef struct xx_xiaomi_hdr1 xx_xiaomi_hdr1_t;
typedef struct xx_xiaomi_hdr1 XXiaomiHdr1;

struct xx_xiaomi_hdr1 {
    Abstractformat format;
    uint64_t number_of_records; /**< Blobs listed before the first zero. */
    uint32_t signature_offset;
    uint32_t crc32;             /**< Raw field (CRC-32/JAMCRC). */
    uint16_t device_id;
    int64_t archive_end;        /**< base + signature_offset + 272, or -1. */
    void *internal;
};

XXFC_API void xx_xiaomi_hdr1_init(xx_xiaomi_hdr1 *hdr, xx_io_device *dev,
                                  int64_t base_address);
XXFC_API xx_xiaomi_hdr1 *xx_xiaomi_hdr1_create(xx_io_device *dev,
                                               int64_t base_address);
XXFC_API void xx_xiaomi_hdr1_destroy(xx_xiaomi_hdr1 *hdr);
XXFC_API void xx_xiaomi_hdr1_free(xx_xiaomi_hdr1 *hdr);

XXFC_API bool xx_xiaomi_hdr1_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_xiaomi_hdr1_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_xiaomi_hdr1_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_xiaomi_hdr1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xiaomi_hdr1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xiaomi_hdr1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xiaomi_hdr1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xiaomi_hdr1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xiaomi_hdr1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_xiaomi_hdr1_to_format(xx_xiaomi_hdr1 *hdr) {
    return hdr ? &hdr->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XIAOMI_HDR1_H */

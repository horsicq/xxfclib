/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xiaomi_hdr2.h @brief Xiaomi "HDR2" router firmware container. */

/*
 * Newer Xiaomi (MiWiFi) router firmware images begin with an 80-byte
 * little-endian "HDR2" header: the HDR1 layout widened with an 8-byte model
 * string and an 8-byte region string.  It points at up to eight blobs and at
 * a trailing RSA signature block.  Layout as described by unblob's hdr2
 * handler (unblob/handlers/archive/xiaomi/hdr.py, MIT) and OpenWrt's
 * firmware-utils/src/xiaomifw.c:
 *
 *   header, at the start of the image
 *   +0x00  char  magic[4]         "HDR2"
 *   +0x04  u32   signature_offset from the start of the image
 *   +0x08  u32   crc32            see below
 *   +0x0C  u32   unused
 *   +0x10  char  device_id[8]     model, e.g. "RA70", NUL padded
 *   +0x18  char  region[8]        e.g. "EU", NUL padded
 *   +0x20  u64   unused[2]
 *   +0x30  u32   blob_offsets[8]  from the start of the image; the list ends
 *                                 at the first zero entry
 *
 *   blob header (48 bytes), at every blob offset, followed by its data
 *   +0x00  u32   magic            0x0000BABE in real images (not checked,
 *                                 as in unblob)
 *   +0x04  u32   flash_offset
 *   +0x08  u32   size             of the data that follows this header
 *   +0x0C  u16   type
 *   +0x0E  u16   unused
 *   +0x10  char  name[32]         NUL padded
 *
 *   signature block (272 bytes), at signature_offset
 *   +0x00  u32   signature_size
 *   +0x04  u32   padding[3]
 *   +0x10  u8    content[0x100]
 *
 * The image ends right after the signature block.  The crc32 field holds the
 * bitwise complement of the ordinary CRC-32 (CRC-32/JAMCRC) of the bytes from
 * +0x0C to that end.  The reader requires the magic, a signature offset past
 * the header, a non-zero first blob offset, the whole image inside the device
 * and a matching CRC; every listed blob must be non-empty and lie between the
 * header and the signature.  Each blob is one member, stored verbatim.
 */

#ifndef XXFCLIB_FORMAT_XIAOMI_HDR2_H
#define XXFCLIB_FORMAT_XIAOMI_HDR2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_XIAOMI_HDR2_MAX_BLOBS 8
#define XX_XIAOMI_HDR2_HEADER_SIZE 80
#define XX_XIAOMI_HDR2_SIGNATURE_SIZE 272

typedef struct xx_xiaomi_hdr2 xx_xiaomi_hdr2;
typedef struct xx_xiaomi_hdr2 xx_xiaomi_hdr2_t;

struct xx_xiaomi_hdr2 {
    Abstractformat format;
    uint64_t number_of_records; /**< Blobs listed before the first zero. */
    uint32_t signature_offset;
    uint32_t crc32;             /**< Raw field (CRC-32/JAMCRC). */
    char device_id[9];          /**< Model string, printable bytes only. */
    char region[9];             /**< Region string, printable bytes only. */
    int64_t archive_end;        /**< base + signature_offset + 272, or -1. */
};

XXFC_API void xx_xiaomi_hdr2_init(xx_xiaomi_hdr2 *hdr, xx_io_device *device,
                                  int64_t base_address);
XXFC_API xx_xiaomi_hdr2 *xx_xiaomi_hdr2_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_xiaomi_hdr2_destroy(xx_xiaomi_hdr2 *hdr);
XXFC_API void xx_xiaomi_hdr2_free(xx_xiaomi_hdr2 *hdr);

XXFC_API bool xx_xiaomi_hdr2_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_xiaomi_hdr2_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_xiaomi_hdr2_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_xiaomi_hdr2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xiaomi_hdr2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xiaomi_hdr2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xiaomi_hdr2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xiaomi_hdr2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xiaomi_hdr2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XIAOMI_HDR2_H */

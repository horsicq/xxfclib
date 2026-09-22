/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_trx.h @brief Broadcom/OpenWrt TRX firmware container reader. */

/* TRX is the flash image wrapper used by Broadcom reference firmware and by
 * OpenWrt for most brcm47xx/brcm63xx targets.  It is a thin header naming a
 * handful of partitions inside one contiguous blob; the partitions themselves
 * are ordinary payloads - an LZMA-compressed kernel, a squashfs rootfs, a
 * loader - that other readers in this library already decode.  The whole point
 * of this reader is therefore to validate the header, verify the CRC and hand
 * each partition out as its own archive record so a caller can recurse.
 *
 * Everything is LITTLE endian, unlike the D-Link and NETGEAR wrappers.
 *
 *   header (v1 is 28 bytes, v2 is 32 bytes)
 *     +0   u32  magic, "HDR0" == 0x30524448 read little endian
 *     +4   u32  len, the length of the whole image INCLUDING this header
 *     +8   u32  crc32, see below
 *     +12  u32  flag_version, flags in bits 0..15, version in bits 16..31
 *     +16  u32  offsets[0], from the start of the header
 *     +20  u32  offsets[1]
 *     +24  u32  offsets[2]
 *     +28  u32  offsets[3]   - v2 only
 *
 * The CRC32 starts at offset 12, not at 0: it covers flag_version, the offset
 * table and every payload byte through base_address + len.  It is also NOT the
 * finished ISO-HDLC CRC32 that the rest of this library computes - OpenWrt's
 * crc32buf() seeds the register with 0xFFFFFFFF and returns it WITHOUT the
 * final XOR, which is the variant catalogued as JAMCRC.  Both details have to
 * be right or every genuine image is rejected.
 *
 * A zero offset means "this partition is absent".  The offsets are otherwise
 * unconstrained 32-bit values straight out of the file, so each one is bounded
 * against len before any record is published.
 */

#ifndef XXFCLIB_FORMAT_TRX_H
#define XXFCLIB_FORMAT_TRX_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_TRX_MAGIC UINT32_C(0x30524448) /**< "HDR0", little endian. */
#define XX_TRX_HEADER_SIZE_V1 28U
#define XX_TRX_HEADER_SIZE_V2 32U
#define XX_TRX_MAX_PARTITIONS 4U

typedef struct xx_trx xx_trx;
typedef struct xx_trx xx_trx_t;
typedef struct xx_trx XTrx;

struct xx_trx {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t image_size;    /**< The header's len field. */
    uint32_t crc32;         /**< The header's crc32 field, verified on parse. */
    uint32_t flags;         /**< flag_version bits 0..15. */
    uint32_t version;       /**< flag_version bits 16..31; 1 or 2. */
    uint32_t header_size;   /**< 28 for v1, 32 for v2. */
    int64_t archive_end;    /**< base_address + len, or -1. */
    void *internal;
};

XXFC_API void xx_trx_init(xx_trx *trx, xx_io_device *dev, int64_t base_address);
XXFC_API xx_trx *xx_trx_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_trx_destroy(xx_trx *trx);
XXFC_API void xx_trx_free(xx_trx *trx);

XXFC_API bool xx_trx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_trx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_trx_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_trx_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_trx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trx_archive_record_move_to_next(Abstractformat *self,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd);
XXFC_API void xx_trx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_trx_get_number_of_records(const xx_trx *trx);
XXFC_API uint64_t xx_trx_get_number_of_members(const xx_trx *trx);
XXFC_API uint32_t xx_trx_get_image_size(const xx_trx *trx);
XXFC_API uint32_t xx_trx_get_crc32(const xx_trx *trx);
XXFC_API uint32_t xx_trx_get_version(const xx_trx *trx);
XXFC_API uint32_t xx_trx_get_flags(const xx_trx *trx);
XXFC_API int64_t xx_trx_get_archive_end(const xx_trx *trx);

static inline Abstractformat *xx_trx_to_format(xx_trx *trx) {
    return trx ? &trx->format : NULL;
}
static inline void XTrx_init(xx_trx *trx, xx_io_device *dev,
                             int64_t base_address) {
    xx_trx_init(trx, dev, base_address);
}
static inline xx_trx *XTrx_create(xx_io_device *dev, int64_t base_address) {
    return xx_trx_create(dev, base_address);
}
static inline void XTrx_free(xx_trx *trx) { xx_trx_free(trx); }
static inline bool XTrx_is_valid(xx_trx *trx, xx_pd_struct *pd) {
    return trx ? xx_format_is_valid(&trx->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TRX_H */

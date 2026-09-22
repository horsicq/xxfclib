/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ubi.h @brief UBI (Unsorted Block Images) volume reader. */

/* UBI is not a filesystem - it is the volume manager that sits between raw
 * flash and a filesystem such as UBIFS. An image is a flat sequence of
 * physical erase blocks (PEBs), all the same size, and each PEB carries two
 * big-endian headers:
 *
 *   ubi_ec_hdr, 64 bytes, always at offset 0 of the PEB
 *     +0   u32  "UBI#"  (0x55424923)
 *     +4   u8   version
 *     +8   u64  erase counter
 *     +16  u32  vid_hdr_offset   where the volume-id header sits in this PEB
 *     +20  u32  data_offset      where the payload sits in this PEB
 *     +24  u32  image_seq
 *     +60  u32  hdr_crc          over bytes 0..59
 *
 *   ubi_vid_hdr, 64 bytes, at PEB + vid_hdr_offset
 *     +0   u32  "UBI!"  (0x55424921)
 *     +5   u8   vol_type   1 dynamic, 2 static
 *     +8   u32  vol_id
 *     +12  u32  lnum       the LOGICAL block number inside the volume
 *     +20  u32  data_size  static volumes only
 *     +24  u32  used_ebs   static volumes only
 *     +40  u64  sqnum      monotonic; the newest copy of a logical block wins
 *     +60  u32  hdr_crc    over bytes 0..59
 *
 * The CRC is crc32 seeded with 0xFFFFFFFF and NOT finally complemented, so it
 * equals the ordinary CRC-32 of the same bytes XOR 0xFFFFFFFF.
 *
 * The payload of a PEB is one logical erase block (LEB) of one volume. The
 * physical order of the PEBs is arbitrary - wear levelling moves them around -
 * so a volume is only recovered by collecting every PEB that carries its
 * vol_id and concatenating them in lnum order. That reassembly is this
 * reader's whole job.
 *
 * Volume names live in the "layout volume", vol_id 0x7FFFF000, whose LEB 0 and
 * LEB 1 each hold 128 ubi_vtbl_record entries of 172 bytes:
 *     +0   u32  reserved_pebs
 *     +12  u8   vol_type
 *     +14  u16  name_len
 *     +16  u8[128] name
 *     +168 u32  crc   over bytes 0..167
 *
 * Each reassembled volume is presented as one archive record. The payload of
 * a record is NOT one contiguous device range, so data_offset only locates the
 * first LEB; extraction walks the block list. A volume whose first block opens
 * with the UBIFS node magic is named "<name>.ubifs" so that the caller can
 * chain xx_ubifs on the extracted image - UBI stops at the volume boundary and
 * deliberately knows nothing about the filesystem inside it.
 */

#ifndef XXFCLIB_FORMAT_UBI_H
#define XXFCLIB_FORMAT_UBI_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ubi xx_ubi;
typedef struct xx_ubi xx_ubi_t;
typedef struct xx_ubi XUbi;

struct xx_ubi {
    Abstractformat format;
    uint64_t number_of_records;  /**< One record per reassembled volume. */
    uint64_t number_of_members;
    uint32_t peb_size;        /**< Detected physical erase block size. */
    uint32_t leb_size;        /**< peb_size - data_offset. */
    uint32_t vid_hdr_offset;  /**< From the first valid erase-counter header. */
    uint32_t data_offset;     /**< From the first valid erase-counter header. */
    uint32_t image_seq;
    uint64_t peb_count;       /**< PEBs examined, valid or not. */
    uint64_t mapped_peb_count; /**< PEBs carrying a volume-id header. */
    uint64_t volume_count;    /**< Non-internal volumes found. */
    int64_t archive_end;      /**< End of the last examined PEB, or -1. */
    void *internal;
};

XXFC_API void xx_ubi_init(xx_ubi *ubi, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ubi *xx_ubi_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ubi_destroy(xx_ubi *ubi);
XXFC_API void xx_ubi_free(xx_ubi *ubi);

XXFC_API bool xx_ubi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ubi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ubi_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ubi_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ubi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ubi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ubi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ubi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ubi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ubi_get_number_of_records(const xx_ubi *ubi);
XXFC_API uint64_t xx_ubi_get_number_of_members(const xx_ubi *ubi);
XXFC_API uint32_t xx_ubi_get_peb_size(const xx_ubi *ubi);
XXFC_API uint32_t xx_ubi_get_leb_size(const xx_ubi *ubi);
XXFC_API uint32_t xx_ubi_get_image_seq(const xx_ubi *ubi);
XXFC_API uint64_t xx_ubi_get_volume_count(const xx_ubi *ubi);
XXFC_API int64_t xx_ubi_get_archive_end(const xx_ubi *ubi);
/** @brief "Dynamic", "Static" or "Unknown" for a ubi_vid_hdr vol_type. */
XXFC_API const char *xx_ubi_volume_type_to_string(uint32_t vol_type);

static inline Abstractformat *xx_ubi_to_format(xx_ubi *ubi) {
    return ubi ? &ubi->format : NULL;
}
static inline void XUbi_init(xx_ubi *ubi, xx_io_device *dev,
                             int64_t base_address) {
    xx_ubi_init(ubi, dev, base_address);
}
static inline xx_ubi *XUbi_create(xx_io_device *dev, int64_t base_address) {
    return xx_ubi_create(dev, base_address);
}
static inline void XUbi_free(xx_ubi *ubi) { xx_ubi_free(ubi); }
static inline bool XUbi_is_valid(xx_ubi *ubi, xx_pd_struct *pd) {
    return ubi ? xx_format_is_valid(&ubi->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UBI_H */

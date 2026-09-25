/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_moof.h @brief Applesauce MOOF (Macintosh 3.5" floppy) reader. */

#ifndef XXFCLIB_FORMAT_MOOF_H
#define XXFCLIB_FORMAT_MOOF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Applesauce MOOF image: bit cells or flux timings per track.
 *
 *   0x00  "MOOF" FF 0A 0D 0A
 *   0x08  u32 LE  CRC-32 of everything after offset 12 (0 = not computed)
 *   0x0C  chunks: char[4] id, u32 LE size, data.  INFO always comes first:
 *           +0 version, +1 disk type (1 SSDD GCR 400K, 2 DSDD GCR 800K,
 *           3 DSHD MFM 1.44M), +2 write protected, +3 synchronized,
 *           +4 optimal bit timing in 125 ns ticks, +5 creator[32], ...
 *         TMAP  160 bytes, index cylinder * 2 + head -> TRKS entry or 0xFF
 *         TRKS  160 x { u16 start block, u16 block count, u32 bit count },
 *               then the track data in 512-byte blocks counted from the
 *               start of the file (MSB-first bit cells)
 *         FLUX  optional, like TMAP; its entries name TRKS entries whose
 *               "bit count" is a byte count of flux intervals (ticks, 0xFF =
 *               add 255 and continue)
 *         META  optional tab-separated "key\tvalue\n" text
 *
 * The members are produced by decoding the tracks: "image.img" holds the
 * 512-byte sectors in logical block order (Macintosh GCR, or IBM MFM for
 * 1.44M disks and for PC disks MAME stores in MOOF), "tags.bin" the 12 tag
 * bytes of every GCR sector when any of them is non-zero, and "meta.txt" the
 * META chunk verbatim.  A sector that is missing or fails its checksum stays
 * zero in the image.
 */
typedef struct xx_moof {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t disk_type;      /**< INFO disk type byte. */
    uint32_t encoding;       /**< XX_MOOF_ENCODING_*, after decoding. */
    uint64_t image_size;     /**< Size of image.img, 0 when nothing decoded. */
    uint32_t good_sectors;   /**< Sectors that passed their checksum. */
} xx_moof;

typedef xx_moof xx_moof_t;

#define XX_MOOF_ENCODING_NONE 0U
#define XX_MOOF_ENCODING_GCR 1U
#define XX_MOOF_ENCODING_MFM 2U

XXFC_API void xx_moof_init(xx_moof *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_moof *xx_moof_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_moof_destroy(xx_moof *archive);
XXFC_API void xx_moof_free(xx_moof *archive);

XXFC_API bool xx_moof_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_moof_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_moof_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_moof_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_moof_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_moof_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_moof_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_moof_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_moof_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MOOF_H */

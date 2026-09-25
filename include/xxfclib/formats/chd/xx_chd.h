/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_chd.h @brief MAME CHD (Compressed Hunks of Data) reader. */

#ifndef XXFCLIB_FORMAT_CHD_H
#define XXFCLIB_FORMAT_CHD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MAME CHD image, versions 1 to 5.
 *
 * All fields are big-endian.  Every version opens with the same 16 bytes:
 *
 *   0x00  char[8] "MComprHD"
 *   0x08  u32     header length (76, 80, 120, 108, 124 for v1..v5)
 *   0x0C  u32     version (1..5)
 *
 * The logical data is cut into equal hunks.  A map gives, for each hunk, how
 * it is stored: compressed, stored, a copy of another hunk of the same file,
 * a hunk of a parent CHD, or (v3/v4) an 8-byte pattern.  Version 5 names up
 * to four codecs in its header (zlib, lzma, huff, flac, zstd and the CD
 * variants cdzl, cdlz, cdfl, cdzs) and Huffman-codes its map; versions 1-4
 * use raw Deflate only.  Metadata entries (v3 and later) say what the data
 * is: a hard disk (GDDD), a CD-ROM (CHT2/CHTR/CHCD), a GD-ROM (CHGD) or a
 * DVD.
 *
 * The reader publishes the decoded data the way MAME's chdman extracts it:
 *   hard disk     disk.img                     (extracthd)
 *   DVD           disc.iso                     (extractdvd)
 *   other data    data.bin                     (extractraw)
 *   CD-ROM        disc.cue + disc.bin          (extractcd -o disc.cue)
 *   GD-ROM        disc.gdi + discNN.bin/.raw   (extractcd -o disc.gdi)
 *
 * Hunks stored in a parent CHD cannot be rebuilt from one file; a member
 * that needs one fails to unpack.  So do LaserDisc (A/V) images.
 */
typedef struct xx_chd {
    Abstractformat format;
    uint32_t version;
    uint32_t hunk_bytes;
    uint32_t unit_bytes;
    uint32_t hunk_count;
    uint64_t logical_bytes;
    uint32_t codecs[4];     /**< v5 codec tags; v1-v4 map to 'zlib'. */
    uint32_t kind;          /**< XX_CHD_KIND_* */
    uint32_t track_count;   /**< CD / GD-ROM tracks. */
    uint64_t number_of_records;
    bool has_parent;
} xx_chd;

typedef xx_chd xx_chd_t;

#define XX_CHD_KIND_RAW 0U
#define XX_CHD_KIND_HD 1U
#define XX_CHD_KIND_CD 2U
#define XX_CHD_KIND_GD 3U
#define XX_CHD_KIND_DVD 4U
#define XX_CHD_KIND_AV 5U

XXFC_API void xx_chd_init(xx_chd *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_chd *xx_chd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_chd_destroy(xx_chd *archive);
XXFC_API void xx_chd_free(xx_chd *archive);

XXFC_API bool xx_chd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_chd_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_chd_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_chd_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_chd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_chd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_chd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_chd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_chd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CHD_H */

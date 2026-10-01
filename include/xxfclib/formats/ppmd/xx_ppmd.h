/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_PPMD_H
#define XXFCLIB_FORMAT_PPMD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PPMd stream file (.pmd), as written by Dmitry Shkarin's PPMd var.H and
 * var.I command-line compressors ("PPMd e file").  Little endian:
 *
 *   0x00  u32  signature 0x84ACAF8F (bytes 8F AF AC 84)
 *   0x04  u32  file attributes of the source file
 *   0x08  u16  info: bits 0..3   model order - 1          (order 2..16)
 *                    bits 4..11  model memory in MiB - 1   (1..256 MiB)
 *                    bits 12..15 variant - 'A'             (7 = H, 8 = I)
 *   0x0A  u16  name length; for var.I bits 14..15 hold the model restore
 *              method (0 restart, 1 cut off, 2 freeze) and bits 0..13 the
 *              length.  At most 512.
 *   0x0C  u32  modification time, DOS format (time in the low word, date in
 *              the high word)
 *   0x10  name bytes (no terminator)
 *   then the range-coded PPMd stream, which ends with its own end marker.
 *
 * var.H uses the PPMd7 model with Subbotin's carryless range coder (not the
 * 7z coder of 7z/ZIP PPMd); var.I rev.1 is the PPMd8 model of ZIP method 98.
 * The packed length is not stored: the end of a stream is where its decoder
 * stops.  Shkarin's tool writes one such header+stream per input file, one
 * after another, so a file may hold several members; each is found by
 * decoding the one before it.  That scan is done only for files up to
 * XX_PPMD_SCAN_MAX_PACKED bytes and XX_PPMD_SCAN_MAX_OUTPUT decoded bytes;
 * past either limit the rest of the file is published as the current member
 * with unknown sizes.  The freeze restore method (2) is not supported (7-Zip
 * does not support it either) and such files are refused.
 *
 * Records: one per member.  XX_META_ID_COMPRESSION_METHOD is the variant
 * (XX_PPMD_METHOD_VAR_H / XX_PPMD_METHOD_VAR_I); XX_META_ID_ATTRIBUTES,
 * XX_META_ID_LAST_MOD_DATE and XX_META_ID_LAST_MOD_TIME come from the header;
 * the sizes are published when the scan measured them.  Stored names are
 * bytes of an unknown code page: bytes above 0x7E become '_', '\' becomes
 * '/'.  Names that are absolute, carry a drive or stream colon, a '.' or
 * '..' component, reserved punctuation or a device name are listed but not
 * extracted; an empty name is published as "payload"; a repeated name gets
 * a "_<n>" suffix so members never overwrite each other.
 */

#define XX_PPMD_METHOD_VAR_H 7U
#define XX_PPMD_METHOD_VAR_I 8U
#define XX_PPMD_MAX_NAME 512U
#define XX_PPMD_MAX_MEMBERS 1024U
#define XX_PPMD_SCAN_MAX_PACKED ((int64_t)64 * 1024 * 1024)
#define XX_PPMD_SCAN_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)

typedef struct xx_ppmd {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t variant;  /**< Of the first member: 7 (var.H) or 8 (var.I). */
    uint32_t order;
    uint32_t memory_mb;
    uint32_t restore;
    bool sizes_known;  /**< Every listed member was decoded to its end. */
} xx_ppmd;

typedef xx_ppmd xx_ppmd_t;

XXFC_API void xx_ppmd_init(xx_ppmd *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_ppmd *xx_ppmd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ppmd_destroy(xx_ppmd *archive);
XXFC_API void xx_ppmd_free(xx_ppmd *archive);

XXFC_API bool xx_ppmd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ppmd_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ppmd_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_ppmd_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ppmd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ppmd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ppmd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ppmd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ppmd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PPMD_H */

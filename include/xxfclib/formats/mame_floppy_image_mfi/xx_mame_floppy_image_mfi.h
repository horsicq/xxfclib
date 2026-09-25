/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MAME_FLOPPY_IMAGE_MFI_H
#define XXFCLIB_FORMAT_MAME_FLOPPY_IMAGE_MFI_H

#include "xxfclib/formats/xx_format.h"

/* MAME floppy image (MFI, "MAMEFLOPPYIMAGE" or the older "MESSFLOPPYIMAGE").
 * A 32-byte header, a table of 16-byte track entries and one zlib stream of
 * flux cells per track.  When the tracks carry IBM FM/MFM sectors the only
 * member is the decoded flat sector image "image.img"; otherwise each
 * formatted track is a member holding its decompressed cell list. */
typedef struct xx_mame_floppy_image_mfi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    /* Result of the last full track scan, reused by later calls.  Private. */
    bool scanned;
    bool has_image;
    uint32_t image_size_code;
    uint32_t image_cylinders;
    uint32_t image_heads;
    uint32_t image_sectors;
    int32_t image_base[2];
} xx_mame_floppy_image_mfi;

XXFC_API void xx_mame_floppy_image_mfi_init(xx_mame_floppy_image_mfi *archive,
                                            xx_io_device *device,
                                            int64_t base_address);
XXFC_API xx_mame_floppy_image_mfi *xx_mame_floppy_image_mfi_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_mame_floppy_image_mfi_destroy(
    xx_mame_floppy_image_mfi *archive);
XXFC_API void xx_mame_floppy_image_mfi_free(xx_mame_floppy_image_mfi *archive);
XXFC_API bool xx_mame_floppy_image_mfi_check_is_valid(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API bool xx_mame_floppy_image_mfi_handle_base_info(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API int64_t xx_mame_floppy_image_mfi_get_format_size(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_mame_floppy_image_mfi_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_mame_floppy_image_mfi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_mame_floppy_image_mfi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mame_floppy_image_mfi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mame_floppy_image_mfi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mame_floppy_image_mfi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif

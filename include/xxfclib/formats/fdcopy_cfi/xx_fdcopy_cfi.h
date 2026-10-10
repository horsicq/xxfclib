/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fdcopy_cfi.h @brief FDCOPY compressed floppy image reader.
 *
 * Reconstructs the complete raw disk and each separately stored track from
 * FDCOPY.COM's `.CFI` track-local RLE/literal stream. CFI has no magic or
 * recorded track count, so callers must select it explicitly by type/name;
 * do not auto-detect arbitrary binary input as CFI. This reader accepts up
 * to 256 uniformly sized 512-byte-sector tracks, 32 KiB decoded per track,
 * 4 MiB raw image and 6 MiB source. No unsupported missing-track zero fill.
 * MEMORY_LIMIT governs retained view and the 96 KiB extraction workspace.
 */
#ifndef XXFCLIB_FORMAT_FDCOPY_CFI_H
#define XXFCLIB_FORMAT_FDCOPY_CFI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fdcopy_cfi {
    Abstractformat format;
    uint32_t number_of_members;
    void *internal;
} xx_fdcopy_cfi;
typedef xx_fdcopy_cfi xx_fdcopy_cfi_t;
XXFC_API void xx_fdcopy_cfi_init(xx_fdcopy_cfi *, xx_io_device *, int64_t);
XXFC_API xx_fdcopy_cfi *xx_fdcopy_cfi_create(xx_io_device *, int64_t);
XXFC_API void xx_fdcopy_cfi_destroy(xx_fdcopy_cfi *);
XXFC_API void xx_fdcopy_cfi_free(xx_fdcopy_cfi *);
XXFC_API bool xx_fdcopy_cfi_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_fdcopy_cfi_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_fdcopy_cfi_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_fdcopy_cfi_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_fdcopy_cfi_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_fdcopy_cfi_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_fdcopy_cfi_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_fdcopy_cfi_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_fdcopy_cfi_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_fdcopy_cfi_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_fdcopy_cfi_to_format(xx_fdcopy_cfi *c)
{
    return c ? &c->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_maxis_far_archive.h @brief Maxis FAR archive reader (The Sims). */

#ifndef XXFCLIB_FORMAT_MAXIS_FAR_ARCHIVE_H
#define XXFCLIB_FORMAT_MAXIS_FAR_ARCHIVE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Maxis "FAR" archive, version 1 (The Sims, The Sims Online).
 *
 * All fields are little-endian.
 *
 *   0x00  char[8] "FAR!byAZ"
 *   0x08  u32     version, 1
 *   0x0C  u32     manifest offset (the manifest normally closes the file)
 *   ...   member data, stored uncompressed
 *   manifest:
 *     u32 member count, then per member:
 *       u32 size, u32 size again (equal: version 1 does not compress),
 *       u32 data offset from the start of the archive,
 *       name length: u32 in variant "1a" (The Sims), u16 in "1b" (TSO),
 *       name bytes (no terminator; '\\' or '/' separate directories).
 *
 * The variant is not recorded anywhere; the reader walks the manifest with
 * 32-bit name lengths first and falls back to 16-bit ones.  A 1b manifest
 * cannot pass as 1a: the wide length would swallow two name bytes and point
 * far past the manifest.  Version 3 (TSO, RefPack-compressed members) is
 * not handled.
 */
typedef struct xx_maxis_far_archive {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t manifest_offset;
    uint32_t name_length_width; /**< 4 for variant 1a, 2 for 1b. */
} xx_maxis_far_archive;

typedef xx_maxis_far_archive xx_maxis_far_archive_t;

XXFC_API void xx_maxis_far_archive_init(xx_maxis_far_archive *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_maxis_far_archive *xx_maxis_far_archive_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_maxis_far_archive_destroy(xx_maxis_far_archive *archive);
XXFC_API void xx_maxis_far_archive_free(xx_maxis_far_archive *archive);

XXFC_API bool xx_maxis_far_archive_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_maxis_far_archive_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_maxis_far_archive_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_maxis_far_archive_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_maxis_far_archive_create_archive_records_reading(Abstractformat *self,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_maxis_far_archive_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_maxis_far_archive_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_maxis_far_archive_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_maxis_far_archive_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MAXIS_FAR_ARCHIVE_H */

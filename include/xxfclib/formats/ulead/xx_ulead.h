/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ulead.h @brief ULEAD ("U_LEAD CORP.") container reader. */

#ifndef XXFCLIB_FORMAT_ULEAD_H
#define XXFCLIB_FORMAT_ULEAD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration pending.
 *
 * xxfc_defs.h carries no XX_FILE_TYPE_ULEAD enumerator yet, and that header
 * is shared, so it is not edited from here. The provisional number below
 * keeps this reader self-contained and compilable; the integrator adding the
 * real enumerator should redefine this to it. The value is NOT part of the
 * on-disk format. */
#ifndef XX_ULEAD_FILE_TYPE_ID
#define XX_ULEAD_FILE_TYPE_ID XX_FILE_TYPE_ULEAD
#endif

/**
 * @brief A ULEAD container: a "U_LEAD CORP." header carrying a block table,
 *        then the blocks. Exactly one member, in one of two header layouts --
 *        only the longer of the two stores a file name.
 */
typedef struct xx_ulead {
    Abstractformat format;
    uint64_t number_of_records;
    /** The member's plaintext length; 0 until parsed. */
    uint64_t uncompressed_size;
    /** Header layout, 1 or 2; 0 until parsed. Layout 2 stores a name. */
    uint32_t layout;
    /** Number of 0x4000-byte blocks the member is cut into. */
    uint32_t block_count;
} xx_ulead;

typedef xx_ulead xx_ulead_t;

XXFC_API void xx_ulead_init(xx_ulead *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_ulead *xx_ulead_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_ulead_destroy(xx_ulead *archive);
XXFC_API void xx_ulead_free(xx_ulead *archive);

XXFC_API bool xx_ulead_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ulead_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_ulead_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_ulead_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ulead_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ulead_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ulead_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ulead_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ulead_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ULEAD_H */

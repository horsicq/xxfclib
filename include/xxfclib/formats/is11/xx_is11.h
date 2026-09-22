/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_is11.h
 *  @brief InstallShield 11/13-era compressed install file (*.EX$, *.CMP).
 */

#ifndef XXFCLIB_FORMAT_IS11_H
#define XXFCLIB_FORMAT_IS11_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The *.??$ / *.CMP data files shipped on 16-bit InstallShield distribution
 * disks.  One container, two generations: generation 1 carries a headerless
 * 12-bit block-mode LZW stream per member, generation 3 carries a raw PKWARE
 * DCL "implode" stream and four more header bytes for the DOS timestamp. */
typedef struct xx_is11 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t generation; /**< 1 or 3, from the header's format word. */
    uint8_t variant;     /**< 1 or 2, from the header's variant byte. */
} xx_is11;

typedef xx_is11 xx_is11_t;
typedef xx_is11 XIS11;

XXFC_API void xx_is11_init(xx_is11 *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_is11 *xx_is11_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_is11_destroy(xx_is11 *archive);
XXFC_API void xx_is11_free(xx_is11 *archive);

XXFC_API bool xx_is11_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_is11_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_is11_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_is11_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_is11_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_is11_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_is11_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_is11_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_is11_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_is11_to_format(xx_is11 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IS11_H */

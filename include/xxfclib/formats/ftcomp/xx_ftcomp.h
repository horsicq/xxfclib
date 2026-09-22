/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ftcomp.h @brief FTCOMP ("A5 96 FD FF") packed-file reader. */

#ifndef XXFCLIB_FORMAT_FTCOMP_H
#define XXFCLIB_FORMAT_FTCOMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An FTCOMP container.
 *
 * FTCOMP is the 0xFFFD dialect of the OS/2 "A5 96" packed-file family (see
 * xx_ibmpack.h for the 0x0A14, 0xFFFE and 0xFFFF dialects, which this reader
 * deliberately does not claim).  One file holds one or more members chained
 * through the u32 at offset 0x14; each member header carries the literal
 * string "FTCOMP" at offset 0x18.
 */
typedef struct xx_ftcomp {
    Abstractformat format;
    uint64_t number_of_records;
    /** Dialect word from offset 2 of the first member header (0xFFFD). */
    uint32_t variant;
} xx_ftcomp;

typedef xx_ftcomp xx_ftcomp_t;

XXFC_API void xx_ftcomp_init(xx_ftcomp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ftcomp *xx_ftcomp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ftcomp_destroy(xx_ftcomp *archive);
XXFC_API void xx_ftcomp_free(xx_ftcomp *archive);

XXFC_API bool xx_ftcomp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ftcomp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ftcomp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ftcomp_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ftcomp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ftcomp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ftcomp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ftcomp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ftcomp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FTCOMP_H */

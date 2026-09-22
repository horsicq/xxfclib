/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_corelltec.h @brief Corel / LEAD "LTEC" install archive (*.lta). */

#ifndef XXFCLIB_FORMAT_CORELLTEC_H
#define XXFCLIB_FORMAT_CORELLTEC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Corel/LEAD LTEC installer archive: a nine-byte header, a directory
 *        of variable-length records that runs straight into the payload, then
 *        SOLID LZH blocks.
 *
 * A member is a byte range inside its block's PLAINTEXT, and one block
 * routinely carries a dozen members, so the packed size published for a member
 * is its whole block's -- the format stores no per-member packed size and
 * inventing one by division would be a lie.
 */
typedef struct xx_corelltec {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_blocks;
} xx_corelltec;

typedef xx_corelltec xx_corelltec_t;

XXFC_API void xx_corelltec_init(xx_corelltec *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_corelltec *xx_corelltec_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_corelltec_destroy(xx_corelltec *archive);
XXFC_API void xx_corelltec_free(xx_corelltec *archive);

XXFC_API bool xx_corelltec_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_corelltec_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_corelltec_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_corelltec_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_corelltec_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_corelltec_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_corelltec_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_corelltec_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_corelltec_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CORELLTEC_H */

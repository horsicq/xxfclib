/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dsl2.h @brief DS'L install 2.0 archive reader. */

#ifndef XXFCLIB_FORMAT_DSL2_H
#define XXFCLIB_FORMAT_DSL2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DS'L install 2.0 installer container: a 28-byte header, two length-prefixed install paths, an obfuscated directory of variable-length records, and a data area whose members are either stored or PKWARE DCL imploded.
 */
typedef struct xx_dsl2 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_dsl2;

typedef xx_dsl2 xx_dsl2_t;

XXFC_API void xx_dsl2_init(xx_dsl2 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_dsl2 *xx_dsl2_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_dsl2_destroy(xx_dsl2 *archive);
XXFC_API void xx_dsl2_free(xx_dsl2 *archive);

XXFC_API bool xx_dsl2_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_dsl2_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_dsl2_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_dsl2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dsl2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dsl2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dsl2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dsl2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dsl2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DSL2_H */

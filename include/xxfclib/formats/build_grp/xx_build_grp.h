/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_build_grp.h @brief Build GRP archive reader. */

#ifndef XXFCLIB_FORMAT_BUILD_GRP_H
#define XXFCLIB_FORMAT_BUILD_GRP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Build GRP container. */
typedef struct xx_build_grp {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_build_grp;

typedef xx_build_grp xx_build_grp_t;

XXFC_API void xx_build_grp_init(xx_build_grp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_build_grp *xx_build_grp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_build_grp_destroy(xx_build_grp *archive);
XXFC_API void xx_build_grp_free(xx_build_grp *archive);

XXFC_API bool xx_build_grp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_build_grp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_build_grp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_build_grp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_build_grp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_build_grp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_build_grp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_build_grp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_build_grp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BUILD_GRP_H */

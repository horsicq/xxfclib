/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_gxl.h
 * @brief GX Library / Genus Graphics Library (.GXL) container reader.
 */

#ifndef XXFCLIB_FORMAT_GXL_H
#define XXFCLIB_FORMAT_GXL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gxl {
    Abstractformat format;      /**< Base format structure (first member). */
    uint64_t number_of_records; /**< Member count taken from the header. */
    uint16_t format_version;    /**< gxLib version field; 100 in every known build. */
} xx_gxl;

XXFC_API void xx_gxl_init(xx_gxl *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_gxl *xx_gxl_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_gxl_destroy(xx_gxl *archive);
XXFC_API void xx_gxl_free(xx_gxl *archive);
XXFC_API bool xx_gxl_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gxl_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gxl_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_gxl_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_gxl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gxl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gxl_unpack_current_archive_record(Abstractformat *self,
                                                   xx_archive_record_state *state,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_gxl_archive_record_move_to_next(Abstractformat *self,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd);
XXFC_API void xx_gxl_free_archive_records_reading(Abstractformat *self,
                                                  xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GXL_H */

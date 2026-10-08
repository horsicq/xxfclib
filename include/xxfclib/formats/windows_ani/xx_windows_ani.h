/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_windows_ani.h @brief Windows RIFF/ACON animated cursor frames. */
#ifndef XXFCLIB_FORMAT_WINDOWS_ANI_H
#define XXFCLIB_FORMAT_WINDOWS_ANI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_windows_ani {
    Abstractformat format;
} xx_windows_ani;

XXFC_API void xx_windows_ani_init(xx_windows_ani *reader,
                                   xx_io_device *device, int64_t base_address);
XXFC_API xx_windows_ani *xx_windows_ani_create(xx_io_device *device,
                                                int64_t base_address);
XXFC_API void xx_windows_ani_destroy(xx_windows_ani *reader);
XXFC_API void xx_windows_ani_free(xx_windows_ani *reader);
XXFC_API bool xx_windows_ani_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_windows_ani_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_windows_ani_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_windows_ani_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_windows_ani_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_windows_ani_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_windows_ani_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_windows_ani_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_windows_ani_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_windows_ani_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_windows_ani_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_windows_ani_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

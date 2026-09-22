/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_xeditpack.h @brief XEDIT PACK file reader. */

#ifndef XXFCLIB_FORMAT_XEDITPACK_H
#define XXFCLIB_FORMAT_XEDITPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CMS XEDIT PACK / COPYFILE PACK file: an eight-byte header naming the record format, followed by a single run/literal coded EBCDIC stream that runs to end-of-file.
 */
typedef struct xx_xeditpack {
    Abstractformat format;
    uint64_t number_of_records;
} xx_xeditpack;

typedef xx_xeditpack xx_xeditpack_t;

XXFC_API void xx_xeditpack_init(xx_xeditpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_xeditpack *xx_xeditpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_xeditpack_destroy(xx_xeditpack *archive);
XXFC_API void xx_xeditpack_free(xx_xeditpack *archive);

XXFC_API bool xx_xeditpack_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_xeditpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_xeditpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_xeditpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xeditpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xeditpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xeditpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xeditpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xeditpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XEDITPACK_H */

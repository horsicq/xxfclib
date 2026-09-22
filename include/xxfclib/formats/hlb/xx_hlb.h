/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_hlb.h @brief HLB library reader. */

#ifndef XXFCLIB_FORMAT_HLB_H
#define XXFCLIB_FORMAT_HLB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An HLB library: a trailing directory of fixed 18-byte entries, each giving only a start offset and a 14-byte name. Member sizes are implicit -- a member runs until the next member's offset, and the last one runs until the directory.
 */
typedef struct xx_hlb {
    Abstractformat format;
    uint64_t number_of_records;
} xx_hlb;

typedef xx_hlb xx_hlb_t;

XXFC_API void xx_hlb_init(xx_hlb *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_hlb *xx_hlb_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_hlb_destroy(xx_hlb *archive);
XXFC_API void xx_hlb_free(xx_hlb *archive);

XXFC_API bool xx_hlb_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_hlb_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_hlb_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_hlb_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hlb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hlb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hlb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hlb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hlb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HLB_H */

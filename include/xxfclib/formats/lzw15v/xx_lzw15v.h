/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzw15v.h @brief Raw LZW15V single-member compressed file reader. */

#ifndef XXFCLIB_FORMAT_LZW15V_H
#define XXFCLIB_FORMAT_LZW15V_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A headerless LZW15V stream as a one-member container - the DOS-era
 * "truncated extension" install payloads (WIPEOUT.EX_, BILLBD.DL_).  There is
 * no magic, no size field and no checksum: byte 0 is already the first 9-bit
 * code, and the only detector is a complete decode of the whole file.
 */
typedef struct xx_lzw15v {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t uncompressed_size; /**< measured plaintext size, -1 if unknown */
} xx_lzw15v;

typedef xx_lzw15v xx_lzw15v_t;

XXFC_API void xx_lzw15v_init(xx_lzw15v *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lzw15v *xx_lzw15v_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lzw15v_destroy(xx_lzw15v *archive);
XXFC_API void xx_lzw15v_free(xx_lzw15v *archive);

XXFC_API bool xx_lzw15v_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzw15v_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lzw15v_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lzw15v_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzw15v_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzw15v_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzw15v_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzw15v_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzw15v_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZW15V_H */

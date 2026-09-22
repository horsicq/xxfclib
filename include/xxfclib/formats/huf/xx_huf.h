/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_huf.h @brief HUF multi-file shared-Huffman archive reader. */

#ifndef XXFCLIB_FORMAT_HUF_H
#define XXFCLIB_FORMAT_HUF_H

#include "xxfclib/algo/huf/xx_huf.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_huf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_huf;

typedef xx_huf xx_huf_t;
typedef xx_huf XHuff;

XXFC_API void xx_huf_init(xx_huf *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_huf *xx_huf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_huf_destroy(xx_huf *archive);
XXFC_API void xx_huf_free(xx_huf *archive);

XXFC_API bool xx_huf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_huf_handle_base_info(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API int64_t xx_huf_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_huf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_huf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_huf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_huf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_huf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_huf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HUF_H */

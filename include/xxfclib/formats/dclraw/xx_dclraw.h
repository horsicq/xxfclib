/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_DCLRAW_H
#define XXFCLIB_FORMAT_DCLRAW_H

#include "xxfclib/formats/xx_format.h"

/* Two or more raw PKWARE Data Compression Library (implode) streams laid back
 * to back with nothing between them (an installer data volume). One record
 * per stream, named data_00000, data_00001, ... in file order. A file holding
 * a single stream is refused: that is the dclft reader's "DCLStream". */
typedef struct xx_dclraw {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_dclraw;

XXFC_API void xx_dclraw_init(xx_dclraw *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_dclraw *xx_dclraw_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dclraw_destroy(xx_dclraw *archive);
XXFC_API void xx_dclraw_free(xx_dclraw *archive);
XXFC_API bool xx_dclraw_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dclraw_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dclraw_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_dclraw_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_dclraw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dclraw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dclraw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dclraw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dclraw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_ARJ_H
#define XXFCLIB_FORMAT_ARJ_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

typedef struct xx_arj xx_arj;
typedef struct xx_arj xx_arj_t;
typedef struct xx_arj XArj;

struct xx_arj {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
};

XXFC_API void xx_arj_init(xx_arj *arj, xx_io_device *dev, int64_t base_address);
XXFC_API xx_arj *xx_arj_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_arj_destroy(xx_arj *arj);
XXFC_API void xx_arj_free(xx_arj *arj);
XXFC_API bool xx_arj_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_arj_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_arj_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_arj_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_arj_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_arj_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arj_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arj_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arj_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#endif

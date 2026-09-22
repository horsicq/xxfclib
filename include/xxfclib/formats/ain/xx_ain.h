/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_AIN_H
#define XXFCLIB_FORMAT_AIN_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_ain { Abstractformat format; uint64_t number_of_records; int64_t archive_end; } xx_ain;
XXFC_API void xx_ain_init(xx_ain *archive,xx_io_device *device,int64_t base_address);
XXFC_API xx_ain *xx_ain_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_ain_destroy(xx_ain *archive);
XXFC_API void xx_ain_free(xx_ain *archive);
XXFC_API bool xx_ain_check_is_valid(Abstractformat *self,xx_pd_struct *pd);
XXFC_API bool xx_ain_handle_base_info(Abstractformat *self,xx_pd_struct *pd);
XXFC_API int64_t xx_ain_get_format_size(Abstractformat *self,xx_pd_struct *pd);
XXFC_API uint64_t xx_ain_get_number_of_archive_records(Abstractformat *self,xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_ain_create_archive_records_reading(Abstractformat *self,const xx_list_s *options,xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ain_get_current_archive_record(Abstractformat *self,xx_archive_record_state *state);
XXFC_API bool xx_ain_unpack_current_archive_record(Abstractformat *self,xx_archive_record_state *state,xx_pd_struct *pd);
XXFC_API bool xx_ain_archive_record_move_to_next(Abstractformat *self,xx_archive_record_state *state,xx_pd_struct *pd);
XXFC_API void xx_ain_free_archive_records_reading(Abstractformat *self,xx_archive_record_state *state);
#endif

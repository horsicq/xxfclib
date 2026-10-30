/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_DATA_INTERNAL_H
#define XXFCLIB_TAR_DATA_INTERNAL_H

#include "xxfclib/formats/archive/tar/xx_tar.h"

const char *xx_tar_data_struct_id_to_string(Abstractformat *self, uint32_t id);
uint32_t xx_tar_data_struct_string_to_id(Abstractformat *self, const char *name);
xx_data_struct_state *xx_tar_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
const xx_data_struct *xx_tar_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
bool xx_tar_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
void xx_tar_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);
xx_data_struct_record_state *xx_tar_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
const xx_data_struct_record *xx_tar_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
bool xx_tar_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
void xx_tar_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

#endif /* XXFCLIB_TAR_DATA_INTERNAL_H */

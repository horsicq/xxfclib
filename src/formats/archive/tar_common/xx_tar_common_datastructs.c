/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_tar_common_data.h"
#include "../tar/xx_tar_data.h"

const char *xx_tar_common_data_struct_id_to_string(xx_tar_common *common, uint32_t id)
{
    return xx_tar_data_struct_id_to_string(common && common->tar ? &common->tar->format : NULL, id);
}

uint32_t xx_tar_common_data_struct_string_to_id(xx_tar_common *common, const char *name)
{
    return xx_tar_data_struct_string_to_id(common && common->tar ? &common->tar->format : NULL, name);
}

xx_data_struct_state *xx_tar_common_create_data_structs_reading(xx_tar_common *common, Abstractformat *outer, xx_tar_common_decode_fn decode, xx_pd_struct *pd)
{
    if (!outer || (!outer->base_info_handled && !xx_tar_common_handle_base_info(common, outer, decode, pd)) || !common || !common->tar) {
        return NULL;
    }
    return xx_tar_create_data_structs_reading(&common->tar->format, pd);
}

const xx_data_struct *xx_tar_common_get_current_data_struct(xx_tar_common *common, xx_data_struct_state *state)
{
    return common && common->tar ? xx_tar_get_current_data_struct(&common->tar->format, state) : NULL;
}

bool xx_tar_common_data_struct_move_to_next(xx_tar_common *common, xx_data_struct_state *state, xx_pd_struct *pd)
{
    return common && common->tar && xx_tar_data_struct_move_to_next(&common->tar->format, state, pd);
}

void xx_tar_common_free_data_structs_reading(xx_tar_common *common, xx_data_struct_state *state)
{
    if (common && common->tar) {
        xx_tar_free_data_structs_reading(&common->tar->format, state);
    } else {
        xx_data_struct_state_free(state);
    }
}

xx_data_struct_record_state *xx_tar_common_create_data_struct_records_reading(xx_tar_common *common, const xx_data_struct *data_struct, xx_pd_struct *pd)
{
    return common && common->tar ? xx_tar_create_data_struct_records_reading(&common->tar->format, data_struct, pd) : NULL;
}

const xx_data_struct_record *xx_tar_common_get_current_data_struct_record(xx_tar_common *common, xx_data_struct_record_state *state)
{
    return common && common->tar ? xx_tar_get_current_data_struct_record(&common->tar->format, state) : NULL;
}

bool xx_tar_common_data_struct_record_move_to_next(xx_tar_common *common, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    return common && common->tar && xx_tar_data_struct_record_move_to_next(&common->tar->format, state, pd);
}

void xx_tar_common_free_data_struct_records_reading(xx_tar_common *common, xx_data_struct_record_state *state)
{
    if (common && common->tar) {
        xx_tar_free_data_struct_records_reading(&common->tar->format, state);
    } else {
        xx_data_struct_record_state_free(state);
    }
}


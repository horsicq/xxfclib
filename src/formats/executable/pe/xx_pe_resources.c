/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pe_resources_internal.h"

xx_resource_state *xx_pe_create_resources_reading(Abstractformat *format, xx_pd_struct *pd)
{
    return pe_resource_stream_create(format, pd);
}

const xx_resource_record *xx_pe_get_current_resource(Abstractformat *format, xx_resource_state *state)
{
    return pe_resource_stream_current(format, state);
}

bool xx_pe_resource_move_to_next(Abstractformat *format, xx_resource_state *state, xx_pd_struct *pd)
{
    return pe_resource_stream_next(format, state, pd);
}

void xx_pe_free_resources_reading(Abstractformat *format, xx_resource_state *state)
{
    pe_resource_stream_free(format, state);
}

uint64_t xx_pe_get_number_of_resources(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_pe_handle_base_info(format, pd)) ? format->number_of_resources : 0U;
}

const StreamResources *xx_pe_get_stream_resources(void)
{
    static const StreamResources stream = {.get_number_of_resources = xx_pe_get_number_of_resources,
                                           .create_resources_reading = xx_pe_create_resources_reading,
                                           .get_current_resource = xx_pe_get_current_resource,
                                           .resource_move_to_next = xx_pe_resource_move_to_next,
                                           .free_resources_reading = xx_pe_free_resources_reading};
    return &stream;
}

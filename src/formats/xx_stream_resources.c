/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

void xx_resource_record_init(xx_resource_record *r)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->type_id = r->name_id = r->language_id = UINT32_MAX;
    r->offset = -1;
}
void xx_resource_record_cleanup(xx_resource_record *r)
{
    if (!r) return;
    xx_mem_free(r->type_name);
    xx_mem_free(r->name);
    xx_mem_free(r->language_name);
    xx_resource_record_init(r);
}

#include "xx_stream_runtime_internal.h"

XX_STREAM_RUNTIME(resource, resources, StreamResources, xx_format_get_stream_resources)
#undef XX_STREAM_RUNTIME

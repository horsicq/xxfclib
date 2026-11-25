/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

void xx_export_record_init(xx_export_record *r)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->offset = -1;
    r->address = UINT64_MAX;
}
void xx_export_record_cleanup(xx_export_record *r)
{
    if (!r) return;
    xx_mem_free(r->name);
    xx_mem_free(r->forwarder);
    xx_export_record_init(r);
}

#include "xx_stream_runtime_internal.h"

XX_STREAM_RUNTIME(export, exports, StreamExport, xx_format_get_stream_export)
#undef XX_STREAM_RUNTIME

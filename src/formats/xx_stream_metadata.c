/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

void xx_metadata_record_init(xx_metadata_record *r)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    xx_var_init(&r->value);
    r->offset = -1;
}
void xx_metadata_record_cleanup(xx_metadata_record *r)
{
    if (!r) return;
    xx_mem_free(r->key);
    xx_var_cleanup(&r->value);
    xx_metadata_record_init(r);
}

#include "xx_stream_runtime_internal.h"

XX_STREAM_RUNTIME(metadata, metadata, StreamMetadata, xx_format_get_stream_metadata)
#undef XX_STREAM_RUNTIME

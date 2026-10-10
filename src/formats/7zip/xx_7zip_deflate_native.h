/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * 7z Deflate (04 01 08) and Deflate64 (04 01 09) coder adapter. Method IDs
 * are defined by the official 7-Zip DOC/Methods.txt. This delegates the
 * actual bitstream to xxfclib's existing native Deflate/Deflate64 decoder.
 */
#ifndef XX_7ZIP_DEFLATE_NATIVE_H
#define XX_7ZIP_DEFLATE_NATIVE_H
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/io/xx_io.h"
#include <stddef.h>
#include <stdint.h>

static bool xx_7zip_deflate_native(const uint8_t *packed, size_t packed_size, uint8_t *output, size_t output_size, bool deflate64, xx_pd_struct *pd)
{
    xx_io_device *sink;
    size_t consumed = 0U;
    bool ok;
    if (!packed || !packed_size || (!output && output_size) || xx_pd_is_stopped(pd)) return false;
#if SIZE_MAX > INT64_MAX
    if (packed_size > (size_t)INT64_MAX || output_size > (size_t)INT64_MAX) return false;
#endif
    sink = xx_io_mem_open(output, output_size);
    if (!sink) return false;
    ok = xx_deflate_unpack_memory_to_device_ex(packed, packed_size, sink, &consumed, deflate64, pd) && consumed == packed_size &&
         xx_io_tell(sink) == (int64_t)output_size && !xx_pd_is_stopped(pd);
    xx_io_close(sink);
    return ok;
}
#endif

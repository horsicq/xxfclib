/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFU_NATIVE_FILTER_FORMATS_H
#define XFU_NATIVE_FILTER_FORMATS_H

#include "core.h"
#include <xxfclib/io/xx_io.h>

/* Encode a complete stream in a supported outer envelope. The caller owns
 * input/output staging and supplies an input rewound to offset zero. */
bool xfu_native_filter_can_encode(const char *name);
/* stream_name: the member's base name for formats that record one (uuencode,
 * Base64, lzop); NULL writes the unnamed "payload" of a filtered archive. */
bool xfu_native_filter_encode(const xfu_request *request, const char *name,
                              const char *stream_name,
                              xx_io_device *input, xx_io_device *output,
                              xx_pd_struct *pd);

#endif

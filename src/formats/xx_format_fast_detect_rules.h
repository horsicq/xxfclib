/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_FORMAT_FAST_DETECT_RULES_H
#define XX_FORMAT_FAST_DETECT_RULES_H
#include "xxfclib/formats/xx_format.h"

/* Internal signature result: 1 matches a declared format, 0 rejects all
 * declared formats, -1 needs the existing structural reader probe. The
 * device cursor is preserved; no readers, payloads or temporary files are
 * opened on the signature path. */
int xx_format_fast_detect_rules(const xx_file_type_t *types, size_t type_count,
                                xx_io_device *device, int64_t base_address,
                                bool is_mapped);
#endif

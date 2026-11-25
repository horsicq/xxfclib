/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_DIE_ENGINE_ENGINE_H
#define XX_DIE_ENGINE_ENGINE_H

#include "xxfclib/die_engine/die_engine.h"

xx_file_type_t die_engine_native_from_xft(XFileType type);
XFileType die_engine_xft_from_native(xx_file_type_t type);
int die_engine_scan_pass_device(xx_io_device *device, DBase *database, xx_scan_options *options, XFileType type, xx_scan_result *result, xx_pd_struct *pd);

#endif

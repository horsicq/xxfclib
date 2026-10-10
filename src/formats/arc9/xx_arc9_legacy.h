/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ARC9_LEGACY_H
#define XX_ARC9_LEGACY_H
#include "xxfclib/formats/xx_format.h"
Abstractformat *xx_arc9_legacy_create(xx_io_device *,int64_t,xx_file_type_t);
void xx_arc9_legacy_free(Abstractformat *);
bool xx_arc9_legacy_probe(xx_io_device *,int64_t,xx_file_type_t);
xx_file_type_t xx_arc9_legacy_detect(xx_io_device *,int64_t);
#endif

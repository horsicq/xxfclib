/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ARC9_COMPAT_H
#define XX_ARC9_COMPAT_H
#include "xxfclib/formats/xx_format.h"
Abstractformat *xx_arc9_compat_create(xx_io_device *,int64_t,xx_file_type_t);
void xx_arc9_compat_free(Abstractformat *);
bool xx_arc9_compat_probe(xx_io_device *,int64_t,xx_file_type_t);
xx_file_type_t xx_arc9_compat_detect(xx_io_device *,int64_t);
#endif

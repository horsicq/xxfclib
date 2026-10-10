/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ARC9_COMPRESSED_H
#define XX_ARC9_COMPRESSED_H
#include "xxfclib/formats/xx_format.h"
Abstractformat *xx_arc9_compressed_create(xx_io_device *, int64_t, xx_file_type_t);
void xx_arc9_compressed_free(Abstractformat *);
bool xx_arc9_compressed_probe(xx_io_device *, int64_t, xx_file_type_t);
xx_file_type_t xx_arc9_compressed_detect(xx_io_device *,int64_t);
#endif

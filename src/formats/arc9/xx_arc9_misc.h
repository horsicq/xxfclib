/* SPDX-License-Identifier: MIT */
#ifndef XX_ARC9_MISC_H
#define XX_ARC9_MISC_H
#include "xxfclib/formats/xx_format.h"
Abstractformat *xx_arc9_misc_create(xx_io_device *,int64_t,xx_file_type_t);
void xx_arc9_misc_free(Abstractformat *);
bool xx_arc9_misc_probe(xx_io_device *,int64_t,xx_file_type_t);
xx_file_type_t xx_arc9_misc_detect(xx_io_device *,int64_t);
#endif

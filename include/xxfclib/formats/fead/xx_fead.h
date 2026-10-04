/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT */
#ifndef XX_FEAD_H
#define XX_FEAD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_fead_create(xx_io_device *, int64_t);
XXFC_API void xx_fead_free(Abstractformat *);
XXFC_API xx_file_type_t xx_fead_detect_device(xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

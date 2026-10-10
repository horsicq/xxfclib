/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * General CFBF and Office Binder share their checked compound-file parser. */
#ifndef XX_CFBF_H
#define XX_CFBF_H
#include "xxfclib/formats/binder/xx_binder.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API xx_file_type_t xx_cfbf_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif

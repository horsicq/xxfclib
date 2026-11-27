/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PY2EXE_H
#define XX_PY2EXE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_py2exe {
    Abstractformat format;
} xx_py2exe;

XXFC_API void xx_py2exe_init(xx_py2exe *reader, xx_io_device *device, int64_t base);
XXFC_API xx_py2exe *xx_py2exe_create(xx_io_device *device, int64_t base);
XXFC_API void xx_py2exe_destroy(xx_py2exe *reader);
XXFC_API void xx_py2exe_free(xx_py2exe *reader);
XXFC_API bool xx_py2exe_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif

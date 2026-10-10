/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_EXCELSIOR_H
#define XX_EXCELSIOR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* ExcelsiorII1 solid LZMA1 + x86 BCJ installer payload; borrowed source IO.
 * Version-dependent24/32-byte settings and Win32/native directory records.
 * TEST decodes bounded RAM; no installer script or binary executes. */
XXFC_API Abstractformat *xx_excelsior_create(xx_io_device *, int64_t);
XXFC_API void xx_excelsior_free(Abstractformat *);
XXFC_API xx_file_type_t xx_excelsior_detect_device(xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

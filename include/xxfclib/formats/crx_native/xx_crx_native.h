/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRX_NATIVE_H
#define XX_CRX_NATIVE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_crx_native_create(xx_io_device *, int64_t);
XXFC_API void xx_crx_native_free(Abstractformat *);
XXFC_API xx_file_type_t xx_crx_native_detect(xx_io_device *, int64_t);
XXFC_API bool xx_crx_native_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_crx_native_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_crx_native_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_crx_native_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_crx_native_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif /* XX_CRX_NATIVE_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CSQ_H
#define XX_CSQ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_csq_create(xx_io_device *, int64_t);
XXFC_API void xx_csq_free(Abstractformat *);
XXFC_API xx_file_type_t xx_csq_detect(xx_io_device *, int64_t);
XXFC_API bool xx_csq_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_csq_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_csq_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_csq_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_csq_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif /* XX_CSQ_H */

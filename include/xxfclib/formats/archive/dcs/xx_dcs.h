/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_DCS_H
#define XX_DCS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_dcs_create(xx_io_device *, int64_t);
XXFC_API void xx_dcs_free(Abstractformat *);
XXFC_API xx_file_type_t xx_dcs_detect(xx_io_device *, int64_t);
XXFC_API bool xx_dcs_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_dcs_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_dcs_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_dcs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dcs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif /* XX_DCS_H */

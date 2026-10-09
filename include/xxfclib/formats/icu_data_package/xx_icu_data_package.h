/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/unicode-org/icu/blob/main/icu4c/source/common/udata.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_ICU_DATA_PACKAGE_H
#define XX_ICU_DATA_PACKAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_icu_data_package { Abstractformat format; } xx_icu_data_package;
XXFC_API void xx_icu_data_package_init(xx_icu_data_package *,xx_io_device *,int64_t);
XXFC_API xx_icu_data_package *xx_icu_data_package_create(xx_io_device *,int64_t);
XXFC_API void xx_icu_data_package_destroy(xx_icu_data_package *);
XXFC_API void xx_icu_data_package_free(xx_icu_data_package *);
XXFC_API bool xx_icu_data_package_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_icu_data_package_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_icu_data_package_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_icu_data_package_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_icu_data_package_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_icu_data_package_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_icu_data_package_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

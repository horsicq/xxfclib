/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_JAVA_CLASS_H
#define XX_JAVA_CLASS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_java_class { Abstractformat format; } xx_java_class;
XXFC_API void xx_java_class_init(xx_java_class *,xx_io_device *,int64_t);
XXFC_API xx_java_class *xx_java_class_create(xx_io_device *,int64_t);
XXFC_API void xx_java_class_destroy(xx_java_class *);
XXFC_API void xx_java_class_free(xx_java_class *);
XXFC_API bool xx_java_class_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_java_class_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_java_class_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_java_class_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_java_class_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_java_class_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_java_class_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

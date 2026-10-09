/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ANDROID_BINARY_XML_H
#define XX_ANDROID_BINARY_XML_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_android_binary_xml { Abstractformat format; } xx_android_binary_xml;
XXFC_API void xx_android_binary_xml_init(xx_android_binary_xml *,xx_io_device *,int64_t);
XXFC_API xx_android_binary_xml *xx_android_binary_xml_create(xx_io_device *,int64_t);
XXFC_API void xx_android_binary_xml_destroy(xx_android_binary_xml *);
XXFC_API void xx_android_binary_xml_free(xx_android_binary_xml *);
XXFC_API bool xx_android_binary_xml_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_android_binary_xml_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_android_binary_xml_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_android_binary_xml_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_android_binary_xml_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_android_binary_xml_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_android_binary_xml_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_JAVA_SERIALIZATION_H
#define XX_JAVA_SERIALIZATION_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_java_serialization {
    Abstractformat format;
} xx_java_serialization;
XXFC_API void xx_java_serialization_init(xx_java_serialization *, xx_io_device *, int64_t);
XXFC_API xx_java_serialization *xx_java_serialization_create(xx_io_device *, int64_t);
XXFC_API void xx_java_serialization_destroy(xx_java_serialization *);
XXFC_API void xx_java_serialization_free(xx_java_serialization *);
XXFC_API bool xx_java_serialization_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_java_serialization_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_java_serialization_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_java_serialization_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_java_serialization_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_java_serialization_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_java_serialization_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

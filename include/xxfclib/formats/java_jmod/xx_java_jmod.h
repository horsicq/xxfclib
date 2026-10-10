/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/openjdk/jdk/master/src/java.base/share/classes/jdk/internal/jmod/JmodFile.java */
#ifndef XX_JAVA_JMOD_H
#define XX_JAVA_JMOD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_java_jmod {
    Abstractformat format;
} xx_java_jmod;
XXFC_API void xx_java_jmod_init(xx_java_jmod *, xx_io_device *, int64_t);
XXFC_API xx_java_jmod *xx_java_jmod_create(xx_io_device *, int64_t);
XXFC_API void xx_java_jmod_destroy(xx_java_jmod *);
XXFC_API void xx_java_jmod_free(xx_java_jmod *);
XXFC_API bool xx_java_jmod_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_java_jmod_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_java_jmod_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_java_jmod_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_java_jmod_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_java_jmod_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_java_jmod_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

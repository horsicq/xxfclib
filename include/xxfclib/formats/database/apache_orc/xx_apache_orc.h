/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_APACHE_ORC_H
#define XX_APACHE_ORC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apache_orc {
    Abstractformat format;
} xx_apache_orc;
XXFC_API void xx_apache_orc_init(xx_apache_orc *, xx_io_device *, int64_t);
XXFC_API xx_apache_orc *xx_apache_orc_create(xx_io_device *, int64_t);
XXFC_API void xx_apache_orc_destroy(xx_apache_orc *);
XXFC_API void xx_apache_orc_free(xx_apache_orc *);
XXFC_API bool xx_apache_orc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apache_orc_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apache_orc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_apache_orc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apache_orc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apache_orc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_apache_orc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

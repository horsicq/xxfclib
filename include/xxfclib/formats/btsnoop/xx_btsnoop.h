/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BTSNOOP_H
#define XX_BTSNOOP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_btsnoop {
    Abstractformat format;
} xx_btsnoop;
XXFC_API void xx_btsnoop_init(xx_btsnoop *, xx_io_device *, int64_t);
XXFC_API xx_btsnoop *xx_btsnoop_create(xx_io_device *, int64_t);
XXFC_API void xx_btsnoop_destroy(xx_btsnoop *);
XXFC_API void xx_btsnoop_free(xx_btsnoop *);
XXFC_API bool xx_btsnoop_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_btsnoop_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_btsnoop_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_btsnoop_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_btsnoop_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_btsnoop_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_btsnoop_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Splashtop "unpackC" setup wrapper: files appended to the stub behind
 * 284-byte records (magic 4, item, size, cumulative size, name), closed by
 * an all-FF record.
 */
#ifndef XX_SPLASHTOP_UNPACKC_H
#define XX_SPLASHTOP_UNPACKC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_splashtop_unpackc {
    Abstractformat format;
} xx_splashtop_unpackc;
XXFC_API void xx_splashtop_unpackc_init(xx_splashtop_unpackc *, xx_io_device *, int64_t);
XXFC_API xx_splashtop_unpackc *xx_splashtop_unpackc_create(xx_io_device *, int64_t);
XXFC_API void xx_splashtop_unpackc_destroy(xx_splashtop_unpackc *);
XXFC_API void xx_splashtop_unpackc_free(xx_splashtop_unpackc *);
XXFC_API bool xx_splashtop_unpackc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_splashtop_unpackc_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_splashtop_unpackc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_splashtop_unpackc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_splashtop_unpackc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_splashtop_unpackc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_splashtop_unpackc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

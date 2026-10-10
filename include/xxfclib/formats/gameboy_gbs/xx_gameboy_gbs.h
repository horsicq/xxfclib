/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_GAMEBOY_GBS_H
#define XX_GAMEBOY_GBS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gameboy_gbs {
    Abstractformat format;
} xx_gameboy_gbs;
XXFC_API void xx_gameboy_gbs_init(xx_gameboy_gbs *, xx_io_device *, int64_t);
XXFC_API xx_gameboy_gbs *xx_gameboy_gbs_create(xx_io_device *, int64_t);
XXFC_API void xx_gameboy_gbs_destroy(xx_gameboy_gbs *);
XXFC_API void xx_gameboy_gbs_free(xx_gameboy_gbs *);
XXFC_API bool xx_gameboy_gbs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_gameboy_gbs_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gameboy_gbs_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gameboy_gbs_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gameboy_gbs_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gameboy_gbs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gameboy_gbs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

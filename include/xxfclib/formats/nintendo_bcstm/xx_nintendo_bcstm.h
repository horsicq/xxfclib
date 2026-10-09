/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://gota7.github.io/Citric-Composer/specs/common.html
 * NintendoWare endian-aware bounded section extraction only. Exports complete encoded INFO/SEEK/DATA sections; no audio decoding or Camelot malformed-size variants.
 */
#ifndef XX_NINTENDO_BCSTM_H
#define XX_NINTENDO_BCSTM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bcstm { Abstractformat format; } xx_nintendo_bcstm;
XXFC_API void xx_nintendo_bcstm_init(xx_nintendo_bcstm *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bcstm *xx_nintendo_bcstm_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bcstm_destroy(xx_nintendo_bcstm *);
XXFC_API void xx_nintendo_bcstm_free(xx_nintendo_bcstm *);
XXFC_API bool xx_nintendo_bcstm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bcstm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bcstm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_bcstm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bcstm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bcstm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_bcstm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_ATARI_SAP_H
#define XX_ATARI_SAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_sap {
    Abstractformat format;
} xx_atari_sap;
XXFC_API void xx_atari_sap_init(xx_atari_sap *, xx_io_device *, int64_t);
XXFC_API xx_atari_sap *xx_atari_sap_create(xx_io_device *, int64_t);
XXFC_API void xx_atari_sap_destroy(xx_atari_sap *);
XXFC_API void xx_atari_sap_free(xx_atari_sap *);
XXFC_API bool xx_atari_sap_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_atari_sap_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_atari_sap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_atari_sap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_atari_sap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_atari_sap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_atari_sap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

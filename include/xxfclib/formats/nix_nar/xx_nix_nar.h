/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_NIX_NAR_H
#define XX_NIX_NAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nix_nar { Abstractformat format; } xx_nix_nar;
XXFC_API void xx_nix_nar_init(xx_nix_nar *,xx_io_device *,int64_t);
XXFC_API xx_nix_nar *xx_nix_nar_create(xx_io_device *,int64_t);
XXFC_API void xx_nix_nar_destroy(xx_nix_nar *);
XXFC_API void xx_nix_nar_free(xx_nix_nar *);
XXFC_API bool xx_nix_nar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nix_nar_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nix_nar_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nix_nar_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nix_nar_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nix_nar_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nix_nar_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

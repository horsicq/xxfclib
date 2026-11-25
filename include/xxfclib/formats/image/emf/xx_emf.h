/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-emf/de081cd7-351f-4cc2-830b-d03fb55e89ab
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_EMF_H
#define XX_EMF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_emf {
    Abstractformat format;
} xx_emf;
XXFC_API void xx_emf_init(xx_emf *, xx_io_device *, int64_t);
XXFC_API xx_emf *xx_emf_create(xx_io_device *, int64_t);
XXFC_API void xx_emf_destroy(xx_emf *);
XXFC_API void xx_emf_free(xx_emf *);
XXFC_API bool xx_emf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_emf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_emf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_emf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_emf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_emf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_emf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

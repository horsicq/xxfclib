/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PARSEC_PMM_H
#define XX_PARSEC_PMM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MTCVTS PSM 2.00 music module with MDH/PLX/SM8 components. */
typedef struct xx_parsec_pmm { Abstractformat format; } xx_parsec_pmm;

XXFC_API void xx_parsec_pmm_init(xx_parsec_pmm *, xx_io_device *, int64_t);
XXFC_API xx_parsec_pmm *xx_parsec_pmm_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_pmm_destroy(xx_parsec_pmm *);
XXFC_API void xx_parsec_pmm_free(xx_parsec_pmm *);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_parsec_pmm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_parsec_pmm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_parsec_pmm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_parsec_pmm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_parsec_pmm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

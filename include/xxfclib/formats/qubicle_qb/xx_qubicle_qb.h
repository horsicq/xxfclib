/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://docs.safe.com/fme/2025.1/html/FME-Form-Documentation/FME-ReadersWriters/qubiclebinary/quick_facts_qubiclebinary.htm
 * QB1.1 complete positive-sized voxel matrices with UTF8 names, signed positions, RGBA/BGRA and right/left-handed Z flags, exact stored voxel arrays or fully framed slice RLE. Original matrix descriptors and encoded voxel arrays exported; no rendering.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_QUBICLE_QB_H
#define XX_QUBICLE_QB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_qubicle_qb {Abstractformat format;} xx_qubicle_qb;
XXFC_API void xx_qubicle_qb_init(xx_qubicle_qb *,xx_io_device *,int64_t);
XXFC_API xx_qubicle_qb *xx_qubicle_qb_create(xx_io_device *,int64_t);
XXFC_API void xx_qubicle_qb_destroy(xx_qubicle_qb *);
XXFC_API void xx_qubicle_qb_free(xx_qubicle_qb *);
XXFC_API bool xx_qubicle_qb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_qubicle_qb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_qubicle_qb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_qubicle_qb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_qubicle_qb_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

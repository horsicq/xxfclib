/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Youjose/PyCriCodecs/main/CriCodecs/src/usm/usm_reader.cpp
 * CRI USM CRID/@SFV/@SFA chunk sequences with standard32-byte headers and unflagged payload types0-3, up to4096 chunks. Checks chunk/padding extents and clear UTF metadata tables using a64-entry string-validation cache per table and one8MiB cumulative uncached string-scan budget shared across the entire container; exports encoded metadata tables and stream packets. Opaque codec/encrypted packet bytes are preserved without decryption/decoding; scan-budget excess, flagged headers, SFSH wrappers and unsupported stream IDs rejected.
 */
#ifndef XX_CRI_USM_H
#define XX_CRI_USM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cri_usm { Abstractformat format; } xx_cri_usm;
XXFC_API void xx_cri_usm_init(xx_cri_usm *,xx_io_device *,int64_t);
XXFC_API xx_cri_usm *xx_cri_usm_create(xx_io_device *,int64_t);
XXFC_API void xx_cri_usm_destroy(xx_cri_usm *);
XXFC_API void xx_cri_usm_free(xx_cri_usm *);
XXFC_API bool xx_cri_usm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cri_usm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cri_usm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cri_usm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cri_usm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cri_usm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cri_usm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

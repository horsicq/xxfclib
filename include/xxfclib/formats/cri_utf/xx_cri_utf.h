/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Youjose/PyCriCodecs/main/CriCodecs/src/utf/utf_reader.cpp
 * Clear CRI UTF versions0/1 with1-128 columns and1-1024 rows, scalar/string/GUID/disjoint binary-reference values and name/default/row storage flags. Checks schema/row widths, string termination and data extents with a64-entry validation cache,256-byte string reads and an8MiB cumulative uncached string-scan budget per table. Exports schema, row/string pools and stored binary values; encrypted UTF, scan-budget excess, overlapping/shared binary references and application interpretation unsupported.
 */
#ifndef XX_CRI_UTF_H
#define XX_CRI_UTF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cri_utf { Abstractformat format; } xx_cri_utf;
XXFC_API void xx_cri_utf_init(xx_cri_utf *,xx_io_device *,int64_t);
XXFC_API xx_cri_utf *xx_cri_utf_create(xx_io_device *,int64_t);
XXFC_API void xx_cri_utf_destroy(xx_cri_utf *);
XXFC_API void xx_cri_utf_free(xx_cri_utf *);
XXFC_API bool xx_cri_utf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cri_utf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cri_utf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cri_utf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cri_utf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

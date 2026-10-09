/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_AMSTRAD_CPC_DSK_H
#define XX_AMSTRAD_CPC_DSK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amstrad_cpc_dsk { Abstractformat format; } xx_amstrad_cpc_dsk;
XXFC_API void xx_amstrad_cpc_dsk_init(xx_amstrad_cpc_dsk *,xx_io_device *,int64_t);
XXFC_API xx_amstrad_cpc_dsk *xx_amstrad_cpc_dsk_create(xx_io_device *,int64_t);
XXFC_API void xx_amstrad_cpc_dsk_destroy(xx_amstrad_cpc_dsk *);
XXFC_API void xx_amstrad_cpc_dsk_free(xx_amstrad_cpc_dsk *);
XXFC_API bool xx_amstrad_cpc_dsk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amstrad_cpc_dsk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amstrad_cpc_dsk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_amstrad_cpc_dsk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amstrad_cpc_dsk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amstrad_cpc_dsk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_amstrad_cpc_dsk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

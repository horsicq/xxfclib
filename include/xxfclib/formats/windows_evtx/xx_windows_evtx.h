/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_WINDOWS_EVTX_H
#define XX_WINDOWS_EVTX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_windows_evtx { Abstractformat format; } xx_windows_evtx;
XXFC_API void xx_windows_evtx_init(xx_windows_evtx *,xx_io_device *,int64_t);
XXFC_API xx_windows_evtx *xx_windows_evtx_create(xx_io_device *,int64_t);
XXFC_API void xx_windows_evtx_destroy(xx_windows_evtx *);
XXFC_API void xx_windows_evtx_free(xx_windows_evtx *);
XXFC_API bool xx_windows_evtx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_windows_evtx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_windows_evtx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_windows_evtx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_windows_evtx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

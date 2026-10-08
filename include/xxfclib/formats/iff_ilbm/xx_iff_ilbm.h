/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://1fish2.github.io/IFF/IFF%20docs%20with%20Commodore%20revisions/ILBM.pdf, https://1fish2.github.io/IFF/
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_IFF_ILBM_H
#define XX_IFF_ILBM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_iff_ilbm { Abstractformat format; } xx_iff_ilbm;
XXFC_API void xx_iff_ilbm_init(xx_iff_ilbm *,xx_io_device *,int64_t);
XXFC_API xx_iff_ilbm *xx_iff_ilbm_create(xx_io_device *,int64_t);
XXFC_API void xx_iff_ilbm_destroy(xx_iff_ilbm *);
XXFC_API void xx_iff_ilbm_free(xx_iff_ilbm *);
XXFC_API bool xx_iff_ilbm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_iff_ilbm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_iff_ilbm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_iff_ilbm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_iff_ilbm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

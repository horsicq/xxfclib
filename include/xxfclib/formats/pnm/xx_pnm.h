/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://netpbm.sourceforge.net/doc/pbm.html, https://netpbm.sourceforge.net/doc/pgm.html, https://netpbm.sourceforge.net/doc/ppm.html, https://netpbm.sourceforge.net/doc/pam.html
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_PNM_H
#define XX_PNM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pnm { Abstractformat format; } xx_pnm;
XXFC_API void xx_pnm_init(xx_pnm *,xx_io_device *,int64_t);
XXFC_API xx_pnm *xx_pnm_create(xx_io_device *,int64_t);
XXFC_API void xx_pnm_destroy(xx_pnm *);
XXFC_API void xx_pnm_free(xx_pnm *);
XXFC_API bool xx_pnm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pnm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pnm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pnm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pnm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

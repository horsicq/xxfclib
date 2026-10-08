/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * LEGO Racers ALP .TUN/.PCM stored-component reader.
 */
#ifndef XX_LEGO_ALP_H
#define XX_LEGO_ALP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lego_alp { Abstractformat format; } xx_lego_alp;
XXFC_API void xx_lego_alp_init(xx_lego_alp *,xx_io_device *,int64_t);
XXFC_API xx_lego_alp *xx_lego_alp_create(xx_io_device *,int64_t);
XXFC_API void xx_lego_alp_destroy(xx_lego_alp *);
XXFC_API void xx_lego_alp_free(xx_lego_alp *);
XXFC_API bool xx_lego_alp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lego_alp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lego_alp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lego_alp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lego_alp_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

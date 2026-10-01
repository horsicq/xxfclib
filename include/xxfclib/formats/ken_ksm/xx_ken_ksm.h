/* SPDX-License-Identifier: MIT */
#ifndef XX_KEN_KSM_H
#define XX_KEN_KSM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_ken_ksm {Abstractformat format;} xx_ken_ksm;
XXFC_API void xx_ken_ksm_init(xx_ken_ksm *,xx_io_device *,int64_t);
XXFC_API xx_ken_ksm *xx_ken_ksm_create(xx_io_device *,int64_t);
XXFC_API void xx_ken_ksm_destroy(xx_ken_ksm *);
XXFC_API void xx_ken_ksm_free(xx_ken_ksm *);
XXFC_API bool xx_ken_ksm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ken_ksm_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif

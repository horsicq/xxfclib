/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/nukeykt/NBlood/blob/master/source/blood/src/resource.h
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_RAVENSOFT_RFF_H
#define XX_RAVENSOFT_RFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ravensoft_rff { Abstractformat format; } xx_ravensoft_rff;
XXFC_API void xx_ravensoft_rff_init(xx_ravensoft_rff *,xx_io_device *,int64_t);
XXFC_API xx_ravensoft_rff *xx_ravensoft_rff_create(xx_io_device *,int64_t);
XXFC_API void xx_ravensoft_rff_destroy(xx_ravensoft_rff *);
XXFC_API void xx_ravensoft_rff_free(xx_ravensoft_rff *);
XXFC_API bool xx_ravensoft_rff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ravensoft_rff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

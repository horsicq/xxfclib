/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos */
#ifndef XX_PHOTONTIMING_PHU_H
#define XX_PHOTONTIMING_PHU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photontiming_phu { Abstractformat format; } xx_photontiming_phu;
XXFC_API void xx_photontiming_phu_init(xx_photontiming_phu *,xx_io_device *,int64_t);
XXFC_API xx_photontiming_phu *xx_photontiming_phu_create(xx_io_device *,int64_t);
XXFC_API void xx_photontiming_phu_destroy(xx_photontiming_phu *);
XXFC_API void xx_photontiming_phu_free(xx_photontiming_phu *);
XXFC_API bool xx_photontiming_phu_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photontiming_phu_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

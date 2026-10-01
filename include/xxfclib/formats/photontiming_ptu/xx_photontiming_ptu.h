/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos */
#ifndef XX_PHOTONTIMING_PTU_H
#define XX_PHOTONTIMING_PTU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photontiming_ptu { Abstractformat format; } xx_photontiming_ptu;
XXFC_API void xx_photontiming_ptu_init(xx_photontiming_ptu *,xx_io_device *,int64_t);
XXFC_API xx_photontiming_ptu *xx_photontiming_ptu_create(xx_io_device *,int64_t);
XXFC_API void xx_photontiming_ptu_destroy(xx_photontiming_ptu *);
XXFC_API void xx_photontiming_ptu_free(xx_photontiming_ptu *);
XXFC_API bool xx_photontiming_ptu_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photontiming_ptu_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

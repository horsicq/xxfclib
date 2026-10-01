/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/rehlds/ReHLDS/master/rehlds/engine/hashpak.h
 * GoldSrc HPAK version1 32-bit on-disk resource records, stored lumps. Checks member MD5 and resource sizes. 64-bit native-struct variants rejected; original paths become numeric safe names.
 */
#ifndef XX_VALVE_HPAK_H
#define XX_VALVE_HPAK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_valve_hpak { Abstractformat format; } xx_valve_hpak;
XXFC_API void xx_valve_hpak_init(xx_valve_hpak *,xx_io_device *,int64_t);
XXFC_API xx_valve_hpak *xx_valve_hpak_create(xx_io_device *,int64_t);
XXFC_API void xx_valve_hpak_destroy(xx_valve_hpak *);
XXFC_API void xx_valve_hpak_free(xx_valve_hpak *);
XXFC_API bool xx_valve_hpak_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_valve_hpak_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

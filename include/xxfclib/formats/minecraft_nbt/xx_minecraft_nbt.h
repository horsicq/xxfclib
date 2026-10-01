/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MINECRAFT_NBT_H
#define XX_MINECRAFT_NBT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_minecraft_nbt { Abstractformat format; } xx_minecraft_nbt;
XXFC_API void xx_minecraft_nbt_init(xx_minecraft_nbt *,xx_io_device *,int64_t);
XXFC_API xx_minecraft_nbt *xx_minecraft_nbt_create(xx_io_device *,int64_t);
XXFC_API void xx_minecraft_nbt_destroy(xx_minecraft_nbt *);
XXFC_API void xx_minecraft_nbt_free(xx_minecraft_nbt *);
XXFC_API bool xx_minecraft_nbt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_minecraft_nbt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

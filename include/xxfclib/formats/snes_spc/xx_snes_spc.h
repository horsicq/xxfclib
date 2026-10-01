/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_SNES_SPC_H
#define XX_SNES_SPC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_snes_spc { Abstractformat format; } xx_snes_spc;
XXFC_API void xx_snes_spc_init(xx_snes_spc *,xx_io_device *,int64_t);
XXFC_API xx_snes_spc *xx_snes_spc_create(xx_io_device *,int64_t);
XXFC_API void xx_snes_spc_destroy(xx_snes_spc *);
XXFC_API void xx_snes_spc_free(xx_snes_spc *);
XXFC_API bool xx_snes_spc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_snes_spc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

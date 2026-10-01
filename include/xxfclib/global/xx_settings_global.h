/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SETTINGS_GLOBAL_H
#define XX_SETTINGS_GLOBAL_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xx_settings_s;

/** @brief Non-owning process-wide settings pointer. NULL (the default) disables persistence. */
XXFC_API void xx_set_settings(struct xx_settings_s *settings);
XXFC_API struct xx_settings_s *xx_get_settings(void);
XXFC_API void xx_global_set_settings(struct xx_settings_s *settings);
XXFC_API struct xx_settings_s *xx_global_get_settings(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_SETTINGS_GLOBAL_H */

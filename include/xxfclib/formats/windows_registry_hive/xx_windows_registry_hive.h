/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_WINDOWS_REGISTRY_HIVE_H
#define XX_WINDOWS_REGISTRY_HIVE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_windows_registry_hive { Abstractformat format; } xx_windows_registry_hive;
XXFC_API void xx_windows_registry_hive_init(xx_windows_registry_hive *,xx_io_device *,int64_t);
XXFC_API xx_windows_registry_hive *xx_windows_registry_hive_create(xx_io_device *,int64_t);
XXFC_API void xx_windows_registry_hive_destroy(xx_windows_registry_hive *);
XXFC_API void xx_windows_registry_hive_free(xx_windows_registry_hive *);
XXFC_API bool xx_windows_registry_hive_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_windows_registry_hive_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_VICE_SNAPSHOT_H
#define XX_VICE_SNAPSHOT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vice_snapshot { Abstractformat format; } xx_vice_snapshot;
XXFC_API void xx_vice_snapshot_init(xx_vice_snapshot *,xx_io_device *,int64_t);
XXFC_API xx_vice_snapshot *xx_vice_snapshot_create(xx_io_device *,int64_t);
XXFC_API void xx_vice_snapshot_destroy(xx_vice_snapshot *);
XXFC_API void xx_vice_snapshot_free(xx_vice_snapshot *);
XXFC_API bool xx_vice_snapshot_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vice_snapshot_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

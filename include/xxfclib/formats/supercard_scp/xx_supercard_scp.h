/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_SUPERCARD_SCP_H
#define XX_SUPERCARD_SCP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_supercard_scp { Abstractformat format; } xx_supercard_scp;
XXFC_API void xx_supercard_scp_init(xx_supercard_scp *,xx_io_device *,int64_t);
XXFC_API xx_supercard_scp *xx_supercard_scp_create(xx_io_device *,int64_t);
XXFC_API void xx_supercard_scp_destroy(xx_supercard_scp *);
XXFC_API void xx_supercard_scp_free(xx_supercard_scp *);
XXFC_API bool xx_supercard_scp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_supercard_scp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

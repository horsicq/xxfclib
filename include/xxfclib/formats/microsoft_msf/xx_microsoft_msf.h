/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MICROSOFT_MSF_H
#define XX_MICROSOFT_MSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microsoft_msf { Abstractformat format; } xx_microsoft_msf;
XXFC_API void xx_microsoft_msf_init(xx_microsoft_msf *,xx_io_device *,int64_t);
XXFC_API xx_microsoft_msf *xx_microsoft_msf_create(xx_io_device *,int64_t);
XXFC_API void xx_microsoft_msf_destroy(xx_microsoft_msf *);
XXFC_API void xx_microsoft_msf_free(xx_microsoft_msf *);
XXFC_API bool xx_microsoft_msf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microsoft_msf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

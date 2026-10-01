/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_LINUX_BTF_H
#define XX_LINUX_BTF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_linux_btf { Abstractformat format; } xx_linux_btf;
XXFC_API void xx_linux_btf_init(xx_linux_btf *,xx_io_device *,int64_t);
XXFC_API xx_linux_btf *xx_linux_btf_create(xx_io_device *,int64_t);
XXFC_API void xx_linux_btf_destroy(xx_linux_btf *);
XXFC_API void xx_linux_btf_free(xx_linux_btf *);
XXFC_API bool xx_linux_btf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_linux_btf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

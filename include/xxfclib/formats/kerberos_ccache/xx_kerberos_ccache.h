/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_KERBEROS_CCACHE_H
#define XX_KERBEROS_CCACHE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_kerberos_ccache {Abstractformat format;} xx_kerberos_ccache;
XXFC_API void xx_kerberos_ccache_init(xx_kerberos_ccache *,xx_io_device *,int64_t);
XXFC_API xx_kerberos_ccache *xx_kerberos_ccache_create(xx_io_device *,int64_t);
XXFC_API void xx_kerberos_ccache_destroy(xx_kerberos_ccache *);
XXFC_API void xx_kerberos_ccache_free(xx_kerberos_ccache *);
XXFC_API bool xx_kerberos_ccache_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_kerberos_ccache_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

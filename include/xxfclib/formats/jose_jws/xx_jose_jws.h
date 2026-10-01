/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_JOSE_JWS_H
#define XX_JOSE_JWS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jose_jws {Abstractformat format;} xx_jose_jws;
XXFC_API void xx_jose_jws_init(xx_jose_jws *,xx_io_device *,int64_t);
XXFC_API xx_jose_jws *xx_jose_jws_create(xx_io_device *,int64_t);
XXFC_API void xx_jose_jws_destroy(xx_jose_jws *);
XXFC_API void xx_jose_jws_free(xx_jose_jws *);
XXFC_API bool xx_jose_jws_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jose_jws_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

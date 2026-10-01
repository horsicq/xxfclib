/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_ETHEREUM_RLP_H
#define XX_ETHEREUM_RLP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ethereum_rlp {Abstractformat format;} xx_ethereum_rlp;
XXFC_API void xx_ethereum_rlp_init(xx_ethereum_rlp *,xx_io_device *,int64_t);
XXFC_API xx_ethereum_rlp *xx_ethereum_rlp_create(xx_io_device *,int64_t);
XXFC_API void xx_ethereum_rlp_destroy(xx_ethereum_rlp *);
XXFC_API void xx_ethereum_rlp_free(xx_ethereum_rlp *);
XXFC_API bool xx_ethereum_rlp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ethereum_rlp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

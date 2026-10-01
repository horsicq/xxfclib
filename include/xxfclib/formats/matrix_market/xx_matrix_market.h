/* SPDX-License-Identifier: MIT
 * Wire specification: https://math.nist.gov/MatrixMarket/formats.html */
#ifndef XX_MATRIX_MARKET_H
#define XX_MATRIX_MARKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_matrix_market { Abstractformat format; } xx_matrix_market;
XXFC_API void xx_matrix_market_init(xx_matrix_market *,xx_io_device *,int64_t);
XXFC_API xx_matrix_market *xx_matrix_market_create(xx_io_device *,int64_t);
XXFC_API void xx_matrix_market_destroy(xx_matrix_market *);
XXFC_API void xx_matrix_market_free(xx_matrix_market *);
XXFC_API bool xx_matrix_market_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_matrix_market_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

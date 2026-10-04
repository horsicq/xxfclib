/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_SVD_H
#define XX_SVD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_svd {Abstractformat format;} xx_svd;
XXFC_API void xx_svd_init(xx_svd *,xx_io_device *,int64_t);
XXFC_API xx_svd *xx_svd_create(xx_io_device *,int64_t);
XXFC_API void xx_svd_destroy(xx_svd *);
XXFC_API void xx_svd_free(xx_svd *);
XXFC_API bool xx_svd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_svd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_ZX_AYEMUL_H
#define XX_ZX_AYEMUL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_zx_ayemul { Abstractformat format; } xx_zx_ayemul;
XXFC_API void xx_zx_ayemul_init(xx_zx_ayemul *,xx_io_device *,int64_t);
XXFC_API xx_zx_ayemul *xx_zx_ayemul_create(xx_io_device *,int64_t);
XXFC_API void xx_zx_ayemul_destroy(xx_zx_ayemul *);
XXFC_API void xx_zx_ayemul_free(xx_zx_ayemul *);
XXFC_API bool xx_zx_ayemul_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_zx_ayemul_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

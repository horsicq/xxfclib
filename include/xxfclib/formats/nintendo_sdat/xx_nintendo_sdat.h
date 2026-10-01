/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_NINTENDO_SDAT_H
#define XX_NINTENDO_SDAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_sdat { Abstractformat format; } xx_nintendo_sdat;
XXFC_API void xx_nintendo_sdat_init(xx_nintendo_sdat *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_sdat *xx_nintendo_sdat_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_sdat_destroy(xx_nintendo_sdat *);
XXFC_API void xx_nintendo_sdat_free(xx_nintendo_sdat *);
XXFC_API bool xx_nintendo_sdat_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_sdat_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

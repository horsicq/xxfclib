/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_ATARI_DIM_H
#define XX_ATARI_DIM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_dim {Abstractformat format;} xx_atari_dim;
XXFC_API void xx_atari_dim_init(xx_atari_dim *,xx_io_device *,int64_t);
XXFC_API xx_atari_dim *xx_atari_dim_create(xx_io_device *,int64_t);
XXFC_API void xx_atari_dim_destroy(xx_atari_dim *);
XXFC_API void xx_atari_dim_free(xx_atari_dim *);
XXFC_API bool xx_atari_dim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_atari_dim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

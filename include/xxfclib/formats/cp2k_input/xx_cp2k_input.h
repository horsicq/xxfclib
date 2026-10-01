/* SPDX-License-Identifier: MIT
 * Wire specification: https://manual.cp2k.org/trunk/CP2K_INPUT.html */
#ifndef XX_CP2K_INPUT_H
#define XX_CP2K_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cp2k_input { Abstractformat format; } xx_cp2k_input;
XXFC_API void xx_cp2k_input_init(xx_cp2k_input *,xx_io_device *,int64_t);
XXFC_API xx_cp2k_input *xx_cp2k_input_create(xx_io_device *,int64_t);
XXFC_API void xx_cp2k_input_destroy(xx_cp2k_input *);
XXFC_API void xx_cp2k_input_free(xx_cp2k_input *);
XXFC_API bool xx_cp2k_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cp2k_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

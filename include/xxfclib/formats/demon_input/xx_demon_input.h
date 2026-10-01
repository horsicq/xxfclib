/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/calculators/demon/demon.html */
#ifndef XX_DEMON_INPUT_H
#define XX_DEMON_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_demon_input { Abstractformat format; } xx_demon_input;
XXFC_API void xx_demon_input_init(xx_demon_input *,xx_io_device *,int64_t);
XXFC_API xx_demon_input *xx_demon_input_create(xx_io_device *,int64_t);
XXFC_API void xx_demon_input_destroy(xx_demon_input *);
XXFC_API void xx_demon_input_free(xx_demon_input *);
XXFC_API bool xx_demon_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_demon_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

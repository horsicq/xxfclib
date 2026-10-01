/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.msg.chem.iastate.edu/gamess/GAMESS_Manual/input.pdf */
#ifndef XX_GAMESS_INPUT_H
#define XX_GAMESS_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gamess_input { Abstractformat format; } xx_gamess_input;
XXFC_API void xx_gamess_input_init(xx_gamess_input *,xx_io_device *,int64_t);
XXFC_API xx_gamess_input *xx_gamess_input_create(xx_io_device *,int64_t);
XXFC_API void xx_gamess_input_destroy(xx_gamess_input *);
XXFC_API void xx_gamess_input_free(xx_gamess_input *);
XXFC_API bool xx_gamess_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gamess_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

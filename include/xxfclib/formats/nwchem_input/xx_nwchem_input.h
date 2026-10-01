/* SPDX-License-Identifier: MIT
 * Wire specification: https://nwchemgit.github.io/Input-and-Output.html */
#ifndef XX_NWCHEM_INPUT_H
#define XX_NWCHEM_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nwchem_input { Abstractformat format; } xx_nwchem_input;
XXFC_API void xx_nwchem_input_init(xx_nwchem_input *,xx_io_device *,int64_t);
XXFC_API xx_nwchem_input *xx_nwchem_input_create(xx_io_device *,int64_t);
XXFC_API void xx_nwchem_input_destroy(xx_nwchem_input *);
XXFC_API void xx_nwchem_input_free(xx_nwchem_input *);
XXFC_API bool xx_nwchem_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nwchem_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

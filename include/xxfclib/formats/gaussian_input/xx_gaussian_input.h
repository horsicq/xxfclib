/* SPDX-License-Identifier: MIT
 * Wire specification: https://gaussian.com/input/ */
#ifndef XX_GAUSSIAN_INPUT_H
#define XX_GAUSSIAN_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gaussian_input { Abstractformat format; } xx_gaussian_input;
XXFC_API void xx_gaussian_input_init(xx_gaussian_input *,xx_io_device *,int64_t);
XXFC_API xx_gaussian_input *xx_gaussian_input_create(xx_io_device *,int64_t);
XXFC_API void xx_gaussian_input_destroy(xx_gaussian_input *);
XXFC_API void xx_gaussian_input_free(xx_gaussian_input *);
XXFC_API bool xx_gaussian_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gaussian_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://numpy.org/doc/stable/reference/generated/numpy.lib.format.html
 * Bounded encoded-component extraction. */
#ifndef XX_NUMPY_NPY_H
#define XX_NUMPY_NPY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_numpy_npy { Abstractformat format; } xx_numpy_npy;
XXFC_API void xx_numpy_npy_init(xx_numpy_npy *,xx_io_device *,int64_t);
XXFC_API xx_numpy_npy *xx_numpy_npy_create(xx_io_device *,int64_t);
XXFC_API void xx_numpy_npy_destroy(xx_numpy_npy *);
XXFC_API void xx_numpy_npy_free(xx_numpy_npy *);
XXFC_API bool xx_numpy_npy_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_numpy_npy_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

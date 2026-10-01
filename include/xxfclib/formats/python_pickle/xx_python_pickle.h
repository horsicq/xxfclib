/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/python/cpython/blob/3.13/Lib/pickletools.py */
#ifndef XX_PYTHON_PICKLE_H
#define XX_PYTHON_PICKLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_python_pickle { Abstractformat format; } xx_python_pickle;
XXFC_API void xx_python_pickle_init(xx_python_pickle *,xx_io_device *,int64_t);
XXFC_API xx_python_pickle *xx_python_pickle_create(xx_io_device *,int64_t);
XXFC_API void xx_python_pickle_destroy(xx_python_pickle *);
XXFC_API void xx_python_pickle_free(xx_python_pickle *);
XXFC_API bool xx_python_pickle_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_python_pickle_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

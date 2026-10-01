/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/python/cpython/blob/3.13/Python/marshal.c */
#ifndef XX_PYTHON_MARSHAL_H
#define XX_PYTHON_MARSHAL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_python_marshal { Abstractformat format; } xx_python_marshal;
XXFC_API void xx_python_marshal_init(xx_python_marshal *,xx_io_device *,int64_t);
XXFC_API xx_python_marshal *xx_python_marshal_create(xx_io_device *,int64_t);
XXFC_API void xx_python_marshal_destroy(xx_python_marshal *);
XXFC_API void xx_python_marshal_free(xx_python_marshal *);
XXFC_API bool xx_python_marshal_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_python_marshal_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

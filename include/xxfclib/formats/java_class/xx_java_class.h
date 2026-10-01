/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_JAVA_CLASS_H
#define XX_JAVA_CLASS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_java_class { Abstractformat format; } xx_java_class;
XXFC_API void xx_java_class_init(xx_java_class *,xx_io_device *,int64_t);
XXFC_API xx_java_class *xx_java_class_create(xx_io_device *,int64_t);
XXFC_API void xx_java_class_destroy(xx_java_class *);
XXFC_API void xx_java_class_free(xx_java_class *);
XXFC_API bool xx_java_class_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_java_class_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

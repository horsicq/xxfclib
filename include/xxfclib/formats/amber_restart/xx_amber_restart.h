/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/ParmEd/ParmEd/blob/master/parmed/amber/asciicrd.py */
#ifndef XX_AMBER_RESTART_H
#define XX_AMBER_RESTART_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amber_restart { Abstractformat format; } xx_amber_restart;
XXFC_API void xx_amber_restart_init(xx_amber_restart *,xx_io_device *,int64_t);
XXFC_API xx_amber_restart *xx_amber_restart_create(xx_io_device *,int64_t);
XXFC_API void xx_amber_restart_destroy(xx_amber_restart *);
XXFC_API void xx_amber_restart_free(xx_amber_restart *);
XXFC_API bool xx_amber_restart_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amber_restart_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

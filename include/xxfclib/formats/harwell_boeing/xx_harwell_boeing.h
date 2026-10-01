/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.scipy.org/doc/scipy/reference/generated/scipy.io.hb_write.html */
#ifndef XX_HARWELL_BOEING_H
#define XX_HARWELL_BOEING_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_harwell_boeing { Abstractformat format; } xx_harwell_boeing;
XXFC_API void xx_harwell_boeing_init(xx_harwell_boeing *,xx_io_device *,int64_t);
XXFC_API xx_harwell_boeing *xx_harwell_boeing_create(xx_io_device *,int64_t);
XXFC_API void xx_harwell_boeing_destroy(xx_harwell_boeing *);
XXFC_API void xx_harwell_boeing_free(xx_harwell_boeing *);
XXFC_API bool xx_harwell_boeing_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_harwell_boeing_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

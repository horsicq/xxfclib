/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_COPYTAPE_H
#define XX_COPYTAPE_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_copytape;
typedef xx_copytape xx_copytape_t;
XXFC_API void xx_copytape_init(xx_copytape *,xx_io_device *,int64_t);
XXFC_API xx_copytape *xx_copytape_create(xx_io_device *,int64_t);
XXFC_API void xx_copytape_destroy(xx_copytape *);
XXFC_API void xx_copytape_free(xx_copytape *);
XXFC_API bool xx_copytape_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_copytape_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

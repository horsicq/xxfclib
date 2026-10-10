/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_LISA_BLU_H
#define XX_LISA_BLU_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_lisa_blu;
typedef xx_lisa_blu xx_lisa_blu_t;
XXFC_API void xx_lisa_blu_init(xx_lisa_blu *, xx_io_device *, int64_t);
XXFC_API xx_lisa_blu *xx_lisa_blu_create(xx_io_device *, int64_t);
XXFC_API void xx_lisa_blu_destroy(xx_lisa_blu *);
XXFC_API void xx_lisa_blu_free(xx_lisa_blu *);
XXFC_API bool xx_lisa_blu_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lisa_blu_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

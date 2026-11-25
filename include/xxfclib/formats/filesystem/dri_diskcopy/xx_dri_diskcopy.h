/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_DRI_DISKCOPY_H
#define XX_DRI_DISKCOPY_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_dri_diskcopy;
typedef xx_dri_diskcopy xx_dri_diskcopy_t;
XXFC_API void xx_dri_diskcopy_init(xx_dri_diskcopy *, xx_io_device *, int64_t);
XXFC_API xx_dri_diskcopy *xx_dri_diskcopy_create(xx_io_device *, int64_t);
XXFC_API void xx_dri_diskcopy_destroy(xx_dri_diskcopy *);
XXFC_API void xx_dri_diskcopy_free(xx_dri_diskcopy *);
XXFC_API bool xx_dri_diskcopy_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dri_diskcopy_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

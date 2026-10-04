/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_WC_DISK_IMAGE_H
#define XX_WC_DISK_IMAGE_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_wc_disk_image;
typedef xx_wc_disk_image xx_wc_disk_image_t;
XXFC_API void xx_wc_disk_image_init(xx_wc_disk_image *,xx_io_device *,int64_t);
XXFC_API xx_wc_disk_image *xx_wc_disk_image_create(xx_io_device *,int64_t);
XXFC_API void xx_wc_disk_image_destroy(xx_wc_disk_image *);
XXFC_API void xx_wc_disk_image_free(xx_wc_disk_image *);
XXFC_API bool xx_wc_disk_image_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wc_disk_image_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

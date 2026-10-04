/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_MAXI_DISK_H
#define XX_MAXI_DISK_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_maxi_disk;
typedef xx_maxi_disk xx_maxi_disk_t;
XXFC_API void xx_maxi_disk_init(xx_maxi_disk *,xx_io_device *,int64_t);
XXFC_API xx_maxi_disk *xx_maxi_disk_create(xx_io_device *,int64_t);
XXFC_API void xx_maxi_disk_destroy(xx_maxi_disk *);
XXFC_API void xx_maxi_disk_free(xx_maxi_disk *);
XXFC_API bool xx_maxi_disk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_maxi_disk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

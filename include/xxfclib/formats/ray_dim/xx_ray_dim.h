/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_RAY_DIM_H
#define XX_RAY_DIM_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_ray_dim;
typedef xx_ray_dim xx_ray_dim_t;
XXFC_API void xx_ray_dim_init(xx_ray_dim *,xx_io_device *,int64_t);
XXFC_API xx_ray_dim *xx_ray_dim_create(xx_io_device *,int64_t);
XXFC_API void xx_ray_dim_destroy(xx_ray_dim *);
XXFC_API void xx_ray_dim_free(xx_ray_dim *);
XXFC_API bool xx_ray_dim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ray_dim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

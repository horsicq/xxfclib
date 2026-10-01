/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/laspy/laspy/master/laspy/header.py */
#ifndef XX_LIDAR_LAS_H
#define XX_LIDAR_LAS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lidar_las { Abstractformat format; } xx_lidar_las;
XXFC_API void xx_lidar_las_init(xx_lidar_las *,xx_io_device *,int64_t);
XXFC_API xx_lidar_las *xx_lidar_las_create(xx_io_device *,int64_t);
XXFC_API void xx_lidar_las_destroy(xx_lidar_las *);
XXFC_API void xx_lidar_las_free(xx_lidar_las *);
XXFC_API bool xx_lidar_las_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lidar_las_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

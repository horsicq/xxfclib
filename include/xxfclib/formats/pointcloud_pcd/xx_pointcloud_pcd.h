/* SPDX-License-Identifier: MIT
 * Wire specification: https://pointclouds.org/documentation/tutorials/pcd_file_format.html */
#ifndef XX_POINTCLOUD_PCD_H
#define XX_POINTCLOUD_PCD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pointcloud_pcd { Abstractformat format; } xx_pointcloud_pcd;
XXFC_API void xx_pointcloud_pcd_init(xx_pointcloud_pcd *,xx_io_device *,int64_t);
XXFC_API xx_pointcloud_pcd *xx_pointcloud_pcd_create(xx_io_device *,int64_t);
XXFC_API void xx_pointcloud_pcd_destroy(xx_pointcloud_pcd *);
XXFC_API void xx_pointcloud_pcd_free(xx_pointcloud_pcd *);
XXFC_API bool xx_pointcloud_pcd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pointcloud_pcd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pointcloud_pcd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pointcloud_pcd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pointcloud_pcd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pointcloud_pcd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pointcloud_pcd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

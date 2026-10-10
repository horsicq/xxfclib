/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/ros/ros_comm/blob/noetic-devel/tools/rosbag/src/rosbag/bag.py */
#ifndef XX_ROSBAG1_H
#define XX_ROSBAG1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_rosbag1 {
    Abstractformat format;
} xx_rosbag1;
XXFC_API void xx_rosbag1_init(xx_rosbag1 *, xx_io_device *, int64_t);
XXFC_API xx_rosbag1 *xx_rosbag1_create(xx_io_device *, int64_t);
XXFC_API void xx_rosbag1_destroy(xx_rosbag1 *);
XXFC_API void xx_rosbag1_free(xx_rosbag1 *);
XXFC_API bool xx_rosbag1_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_rosbag1_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rosbag1_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_rosbag1_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rosbag1_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rosbag1_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_rosbag1_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_DRAGON_VDK_H
#define XX_DRAGON_VDK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dragon_vdk { Abstractformat format; } xx_dragon_vdk;
XXFC_API void xx_dragon_vdk_init(xx_dragon_vdk *,xx_io_device *,int64_t);
XXFC_API xx_dragon_vdk *xx_dragon_vdk_create(xx_io_device *,int64_t);
XXFC_API void xx_dragon_vdk_destroy(xx_dragon_vdk *);
XXFC_API void xx_dragon_vdk_free(xx_dragon_vdk *);
XXFC_API bool xx_dragon_vdk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dragon_vdk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dragon_vdk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dragon_vdk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dragon_vdk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dragon_vdk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dragon_vdk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

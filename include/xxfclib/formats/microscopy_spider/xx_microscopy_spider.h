/* SPDX-License-Identifier: MIT
 * Wire specification: https://spider.wadsworth.org/spider_doc/spider/docs/image_doc.html */
#ifndef XX_MICROSCOPY_SPIDER_H
#define XX_MICROSCOPY_SPIDER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microscopy_spider {
    Abstractformat format;
} xx_microscopy_spider;
XXFC_API void xx_microscopy_spider_init(xx_microscopy_spider *, xx_io_device *, int64_t);
XXFC_API xx_microscopy_spider *xx_microscopy_spider_create(xx_io_device *, int64_t);
XXFC_API void xx_microscopy_spider_destroy(xx_microscopy_spider *);
XXFC_API void xx_microscopy_spider_free(xx_microscopy_spider *);
XXFC_API bool xx_microscopy_spider_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_microscopy_spider_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microscopy_spider_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_microscopy_spider_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microscopy_spider_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microscopy_spider_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_microscopy_spider_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

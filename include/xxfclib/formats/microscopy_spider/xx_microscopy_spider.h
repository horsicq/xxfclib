/* SPDX-License-Identifier: MIT
 * Wire specification: https://spider.wadsworth.org/spider_doc/spider/docs/image_doc.html */
#ifndef XX_MICROSCOPY_SPIDER_H
#define XX_MICROSCOPY_SPIDER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microscopy_spider { Abstractformat format; } xx_microscopy_spider;
XXFC_API void xx_microscopy_spider_init(xx_microscopy_spider *,xx_io_device *,int64_t);
XXFC_API xx_microscopy_spider *xx_microscopy_spider_create(xx_io_device *,int64_t);
XXFC_API void xx_microscopy_spider_destroy(xx_microscopy_spider *);
XXFC_API void xx_microscopy_spider_free(xx_microscopy_spider *);
XXFC_API bool xx_microscopy_spider_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microscopy_spider_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

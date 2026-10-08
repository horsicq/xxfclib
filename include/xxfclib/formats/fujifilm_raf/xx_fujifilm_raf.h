/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/Exiv2/exiv2/main/src/rafimage.cpp */
#ifndef XX_FUJIFILM_RAF_H
#define XX_FUJIFILM_RAF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fujifilm_raf { Abstractformat format; } xx_fujifilm_raf;
XXFC_API void xx_fujifilm_raf_init(xx_fujifilm_raf *,xx_io_device *,int64_t);
XXFC_API xx_fujifilm_raf *xx_fujifilm_raf_create(xx_io_device *,int64_t);
XXFC_API void xx_fujifilm_raf_destroy(xx_fujifilm_raf *);
XXFC_API void xx_fujifilm_raf_free(xx_fujifilm_raf *);
XXFC_API bool xx_fujifilm_raf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_fujifilm_raf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fujifilm_raf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fujifilm_raf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fujifilm_raf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

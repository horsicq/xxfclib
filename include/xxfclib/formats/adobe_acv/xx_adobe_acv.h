/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Adobe curve file versions1/4: complete counted curves and sorted distinct input/output 8-bit point pairs with exact EOF. Original header and individual curve records exported; extended curve-map/footer variants declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ADOBE_ACV_H
#define XX_ADOBE_ACV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_acv {Abstractformat format;} xx_adobe_acv;
XXFC_API void xx_adobe_acv_init(xx_adobe_acv *,xx_io_device *,int64_t);
XXFC_API xx_adobe_acv *xx_adobe_acv_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_acv_destroy(xx_adobe_acv *);
XXFC_API void xx_adobe_acv_free(xx_adobe_acv *);
XXFC_API bool xx_adobe_acv_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_acv_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adobe_acv_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adobe_acv_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adobe_acv_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adobe_acv_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adobe_acv_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

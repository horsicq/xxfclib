/* SPDX-License-Identifier: MIT
 * Primary reference: https://ates.dev/pages/acb-spec/
 * Adobe Color Book v1: complete UTF16BE descriptor and bounded RGB/CMYK/Lab color records, page parameters and optional typed spot/process trailer. Original encoded
 * descriptor/colors exported; no color conversion. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ADOBE_ACB_H
#define XX_ADOBE_ACB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_acb {
    Abstractformat format;
} xx_adobe_acb;
XXFC_API void xx_adobe_acb_init(xx_adobe_acb *, xx_io_device *, int64_t);
XXFC_API xx_adobe_acb *xx_adobe_acb_create(xx_io_device *, int64_t);
XXFC_API void xx_adobe_acb_destroy(xx_adobe_acb *);
XXFC_API void xx_adobe_acb_free(xx_adobe_acb *);
XXFC_API bool xx_adobe_acb_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_adobe_acb_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adobe_acb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adobe_acb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adobe_acb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adobe_acb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adobe_acb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

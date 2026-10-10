/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_HSC_H
#define XX_ADLIB_HSC_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_hsc {
    Abstractformat format;
} xx_adlib_hsc;
XXFC_API void xx_adlib_hsc_init(xx_adlib_hsc *, xx_io_device *, int64_t);
XXFC_API xx_adlib_hsc *xx_adlib_hsc_create(xx_io_device *, int64_t);
XXFC_API void xx_adlib_hsc_destroy(xx_adlib_hsc *);
XXFC_API void xx_adlib_hsc_free(xx_adlib_hsc *);
XXFC_API bool xx_adlib_hsc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_adlib_hsc_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_hsc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adlib_hsc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_hsc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_hsc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adlib_hsc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

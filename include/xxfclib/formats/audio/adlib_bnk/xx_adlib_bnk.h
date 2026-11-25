/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_BNK_H
#define XX_ADLIB_BNK_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_bnk {
    Abstractformat format;
} xx_adlib_bnk;
XXFC_API void xx_adlib_bnk_init(xx_adlib_bnk *, xx_io_device *, int64_t);
XXFC_API xx_adlib_bnk *xx_adlib_bnk_create(xx_io_device *, int64_t);
XXFC_API void xx_adlib_bnk_destroy(xx_adlib_bnk *);
XXFC_API void xx_adlib_bnk_free(xx_adlib_bnk *);
XXFC_API bool xx_adlib_bnk_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_adlib_bnk_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_bnk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adlib_bnk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_bnk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_bnk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adlib_bnk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

/* SPDX-License-Identifier: MIT */
#ifndef XX_CREATIVE_CMF_H
#define XX_CREATIVE_CMF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_creative_cmf {
    Abstractformat format;
} xx_creative_cmf;
XXFC_API void xx_creative_cmf_init(xx_creative_cmf *, xx_io_device *, int64_t);
XXFC_API xx_creative_cmf *xx_creative_cmf_create(xx_io_device *, int64_t);
XXFC_API void xx_creative_cmf_destroy(xx_creative_cmf *);
XXFC_API void xx_creative_cmf_free(xx_creative_cmf *);
XXFC_API bool xx_creative_cmf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_creative_cmf_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_creative_cmf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_creative_cmf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_creative_cmf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_creative_cmf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_creative_cmf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

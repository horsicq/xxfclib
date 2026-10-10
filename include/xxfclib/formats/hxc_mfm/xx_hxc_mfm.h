/* SPDX-License-Identifier: MIT */
#ifndef XX_HXC_MFM_H
#define XX_HXC_MFM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_hxc_mfm {
    Abstractformat format;
} xx_hxc_mfm;
XXFC_API void xx_hxc_mfm_init(xx_hxc_mfm *, xx_io_device *, int64_t);
XXFC_API xx_hxc_mfm *xx_hxc_mfm_create(xx_io_device *, int64_t);
XXFC_API void xx_hxc_mfm_destroy(xx_hxc_mfm *);
XXFC_API void xx_hxc_mfm_free(xx_hxc_mfm *);
XXFC_API bool xx_hxc_mfm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hxc_mfm_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hxc_mfm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_hxc_mfm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hxc_mfm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hxc_mfm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_hxc_mfm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

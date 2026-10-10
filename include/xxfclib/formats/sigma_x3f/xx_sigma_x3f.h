/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/LibRaw/LibRaw/master/internal/x3f_tools.h */
#ifndef XX_SIGMA_X3F_H
#define XX_SIGMA_X3F_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sigma_x3f {
    Abstractformat format;
} xx_sigma_x3f;
XXFC_API void xx_sigma_x3f_init(xx_sigma_x3f *, xx_io_device *, int64_t);
XXFC_API xx_sigma_x3f *xx_sigma_x3f_create(xx_io_device *, int64_t);
XXFC_API void xx_sigma_x3f_destroy(xx_sigma_x3f *);
XXFC_API void xx_sigma_x3f_free(xx_sigma_x3f *);
XXFC_API bool xx_sigma_x3f_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sigma_x3f_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sigma_x3f_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sigma_x3f_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sigma_x3f_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sigma_x3f_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sigma_x3f_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

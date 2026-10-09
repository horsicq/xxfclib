/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/theochem/iodata/blob/master/iodata/formats/fchk.py */
#ifndef XX_GAUSSIAN_FCHK_H
#define XX_GAUSSIAN_FCHK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gaussian_fchk { Abstractformat format; } xx_gaussian_fchk;
XXFC_API void xx_gaussian_fchk_init(xx_gaussian_fchk *,xx_io_device *,int64_t);
XXFC_API xx_gaussian_fchk *xx_gaussian_fchk_create(xx_io_device *,int64_t);
XXFC_API void xx_gaussian_fchk_destroy(xx_gaussian_fchk *);
XXFC_API void xx_gaussian_fchk_free(xx_gaussian_fchk *);
XXFC_API bool xx_gaussian_fchk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gaussian_fchk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gaussian_fchk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gaussian_fchk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gaussian_fchk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gaussian_fchk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gaussian_fchk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

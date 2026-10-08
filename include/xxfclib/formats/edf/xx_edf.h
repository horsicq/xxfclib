/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.edfplus.info/specs/edf.html */
#ifndef XX_EDF_H
#define XX_EDF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_edf { Abstractformat format; } xx_edf;
XXFC_API void xx_edf_init(xx_edf *,xx_io_device *,int64_t);
XXFC_API xx_edf *xx_edf_create(xx_io_device *,int64_t);
XXFC_API void xx_edf_destroy(xx_edf *);
XXFC_API void xx_edf_free(xx_edf *);
XXFC_API bool xx_edf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_edf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_edf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_edf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_edf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

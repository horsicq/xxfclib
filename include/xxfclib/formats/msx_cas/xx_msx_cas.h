/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_MSX_CAS_H
#define XX_MSX_CAS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_msx_cas { Abstractformat format; } xx_msx_cas;
XXFC_API void xx_msx_cas_init(xx_msx_cas *,xx_io_device *,int64_t);
XXFC_API xx_msx_cas *xx_msx_cas_create(xx_io_device *,int64_t);
XXFC_API void xx_msx_cas_destroy(xx_msx_cas *);
XXFC_API void xx_msx_cas_free(xx_msx_cas *);
XXFC_API bool xx_msx_cas_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_msx_cas_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_msx_cas_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_msx_cas_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_msx_cas_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_msx_cas_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_msx_cas_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

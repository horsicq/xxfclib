/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_VTECH_VZ_H
#define XX_VTECH_VZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vtech_vz { Abstractformat format; } xx_vtech_vz;
XXFC_API void xx_vtech_vz_init(xx_vtech_vz *,xx_io_device *,int64_t);
XXFC_API xx_vtech_vz *xx_vtech_vz_create(xx_io_device *,int64_t);
XXFC_API void xx_vtech_vz_destroy(xx_vtech_vz *);
XXFC_API void xx_vtech_vz_free(xx_vtech_vz *);
XXFC_API bool xx_vtech_vz_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vtech_vz_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vtech_vz_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vtech_vz_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vtech_vz_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

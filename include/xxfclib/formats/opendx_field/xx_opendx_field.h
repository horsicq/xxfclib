/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.ks.uiuc.edu/Research/vmd/plugins/molfile/dxplugin.html */
#ifndef XX_OPENDX_FIELD_H
#define XX_OPENDX_FIELD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_opendx_field { Abstractformat format; } xx_opendx_field;
XXFC_API void xx_opendx_field_init(xx_opendx_field *,xx_io_device *,int64_t);
XXFC_API xx_opendx_field *xx_opendx_field_create(xx_io_device *,int64_t);
XXFC_API void xx_opendx_field_destroy(xx_opendx_field *);
XXFC_API void xx_opendx_field_free(xx_opendx_field *);
XXFC_API bool xx_opendx_field_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_opendx_field_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_opendx_field_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_opendx_field_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_opendx_field_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

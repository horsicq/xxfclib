/* SPDX-License-Identifier: MIT
 * Wire specification: https://images.autodesk.com/adsk/files/autocad_2014_pdf_dxf_reference_enu.pdf */
#ifndef XX_AUTOCAD_DXF_H
#define XX_AUTOCAD_DXF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autocad_dxf { Abstractformat format; } xx_autocad_dxf;
XXFC_API void xx_autocad_dxf_init(xx_autocad_dxf *,xx_io_device *,int64_t);
XXFC_API xx_autocad_dxf *xx_autocad_dxf_create(xx_io_device *,int64_t);
XXFC_API void xx_autocad_dxf_destroy(xx_autocad_dxf *);
XXFC_API void xx_autocad_dxf_free(xx_autocad_dxf *);
XXFC_API bool xx_autocad_dxf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_autocad_dxf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_autocad_dxf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_autocad_dxf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_autocad_dxf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

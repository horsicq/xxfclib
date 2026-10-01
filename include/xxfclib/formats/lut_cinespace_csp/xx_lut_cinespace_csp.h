/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatCSP.cpp */
#ifndef XX_LUT_CINESPACE_CSP_H
#define XX_LUT_CINESPACE_CSP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lut_cinespace_csp { Abstractformat format; } xx_lut_cinespace_csp;
XXFC_API void xx_lut_cinespace_csp_init(xx_lut_cinespace_csp *,xx_io_device *,int64_t);
XXFC_API xx_lut_cinespace_csp *xx_lut_cinespace_csp_create(xx_io_device *,int64_t);
XXFC_API void xx_lut_cinespace_csp_destroy(xx_lut_cinespace_csp *);
XXFC_API void xx_lut_cinespace_csp_free(xx_lut_cinespace_csp *);
XXFC_API bool xx_lut_cinespace_csp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lut_cinespace_csp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

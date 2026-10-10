/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.xcrysden.org/doc/XSF.html */
#ifndef XX_XCRYSDEN_XSF_H
#define XX_XCRYSDEN_XSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xcrysden_xsf {
    Abstractformat format;
} xx_xcrysden_xsf;
XXFC_API void xx_xcrysden_xsf_init(xx_xcrysden_xsf *, xx_io_device *, int64_t);
XXFC_API xx_xcrysden_xsf *xx_xcrysden_xsf_create(xx_io_device *, int64_t);
XXFC_API void xx_xcrysden_xsf_destroy(xx_xcrysden_xsf *);
XXFC_API void xx_xcrysden_xsf_free(xx_xcrysden_xsf *);
XXFC_API bool xx_xcrysden_xsf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_xcrysden_xsf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xcrysden_xsf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_xcrysden_xsf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xcrysden_xsf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xcrysden_xsf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_xcrysden_xsf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

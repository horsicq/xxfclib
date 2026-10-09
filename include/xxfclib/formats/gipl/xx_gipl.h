/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/InsightSoftwareConsortium/ITK/master/Modules/IO/GIPL/src/itkGiplImageIO.cxx */
#ifndef XX_GIPL_H
#define XX_GIPL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gipl { Abstractformat format; } xx_gipl;
XXFC_API void xx_gipl_init(xx_gipl *,xx_io_device *,int64_t);
XXFC_API xx_gipl *xx_gipl_create(xx_io_device *,int64_t);
XXFC_API void xx_gipl_destroy(xx_gipl *);
XXFC_API void xx_gipl_free(xx_gipl *);
XXFC_API bool xx_gipl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gipl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gipl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gipl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gipl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gipl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gipl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

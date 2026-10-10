/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.vtk.org/en/latest/vtk_file_formats/vtk_legacy_file_format.html */
#ifndef XX_VTK_LEGACY_H
#define XX_VTK_LEGACY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vtk_legacy {
    Abstractformat format;
} xx_vtk_legacy;
XXFC_API void xx_vtk_legacy_init(xx_vtk_legacy *, xx_io_device *, int64_t);
XXFC_API xx_vtk_legacy *xx_vtk_legacy_create(xx_io_device *, int64_t);
XXFC_API void xx_vtk_legacy_destroy(xx_vtk_legacy *);
XXFC_API void xx_vtk_legacy_free(xx_vtk_legacy *);
XXFC_API bool xx_vtk_legacy_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_vtk_legacy_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vtk_legacy_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_vtk_legacy_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vtk_legacy_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vtk_legacy_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_vtk_legacy_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

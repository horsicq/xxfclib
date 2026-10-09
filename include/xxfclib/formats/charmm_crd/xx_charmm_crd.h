/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.mdanalysis.org/stable/documentation_pages/coordinates/CRD.html */
#ifndef XX_CHARMM_CRD_H
#define XX_CHARMM_CRD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_charmm_crd { Abstractformat format; } xx_charmm_crd;
XXFC_API void xx_charmm_crd_init(xx_charmm_crd *,xx_io_device *,int64_t);
XXFC_API xx_charmm_crd *xx_charmm_crd_create(xx_io_device *,int64_t);
XXFC_API void xx_charmm_crd_destroy(xx_charmm_crd *);
XXFC_API void xx_charmm_crd_free(xx_charmm_crd *);
XXFC_API bool xx_charmm_crd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_charmm_crd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_charmm_crd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_charmm_crd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_charmm_crd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_charmm_crd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_charmm_crd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

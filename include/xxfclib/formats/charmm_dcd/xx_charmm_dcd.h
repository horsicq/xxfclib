/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/openmm/openmm/master/wrappers/python/openmm/app/dcdfile.py */
#ifndef XX_CHARMM_DCD_H
#define XX_CHARMM_DCD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_charmm_dcd { Abstractformat format; } xx_charmm_dcd;
XXFC_API void xx_charmm_dcd_init(xx_charmm_dcd *,xx_io_device *,int64_t);
XXFC_API xx_charmm_dcd *xx_charmm_dcd_create(xx_io_device *,int64_t);
XXFC_API void xx_charmm_dcd_destroy(xx_charmm_dcd *);
XXFC_API void xx_charmm_dcd_free(xx_charmm_dcd *);
XXFC_API bool xx_charmm_dcd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_charmm_dcd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_charmm_dcd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_charmm_dcd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_charmm_dcd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_charmm_dcd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_charmm_dcd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

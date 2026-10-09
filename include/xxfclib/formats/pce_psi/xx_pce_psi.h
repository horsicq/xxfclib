/* SPDX-License-Identifier: MIT */
#ifndef XX_PCE_PSI_H
#define XX_PCE_PSI_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_pce_psi {Abstractformat format;} xx_pce_psi;
XXFC_API void xx_pce_psi_init(xx_pce_psi *,xx_io_device *,int64_t);
XXFC_API xx_pce_psi *xx_pce_psi_create(xx_io_device *,int64_t);
XXFC_API void xx_pce_psi_destroy(xx_pce_psi *);
XXFC_API void xx_pce_psi_free(xx_pce_psi *);
XXFC_API bool xx_pce_psi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pce_psi_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pce_psi_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pce_psi_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pce_psi_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pce_psi_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pce_psi_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

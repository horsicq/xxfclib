/* SPDX-License-Identifier: MIT
 * Wire specification: https://pmc.ncbi.nlm.nih.gov/articles/PMC2892967/ */
#ifndef XX_FCS_H
#define XX_FCS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fcs { Abstractformat format; } xx_fcs;
XXFC_API void xx_fcs_init(xx_fcs *,xx_io_device *,int64_t);
XXFC_API xx_fcs *xx_fcs_create(xx_io_device *,int64_t);
XXFC_API void xx_fcs_destroy(xx_fcs *);
XXFC_API void xx_fcs_free(xx_fcs *);
XXFC_API bool xx_fcs_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_fcs_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fcs_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_fcs_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fcs_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fcs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_fcs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

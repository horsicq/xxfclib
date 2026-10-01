/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.ncbi.nlm.nih.gov/assembly/agp/AGP_Specification/ */
#ifndef XX_GENOMICS_AGP_H
#define XX_GENOMICS_AGP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_agp { Abstractformat format; } xx_genomics_agp;
XXFC_API void xx_genomics_agp_init(xx_genomics_agp *,xx_io_device *,int64_t);
XXFC_API xx_genomics_agp *xx_genomics_agp_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_agp_destroy(xx_genomics_agp *);
XXFC_API void xx_genomics_agp_free(xx_genomics_agp *);
XXFC_API bool xx_genomics_agp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_agp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

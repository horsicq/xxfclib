/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.sas.com/content/dam/sasdam/documents/20260302/record-layout-of-a-sas-version-5-or-6-data-set-in-sas-transport-xport-format.pdf
 * Bounded encoded-component extraction. */
#ifndef XX_SAS_XPORT_H
#define XX_SAS_XPORT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sas_xport { Abstractformat format; } xx_sas_xport;
XXFC_API void xx_sas_xport_init(xx_sas_xport *,xx_io_device *,int64_t);
XXFC_API xx_sas_xport *xx_sas_xport_create(xx_io_device *,int64_t);
XXFC_API void xx_sas_xport_destroy(xx_sas_xport *);
XXFC_API void xx_sas_xport_free(xx_sas_xport *);
XXFC_API bool xx_sas_xport_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sas_xport_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sas_xport_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sas_xport_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sas_xport_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

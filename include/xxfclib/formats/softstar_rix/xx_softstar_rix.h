/* SPDX-License-Identifier: MIT */
#ifndef XX_SOFTSTAR_RIX_H
#define XX_SOFTSTAR_RIX_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_softstar_rix {Abstractformat format;} xx_softstar_rix;
XXFC_API void xx_softstar_rix_init(xx_softstar_rix *,xx_io_device *,int64_t);
XXFC_API xx_softstar_rix *xx_softstar_rix_create(xx_io_device *,int64_t);
XXFC_API void xx_softstar_rix_destroy(xx_softstar_rix *);
XXFC_API void xx_softstar_rix_free(xx_softstar_rix *);
XXFC_API bool xx_softstar_rix_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_softstar_rix_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_softstar_rix_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_softstar_rix_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_softstar_rix_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

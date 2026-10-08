/* SPDX-License-Identifier: MIT */
#ifndef XX_DOSBOX_DRO_H
#define XX_DOSBOX_DRO_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_dosbox_dro {Abstractformat format;} xx_dosbox_dro;
XXFC_API void xx_dosbox_dro_init(xx_dosbox_dro *,xx_io_device *,int64_t);
XXFC_API xx_dosbox_dro *xx_dosbox_dro_create(xx_io_device *,int64_t);
XXFC_API void xx_dosbox_dro_destroy(xx_dosbox_dro *);
XXFC_API void xx_dosbox_dro_free(xx_dosbox_dro *);
XXFC_API bool xx_dosbox_dro_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dosbox_dro_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dosbox_dro_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dosbox_dro_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dosbox_dro_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

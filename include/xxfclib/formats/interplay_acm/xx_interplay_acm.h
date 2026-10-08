/* SPDX-License-Identifier: MIT. Bounded native interplay_acm reader. */
#ifndef XX_INTERPLAY_ACM_H
#define XX_INTERPLAY_ACM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_interplay_acm { Abstractformat format; } xx_interplay_acm;
XXFC_API void xx_interplay_acm_init(xx_interplay_acm *,xx_io_device *,int64_t);
XXFC_API xx_interplay_acm *xx_interplay_acm_create(xx_io_device *,int64_t);
XXFC_API void xx_interplay_acm_destroy(xx_interplay_acm *);
XXFC_API void xx_interplay_acm_free(xx_interplay_acm *);
XXFC_API bool xx_interplay_acm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_interplay_acm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_interplay_acm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_interplay_acm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_interplay_acm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

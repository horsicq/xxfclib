/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_TRACKER_DTT_H
#define XX_TRACKER_DTT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_dtt { Abstractformat format; } xx_tracker_dtt;
XXFC_API void xx_tracker_dtt_init(xx_tracker_dtt *,xx_io_device *,int64_t);
XXFC_API xx_tracker_dtt *xx_tracker_dtt_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_dtt_destroy(xx_tracker_dtt *);
XXFC_API void xx_tracker_dtt_free(xx_tracker_dtt *);
XXFC_API bool xx_tracker_dtt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_dtt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_dtt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_dtt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_dtt_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif

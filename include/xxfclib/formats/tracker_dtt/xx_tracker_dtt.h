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
#endif

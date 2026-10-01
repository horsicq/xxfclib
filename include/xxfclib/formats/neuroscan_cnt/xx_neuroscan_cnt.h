/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/mne-tools/mne-python/blob/main/mne/io/cnt/cnt.py */
#ifndef XX_NEUROSCAN_CNT_H
#define XX_NEUROSCAN_CNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_neuroscan_cnt { Abstractformat format; } xx_neuroscan_cnt;
XXFC_API void xx_neuroscan_cnt_init(xx_neuroscan_cnt *,xx_io_device *,int64_t);
XXFC_API xx_neuroscan_cnt *xx_neuroscan_cnt_create(xx_io_device *,int64_t);
XXFC_API void xx_neuroscan_cnt_destroy(xx_neuroscan_cnt *);
XXFC_API void xx_neuroscan_cnt_free(xx_neuroscan_cnt *);
XXFC_API bool xx_neuroscan_cnt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_neuroscan_cnt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

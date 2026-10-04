/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_MICRAL_N_RAW_H
#define XX_MICRAL_N_RAW_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_micral_n_raw;
typedef xx_micral_n_raw xx_micral_n_raw_t;
XXFC_API void xx_micral_n_raw_init(xx_micral_n_raw *,xx_io_device *,int64_t);
XXFC_API xx_micral_n_raw *xx_micral_n_raw_create(xx_io_device *,int64_t);
XXFC_API void xx_micral_n_raw_destroy(xx_micral_n_raw *);
XXFC_API void xx_micral_n_raw_free(xx_micral_n_raw *);
XXFC_API bool xx_micral_n_raw_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_micral_n_raw_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: MIT */
#ifndef XX_RDOS_RAW_H
#define XX_RDOS_RAW_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_rdos_raw {Abstractformat format;} xx_rdos_raw;
XXFC_API void xx_rdos_raw_init(xx_rdos_raw *,xx_io_device *,int64_t);
XXFC_API xx_rdos_raw *xx_rdos_raw_create(xx_io_device *,int64_t);
XXFC_API void xx_rdos_raw_destroy(xx_rdos_raw *);
XXFC_API void xx_rdos_raw_free(xx_rdos_raw *);
XXFC_API bool xx_rdos_raw_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rdos_raw_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif

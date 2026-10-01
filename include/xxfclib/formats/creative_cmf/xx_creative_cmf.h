/* SPDX-License-Identifier: MIT */
#ifndef XX_CREATIVE_CMF_H
#define XX_CREATIVE_CMF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_creative_cmf {Abstractformat format;} xx_creative_cmf;
XXFC_API void xx_creative_cmf_init(xx_creative_cmf *,xx_io_device *,int64_t);
XXFC_API xx_creative_cmf *xx_creative_cmf_create(xx_io_device *,int64_t);
XXFC_API void xx_creative_cmf_destroy(xx_creative_cmf *);
XXFC_API void xx_creative_cmf_free(xx_creative_cmf *);
XXFC_API bool xx_creative_cmf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_creative_cmf_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT */
#ifndef XX_SAR_H
#define XX_SAR_H
#include "xxfclib/formats/lha/xx_lha.h"
#ifdef __cplusplus
extern "C" {
#endif
/* SAR's member grammar is LHA level 0/1 with spaced uppercase LH tags. */
typedef xx_lha xx_sar;
XXFC_API void xx_sar_init(xx_sar *, xx_io_device *, int64_t);
XXFC_API xx_sar *xx_sar_create(xx_io_device *, int64_t);
XXFC_API void xx_sar_destroy(xx_sar *);
XXFC_API void xx_sar_free(xx_sar *);
XXFC_API bool xx_sar_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sar_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_sar_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif

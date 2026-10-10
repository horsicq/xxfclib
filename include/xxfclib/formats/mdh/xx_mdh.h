/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_MDH_H
#define XX_MDH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mdh { Abstractformat format; } xx_mdh;
XXFC_API void xx_mdh_init(xx_mdh *, xx_io_device *, int64_t);
XXFC_API xx_mdh *xx_mdh_create(xx_io_device *, int64_t);
XXFC_API void xx_mdh_destroy(xx_mdh *);
XXFC_API void xx_mdh_free(xx_mdh *);
XXFC_API bool xx_mdh_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mdh_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_mdh_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif

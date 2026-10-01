/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SFNT_COLLECTION_H
#define XX_SFNT_COLLECTION_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfnt_collection { Abstractformat format; } xx_sfnt_collection;
XXFC_API void xx_sfnt_collection_init(xx_sfnt_collection *,xx_io_device *,int64_t);
XXFC_API xx_sfnt_collection *xx_sfnt_collection_create(xx_io_device *,int64_t);
XXFC_API void xx_sfnt_collection_destroy(xx_sfnt_collection *);
XXFC_API void xx_sfnt_collection_free(xx_sfnt_collection *);
XXFC_API bool xx_sfnt_collection_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfnt_collection_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

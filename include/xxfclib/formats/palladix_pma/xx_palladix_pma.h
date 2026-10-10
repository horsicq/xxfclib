/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PALLADIX_PMA_H
#define XX_PALLADIX_PMA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_palladix_pma { Abstractformat format; } xx_palladix_pma;
XXFC_API void xx_palladix_pma_init(xx_palladix_pma *, xx_io_device *, int64_t);
XXFC_API xx_palladix_pma *xx_palladix_pma_create(xx_io_device *, int64_t);
XXFC_API void xx_palladix_pma_destroy(xx_palladix_pma *);
XXFC_API void xx_palladix_pma_free(xx_palladix_pma *);
XXFC_API bool xx_palladix_pma_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_palladix_pma_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_palladix_pma_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif

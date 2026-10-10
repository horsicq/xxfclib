/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT */
#ifndef XX_LARC_PFX_H
#define XX_LARC_PFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_larc_pfx {
    Abstractformat format;
} xx_larc_pfx;
XXFC_API void xx_larc_pfx_init(xx_larc_pfx *, xx_io_device *, int64_t);
XXFC_API xx_larc_pfx *xx_larc_pfx_create(xx_io_device *, int64_t);
XXFC_API void xx_larc_pfx_destroy(xx_larc_pfx *);
XXFC_API void xx_larc_pfx_free(xx_larc_pfx *);
XXFC_API bool xx_larc_pfx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_larc_pfx_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_larc_pfx_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif

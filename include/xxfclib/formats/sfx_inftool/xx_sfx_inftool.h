/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SFX_INFTOOL_H
#define XX_SFX_INFTOOL_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/cab/xx_cab.h"
#include "xxfclib/formats/zip/xx_zip.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sfx_inftool {
    Abstractformat format;
    uint8_t *cabinet;
    xx_io_device *cabinet_device;
    xx_cab inner;
    bool inner_ready;
    xx_zip legacy_zip;
    bool zip_ready;
} xx_sfx_inftool;

XXFC_API void xx_sfx_inftool_init(xx_sfx_inftool *, xx_io_device *, int64_t);
XXFC_API xx_sfx_inftool *xx_sfx_inftool_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_inftool_destroy(xx_sfx_inftool *);
XXFC_API void xx_sfx_inftool_free(xx_sfx_inftool *);
XXFC_API bool xx_sfx_inftool_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_inftool_handle_base_info(Abstractformat *, xx_pd_struct *);

#ifdef __cplusplus
}
#endif
#endif

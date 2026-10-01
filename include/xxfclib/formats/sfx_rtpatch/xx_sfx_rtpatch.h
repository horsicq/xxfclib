/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_RTPATCH_H
#define XX_SFX_RTPATCH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
struct xx_rtpatch;
typedef struct xx_sfx_rtpatch {
    Abstractformat format;
    struct xx_rtpatch *inner; /**< Validated v4.00 package at the PE overlay. */
    uint8_t probe_mode;       /**< 0 unknown, 1 nested records, 2 old wrapper. */
} xx_sfx_rtpatch;
XXFC_API void xx_sfx_rtpatch_init(xx_sfx_rtpatch *,xx_io_device *,int64_t);
XXFC_API xx_sfx_rtpatch *xx_sfx_rtpatch_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_rtpatch_destroy(xx_sfx_rtpatch *);
XXFC_API void xx_sfx_rtpatch_free(xx_sfx_rtpatch *);
XXFC_API bool xx_sfx_rtpatch_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_rtpatch_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif

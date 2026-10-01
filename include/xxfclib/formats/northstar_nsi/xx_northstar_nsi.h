/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_NORTHSTAR_NSI_H
#define XX_NORTHSTAR_NSI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Headerless North Star NSI sector dumps. The exact image size chooses one
 * of the three layouts emitted/recognized by Greaseweazle: 35 tracks,
 * 10 sectors/track, and 256-byte FM or 512-byte MFM sectors. In the
 * double-sided MFM layout, all side-0 tracks precede the side-1 tracks in
 * reverse cylinder order. Selection must be explicit, never autodetected. */
typedef struct xx_northstar_nsi_s {
    Abstractformat format;
} xx_northstar_nsi;

XXFC_API void xx_northstar_nsi_init(xx_northstar_nsi *, xx_io_device *,
                                    int64_t);
XXFC_API xx_northstar_nsi *xx_northstar_nsi_create(xx_io_device *, int64_t);
XXFC_API void xx_northstar_nsi_destroy(xx_northstar_nsi *);
XXFC_API void xx_northstar_nsi_free(xx_northstar_nsi *);

#ifdef __cplusplus
}
#endif
#endif

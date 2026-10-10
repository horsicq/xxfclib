/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/mac_ts/xx_mac_ts.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return al_parse(f, s, pd, 9U);
}
AF_DEFINE_READER(mac_ts, XX_FILE_TYPE_MAC_TS, "img")

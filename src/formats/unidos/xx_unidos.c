/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/unidos/xx_unidos.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return al_parse(f, s, pd, 3U);
}
AF_DEFINE_READER(unidos, XX_FILE_TYPE_UNIDOS, "dsk")

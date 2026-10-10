/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/ozdos/xx_ozdos.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return al_parse(f, s, pd, 2U);
}
AF_DEFINE_READER(ozdos, XX_FILE_TYPE_OZDOS, "dsk")

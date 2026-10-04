/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/apple_dos_hybrid/xx_apple_dos_hybrid.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return al_parse(f,s,pd,5U); }
AF_DEFINE_READER(apple_dos_hybrid,XX_FILE_TYPE_APPLE_DOS_HYBRID,"dsk")

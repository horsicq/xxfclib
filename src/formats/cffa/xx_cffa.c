/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/cffa/xx_cffa.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return al_parse(f,s,pd,4U); }
AF_DEFINE_READER(cffa,XX_FILE_TYPE_CFFA,"img")

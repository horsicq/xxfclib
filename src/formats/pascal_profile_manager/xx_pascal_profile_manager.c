/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/pascal_profile_manager/xx_pascal_profile_manager.h"
#include "../apple_family/xx_apple_layouts.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return al_parse(f,s,pd,10U); }
AF_DEFINE_READER(pascal_profile_manager,XX_FILE_TYPE_PASCAL_PROFILE_MANAGER,"po")

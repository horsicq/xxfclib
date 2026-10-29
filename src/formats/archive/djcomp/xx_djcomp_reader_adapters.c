/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/archive/djcomp/xx_djcomp.h"
XX_FORMAT_DEFINE_READER_ADAPTER(djcomp, XX_FILE_TYPE_DJCOMP,
    xx_djcomp_create(device, 0), xx_djcomp_free, XX_READER_TYPE_FILL_UNKNOWN)

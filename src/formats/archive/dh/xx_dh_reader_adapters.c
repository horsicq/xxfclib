/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/archive/dh/xx_dh.h"
XX_FORMAT_DEFINE_READER_ADAPTER(dh, XX_FILE_TYPE_DH,
    xx_dh_create(device, 0), xx_dh_free, XX_READER_TYPE_FILL_UNKNOWN)

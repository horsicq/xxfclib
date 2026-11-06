/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/installer/dsl1/xx_dsl1.h"
XX_FORMAT_DEFINE_READER_ADAPTER(dsl1, XX_FILE_TYPE_DSL1,
    xx_dsl1_create(device, 0), xx_dsl1_free, XX_READER_TYPE_FILL_UNKNOWN)

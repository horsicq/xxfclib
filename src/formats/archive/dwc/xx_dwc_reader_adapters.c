/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/archive/dwc/xx_dwc.h"
XX_FORMAT_DEFINE_READER_ADAPTER(dwc, XX_FILE_TYPE_DWC,
    xx_dwc_create(device, 0), xx_dwc_free, XX_READER_TYPE_FILL_UNKNOWN)

/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/archive/dpc/xx_dpc.h"
XX_FORMAT_DEFINE_READER_ADAPTER(dpc, XX_FILE_TYPE_DPC,
    xx_dpc_create(device, 0), xx_dpc_free, XX_READER_TYPE_FILL_UNKNOWN)

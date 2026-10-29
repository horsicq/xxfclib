/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../../adapters/xx_reader_adapter.h"
#include "xxfclib/formats/filesystem/dfsee_imz/xx_dfsee_imz.h"
XX_FORMAT_DEFINE_READER_ADAPTER(dfsee_imz, XX_FILE_TYPE_DFSEE_IMZ,
    xx_dfsee_imz_create(device, 0), xx_dfsee_imz_free, XX_READER_TYPE_FILL_UNKNOWN)

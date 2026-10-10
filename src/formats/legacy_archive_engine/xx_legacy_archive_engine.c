/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/legacy_archive_engine/xx_legacy_archive_engine.h"
#include "xxfclib/formats/sevenzip_engine/xx_sevenzip_engine.h"
extern Abstractformat *xx_dgca_native_create(xx_io_device *, int64_t);
extern void xx_dgca_native_free(Abstractformat *);
Abstractformat *xx_uharc_payload_create(xx_io_device *d, int64_t base)
{
    return xx_sevenzip_engine_create_helper(d, base, "UHARC", "xfu_legacy_helper.exe", XX_FILE_TYPE_UHARC, "uha");
}
Abstractformat *xx_dgca_create(xx_io_device *d, int64_t base)
{
    return xx_dgca_native_create(d, base);
}
void xx_legacy_archive_free(Abstractformat *f)
{
    if (f && f->file_type == XX_FILE_TYPE_DGCA) xx_dgca_native_free(f);
    else xx_sevenzip_engine_free(f);
}

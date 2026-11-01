/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared parser internals for the optional data-structure stream.
 */
#ifndef XX_ZIP_DATASTRUCTS_INTERNAL_H
#define XX_ZIP_DATASTRUCTS_INTERNAL_H
#include "xxfclib/codepages/xx_codepage.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xx_zip_defs.h"
#include "xx_zip_legacy_encoder.h"
#include "xxfclib/io/xx_io.h"
#include "../../../io/xx_io_policy.h"
#include "xxfclib/global/xx_global.h"
#include "../../../io/platforms/xx_io_platform.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/ppmd7/xx_ppmd7.h"
#include "xxfclib/algo/ppmd8/xx_ppmd8.h"
#include "xxfclib/algo/shrink/xx_shrink.h"
#include "xxfclib/algo/reduce/xx_reduce.h"
#include "xxfclib/algo/implode/xx_implode.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/formats/archive/xz/xx_xz.h"
#include "xxfclib/algo/zipcrypto/xx_zipcrypto.h"
#include "xxfclib/algo/aes_winzip/xx_aes_winzip.h"
#include "xxfclib/algo/cmpsc/xx_cmpsc.h"
#if !defined(XXFC_PROFILE_DIE_ENGINE)
#include "xxfclib/algo/packmp3/xx_packmp3.h"
#include "xxfclib/algo/wavpack/xx_wavpack.h"
#include "xxfclib/algo/winzipjpeg/xx_winzipjpeg.h"
#endif
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

bool xx_zip_parse_cd_entry(xx_zip *zip, xx_io_device *device, int64_t dev_size, int64_t curr_offset, xx_archive_record *rec, int64_t *entry_size_out);

#endif

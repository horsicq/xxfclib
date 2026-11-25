/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nhd.h @brief Native NHD disk-container reader.
 * T98-Next NHD r0 hard-disk image. Lists one disk.img and extracts
 * its raw CHS-ordered sectors. Uses the producer's 2001-01-22 NHD r0 structure
 * specification: 512-byte base header, signature T98HDDIMAGE.R0, declared
 * header length, cylinders/heads/sectors and sector size. Reserved base-header
 * fields must be zero. Supports sector sizes 128..16384 (powers of two),
 * headers up to 1 MiB and images up to 16 TiB; truncated payloads are rejected.
 *
 * Devices are borrowed. All operations restore the input cursor when tell is
 * available. Record names are generated safe flat names. Listing does not
 * allocate the guest image. Extraction streams through at most 64 KiB and
 * honors cancellation, MAX_MEMBER_SIZE and MEMORY_LIMIT options.
 */
#ifndef XXFCLIB_FORMAT_NHD_H
#define XXFCLIB_FORMAT_NHD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_nhd xx_nhd;
typedef struct xx_nhd xx_nhd_t;
struct xx_nhd {
    Abstractformat format;
};

XXFC_API void xx_nhd_init(xx_nhd *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_nhd *xx_nhd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nhd_destroy(xx_nhd *reader);
XXFC_API void xx_nhd_free(xx_nhd *reader);
XXFC_API bool xx_nhd_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_nhd_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream a zero-based member to a borrowed output device. Output starts at
 * its current cursor. Failure may leave partial output; input cursor is restored.
 * Output must differ from input. This function does not close either device.
 */
XXFC_API bool xx_nhd_unpack_to_device(xx_nhd *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_nhd_to_format(xx_nhd *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

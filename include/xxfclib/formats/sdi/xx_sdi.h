/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sdi.h @brief Native SDI disk-container reader.
 * Microsoft System Deployment Image ($SDI0001). Lists and extracts
 * stored BOOT/LOAD/PART/DISK/WIM or other safe named opaque sections. A PART
 * member is a raw partition; its filesystem can be opened with another reader.
 * Uses the 512-byte header and 64-byte TOC records documented by DiscUtils.Sdi.
 * Supports unencoded sections (attributes 0), at most 64 sections, header/TOC
 * page size at most 1 MiB, and sections at most 16 TiB. Header checksum is
 * checked, section bounds and non-overlap are validated, and missing/truncated
 * data is rejected. No boot code is executed.
 *
 * Devices are borrowed. All operations restore the input cursor when tell is
 * available. Record names are generated safe flat names. Listing does not
 * allocate the guest image. Extraction streams through at most 64 KiB and
 * honors cancellation, MAX_MEMBER_SIZE and MEMORY_LIMIT options.
 */
#ifndef XXFCLIB_FORMAT_SDI_H
#define XXFCLIB_FORMAT_SDI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sdi xx_sdi;
typedef struct xx_sdi xx_sdi_t;
struct xx_sdi {
    Abstractformat format;
};

XXFC_API void xx_sdi_init(xx_sdi *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_sdi *xx_sdi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sdi_destroy(xx_sdi *reader);
XXFC_API void xx_sdi_free(xx_sdi *reader);
XXFC_API bool xx_sdi_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_sdi_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream a zero-based member to a borrowed output device. Output starts at
 * its current cursor. Failure may leave partial output; input cursor is restored.
 * Output must differ from input. This function does not close either device.
 */
XXFC_API bool xx_sdi_unpack_to_device(xx_sdi *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_sdi_to_format(xx_sdi *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

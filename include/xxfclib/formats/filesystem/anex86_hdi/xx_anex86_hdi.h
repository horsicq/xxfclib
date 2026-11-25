/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_anex86_hdi.h @brief Native Anex86 HDI hard-disk image reader.
 * Signatureless 32-byte LE geometry header followed by raw CHS-ordered sectors.
 * This supports the hard-disk geometry omitted by the floppy-only FDI reader.
 * Select explicitly; geometry alone is insufficient for safe automatic detection.
 * Source: Aaru.Images/Anex86 Structs.cs, Identify.cs, Read.cs and Write.cs.
 * Lists and extracts disk.img. Header size is 32 bytes..1 MiB; sector size is
 * a power of two in 128..16384; declared data length is a positive unsigned
 * 32-bit byte count and must exactly equal cylinders*heads*sectors*sector_size.
 * Reserved first word must be zero. The payload must be physically present;
 * bytes after the declared payload are reported as overlay, never extracted.
 * Devices are borrowed. Input cursor is restored when tell is available.
 * Listing allocates no guest image. Extraction streams at most 64 KiB and
 * honors cancellation, MAX_MEMBER_SIZE and MEMORY_LIMIT options.
 */
#ifndef XXFCLIB_FORMAT_ANEX86_HDI_H
#define XXFCLIB_FORMAT_ANEX86_HDI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_anex86_hdi xx_anex86_hdi;
typedef struct xx_anex86_hdi xx_anex86_hdi_t;
struct xx_anex86_hdi {
    Abstractformat format;
};
XXFC_API void xx_anex86_hdi_init(xx_anex86_hdi *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_anex86_hdi *xx_anex86_hdi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_anex86_hdi_destroy(xx_anex86_hdi *reader);
XXFC_API void xx_anex86_hdi_free(xx_anex86_hdi *reader);
XXFC_API bool xx_anex86_hdi_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_anex86_hdi_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream the zero-based member to a borrowed device at its current cursor.
 * Output must differ from input. Failure may leave partial output.
 */
XXFC_API bool xx_anex86_hdi_unpack_to_device(xx_anex86_hdi *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_anex86_hdi_to_format(xx_anex86_hdi *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wux.h @brief Native WUX disk-container reader.
 * Wii U WUX deduplicated-sector image. Lists one disk.wud and reconstructs
 * every guest sector through the checked index map. This extracts the original
 * WUD bytes; Wii U filesystem decryption requires separate keys/readers.
 * Source: Cemu src/Cafe/Filesystem/WUD/wud.h and wud.cpp.
 * Header: WUX0 + LE 0x1099d02e, sector size at 8, u64 size at 16,
 * flags at 24; u32 index table at 32, then sector-aligned stored sectors.
 * Supports flags 0, 256-byte-multiple sectors below 256 MiB, at most
 * 4194304 index entries (16 MiB) and a guest image at most 16 TiB.
 *
 * Devices are borrowed. All operations restore the input cursor when tell is
 * available. Record names are generated safe flat names. Listing does not
 * allocate the guest image. Extraction streams through at most 64 KiB and
 * honors cancellation, MAX_MEMBER_SIZE and MEMORY_LIMIT options.
 */
#ifndef XXFCLIB_FORMAT_WUX_H
#define XXFCLIB_FORMAT_WUX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_wux xx_wux;
typedef struct xx_wux xx_wux_t;
struct xx_wux {
    Abstractformat format;
};

XXFC_API void xx_wux_init(xx_wux *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_wux *xx_wux_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_wux_destroy(xx_wux *reader);
XXFC_API void xx_wux_free(xx_wux *reader);
XXFC_API bool xx_wux_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_wux_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream a zero-based member to a borrowed output device. Output starts at
 * its current cursor. Failure may leave partial output; input cursor is restored.
 * Output must differ from input. This function does not close either device.
 */
XXFC_API bool xx_wux_unpack_to_device(xx_wux *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_wux_to_format(xx_wux *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

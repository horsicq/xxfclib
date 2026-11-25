/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_virtual98.h @brief Native VIRTUAL98 disk-container reader.
 * Virtual98 VHD1.00 hard-disk image (unrelated to Microsoft VHD).
 * Lists one disk.img and reconstructs its raw sectors. The 220-byte header
 * contains the sector size at 142 and authoritative sector count at 148.
 * Virtual98 images may be lazily allocated: a physically absent suffix means
 * zero sectors, as in the Aaru Virtual98 producer/reader. A partially present
 * sector is corruption and is rejected. Supports sector sizes 128..16384
 * (powers of two), unsigned 32-bit sector counts and at most 16 TiB.
 * Embedded filesystems are handled by separate filesystem readers.
 *
 * Devices are borrowed. All operations restore the input cursor when tell is
 * available. Record names are generated safe flat names. Listing does not
 * allocate the guest image. Extraction streams through at most 64 KiB and
 * honors cancellation, MAX_MEMBER_SIZE and MEMORY_LIMIT options.
 */
#ifndef XXFCLIB_FORMAT_VIRTUAL98_H
#define XXFCLIB_FORMAT_VIRTUAL98_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_virtual98 xx_virtual98;
typedef struct xx_virtual98 xx_virtual98_t;
struct xx_virtual98 {
    Abstractformat format;
};

XXFC_API void xx_virtual98_init(xx_virtual98 *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_virtual98 *xx_virtual98_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_virtual98_destroy(xx_virtual98 *reader);
XXFC_API void xx_virtual98_free(xx_virtual98 *reader);
XXFC_API bool xx_virtual98_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_virtual98_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream a zero-based member to a borrowed output device. Output starts at
 * its current cursor. Failure may leave partial output; input cursor is restored.
 * Output must differ from input. This function does not close either device.
 */
XXFC_API bool xx_virtual98_unpack_to_device(xx_virtual98 *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_virtual98_to_format(xx_virtual98 *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif

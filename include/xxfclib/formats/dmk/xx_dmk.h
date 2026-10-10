/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dmk.h @brief Native healthy-sector reconstruction of DMK images.
 *
 * Independently implemented from David Keil's original producer description:
 * http://cpmarchives.classiccmp.org/trs80/mirrors/www.discover-net.net/~dmkeil/trs80/trstech.htm#Technical-DMK-disks
 * CRC conventions cross-checked against the producer/emulator implementations:
 * https://github.com/TimothyPMann/xtrs/blob/master/trs_disk.c
 * https://github.com/openMSX/openMSX/blob/master/src/fdc/RawTrack.cc
 * No upstream implementation code is imported.
 *
 * Native format headers, one/two sides and FM/MFM sectors are supported,
 * including doubled FM bytes and header bit 6's compact FM layout. Healthy
 * FM F8/F9/FA/FB data marks and MFM F8/FB marks are retained as actual data.
 * disk.img reconstructs uniform CHS layouts in cylinder/head/sector order.
 * Sector IDs may begin at 0 or 1 and physical interleave/mixed density is
 * allowed. header.bin and track-NNN-side-N.raw preserve the original header
 * and every complete track, including the 128-byte IDAM table and gaps.
 * These are decoded physical track bytes, not a flux capture.
 *
 * Invalid/missing/duplicate sectors, wrong C/H IDs, CRC errors, inconsistent
 * geometry, overlapping/wrapping sectors, header bit 7's legacy density mode,
 * unknown flags and real-device specification files fail the parse. The
 * write-protected header value FF is accepted for read-only extraction.
 * Limits: 255 cylinders, 2 heads, 64 sectors/track, sector sizes 128..8192,
 * track length below 16 KiB, 512 records and at most 128 KiB of sector map.
 * Cancellation, memory/member limits and short I/O are honored; cursor and
 * existing filesystem destinations are preserved on failure. The device is
 * borrowed and must remain open and unchanged while any reader is in use.
 */
#ifndef XXFCLIB_FORMAT_DMK_H
#define XXFCLIB_FORMAT_DMK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dmk {
    Abstractformat format;
} xx_dmk;
typedef xx_dmk xx_dmk_t;
typedef xx_dmk XDmk;
XXFC_API void xx_dmk_init(xx_dmk *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_dmk *xx_dmk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dmk_destroy(xx_dmk *reader);
XXFC_API void xx_dmk_free(xx_dmk *reader);
XXFC_API bool xx_dmk_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_dmk_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream the selected member to a borrowed output device. */
XXFC_API bool xx_dmk_unpack_to_device(xx_dmk *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_jvc.h @brief Tandy CoCo / Dragon JVC (and headerless .dsk)
 * floppy image holding a Dragon DOS or an OS-9 RBF filesystem. */

#ifndef XXFCLIB_FORMAT_JVC_H
#define XXFCLIB_FORMAT_JVC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JVC floppy image with a Dragon DOS or OS-9 volume.
 *
 * Container.  JVC has no magic: the first (size mod 256) bytes are an
 * optional header, then the sectors follow, 256 bytes each, sector by
 * sector, head 0 and head 1 of one track in turn, track by track.  So a
 * sector's logical number (LSN) is also its index in the image.  Header
 * bytes, all optional, and what this reader accepts:
 *   0  sectors per track   1..255 (18 for Dragon DOS)
 *   1  sides               1 or 2
 *   2  sector size code    1 (256 bytes)
 *   3  first sector ID     0 or 1 (does not change the layout)
 *   4  sector attributes   0 (no attribute byte per sector)
 * A header longer than 5 bytes is refused.
 *
 * RS-DOS volumes on the same container are read by rsdos_fs; this reader
 * refuses them.
 *
 * Dragon DOS.  Track 20 is the directory track (LSN 20 * spt, with spt 18
 * on one side and 36 on two).  Its sector 1 carries the allocation bitmap
 * and, at 0xFC..0xFF, tracks, sectors per track (18 / 36) and the ones'
 * complement of both.  Sectors 3..18 hold 160 entries of 25 bytes, ten
 * per sector:
 *   0   flags: 0x80 deleted, 0x20 continued (byte 24 is the index of
 *       the next entry), 0x08 end of the directory, 0x02 protected,
 *       0x01 continuation entry
 *   head entry:          1 name[8], 9 ext[3], 12 four extents
 *   continuation entry:  1 seven extents
 *   24  next entry index when continued, else bytes used in the last
 *       sector (0 = 256)
 * An extent is a big-endian u16 LSN and a u8 sector count.
 *
 * OS-9 RBF.  LSN 0 is the identification sector: DD.TOT (u24 BE, total
 * sectors), DD.TKS (u8), DD.MAP (u16, bitmap bytes), DD.BIT (u16, sectors
 * per cluster), DD.DIR (u24, root directory file descriptor).  A file
 * descriptor sector has FD.ATT at 0 (0x80 directory), FD.SIZ (u32 BE) at 9
 * and up to 48 segments of (u24 LSN, u16 count) from 0x10; a zero count
 * ends the list.  A directory is a file of 32-byte entries: a name of up
 * to 29 bytes whose last byte has bit 7 set (first byte 0 = deleted) and
 * the u24 LSN of the entry's file descriptor.  The root directory starts
 * with ".." and ".".
 *
 * Detection is a late probe: size mod 256 at most 5, a valid header, then
 * either an OS-9 identification sector whose fields agree with each other
 * and with the image size and whose root directory starts with ".." and
 * ".", or a Dragon DOS geometry quad at the directory track together with
 * a directory whose every live entry has a printable name and in-range
 * extents.  An empty volume is valid (no records).
 *
 * Members: Dragon DOS files in directory order as "NAME.EXT"; OS-9 files
 * in directory-walk order with their path joined by '/'.  Directories are
 * not records.  Unsafe characters become '_', Windows device stems get a
 * '_' prefix, and a name that repeats an earlier one (case-insensitive)
 * gets "~<n>" before its extension.  A file whose sectors lie past the end
 * of a truncated image is listed but refuses to unpack.
 */
typedef struct xx_jvc {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;     /**< Bytes from base_address to the end. */
    uint32_t header_size;   /**< JVC header bytes, 0..5. */
    uint32_t filesystem;    /**< 1 Dragon DOS, 2 OS-9 RBF. */
    uint32_t total_sectors; /**< Volume size in sectors, from the FS. */
} xx_jvc;

typedef xx_jvc xx_jvc_t;

XXFC_API void xx_jvc_init(xx_jvc *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_jvc *xx_jvc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_jvc_destroy(xx_jvc *archive);
XXFC_API void xx_jvc_free(xx_jvc *archive);

XXFC_API bool xx_jvc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jvc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_jvc_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_jvc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jvc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jvc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jvc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jvc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jvc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JVC_H */

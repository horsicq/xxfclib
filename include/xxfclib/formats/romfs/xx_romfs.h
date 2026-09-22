/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_romfs.h @brief romfs read-only filesystem reader. */

/* romfs - the read-only Linux filesystem, also used as an initrd payload.
 * Everything is big endian and every structure starts on a 16-byte boundary.
 *
 *   superblock
 *     +0   "-rom1fs-"
 *     +8   u32  full size, the number of accessible bytes in the image
 *     +12  u32  checksum
 *     +16  the volume name, NUL terminated and padded to 16 bytes
 *
 *   file header
 *     +0   u32  offset of the next header; its LOW FOUR BITS carry the type
 *               (0 hard link, 1 directory, 2 regular file, 3 symlink,
 *               4 block device, 5 character device, 6 socket, 7 fifo) in
 *               bits 0..2 and the executable flag in bit 3
 *     +4   u32  spec info - for a directory, the offset of its first entry
 *     +8   u32  size
 *     +12  u32  checksum
 *     +16  the name, NUL terminated and padded to 16 bytes; data follows
 *
 * Every directory begins with "." and ".." entries whose spec fields point
 * back into the tree, so they are skipped by name rather than by offset.
 * The next-header chain and the first-entry pointers are unconstrained
 * offsets, so a hostile image can describe a cycle; the walker keeps a
 * visited set of header offsets and a hard node cap to bound it.
 */

#ifndef XXFCLIB_FORMAT_ROMFS_H
#define XXFCLIB_FORMAT_ROMFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_romfs xx_romfs;
typedef struct xx_romfs xx_romfs_t;
typedef struct xx_romfs XRomfs;

struct xx_romfs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t volume_size;  /**< The superblock's "full size" field. */
    uint32_t checksum;     /**< The superblock's checksum field, unverified. */
    int64_t archive_end;   /**< base_address + volume_size, or -1. */
    void *internal;
};

XXFC_API void xx_romfs_init(xx_romfs *romfs, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_romfs *xx_romfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_romfs_destroy(xx_romfs *romfs);
XXFC_API void xx_romfs_free(xx_romfs *romfs);

XXFC_API bool xx_romfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_romfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_romfs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_romfs_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_romfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_romfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_romfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_romfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_romfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_romfs_get_number_of_records(const xx_romfs *romfs);
XXFC_API uint64_t xx_romfs_get_number_of_members(const xx_romfs *romfs);
XXFC_API uint32_t xx_romfs_get_volume_size(const xx_romfs *romfs);
XXFC_API uint32_t xx_romfs_get_checksum(const xx_romfs *romfs);
XXFC_API int64_t xx_romfs_get_archive_end(const xx_romfs *romfs);

static inline Abstractformat *xx_romfs_to_format(xx_romfs *romfs) {
    return romfs ? &romfs->format : NULL;
}
static inline void XRomfs_init(xx_romfs *romfs, xx_io_device *dev,
                               int64_t base_address) {
    xx_romfs_init(romfs, dev, base_address);
}
static inline xx_romfs *XRomfs_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_romfs_create(dev, base_address);
}
static inline void XRomfs_free(xx_romfs *romfs) { xx_romfs_free(romfs); }
static inline bool XRomfs_is_valid(xx_romfs *romfs, xx_pd_struct *pd) {
    return romfs ? xx_format_is_valid(&romfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ROMFS_H */

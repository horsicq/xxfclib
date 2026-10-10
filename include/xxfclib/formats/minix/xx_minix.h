/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_minix.h @brief MINIX filesystem (v1, v2, v3) reader. */

/* MINIX filesystem image, versions 1, 2 and 3, either byte order.
 *
 *   byte 0      boot block
 *   byte 1024   superblock
 *     v1/v2:  +0 u16 ninodes, +2 u16 nzones (v1), +4 u16 imap_blocks,
 *             +6 u16 zmap_blocks, +8 u16 firstdatazone, +10 u16
 *             log_zone_size, +12 u32 max_size, +16 u16 magic, +18 u16
 *             state, +20 u32 zones (v2)
 *             magic 0x137F / 0x138F (v1, 14 / 30 byte names),
 *                   0x2468 / 0x2478 (v2, 14 / 30 byte names)
 *     v3:     +0 u32 ninodes, +6 u16 imap_blocks, +8 u16 zmap_blocks,
 *             +10 u16 firstdatazone, +12 u16 log_zone_size, +16 u32
 *             max_size, +20 u32 zones, +24 u16 magic 0x4D5A, +28 u16
 *             block size, +30 u8 disk version; 60-byte names
 *   block 2     inode bitmap, then zone bitmap, then the inode table
 *               (block size 1024 for v1/v2, the superblock's for v3)
 *   zone z      at byte z * (block size << log_zone_size)
 *
 *   inode v1 (32 bytes): u16 mode, u16 uid, u32 size, u32 time, u8 gid,
 *       u8 nlinks, u16 zone[9] - 7 direct, single and double indirect
 *   inode v2/v3 (64 bytes): u16 mode, u16 nlinks, u16 uid, u16 gid,
 *       u32 size, u32 atime, u32 mtime, u32 ctime, u32 zone[10] - 7 direct,
 *       single, double and triple indirect
 *   directory entry: u16 (v1/v2) or u32 (v3) inode, then the name padded
 *       with NULs to the fixed name length; inode 0 marks a free slot
 *
 * The root directory is inode 1. The byte order is the one in which the
 * magic reads correctly. A zone pointer of 0 inside a file is a hole and
 * reads as zeros. Directories and regular files become records; symbolic
 * links, devices, fifos and sockets carry no extractable payload and are
 * skipped. A directory reachable twice (a loop) is walked once.
 */

#ifndef XXFCLIB_FORMAT_MINIX_H
#define XXFCLIB_FORMAT_MINIX_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_minix xx_minix;
typedef struct xx_minix xx_minix_t;
typedef struct xx_minix XMinix;

struct xx_minix {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;     /**< 1, 2 or 3; 0 before base info. */
    uint32_t name_length; /**< 14, 30 or 60. */
    uint32_t block_size;  /**< 1024 for v1/v2. */
    uint32_t zone_size;   /**< block_size << log_zone_size. */
    uint64_t inode_count;
    uint64_t zone_count;
    bool big_endian;
    void *internal;
};

XXFC_API void xx_minix_init(xx_minix *minix, xx_io_device *dev, int64_t base_address);
XXFC_API xx_minix *xx_minix_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_minix_destroy(xx_minix *minix);
XXFC_API void xx_minix_free(xx_minix *minix);

XXFC_API bool xx_minix_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_minix_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_minix_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_minix_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_minix_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_minix_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_minix_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_minix_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_minix_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_minix_get_number_of_records(const xx_minix *minix);
XXFC_API uint32_t xx_minix_get_version(const xx_minix *minix);
XXFC_API uint32_t xx_minix_get_name_length(const xx_minix *minix);
XXFC_API uint32_t xx_minix_get_block_size(const xx_minix *minix);

static inline Abstractformat *xx_minix_to_format(xx_minix *minix)
{
    return minix ? &minix->format : NULL;
}
static inline void XMinix_init(xx_minix *minix, xx_io_device *dev, int64_t base_address)
{
    xx_minix_init(minix, dev, base_address);
}
static inline xx_minix *XMinix_create(xx_io_device *dev, int64_t base_address)
{
    return xx_minix_create(dev, base_address);
}
static inline void XMinix_free(xx_minix *minix)
{
    xx_minix_free(minix);
}
static inline bool XMinix_is_valid(xx_minix *minix, xx_pd_struct *pd)
{
    return minix ? xx_format_is_valid(&minix->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MINIX_H */

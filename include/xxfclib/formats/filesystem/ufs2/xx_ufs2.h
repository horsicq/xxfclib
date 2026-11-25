/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ufs2.h @brief UFS2 (FreeBSD / NetBSD FFSv2) filesystem reader. */

/* UFS2 as written by FreeBSD newfs -O2 and makefs -t ffs -o version=2, in
 * either byte order (the filesystem is stored in host order).
 *
 *   superblock at byte 65536 (or 262144), fields used here:
 *     +0x10 u32 fs_iblkno   inode table, frags from the group start
 *     +0x2C u32 fs_ncg      cylinder groups
 *     +0x30 u32 fs_bsize    block size (power of two, 4096..65536)
 *     +0x34 u32 fs_fsize    fragment size (power of two, >= 512)
 *     +0x38 u32 fs_frag     bsize / fsize (1, 2, 4 or 8)
 *     +0x50 u32 fs_bshift / +0x54 u32 fs_fshift
 *     +0x74 u32 fs_nindir   bsize / 8
 *     +0x78 u32 fs_inopb    bsize / 256
 *     +0xB8 u32 fs_ipg      inodes per group
 *     +0xBC u32 fs_fpg      fragments per group
 *     +0x3E8 u64 fs_sblockloc  byte offset of this superblock
 *     +0x438 u64 fs_size   filesystem size in fragments
 *     +0x55C u32 fs_magic   0x19540119 (0x19012038 for NetBSD UFS2ea)
 *   group c starts at fragment c * fpg (no rotation in UFS2)
 *   inode i (256 bytes) sits at fragment c * fpg + iblkno, c = i / ipg,
 *       plus (i % ipg) * 256 bytes
 *   inode: +0 u16 mode, +16 u64 size, +112 u64 db[12], +208 u64 ib[3]
 *       (single, double, triple indirect), all addresses in fragments;
 *       a zero address inside a file is a hole and reads as zeros
 *   directory: 512-byte chunks of entries u32 ino, u16 reclen, u8 type,
 *       u8 namlen, name (NUL terminated, padded to 4); ino 0 = free entry
 *
 * The root directory is inode 2. Directories and regular files become
 * records; symbolic links, devices, fifos, sockets and whiteouts are
 * skipped, as are extended attribute blocks. A directory reachable twice is
 * walked once.
 */

#ifndef XXFCLIB_FORMAT_UFS2_H
#define XXFCLIB_FORMAT_UFS2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ufs2 xx_ufs2;
typedef struct xx_ufs2 xx_ufs2_t;
typedef struct xx_ufs2 XUfs2;

struct xx_ufs2 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t block_size;       /**< fs_bsize; 0 before base info. */
    uint32_t fragment_size;    /**< fs_fsize. */
    uint32_t group_count;      /**< fs_ncg. */
    uint64_t fragment_count;   /**< fs_size. */
    int64_t superblock_offset; /**< 65536 or 262144, from the base. */
    bool big_endian;
    void *internal;
};

XXFC_API void xx_ufs2_init(xx_ufs2 *ufs2, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ufs2 *xx_ufs2_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ufs2_destroy(xx_ufs2 *ufs2);
XXFC_API void xx_ufs2_free(xx_ufs2 *ufs2);

XXFC_API bool xx_ufs2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ufs2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ufs2_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ufs2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ufs2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ufs2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ufs2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ufs2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ufs2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ufs2_get_number_of_records(const xx_ufs2 *ufs2);
XXFC_API uint32_t xx_ufs2_get_block_size(const xx_ufs2 *ufs2);
XXFC_API uint32_t xx_ufs2_get_fragment_size(const xx_ufs2 *ufs2);
XXFC_API int64_t xx_ufs2_get_superblock_offset(const xx_ufs2 *ufs2);
XXFC_API bool xx_ufs2_is_big_endian(const xx_ufs2 *ufs2);

static inline Abstractformat *xx_ufs2_to_format(xx_ufs2 *ufs2)
{
    return ufs2 ? &ufs2->format : NULL;
}
static inline void XUfs2_init(xx_ufs2 *ufs2, xx_io_device *dev, int64_t base_address)
{
    xx_ufs2_init(ufs2, dev, base_address);
}
static inline xx_ufs2 *XUfs2_create(xx_io_device *dev, int64_t base_address)
{
    return xx_ufs2_create(dev, base_address);
}
static inline void XUfs2_free(xx_ufs2 *ufs2)
{
    xx_ufs2_free(ufs2);
}
static inline bool XUfs2_is_valid(xx_ufs2 *ufs2, xx_pd_struct *pd)
{
    return ufs2 ? xx_format_is_valid(&ufs2->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UFS2_H */

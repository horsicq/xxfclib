/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sufs.h @brief Solaris UFS1 filesystem reader. */

/* Solaris / illumos UFS (UFS1 layout with the old directory format), either
 * byte order: SPARC images are big-endian, x86 images little-endian.
 *
 *   byte 0       boot block area (8 KiB)
 *   byte 8192    superblock (struct fs), fields used here:
 *     +0x10 u32 fs_iblkno   inode table, frags from the group start
 *     +0x18 u32 fs_cgoffset / +0x1C u32 fs_cgmask  group rotation
 *     +0x24 u32 fs_size     filesystem size in fragments
 *     +0x2C u32 fs_ncg      cylinder groups
 *     +0x30 u32 fs_bsize    block size (power of two, 4096..65536)
 *     +0x34 u32 fs_fsize    fragment size (power of two, >= 512)
 *     +0x38 u32 fs_frag     bsize / fsize (1, 2, 4 or 8)
 *     +0x50 u32 fs_bshift / +0x54 u32 fs_fshift
 *     +0x74 u32 fs_nindir   bsize / 4
 *     +0x78 u32 fs_inopb    bsize / 128
 *     +0xB8 u32 fs_ipg      inodes per group
 *     +0xBC u32 fs_fpg      fragments per group
 *     +0x52C u32 reserved (4.4BSD fs_inodefmt; 2 there means BSD format)
 *     +0x550 u32 fs_nrpos   8
 *     +0x55C u32 fs_magic   0x00011954
 *   group c starts at fragment  c * fpg + cgoffset * (c & ~cgmask)
 *   inode i (128 bytes) sits at fragment cgstart(i / ipg) + iblkno, plus
 *       (i % ipg) * 128 bytes
 *   inode: +0 u16 mode, +8 u64 size, +0x28 u32 db[12], +0x58 u32 ib[3]
 *       (single, double, triple indirect), all addresses in fragments;
 *       a zero address inside a file is a hole and reads as zeros
 *   directory: 512-byte chunks of entries u32 ino, u16 reclen, u16 namlen,
 *       name (NUL terminated, padded to 4); ino 0 marks a free entry
 *
 * The root directory is inode 2. Directories and regular files become
 * records; symbolic links, devices, fifos, sockets, shadow (ACL) inodes and
 * extended-attribute directories are skipped. A directory reachable twice
 * is walked once. 4.4BSD UFS1 (one-byte d_namlen plus d_type, fs_inodefmt 2)
 * is not accepted. The NeXTSTEP disk-image reader explicitly enables a
 * separate 4.3BSD profile with 1024-byte directory chunks; normal SUFS
 * detection retains the Solaris checks above.
 */

#ifndef XXFCLIB_FORMAT_SUFS_H
#define XXFCLIB_FORMAT_SUFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sufs xx_sufs;
typedef struct xx_sufs xx_sufs_t;
typedef struct xx_sufs XSufs;

struct xx_sufs {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t block_size;      /**< fs_bsize; 0 before base info. */
    uint32_t fragment_size;   /**< fs_fsize. */
    uint32_t group_count;     /**< fs_ncg. */
    uint64_t fragment_count;  /**< fs_size. */
    bool big_endian;
    /* Explicitly enabled by the NeXT disk-image wrapper only. */
    bool nextstep_legacy;
    void *internal;
};

XXFC_API void xx_sufs_init(xx_sufs *sufs, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_sufs *xx_sufs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_sufs_destroy(xx_sufs *sufs);
XXFC_API void xx_sufs_free(xx_sufs *sufs);
XXFC_API void xx_sufs_enable_nextstep_legacy(xx_sufs *sufs);

XXFC_API bool xx_sufs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sufs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sufs_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_sufs_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sufs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sufs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sufs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sufs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sufs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_sufs_get_number_of_records(const xx_sufs *sufs);
XXFC_API uint32_t xx_sufs_get_block_size(const xx_sufs *sufs);
XXFC_API uint32_t xx_sufs_get_fragment_size(const xx_sufs *sufs);
XXFC_API bool xx_sufs_is_big_endian(const xx_sufs *sufs);

static inline Abstractformat *xx_sufs_to_format(xx_sufs *sufs) {
    return sufs ? &sufs->format : NULL;
}
static inline void XSufs_init(xx_sufs *sufs, xx_io_device *dev,
                              int64_t base_address) {
    xx_sufs_init(sufs, dev, base_address);
}
static inline xx_sufs *XSufs_create(xx_io_device *dev, int64_t base_address) {
    return xx_sufs_create(dev, base_address);
}
static inline void XSufs_free(xx_sufs *sufs) { xx_sufs_free(sufs); }
static inline bool XSufs_is_valid(xx_sufs *sufs, xx_pd_struct *pd) {
    return sufs ? xx_format_is_valid(&sufs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SUFS_H */

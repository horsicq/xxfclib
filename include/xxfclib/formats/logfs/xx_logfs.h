/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_logfs.h @brief LogFS log-structured flash filesystem identifier. */

/* LogFS - a log-structured filesystem for raw flash, merged into Linux 2.6.34
 * and removed again in 4.10. The on-disk layout below is taken from the last
 * kernel that carried it, fs/logfs/logfs_abi.h and fs/logfs/super.c.
 *
 * Everything on disk is BIG endian.
 *
 *   struct logfs_segment_header, 0x18 bytes, starts every segment:
 *     +0   be32  crc, over bytes 4..0x18 of this header
 *     +4   be16  pad
 *     +6   u8    type: 1 SEG_SUPER, 2 SEG_JOURNAL, 3 SEG_OSTORE
 *     +7   u8    level
 *     +8   be32  segno
 *     +12  be32  ec, the erase count of this segment
 *     +16  be64  gec, the global erase count
 *
 *   struct logfs_disk_super, 0x100 bytes, SEG_SUPER:
 *     +0    the segment header above
 *     +24   be64  ds_magic, 0x7a3a8e5cb9d5bf67
 *     +32   be32  ds_crc, over bytes 0x24..0x100 of the superblock
 *     +36   u8    ds_ifile_levels
 *     +37   u8    ds_iblock_levels
 *     +38   u8    ds_data_levels
 *     +39   u8    ds_segment_shift, log2 of the segment size
 *     +40   u8    ds_block_shift, log2 of the block size
 *     +41   u8    ds_write_shift, log2 of the write size
 *     +42   u8    pad0[6]
 *     +48   be64  ds_filesystem_size
 *     +56   be32  ds_segment_size
 *     +60   be32  ds_bad_seg_reserve
 *     +64   be64  ds_feature_incompat
 *     +72   be64  ds_feature_ro_compat
 *     +80   be64  ds_feature_compat
 *     +88   be64  ds_feature_flags
 *     +96   be64  ds_root_reserve
 *     +104  be64  ds_speed_reserve
 *     +112  be32  ds_journal_seg[16]
 *     +176  be64  ds_super_ofs[2]
 *     +192  be64  pad3[8]
 *
 * Both CRCs are the Linux kernel's crc32_le seeded with 0xFFFFFFFF and with
 * no final complement - NOT the zlib/PKZIP CRC-32, which complements both
 * ends. The value is stored big endian.
 *
 * A block device carries two copies: one at offset 0 and one at
 * (device_size & ~0xFFF) - 0x1000, per fs/logfs/dev_bdev.c.
 *
 * SCOPE. This reader identifies a LogFS volume and reports its superblock.
 * It deliberately does NOT enumerate files. Reaching a directory entry in
 * LogFS means replaying the journal to find the current inode-file (ifile)
 * mapping, then walking the ifile's block tree; none of that is attempted
 * here, and the format installs no archive-record callbacks so that a caller
 * is told "nothing to list" rather than being handed an invented tree.
 */

#ifndef XXFCLIB_FORMAT_LOGFS_H
#define XXFCLIB_FORMAT_LOGFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Superblock magic, as a host-order 64-bit value read big endian. */
#define XX_LOGFS_MAGIC UINT64_C(0x7a3a8e5cb9d5bf67)

/** Segment header types. */
#define XX_LOGFS_SEG_SUPER 0x01U
#define XX_LOGFS_SEG_JOURNAL 0x02U
#define XX_LOGFS_SEG_OSTORE 0x03U

#define XX_LOGFS_SEGMENT_HEADER_SIZE 0x18U
#define XX_LOGFS_DISK_SUPER_SIZE 0x100U
#define XX_LOGFS_JOURNAL_SEGS 16U

typedef struct xx_logfs xx_logfs;
typedef struct xx_logfs xx_logfs_t;
typedef struct xx_logfs XLogfs;

struct xx_logfs {
    Abstractformat format;

    /* Segment header of the superblock that was accepted. */
    uint32_t segment_number;
    uint32_t erase_count;
    uint64_t global_erase_count;
    uint8_t segment_type;
    uint8_t segment_level;

    /* struct logfs_disk_super. */
    uint8_t ifile_levels;
    uint8_t iblock_levels;
    uint8_t data_levels;
    uint8_t segment_shift;
    uint8_t block_shift;
    uint8_t write_shift;
    uint64_t filesystem_size;
    uint32_t segment_size;
    uint32_t bad_seg_reserve;
    uint64_t feature_incompat;
    uint64_t feature_ro_compat;
    uint64_t feature_compat;
    uint64_t feature_flags;
    uint64_t root_reserve;
    uint64_t speed_reserve;
    uint32_t journal_seg[XX_LOGFS_JOURNAL_SEGS];
    uint64_t super_ofs[2];

    /** Device offset the accepted superblock was read from. */
    int64_t super_offset;
    /** Device offset of the trailing copy, or -1 when out of range. */
    int64_t mirror_offset;
    /** True when the trailing copy also passed magic and both CRCs. */
    bool mirror_valid;

    void *internal;
};

XXFC_API void xx_logfs_init(xx_logfs *logfs, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_logfs *xx_logfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_logfs_destroy(xx_logfs *logfs);
XXFC_API void xx_logfs_free(xx_logfs *logfs);

XXFC_API bool xx_logfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_logfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_logfs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);

XXFC_API uint64_t xx_logfs_get_filesystem_size(const xx_logfs *logfs);
XXFC_API uint32_t xx_logfs_get_segment_size(const xx_logfs *logfs);
XXFC_API uint32_t xx_logfs_get_block_size(const xx_logfs *logfs);
XXFC_API uint32_t xx_logfs_get_write_size(const xx_logfs *logfs);
XXFC_API int64_t xx_logfs_get_super_offset(const xx_logfs *logfs);
XXFC_API bool xx_logfs_get_mirror_valid(const xx_logfs *logfs);

/** The kernel's crc32_le: init 0xFFFFFFFF, reflected, no final complement. */
XXFC_API uint32_t xx_logfs_crc32(const void *data, size_t size);

static inline Abstractformat *xx_logfs_to_format(xx_logfs *logfs) {
    return logfs ? &logfs->format : NULL;
}
static inline void XLogfs_init(xx_logfs *logfs, xx_io_device *dev,
                               int64_t base_address) {
    xx_logfs_init(logfs, dev, base_address);
}
static inline xx_logfs *XLogfs_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_logfs_create(dev, base_address);
}
static inline void XLogfs_free(xx_logfs *logfs) { xx_logfs_free(logfs); }
static inline bool XLogfs_is_valid(xx_logfs *logfs, xx_pd_struct *pd) {
    return logfs ? xx_format_is_valid(&logfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LOGFS_H */

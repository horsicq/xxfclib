/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ext.h @brief ext2/ext3/ext4 filesystem reader. */

/* The ext family shares one on-disk skeleton; the differences between ext2,
 * ext3 and ext4 are expressed entirely through the three feature words in
 * the superblock rather than through a version number.
 *
 *   superblock
 *     Always at byte 1024 of the volume, always 1024 bytes long, always
 *     little endian regardless of the host that wrote it.
 *       +0x18  u32  log_block_size; the block size is 1024 << this
 *       +0x20  u32  blocks_per_group
 *       +0x28  u32  inodes_per_group
 *       +0x38  u16  magic, 0xEF53 - so 1080 from the start of the volume
 *       +0x58  u16  inode_size (revision 1 and later; 128 before that)
 *       +0x5C  u32  feature_compat
 *       +0x60  u32  feature_incompat
 *       +0x64  u32  feature_ro_compat
 *       +0xFE  u16  desc_size, the group descriptor size when 64BIT is set
 *
 *   block group descriptor table
 *     Starts in the block after the superblock's block. Each descriptor is
 *     32 bytes, or desc_size (typically 64) when INCOMPAT_64BIT is set, and
 *     the only field this reader needs is the inode table block at +0x08,
 *     extended by the high half at +0x28 in the 64-byte form.
 *
 *   inode
 *     Found by number: group = (ino - 1) / inodes_per_group, and the inode
 *     sits inodes_per_group-relative at index * inode_size inside that
 *     group's inode table. Inode 2 is the root directory.
 *       +0x00  u16  i_mode; 0x8000 regular, 0x4000 directory, 0xA000 symlink
 *       +0x04  u32  i_size_lo, extended by i_size_high at +0x6C
 *       +0x20  u32  i_flags; 0x00080000 EXT4_EXTENTS_FL
 *       +0x28  60 bytes of i_block[15]
 *
 *   i_block, two entirely different layouts
 *     Without EXT4_EXTENTS_FL it is the classic ext2 block map: twelve
 *     direct block numbers, then a single, a double and a triple indirect
 *     block, each holding block_size/4 further u32 block numbers. A zero
 *     entry is a hole that reads as zeros.
 *     With EXT4_EXTENTS_FL it is the inline root of an extent tree: a
 *     12-byte header with magic 0xF30A, an entry count and a depth, followed
 *     by 12-byte records. At depth 0 those are extents (logical block,
 *     length, 48-bit physical start); above it they are index entries
 *     pointing at a block holding the next level down.
 *
 *   directory
 *     A directory's data blocks hold a linear chain of records:
 *       +0x00  u32  inode, zero for an unused slot
 *       +0x04  u16  rec_len, the distance to the next record
 *       +0x06  u8   name_len - a u16 when INCOMPAT_FILETYPE is absent
 *       +0x07  u8   file_type
 *       +0x08  the name, not NUL terminated
 *     A hashed directory (EXT4_INDEX_FL) stores its tree in records whose
 *     inode field is zero, so reading the same blocks linearly and ignoring
 *     zero-inode records yields exactly the directory's contents.
 *
 * Every block number, extent record and rec_len in an image is attacker
 * controlled, so the walker carries a visited-inode set, a depth cap on both
 * the directory tree and the extent tree, and a cap on the number of runs a
 * single file may describe.
 */

#ifndef XXFCLIB_FORMAT_EXT_H
#define XXFCLIB_FORMAT_EXT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration of a shared enumerator in xxfc_defs.h is still pending.
 * xxfc_defs.h gives every registered format a bare alias macro, so testing
 * for EXT is the same as testing for the enumerator; until it lands the
 * reader reports XX_FILE_TYPE_UNKNOWN rather than borrowing another id. */
#ifdef EXT
#define XX_EXT_FILE_TYPE XX_FILE_TYPE_EXT
#else
#define XX_EXT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Superblock magic, at +0x38 inside the superblock and 1080 in the volume. */
#define XX_EXT_MAGIC 0xEF53U
/** Extent header magic, at the start of every extent tree node. */
#define XX_EXT_EXTENT_MAGIC 0xF30AU

/** feature_incompat bits this reader reasons about. */
#define XX_EXT_INCOMPAT_FILETYPE 0x0002U
#define XX_EXT_INCOMPAT_RECOVER 0x0004U
#define XX_EXT_INCOMPAT_JOURNAL_DEV 0x0008U
#define XX_EXT_INCOMPAT_META_BG 0x0010U
#define XX_EXT_INCOMPAT_EXTENTS 0x0040U
#define XX_EXT_INCOMPAT_64BIT 0x0080U
#define XX_EXT_INCOMPAT_FLEX_BG 0x0200U
#define XX_EXT_INCOMPAT_INLINE_DATA 0x8000U
#define XX_EXT_INCOMPAT_ENCRYPT 0x10000U

/** feature_compat bit that tells ext3 apart from ext2. */
#define XX_EXT_COMPAT_HAS_JOURNAL 0x0004U

typedef struct xx_ext xx_ext;
typedef struct xx_ext xx_ext_t;
typedef struct xx_ext XExt;

struct xx_ext {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t block_size;       /**< 1024 << s_log_block_size. */
    uint64_t block_count;      /**< s_blocks_count, 64-bit when 64BIT is set. */
    uint32_t inode_count;      /**< s_inodes_count. */
    uint32_t inode_size;       /**< 128 on revision 0. */
    uint32_t group_count;      /**< Derived from block and group counts. */
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    int64_t archive_end;       /**< base_address + block_count * block_size. */
    void *internal;
};

XXFC_API void xx_ext_init(xx_ext *ext, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ext *xx_ext_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ext_destroy(xx_ext *ext);
XXFC_API void xx_ext_free(xx_ext *ext);

XXFC_API bool xx_ext_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ext_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ext_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ext_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ext_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ext_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ext_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ext_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ext_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ext_get_number_of_records(const xx_ext *ext);
XXFC_API uint64_t xx_ext_get_number_of_members(const xx_ext *ext);
XXFC_API uint32_t xx_ext_get_block_size(const xx_ext *ext);
XXFC_API uint64_t xx_ext_get_block_count(const xx_ext *ext);
XXFC_API uint32_t xx_ext_get_inode_count(const xx_ext *ext);
XXFC_API int64_t xx_ext_get_archive_end(const xx_ext *ext);

/** "ext4", "ext3" or "ext2", decided from the feature words alone. */
XXFC_API const char *xx_ext_get_generation(const xx_ext *ext);

static inline Abstractformat *xx_ext_to_format(xx_ext *ext) {
    return ext ? &ext->format : NULL;
}
static inline void XExt_init(xx_ext *ext, xx_io_device *dev,
                             int64_t base_address) {
    xx_ext_init(ext, dev, base_address);
}
static inline xx_ext *XExt_create(xx_io_device *dev, int64_t base_address) {
    return xx_ext_create(dev, base_address);
}
static inline void XExt_free(xx_ext *ext) { xx_ext_free(ext); }
static inline bool XExt_is_valid(xx_ext *ext, xx_pd_struct *pd) {
    return ext ? xx_format_is_valid(&ext->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EXT_H */

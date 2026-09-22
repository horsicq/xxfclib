/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_btrfs.h @brief Btrfs filesystem reader. */

/* Btrfs. The on-disk format is described on the Btrfs wiki and, normatively,
 * by fs/btrfs/ctree.h in the Linux kernel. Everything is LITTLE endian.
 *
 *   superblock, 4096 bytes, at 0x10000 with mirrors at 0x4000000 and
 *   0x4000000000 (btrfs_sb_offset(): 16 KiB << 12*mirror):
 *     +0    u8[32] csum, over bytes 32..4096
 *     +32   u8[16] fsid
 *     +48   u64  bytenr, the offset this copy was written to
 *     +64   u64  magic, "_BHRfS_M"
 *     +72   u64  generation
 *     +80   u64  root,        logical address of the root tree
 *     +88   u64  chunk_root,  logical address of the chunk tree
 *     +112  u64  total_bytes
 *     +120  u64  bytes_used
 *     +128  u64  root_dir_objectid, normally 256
 *     +136  u64  num_devices
 *     +144  u32  sectorsize
 *     +148  u32  nodesize
 *     +160  u32  sys_chunk_array_size
 *     +188  u64  incompat_flags
 *     +196  u16  csum_type: 0 CRC32C, 1 XXHASH64, 2 SHA256, 3 BLAKE2B
 *     +198  u8   root_level
 *     +199  u8   chunk_root_level
 *     +201  btrfs_dev_item, 98 bytes; its devid is at +201
 *     +299  char[256] label
 *     +811  u8[2048] sys_chunk_array
 *
 *   tree block header, 101 bytes:
 *     +0    u8[32] csum, over bytes 32..nodesize
 *     +32   u8[16] fsid
 *     +48   u64  bytenr, the logical address of this block
 *     +64   u8[16] chunk_tree_uuid
 *     +80   u64  generation
 *     +88   u64  owner
 *     +96   u32  nritems
 *     +100  u8   level, 0 for a leaf
 *
 *   a leaf then carries nritems btrfs_item of 25 bytes each:
 *     disk_key{u64 objectid, u8 type, u64 offset}, u32 offset, u32 size
 *   where "offset" is measured from the END of the header, so the payload
 *   lives at block + 101 + item.offset. An internal node carries nritems
 *   btrfs_key_ptr of 33 bytes: disk_key, u64 blockptr, u64 generation.
 *
 * Nothing in the tree is reachable without the chunk tree: every "bytenr" is
 * a LOGICAL address. The superblock's sys_chunk_array bootstraps just enough
 * chunks to reach the chunk tree itself, which then maps everything else.
 *
 * SCOPE. This reader reaches a directory listing:
 *   1. superblock identification and metadata, with the CRC32C verified;
 *   2. logical -> physical translation via the system chunk array and the
 *      chunk tree;
 *   3. a directory listing, built from the FS_TREE's INODE_ITEM and DIR_ITEM
 *      records;
 *   4. extraction, but only for a file stored either inline or as one
 *      uncompressed regular extent. Compressed extents (zlib, LZO, zstd),
 *      multi-extent files, RAID0/10/5/6 chunk profiles and multi-device
 *      volumes are listed and marked unsupported rather than guessed at.
 * Only csum_type 0 (CRC32C) is verified; a volume using XXHASH64, SHA256 or
 * BLAKE2B is identified from the superblock but its trees are not walked,
 * because an unverified tree block is attacker-controlled data.
 */

#ifndef XXFCLIB_FORMAT_BTRFS_H
#define XXFCLIB_FORMAT_BTRFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "_BHRfS_M" read as a little-endian 64-bit value. */
#define XX_BTRFS_MAGIC UINT64_C(0x4D5F53665248425F)

#define XX_BTRFS_SUPER_OFFSET INT64_C(0x10000)
#define XX_BTRFS_SUPER_MIRROR1 INT64_C(0x4000000)
#define XX_BTRFS_SUPER_MIRROR2 INT64_C(0x4000000000)
#define XX_BTRFS_SUPER_SIZE 4096U
#define XX_BTRFS_LABEL_SIZE 256U
#define XX_BTRFS_UUID_SIZE 16U

/** btrfs_super_block.csum_type. */
#define XX_BTRFS_CSUM_CRC32C 0U
#define XX_BTRFS_CSUM_XXHASH64 1U
#define XX_BTRFS_CSUM_SHA256 2U
#define XX_BTRFS_CSUM_BLAKE2B 3U

/** btrfs_file_extent_item.compression. */
#define XX_BTRFS_COMPRESS_NONE 0U
#define XX_BTRFS_COMPRESS_ZLIB 1U
#define XX_BTRFS_COMPRESS_LZO 2U
#define XX_BTRFS_COMPRESS_ZSTD 3U

typedef struct xx_btrfs xx_btrfs;
typedef struct xx_btrfs xx_btrfs_t;
typedef struct xx_btrfs XBtrfs;

struct xx_btrfs {
    Abstractformat format;

    uint64_t number_of_records;
    uint64_t number_of_members;

    /* Superblock. */
    uint8_t fsid[XX_BTRFS_UUID_SIZE];
    uint8_t metadata_uuid[XX_BTRFS_UUID_SIZE];
    char label[XX_BTRFS_LABEL_SIZE + 1U];
    uint64_t generation;
    uint64_t root_tree;      /**< Logical address of the root tree. */
    uint64_t chunk_root;     /**< Logical address of the chunk tree. */
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t num_devices;
    uint64_t devid;          /**< This device's id, from btrfs_dev_item. */
    uint64_t incompat_flags;
    uint64_t compat_ro_flags;
    uint32_t sector_size;
    uint32_t node_size;
    uint16_t csum_type;
    uint8_t root_level;
    uint8_t chunk_root_level;

    /** Device offset the accepted superblock copy was read from. */
    int64_t super_offset;
    /** True when csum_type is CRC32C and the stored checksum matched. */
    bool csum_verified;
    /** Number of chunk map entries recovered. */
    uint64_t number_of_chunks;
    /** Logical address of the FS_TREE root, or 0 when it was not found. */
    uint64_t fs_tree_root;
    /** Records listed but not extractable (compressed / multi-extent / ...). */
    uint64_t number_of_unsupported;

    void *internal;
};

XXFC_API void xx_btrfs_init(xx_btrfs *btrfs, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_btrfs *xx_btrfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_btrfs_destroy(xx_btrfs *btrfs);
XXFC_API void xx_btrfs_free(xx_btrfs *btrfs);

XXFC_API bool xx_btrfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_btrfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_btrfs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_btrfs_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_btrfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_btrfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_btrfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_btrfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_btrfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_btrfs_get_label(const xx_btrfs *btrfs);
XXFC_API uint32_t xx_btrfs_get_node_size(const xx_btrfs *btrfs);
XXFC_API uint64_t xx_btrfs_get_number_of_chunks(const xx_btrfs *btrfs);
XXFC_API bool xx_btrfs_get_csum_verified(const xx_btrfs *btrfs);

static inline Abstractformat *xx_btrfs_to_format(xx_btrfs *btrfs) {
    return btrfs ? &btrfs->format : NULL;
}
static inline void XBtrfs_init(xx_btrfs *btrfs, xx_io_device *dev,
                               int64_t base_address) {
    xx_btrfs_init(btrfs, dev, base_address);
}
static inline xx_btrfs *XBtrfs_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_btrfs_create(dev, base_address);
}
static inline void XBtrfs_free(xx_btrfs *btrfs) { xx_btrfs_free(btrfs); }
static inline bool XBtrfs_is_valid(xx_btrfs *btrfs, xx_pd_struct *pd) {
    return btrfs ? xx_format_is_valid(&btrfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BTRFS_H */

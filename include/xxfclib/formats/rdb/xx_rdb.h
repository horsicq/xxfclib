/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rdb.h @brief Amiga Rigid Disk Block (RDB) partitioned hard disk
 * reader (AmigaOS devices/hardblocks.h layout). */

/* An RDB disk carries its partition table in a chain of blocks near the
 * start of the disk. Every field is a big-endian 32-bit long; a block number
 * of 0xFFFFFFFF ends a chain. Every block starts with the same header:
 *
 *     +0   ID           'RDSK', 'PART', 'FSHD', 'LSEG', 'BADB'
 *     +4   SummedLongs  size of the checksummed part of the block, in longs
 *     +8   ChkSum       the SummedLongs longs add up to zero (mod 2^32)
 *     +12  HostID
 *
 *   RigidDiskBlock 'RDSK' (in one of the first 16 blocks of the disk)
 *     +16  BlockBytes        size of an RDB block: 512 on almost every disk
 *     +20  Flags
 *     +24  BadBlockList      first BADB block
 *     +28  PartitionList     first PART block
 *     +32  FileSysHeaderList first FSHD block
 *     +64  Cylinders, +68 Sectors, +72 Heads   physical geometry
 *     +128 RDBBlocksLo, +132 RDBBlocksHi       blocks reserved for the RDB
 *
 *   PartitionBlock 'PART'
 *     +16  Next
 *     +20  Flags (1 = bootable, 2 = do not mount)
 *     +36  DriveName, a BCPL string (length byte + up to 31 characters)
 *     +128 Environment (DosEnvec), longs:
 *          [0] TableSize  [1] SizeBlock (longs per sector)  [3] Surfaces
 *          [4] SectorPerBlock  [5] BlocksPerTrack  [9] LowCyl  [10] HighCyl
 *          [16] DosType
 *
 *   FileSysHeaderBlock 'FSHD'
 *     +16  Next, +32 DosType, +36 Version, +72 SegListBlocks (first LSEG)
 *
 *   LoadSegBlock 'LSEG'
 *     +16  Next, +20 LoadData: SummedLongs - 5 longs of the filesystem
 *          handler, an AmigaDOS hunk executable split across the chain.
 *
 * Block numbers in the RDB are in BlockBytes units from the start of the
 * disk. A partition starts at LowCyl * Surfaces * BlocksPerTrack blocks of
 * BlockBytes bytes and spans HighCyl - LowCyl + 1 cylinders, which is how the
 * Linux amiga partition parser and amitools place it. de_SizeBlock and
 * de_SectorPerBlock describe the filesystem block and do not move the
 * partition; on the usual disk SizeBlock * 4 equals BlockBytes anyway.
 *
 * Records. Every partition is published as "partition<n>" (n is its 1-based
 * position in the PART chain) and carried verbatim, clamped to the bytes the
 * device holds; the drive name and DosType go to the record comment and never
 * become a path. Every filesystem with load code is published as
 * "filesystem<n>" (1-based position in the FSHD chain) whose data is the
 * LoadData of its LSEG chain concatenated, i.e. the handler hunk file.
 * Bad-block (BADB) and drive-init blocks are not published.
 *
 * The format size runs to the end of the farthest RDB block or partition, or
 * to the physical geometry (Cylinders * Heads * Sectors * BlockBytes) or the
 * reserved RDB area when those are larger, never past the device.
 *
 * Hostile input. The RDSK block needs a valid checksum, a power-of-two
 * BlockBytes and SummedLongs that fit in the block. Chains are capped
 * (XX_RDB_MAX_PARTITIONS, XX_RDB_MAX_FILESYSTEMS, XX_RDB_MAX_LSEG_BLOCKS)
 * and stop at a repeated block, at a block of the wrong type or at the end of
 * the device. A PART or FSHD block with a bad checksum is skipped; a
 * filesystem whose LSEG chain is broken, repeats or exceeds the cap is not
 * published. Every block-to-byte conversion is checked in 64 bits.
 */

#ifndef XXFCLIB_FORMAT_RDB_H
#define XXFCLIB_FORMAT_RDB_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most PART blocks followed; a longer chain is cut there. */
#define XX_RDB_MAX_PARTITIONS 128U
/** Most FSHD blocks followed. */
#define XX_RDB_MAX_FILESYSTEMS 64U
/** Most LSEG blocks accepted over all filesystems of one disk. */
#define XX_RDB_MAX_LSEG_BLOCKS 16384U
/** The RDSK block is searched for in the first 16 blocks of 512 bytes. */
#define XX_RDB_SCAN_BLOCKS 16U

typedef struct xx_rdb xx_rdb;
typedef struct xx_rdb xx_rdb_t;
typedef struct xx_rdb XRdb;

/** One published member, as seen by a caller that wants to recurse. */
typedef struct xx_rdb_member_info {
    bool is_filesystem;     /**< false: partition, true: filesystem code. */
    uint32_t index;         /**< 1-based position in its chain. */
    int64_t offset;         /**< Partition: absolute device offset; else -1. */
    int64_t size;           /**< Bytes the record extracts. */
    uint64_t declared_size; /**< Partition: size from the geometry. */
    uint32_t dos_type;      /**< DosType of the partition or filesystem. */
    uint32_t flags;         /**< PART Flags, or FSHD Version for a filesystem. */
    uint32_t low_cyl;       /**< Partition only. */
    uint32_t high_cyl;      /**< Partition only. */
    const char *drive_name; /**< PART DriveName, printable ASCII, "" if unset. */
    const char *name;       /**< Record name, e.g. "partition2". */
} xx_rdb_member_info;

struct xx_rdb {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t block_bytes; /**< RDSK BlockBytes. */
    uint32_t rdsk_block;  /**< Block number that holds the RDSK block. */
    uint32_t partitions;  /**< Partitions published. */
    uint32_t filesystems; /**< Filesystems published. */
    int64_t archive_end;  /**< End of the disk as far as known, or -1. */
    void *internal;
};

XXFC_API void xx_rdb_init(xx_rdb *rdb, xx_io_device *dev, int64_t base_address);
XXFC_API xx_rdb *xx_rdb_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_rdb_destroy(xx_rdb *rdb);
XXFC_API void xx_rdb_free(xx_rdb *rdb);

XXFC_API bool xx_rdb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rdb_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rdb_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_rdb_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rdb_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rdb_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rdb_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rdb_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rdb_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_rdb_get_number_of_records(const xx_rdb *rdb);
XXFC_API uint64_t xx_rdb_get_number_of_members(const xx_rdb *rdb);
XXFC_API uint32_t xx_rdb_get_block_bytes(const xx_rdb *rdb);
XXFC_API int64_t xx_rdb_get_archive_end(const xx_rdb *rdb);

/** Fill info for the index-th published record (partitions first, then
 * filesystems). Requires that base info has already been handled. */
XXFC_API bool xx_rdb_get_member_info(const xx_rdb *rdb, uint64_t index, xx_rdb_member_info *info);

static inline Abstractformat *xx_rdb_to_format(xx_rdb *rdb)
{
    return rdb ? &rdb->format : NULL;
}
static inline void XRdb_init(xx_rdb *rdb, xx_io_device *dev, int64_t base_address)
{
    xx_rdb_init(rdb, dev, base_address);
}
static inline xx_rdb *XRdb_create(xx_io_device *dev, int64_t base_address)
{
    return xx_rdb_create(dev, base_address);
}
static inline void XRdb_free(xx_rdb *rdb)
{
    xx_rdb_free(rdb);
}
static inline bool XRdb_is_valid(xx_rdb *rdb, xx_pd_struct *pd)
{
    return rdb ? xx_format_is_valid(&rdb->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rdb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_rdb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rdb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rdb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_rdb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_RDB_H */

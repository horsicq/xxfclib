/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apm.h @brief Apple Partition Map reader (Inside Macintosh:
 * Devices, "SCSI Manager", Block0 and Partition records). */

/* An APM disk is a sequence of blocks. Block 0 is the Driver Descriptor Map
 * and the partition map follows it, one 512-byte entry per block. Every
 * multi-byte field is big endian.
 *
 *   Driver Descriptor Map (block 0)
 *     +0   u16  sbSig, 'ER' (0x4552)
 *     +2   u16  sbBlkSize, the device block size: 512, 1024, 2048 or 4096
 *     +4   u32  sbBlkCount, blocks on the device
 *     +8   u16  sbDevType
 *     +10  u16  sbDevId
 *     +12  u32  sbData, reserved
 *     +16  u16  sbDrvrCount, followed by 8-byte driver descriptors
 *
 *   partition map entry (map blocks 1..pmMapBlkCnt)
 *     +0   u16  pmSig, 'PM' (0x504D)
 *     +2   u16  pmSigPad, zero
 *     +4   u32  pmMapBlkCnt, number of entries in the map
 *     +8   u32  pmPyPartStart, first block of the partition
 *     +12  u32  pmPartBlkCnt, blocks in the partition
 *     +16  32   pmPartName, NUL padded
 *     +48  32   pmParType, NUL padded ("Apple_partition_map", "Apple_HFS",
 *               "Apple_Driver43", "Apple_Free", ...)
 *     +80  u32  pmLgDataStart
 *     +84  u32  pmDataCnt
 *     +88  u32  pmPartStatus
 *     +92  ...  boot code location, size, entry, checksum
 *     +120 16   pmProcessor
 *
 * Map geometry. On a hard disk the device block is 512 bytes and entry n
 * lives at byte n * 512. A CD-ROM declares 2048-byte blocks, and two layouts
 * exist: the classic Mac OS one keeps the map at 512-byte steps with every
 * block number in 512-byte units, while others step the map (and count the
 * blocks) in 2048-byte units. The reader takes the 512-byte layout when the
 * block at byte 512 is a map entry that describes the map itself (its first
 * block is 1) - that entry cannot exist in the wide layout, where block 1
 * starts at the declared block size - and the declared block size otherwise.
 * xx_apm_get_map_step() reports the unit chosen.
 *
 * Every map entry - including the map itself, drivers and free space - is
 * published as an archive record named "partition<n>", n being the entry's
 * 1-based position in the map. The type and the name the map gives it go to
 * the record comment (and xx_apm_get_partition_info()); the on-disk name is
 * never used as a path. The payload is carried verbatim, clamped to the bytes
 * the device holds; an entry of zero blocks, one starting past the end of
 * the device or one whose block range wraps 32 bits has no payload and is
 * not published.
 *
 * The format size runs to the end of the farthest map entry or partition,
 * or to sbBlkCount * sbBlkSize when that is larger, never past the device.
 *
 * Hostile input. A first entry declaring more than XX_APM_MAX_MAP_ENTRIES
 * entries refuses the map before anything else is read, the walk stops at
 * the first block that is not a map entry or at the end of the device, and
 * every block-to-byte conversion is done in 64 bits and checked against the
 * device.
 */

#ifndef XXFCLIB_FORMAT_APM_H
#define XXFCLIB_FORMAT_APM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Largest pmMapBlkCnt accepted; a larger count refuses the map. Apple's
 * tools create a 63-entry map. The cap also bounds how often overlapping
 * entries can repeat the same bytes on extraction. */
#define XX_APM_MAX_MAP_ENTRIES 256U

typedef struct xx_apm xx_apm;
typedef struct xx_apm xx_apm_t;
typedef struct xx_apm XApm;

/** One published partition, as seen by a caller that wants to recurse. */
typedef struct xx_apm_partition_info {
    int64_t offset;          /**< Absolute device offset of the payload. */
    int64_t size;            /**< Bytes actually present on the device. */
    uint64_t declared_size;  /**< pmPartBlkCnt * map step. */
    uint32_t start_block;    /**< pmPyPartStart, in map-step units. */
    uint32_t block_count;    /**< pmPartBlkCnt, in map-step units. */
    uint32_t status;         /**< pmPartStatus. */
    uint32_t entry_index;    /**< 1-based position in the map. */
    const char *partition_name; /**< pmPartName, printable ASCII, "" if unset. */
    const char *partition_type; /**< pmParType, printable ASCII, "" if unset. */
    const char *name;        /**< Record name, e.g. "partition2". */
} xx_apm_partition_info;

struct xx_apm {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t block_size;     /**< sbBlkSize from the driver descriptor map. */
    uint32_t device_blocks;  /**< sbBlkCount from the driver descriptor map. */
    uint32_t map_step;       /**< Bytes per map entry and per block number. */
    uint32_t map_entries;    /**< pmMapBlkCnt of the first entry. */
    uint32_t entries_read;   /**< Map entries actually found. */
    int64_t archive_end;     /**< End of the farthest partition, or -1. */
    void *internal;
};

XXFC_API void xx_apm_init(xx_apm *apm, xx_io_device *dev, int64_t base_address);
XXFC_API xx_apm *xx_apm_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apm_destroy(xx_apm *apm);
XXFC_API void xx_apm_free(xx_apm *apm);

XXFC_API bool xx_apm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_apm_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_apm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_apm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_apm_get_number_of_records(const xx_apm *apm);
XXFC_API uint64_t xx_apm_get_number_of_members(const xx_apm *apm);
XXFC_API uint32_t xx_apm_get_block_size(const xx_apm *apm);
XXFC_API uint32_t xx_apm_get_map_step(const xx_apm *apm);
XXFC_API int64_t xx_apm_get_archive_end(const xx_apm *apm);

/** Fill info for the index-th published partition. Requires that base info
 * has already been handled. Returns false for an out-of-range index. */
XXFC_API bool xx_apm_get_partition_info(const xx_apm *apm, uint64_t index,
                                        xx_apm_partition_info *info);

static inline Abstractformat *xx_apm_to_format(xx_apm *apm) {
    return apm ? &apm->format : NULL;
}
static inline void XApm_init(xx_apm *apm, xx_io_device *dev,
                             int64_t base_address) {
    xx_apm_init(apm, dev, base_address);
}
static inline xx_apm *XApm_create(xx_io_device *dev, int64_t base_address) {
    return xx_apm_create(dev, base_address);
}
static inline void XApm_free(xx_apm *apm) { xx_apm_free(apm); }
static inline bool XApm_is_valid(xx_apm *apm, xx_pd_struct *pd) {
    return apm ? xx_format_is_valid(&apm->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APM_H */

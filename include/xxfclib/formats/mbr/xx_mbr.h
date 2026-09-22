/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mbr.h @brief MBR (Master Boot Record) partition table reader. */

/* The MBR is the first 512-byte sector of a PC-partitioned disk. It carries
 * boot code, an optional disk signature and a four-entry partition table:
 *
 *   +0    boot code (440 bytes)
 *   +440  u32  disk signature (NT drive serial; 0 on many images)
 *   +444  u16  reserved, usually 0 and 0x5A5A when the disk is read-only
 *   +446  4 x 16-byte partition entries
 *   +510  u16  0xAA55 (stored little endian, i.e. the bytes 0x55 0xAA)
 *
 * One partition entry:
 *   +0   u8   status: 0x80 bootable, 0x00 inactive; anything else is bogus
 *   +1   u24  CHS of the first sector (ignored here, it cannot address a
 *             modern disk and is routinely filled with 0xFE 0xFF 0xFF)
 *   +4   u8   partition type byte
 *   +5   u24  CHS of the last sector (ignored)
 *   +8   u32  first sector, LBA, relative to the start of the disk for a
 *             primary entry and to the enclosing EBR for a logical one
 *   +12  u32  sector count
 *
 * An LBA is always 512 bytes here: the on-disk fields are defined in terms
 * of that unit regardless of the physical sector size of the medium.
 *
 * Extended partitions. A primary entry whose type is 0x05, 0x0F or 0x85 does
 * not hold a filesystem; it delimits a region containing a singly linked list
 * of Extended Boot Records. Each EBR uses the same 512-byte layout, and only
 * its first two table entries mean anything:
 *   entry 0 - the logical partition living in this EBR's own region; its
 *             first-sector field is relative to the EBR sector itself
 *   entry 1 - the next EBR, with a first-sector field relative to the START
 *             OF THE EXTENDED PARTITION, not to this EBR
 * The two different bases are the classic source of bugs, and the chain is a
 * plain untrusted linked list: a crafted image can point an EBR at itself or
 * build an arbitrarily long loop. The walker therefore keeps a visited set of
 * EBR sector offsets and a hard step cap (XX_MBR_MAX_EBR_STEPS).
 *
 * Protective MBR. A GPT disk begins with an MBR whose single entry has type
 * 0xEE and spans the whole medium, so that MBR-only tools see one unknown
 * partition rather than free space. That entry is not a filesystem, so this
 * reader publishes no records for it and reports the disk through
 * xx_mbr_is_protective() instead. Detection should try GPT before MBR, and
 * may additionally require !xx_mbr_is_protective() before claiming a file.
 *
 * The 0x55 0xAA trailer is shared with FAT and NTFS boot sectors, which are
 * themselves valid first sectors of a partitioned image seen at its own base.
 * The reader rejects a sector carrying a plausible BIOS Parameter Block or an
 * "NTFS    " / "EXFAT   " / FAT type string, so a bare filesystem image is
 * left to its own reader.
 *
 * Partitions are published as archive records: header_offset points at the
 * 16-byte table entry that describes the partition, data_offset/
 * compressed_size at the partition payload itself. A caller can hand that
 * range straight back to the format detector and recurse into the filesystem
 * inside.
 */

#ifndef XXFCLIB_FORMAT_MBR_H
#define XXFCLIB_FORMAT_MBR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_mbr xx_mbr;
typedef struct xx_mbr xx_mbr_t;
typedef struct xx_mbr XMbr;

/** One published partition, as seen by a caller that wants to recurse. */
typedef struct xx_mbr_partition_info {
    int64_t offset;         /**< Absolute device offset of the payload. */
    int64_t size;           /**< Bytes actually present on the device. */
    uint64_t declared_size; /**< sector_count * 512, before truncation. */
    uint32_t start_lba;     /**< Absolute first sector of the partition. */
    uint32_t sector_count;  /**< The entry's sector-count field. */
    uint8_t type;           /**< Partition type byte. */
    uint8_t status;         /**< 0x80 when bootable, else 0x00. */
    bool is_logical;        /**< True when reached through the EBR chain. */
    const char *type_name;  /**< Static human-readable type, never NULL. */
    const char *name;       /**< Record name, e.g. "partition1". */
} xx_mbr_partition_info;

struct xx_mbr {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t number_of_primary;   /**< Published primary partitions. */
    uint64_t number_of_logical;   /**< Published logical partitions. */
    uint64_t number_of_extended;  /**< Extended container entries walked. */
    uint32_t disk_signature;      /**< The u32 at offset 440. */
    bool is_protective;           /**< A GPT disk's protective MBR. */
    int64_t archive_end;          /**< End of the farthest partition, or -1. */
    void *internal;
};

XXFC_API void xx_mbr_init(xx_mbr *mbr, xx_io_device *dev, int64_t base_address);
XXFC_API xx_mbr *xx_mbr_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_mbr_destroy(xx_mbr *mbr);
XXFC_API void xx_mbr_free(xx_mbr *mbr);

XXFC_API bool xx_mbr_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mbr_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mbr_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_mbr_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mbr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mbr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mbr_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mbr_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mbr_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_mbr_get_number_of_records(const xx_mbr *mbr);
XXFC_API uint64_t xx_mbr_get_number_of_members(const xx_mbr *mbr);
XXFC_API uint32_t xx_mbr_get_disk_signature(const xx_mbr *mbr);
XXFC_API bool xx_mbr_is_protective(const xx_mbr *mbr);
XXFC_API int64_t xx_mbr_get_archive_end(const xx_mbr *mbr);

/** Human-readable name for a partition type byte; never NULL. */
XXFC_API const char *xx_mbr_type_name(uint8_t type);

/** Fill info for the index-th published partition. Requires that base info
 * has already been handled. Returns false for an out-of-range index. */
XXFC_API bool xx_mbr_get_partition_info(const xx_mbr *mbr, uint64_t index,
                                        xx_mbr_partition_info *info);

static inline Abstractformat *xx_mbr_to_format(xx_mbr *mbr) {
    return mbr ? &mbr->format : NULL;
}
static inline void XMbr_init(xx_mbr *mbr, xx_io_device *dev,
                             int64_t base_address) {
    xx_mbr_init(mbr, dev, base_address);
}
static inline xx_mbr *XMbr_create(xx_io_device *dev, int64_t base_address) {
    return xx_mbr_create(dev, base_address);
}
static inline void XMbr_free(xx_mbr *mbr) { xx_mbr_free(mbr); }
static inline bool XMbr_is_valid(xx_mbr *mbr, xx_pd_struct *pd) {
    return mbr ? xx_format_is_valid(&mbr->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MBR_H */

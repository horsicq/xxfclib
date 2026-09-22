/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gpt.h @brief GUID Partition Table reader (UEFI spec, chapter 5). */

/* A GPT disk starts with a protective MBR at LBA 0, the GPT header at LBA 1
 * and the partition entry array immediately after it. The same two structures
 * are mirrored at the end of the medium: the backup array, then the backup
 * header in the very last LBA. Every multi-byte field is little endian.
 *
 *   GPT header (92 bytes defined; header_size may be larger)
 *     +0   "EFI PART"
 *     +8   u32  revision, 0x00010000 for every published UEFI revision
 *     +12  u32  header_size, 92..LBA size
 *     +16  u32  CRC32 of the first header_size bytes, computed with this
 *               field taken as zero
 *     +20  u32  reserved, must be zero
 *     +24  u64  my_lba, the LBA holding this very header
 *     +32  u64  alternate_lba, the LBA holding the other copy
 *     +40  u64  first_usable_lba
 *     +48  u64  last_usable_lba
 *     +56  16   disk GUID
 *     +72  u64  partition_entry_lba
 *     +80  u32  number_of_partition_entries
 *     +84  u32  size_of_partition_entry, >= 128 and a multiple of 8
 *     +88  u32  CRC32 of number_of_partition_entries * size_of_partition_entry
 *               bytes of the entry array
 *
 *   partition entry (the first 128 bytes; a larger size_of_partition_entry
 *   appends vendor data that is read for the CRC but not interpreted)
 *     +0   16   partition type GUID; all zero means the slot is unused
 *     +16  16   unique partition GUID
 *     +32  u64  starting LBA
 *     +40  u64  ending LBA, INCLUSIVE - the partition is
 *               (ending - starting + 1) blocks long
 *     +48  u64  attributes
 *     +56  72   partition name, 36 UTF-16LE code units, NUL padded
 *
 * A GUID is stored mixed endian: the first three fields are little endian and
 * the remaining eight bytes are in display order, which is why
 * C12A7328-F81F-11D2-BA4B-00A0C93EC93B appears on disk as
 * 28 73 2A C1 1F F8 D2 11 BA 4B 00 A0 C9 3E C9 3B.
 *
 * Both CRCs are the standard reflected CRC-32 (the zlib/PKZIP one), computed
 * with xx_crc32_calc() starting from zero.
 *
 * Failure handling. When the primary header fails - bad signature, bad CRC,
 * impossible geometry - or its entry array fails its CRC, the reader falls
 * back to the backup header in the last LBA and the array that header names.
 * That is the recovery path the spec mandates and the case a disk with a
 * clobbered first sector actually presents. xx_gpt_used_backup() reports
 * which copy was used.
 *
 * Hostile input. number_of_partition_entries and size_of_partition_entry are
 * attacker controlled and multiply into an array size, so both are capped
 * (XX_GPT_MAX_ENTRY_COUNT, XX_GPT_MAX_ENTRY_SIZE) and their product is
 * checked against a byte budget and against the device before a single byte
 * is read. The array CRC is computed by streaming, never by allocating the
 * array.
 *
 * Block size. The spec defines everything in LBAs, not bytes; a 4Kn disk puts
 * its header at byte 4096. The reader probes a 512-byte block first and then
 * a 4096-byte one, accepting whichever produces a header whose my_lba is 1.
 *
 * Partitions are published as archive records - header_offset at the entry
 * that describes them, data_offset/compressed_size at the payload - so a
 * caller can recurse straight into the filesystem inside.
 */

#ifndef XXFCLIB_FORMAT_GPT_H
#define XXFCLIB_FORMAT_GPT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gpt xx_gpt;
typedef struct xx_gpt xx_gpt_t;
typedef struct xx_gpt XGpt;

/** One published partition, as seen by a caller that wants to recurse. */
typedef struct xx_gpt_partition_info {
    int64_t offset;          /**< Absolute device offset of the payload. */
    int64_t size;            /**< Bytes actually present on the device. */
    uint64_t declared_size;  /**< (ending - starting + 1) * block size. */
    uint64_t starting_lba;
    uint64_t ending_lba;     /**< Inclusive, as stored. */
    uint64_t attributes;
    uint32_t entry_index;    /**< Slot in the entry array. */
    const char *type_guid;   /**< Canonical uppercase type GUID text. */
    const char *unique_guid; /**< Canonical uppercase unique GUID text. */
    const char *type_name;   /**< Static human-readable type, never NULL. */
    const char *label;       /**< UTF-8 partition name, "" when unset. */
    const char *name;        /**< Record name, e.g. "partition1". */
} xx_gpt_partition_info;

struct xx_gpt {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t block_size;        /**< 512 or 4096, as probed. */
    uint32_t entry_count;       /**< Declared array slots. */
    uint32_t entry_size;        /**< Declared bytes per slot. */
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint64_t partition_entry_lba;
    bool used_backup;           /**< The primary header or array was bad. */
    bool header_crc_valid;      /**< The header actually used verified. */
    bool entries_crc_valid;     /**< The array actually used verified. */
    int64_t archive_end;        /**< End of the farthest partition, or -1. */
    void *internal;
};

XXFC_API void xx_gpt_init(xx_gpt *gpt, xx_io_device *dev, int64_t base_address);
XXFC_API xx_gpt *xx_gpt_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_gpt_destroy(xx_gpt *gpt);
XXFC_API void xx_gpt_free(xx_gpt *gpt);

XXFC_API bool xx_gpt_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gpt_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gpt_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_gpt_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gpt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gpt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gpt_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gpt_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gpt_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_gpt_get_number_of_records(const xx_gpt *gpt);
XXFC_API uint64_t xx_gpt_get_number_of_members(const xx_gpt *gpt);
XXFC_API uint32_t xx_gpt_get_block_size(const xx_gpt *gpt);
XXFC_API bool xx_gpt_used_backup(const xx_gpt *gpt);
XXFC_API int64_t xx_gpt_get_archive_end(const xx_gpt *gpt);
/** Canonical uppercase disk GUID text, or NULL before base info is handled. */
XXFC_API const char *xx_gpt_get_disk_guid(const xx_gpt *gpt);

/** Human-readable name for a canonical uppercase type GUID; never NULL. */
XXFC_API const char *xx_gpt_type_name(const char *type_guid);

/** Fill info for the index-th published partition. Requires that base info
 * has already been handled. Returns false for an out-of-range index. */
XXFC_API bool xx_gpt_get_partition_info(const xx_gpt *gpt, uint64_t index,
                                        xx_gpt_partition_info *info);

static inline Abstractformat *xx_gpt_to_format(xx_gpt *gpt) {
    return gpt ? &gpt->format : NULL;
}
static inline void XGpt_init(xx_gpt *gpt, xx_io_device *dev,
                             int64_t base_address) {
    xx_gpt_init(gpt, dev, base_address);
}
static inline xx_gpt *XGpt_create(xx_io_device *dev, int64_t base_address) {
    return xx_gpt_create(dev, base_address);
}
static inline void XGpt_free(xx_gpt *gpt) { xx_gpt_free(gpt); }
static inline bool XGpt_is_valid(xx_gpt *gpt, xx_pd_struct *pd) {
    return gpt ? xx_format_is_valid(&gpt->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GPT_H */

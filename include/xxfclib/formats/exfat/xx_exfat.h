/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_exfat.h @brief Native, read-only exFAT volume listing and extraction.
 *
 * Original implementation of Microsoft's exFAT 1.00 on-disk specification:
 * https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification
 * The volume begins at base_address, including for an embedded partition.
 * Both FAT-chain and NoFatChain streams, nested directories, UTF-16 names,
 * the active FAT/allocation bitmap, and compressed/uncompressed up-case
 * tables are supported. Bytes beyond ValidDataLength are returned as zero.
 * Boot, entry-set, up-case-table and name checksums are verified. A valid
 * backup boot region can recover a damaged main region on a one-FAT volume;
 * backup flags cannot safely select the active pair on a two-FAT volume.
 * Input positions are preserved. The device is borrowed and must remain open
 * and unchanged until all readers are released.
 *
 * No journal replay, deleted-file recovery, writing, encryption, or unknown
 * critical/vendor file extensions are implemented. Unrecognized file entry
 * sets, loops, crosslinks and inconsistent allocation fail the parse.
 * Resource limits: 128 million clusters (16 MiB per allocation/claim bitmap),
 * 100,000 records, 1 million runs, 16 million allocated-cluster steps and
 * 4 million directory slots per parse, 64 directory levels, 4096-byte paths.
 * Extraction resolves MAX_MEMBER_SIZE and MEMORY_LIMIT from iterator options
 * before format options. The memory budget covers retained parser allocations,
 * one iterator cursor and a transfer buffer of at most64KiB; initial parsing,
 * generic record/options bookkeeping and caller devices are excluded. Limits
 * are checked before output bytes or file creation. Files are staged in an
 * exclusive sibling and published on success; overwrite defaults to false.
 */
#ifndef XXFCLIB_FORMAT_EXFAT_H
#define XXFCLIB_FORMAT_EXFAT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_exfat {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t volume_size;
    uint32_t bytes_per_sector;
    uint32_t bytes_per_cluster;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t volume_serial;
    uint8_t active_fat;
    bool used_backup_boot;
    void *internal;
} xx_exfat;
typedef xx_exfat xx_exfat_t;
typedef xx_exfat XExfat;

XXFC_API void xx_exfat_init(xx_exfat *volume, xx_io_device *device, int64_t base_address);
XXFC_API xx_exfat *xx_exfat_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_exfat_destroy(xx_exfat *volume);
XXFC_API void xx_exfat_free(xx_exfat *volume);
XXFC_API bool xx_exfat_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_exfat_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_exfat_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_exfat_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_exfat_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_exfat_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_exfat_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_exfat_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Stream a regular member to a borrowed writable device. NULL verifies
 *  readable file bytes. Folders produce no bytes. Input position is preserved;
 *  destination advances and must differ from the volume's device. */
XXFC_API bool xx_exfat_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API void xx_exfat_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_exfat_to_format(xx_exfat *volume)
{
    return volume ? &volume->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_EXFAT_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_fat.h @brief FAT12/FAT16/FAT32 volume reader (read-only).
 *
 * Bounded, read-only listing and extraction of the files in a single FAT
 * volume that begins at the format's base address. The volume is never
 * mounted: the BIOS Parameter Block in the boot sector supplies the geometry,
 * the first FAT supplies the cluster chains, and directory entries are decoded
 * straight out of the data region.
 *
 * FAT HAS NO MAGIC. There is no signature word that says "this is a FAT
 * volume"; identification is entirely structural. This reader accepts a volume
 * only when the whole BPB is self-consistent:
 *
 *   +0   jump, 0xEB xx 0x90 or 0xE9 xx xx
 *   +11  u16  bytes per sector, one of 512 / 1024 / 2048 / 4096
 *   +13  u8   sectors per cluster, a power of two in 1..128
 *   +14  u16  reserved sectors, non-zero (this is what rejects NTFS)
 *   +16  u8   number of FATs, 1 or 2
 *   +17  u16  root entry count  (0 on FAT32, non-zero and 32-byte aligned
 *             against the sector size on FAT12/16)
 *   +19  u16  total sectors, or 0 when the 32-bit field is used
 *   +21  u8   media descriptor, 0xF0 or 0xF8..0xFF
 *   +22  u16  sectors per FAT  (0 on FAT32)
 *   +32  u32  total sectors, used when +19 is 0
 *   +36  u32  sectors per FAT, FAT32 only
 *   +44  u32  first cluster of the root directory, FAT32 only
 *   +510 u16  0x55AA
 *
 * and only after computing the FAT type the way the Microsoft FAT32
 * specification (fatgen103) mandates, from the count of data clusters:
 *
 *   root_dir_sectors = (root_entry_count * 32 + bytes_per_sector - 1)
 *                      / bytes_per_sector
 *   data_sectors     = total_sectors - (reserved + num_fats * fat_size
 *                                       + root_dir_sectors)
 *   cluster_count    = data_sectors / sectors_per_cluster
 *   cluster_count <  4085  -> FAT12
 *   cluster_count < 65525  -> FAT16
 *   otherwise              -> FAT32
 *
 * The "FAT12   " / "FAT16   " / "FAT32   " strings at +54 (FAT12/16) or +82
 * (FAT32) are treated as hints only. Real images routinely leave them blank or
 * wrong, and the cluster count is the only authority.
 *
 * Long file names are reassembled from the VFAT sequence that precedes each
 * 8.3 entry, and are accepted only when the sequence is complete, correctly
 * ordered, and its checksum matches the 8.3 name it claims to describe. An
 * incomplete or mismatched sequence is discarded and the 8.3 name is used.
 *
 * exFAT is NOT supported. Despite the name it shares nothing with FAT beyond
 * the first three bytes of the boot sector: different BPB, a different
 * allocation bitmap, a different directory-entry encoding and an up-case
 * table. An exFAT volume carries a zeroed legacy BPB, so it is rejected here
 * by the bytes-per-sector and reserved-sector checks rather than misparsed.
 *
 * Also deliberately unsupported: writing, the second FAT copy (only FAT #0 is
 * consulted), FAT32 backup boot sectors, the FSInfo sector, free-space
 * accounting, and recovery of deleted entries (a 0xE5 entry is skipped, not
 * resurrected - its first name character is gone and its chain is no longer
 * trustworthy).
 *
 * A hostile image is assumed throughout. A cluster chain can be made to point
 * at itself or to form a loop of any length, and a directory can be made to
 * contain itself; both are bounded here by a visited-cluster bitmap, a global
 * step budget and a directory nesting cap.
 */

#ifndef XXFCLIB_FORMAT_FAT_H
#define XXFCLIB_FORMAT_FAT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which of the three FAT variants a volume turned out to be. */
typedef enum xx_fat_kind_e {
    XX_FAT_KIND_NONE = 0,
    XX_FAT_KIND_FAT12 = 12,
    XX_FAT_KIND_FAT16 = 16,
    XX_FAT_KIND_FAT32 = 32
} xx_fat_kind;

typedef struct xx_fat xx_fat;
typedef struct xx_fat xx_fat_t;
typedef struct xx_fat XFat;

struct xx_fat {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t fat_kind;          /**< One of the xx_fat_kind values. */
    uint32_t bytes_per_sector;
    uint32_t bytes_per_cluster;
    uint32_t cluster_count;     /**< Count of data clusters, the FAT type key. */
    uint32_t root_cluster;      /**< FAT32 root start cluster; 0 on FAT12/16. */
    uint64_t volume_size;       /**< total_sectors * bytes_per_sector. */
    int64_t volume_end;         /**< base_address + volume_size, or -1. */
    void *internal;
};

XXFC_API void xx_fat_init(xx_fat *fat, xx_io_device *dev, int64_t base_address);
XXFC_API xx_fat *xx_fat_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_fat_destroy(xx_fat *fat);
XXFC_API void xx_fat_free(xx_fat *fat);

XXFC_API bool xx_fat_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_fat_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_fat_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_fat_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fat_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fat_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fat_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fat_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fat_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_fat_get_number_of_records(const xx_fat *fat);
XXFC_API uint64_t xx_fat_get_number_of_members(const xx_fat *fat);
XXFC_API uint32_t xx_fat_get_kind(const xx_fat *fat);
XXFC_API uint32_t xx_fat_get_bytes_per_sector(const xx_fat *fat);
XXFC_API uint32_t xx_fat_get_bytes_per_cluster(const xx_fat *fat);
XXFC_API uint32_t xx_fat_get_cluster_count(const xx_fat *fat);
XXFC_API uint32_t xx_fat_get_root_cluster(const xx_fat *fat);
XXFC_API uint64_t xx_fat_get_volume_size(const xx_fat *fat);
XXFC_API int64_t xx_fat_get_volume_end(const xx_fat *fat);
/** Volume label, or NULL. Owned by the reader; valid until it is destroyed. */
XXFC_API const char *xx_fat_get_volume_label(const xx_fat *fat);

static inline Abstractformat *xx_fat_to_format(xx_fat *fat) {
    return fat ? &fat->format : NULL;
}
static inline void XFat_init(xx_fat *fat, xx_io_device *dev,
                             int64_t base_address) {
    xx_fat_init(fat, dev, base_address);
}
static inline xx_fat *XFat_create(xx_io_device *dev, int64_t base_address) {
    return xx_fat_create(dev, base_address);
}
static inline void XFat_free(xx_fat *fat) { xx_fat_free(fat); }
static inline bool XFat_is_valid(xx_fat *fat, xx_pd_struct *pd) {
    return fat ? xx_format_is_valid(&fat->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FAT_H */

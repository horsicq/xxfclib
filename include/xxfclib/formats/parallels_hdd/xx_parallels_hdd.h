/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_parallels_hdd.h @brief Parallels expanding disk image (.hds). */

/* Parallels "expanding" hard disk data file, header version 2 - the .hds
 * file that sits inside a Parallels .hdd bundle, and the single-file image
 * QEMU reads and writes as format "parallels". The layout follows QEMU's
 * docs/interop/parallels.rst. Every field is LITTLE endian.
 *
 *   header, 64 bytes
 *     +0    char[16] magic, "WithoutFreeSpace" (old) or "WithouFreSpacExt"
 *     +16   u32  version, must be 2
 *     +20   u32  heads      guest geometry, informational
 *     +24   u32  cylinders  guest geometry, informational
 *     +28   u32  tracks     the cluster size in 512-byte sectors
 *     +32   u32  bat_entries, the number of BAT entries (disk size in
 *                clusters)
 *     +36   u64  nb_sectors, the VIRTUAL disk size in 512-byte sectors;
 *                an old-magic image only uses the low 32 bits
 *     +44   u32  in_use, 0x746F6E59 while open read/write, 0x312E3276
 *                once closed, 0 from software predating the extension
 *     +48   u32  data_off, first sector of the data area; 0 is allowed in
 *                an old-magic image and means "just after the BAT"
 *     +52   u32  flags, bit 0 = "empty image"
 *     +56   u64  ext_off, sector of the Format Extension cluster, or 0
 *   BAT at +64: bat_entries u32 values, one per guest cluster.
 *
 * A BAT entry of 0 is an unallocated cluster and reads as zeros. Otherwise
 * it is the host position of the cluster - in SECTORS for the old magic and
 * in CLUSTERS for "WithouFreSpacExt".
 *
 * The reader publishes ONE member, the guest disk, nb_sectors * 512 bytes.
 * Mapping follows what QEMU's read path does with an image opened read
 * only, so the extracted bytes match `qemu-img convert -O raw`: a cluster
 * whose host position lies before the data area, or past the last cluster
 * the file can hold, reads as zeros, and the part of a cluster that runs
 * past the end of the file reads as zeros. The Format Extension (dirty
 * bitmaps) never changes guest data and is not parsed. A differential
 * (snapshot) image names its parent only in the bundle's DiskDescriptor.xml,
 * so here its unallocated clusters read as zeros.
 *
 * Nothing proportional to the BAT is allocated: entries are fetched in small
 * chunks as the guest disk is produced.
 */

#ifndef XXFCLIB_FORMAT_PARALLELS_HDD_H
#define XXFCLIB_FORMAT_PARALLELS_HDD_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_parallels_hdd xx_parallels_hdd;
typedef struct xx_parallels_hdd xx_parallels_hdd_t;

struct xx_parallels_hdd {
    Abstractformat format;
    uint64_t number_of_records;   /**< Always 1: the guest disk image. */
    uint64_t virtual_size;        /**< Guest-visible disk size in bytes. */
    uint64_t nb_sectors;          /**< As used (old magic: low 32 bits). */
    uint64_t ext_off;             /**< Format Extension sector, 0 if none. */
    uint64_t data_start;          /**< Effective data area start, sectors. */
    uint64_t allocated_clusters;  /**< BAT entries that map to file data. */
    uint32_t version;             /**< Always 2. */
    uint32_t heads;
    uint32_t cylinders;
    uint32_t tracks;              /**< Sectors per cluster. */
    uint32_t cluster_size;        /**< tracks * 512. */
    uint32_t bat_entries;
    uint32_t in_use;
    uint32_t data_off;            /**< The header field as stored. */
    uint32_t flags;
    bool is_extended;             /**< "WithouFreSpacExt": BAT in clusters. */
    bool truncated;               /**< Some mapped data lies past the end. */
};

/** Size of the fixed header that precedes the BAT. */
#define XX_PARALLELS_HDD_HEADER_SIZE 64

XXFC_API void xx_parallels_hdd_init(xx_parallels_hdd *image,
                                    xx_io_device *dev, int64_t base_address);
XXFC_API xx_parallels_hdd *xx_parallels_hdd_create(xx_io_device *dev,
                                                   int64_t base_address);
XXFC_API void xx_parallels_hdd_destroy(xx_parallels_hdd *image);
XXFC_API void xx_parallels_hdd_free(xx_parallels_hdd *image);

XXFC_API bool xx_parallels_hdd_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_parallels_hdd_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_parallels_hdd_get_format_size(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API uint64_t xx_parallels_hdd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_parallels_hdd_create_archive_records_reading(Abstractformat *self,
                                                const xx_list_s *options,
                                                xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_parallels_hdd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_parallels_hdd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_parallels_hdd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_parallels_hdd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Write the guest disk to @p destination.
 *
 * With a NULL destination nothing is written and only the BAT is walked,
 * which checks that every entry can be fetched.
 */
XXFC_API bool xx_parallels_hdd_unpack_to_device(xx_parallels_hdd *image,
                                                xx_io_device *destination,
                                                xx_pd_struct *pd);

static inline Abstractformat *xx_parallels_hdd_to_format(
    xx_parallels_hdd *image) {
    return image ? &image->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PARALLELS_HDD_H */

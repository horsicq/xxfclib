/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qemu_enhanced_disk.h @brief QEMU Enhanced Disk (QED) reader. */

/* QED - the QEMU Enhanced Disk image. Every field is LITTLE endian.
 *
 *   header (64 bytes, at the start of the first cluster)
 *     +0    u32  magic, "QED\0" (0x00444551)
 *     +4    u32  cluster_size in bytes, a power of two, 4 KB .. 64 MB
 *     +8    u32  table_size, the length of an L1 or L2 table in clusters,
 *                a power of two, 1 .. 16
 *     +12   u32  header_size, the length of the header area in clusters
 *     +16   u64  features: 1 backing file, 2 needs check (dirty),
 *                4 backing file is raw (do not probe its format)
 *     +24   u64  compat_features, none defined
 *     +32   u64  autoclear_features, none defined
 *     +40   u64  l1_table_offset, byte offset of the L1 table
 *     +48   u64  image_size, the VIRTUAL disk size, a multiple of 512
 *     +56   u32  backing_filename_offset, byte offset into the header area
 *     +60   u32  backing_filename_size, not NUL terminated
 *
 * A table holds table_size * cluster_size / 8 u64 entries. Guest byte pos
 * lives in guest cluster pos / cluster_size; L1 entry
 * (cluster >> log2(entries)) is the byte offset of an L2 table (0: none),
 * and L2 entry (cluster & (entries - 1)) describes the cluster:
 *
 *   0                  unallocated: reads from the backing file, or zeros
 *   1                  a zero cluster: reads as zeros
 *   otherwise          the byte offset of the data cluster
 *
 * There is no compression and no encryption. A table or data offset is
 * valid only when it is cluster aligned, lies past the header area, and the
 * whole table or cluster ends inside the file (QEMU rounds the file length
 * down to a whole cluster before comparing).
 *
 * Everything in the header is attacker controlled. The sizes are bounded to
 * the specification, the virtual size is capped, only the L1 entries that
 * the virtual size can reach are read (at most 32768 of them), and every L1
 * entry is validated before anything is extracted. L2 entries are fetched a
 * small window at a time as the output is produced and every one of them is
 * re-validated, so nothing an L2 entry says turns into a large allocation or
 * a read outside the device. Clusters are copied through a fixed 64 KB
 * buffer, however large the cluster size is.
 */

#ifndef XXFCLIB_FORMAT_QEMU_ENHANCED_DISK_H
#define XXFCLIB_FORMAT_QEMU_ENHANCED_DISK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_qemu_enhanced_disk xx_qemu_enhanced_disk;
typedef struct xx_qemu_enhanced_disk xx_qemu_enhanced_disk_t;
typedef struct xx_qemu_enhanced_disk XQemuEnhancedDisk;

struct xx_qemu_enhanced_disk {
    Abstractformat format;
    uint64_t number_of_records;     /**< Always 1: the guest disk image. */
    uint64_t image_size;            /**< Guest-visible disk size in bytes. */
    uint64_t l1_table_offset;
    uint64_t features;
    uint64_t compat_features;
    uint64_t autoclear_features;
    uint32_t cluster_size;
    uint32_t table_size;            /**< In clusters. */
    uint32_t header_size;           /**< In clusters. */
    uint32_t backing_filename_offset;
    uint32_t backing_filename_size;
    bool has_backing_file;          /**< Feature bit 1 is set. */
    bool needs_check;               /**< Feature bit 2: not cleanly closed. */
    void *internal;
};

XXFC_API void xx_qemu_enhanced_disk_init(xx_qemu_enhanced_disk *archive,
                                         xx_io_device *dev,
                                         int64_t base_address);
XXFC_API xx_qemu_enhanced_disk *xx_qemu_enhanced_disk_create(
    xx_io_device *dev, int64_t base_address);
XXFC_API void xx_qemu_enhanced_disk_destroy(xx_qemu_enhanced_disk *archive);
XXFC_API void xx_qemu_enhanced_disk_free(xx_qemu_enhanced_disk *archive);

XXFC_API bool xx_qemu_enhanced_disk_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_qemu_enhanced_disk_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_qemu_enhanced_disk_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_qemu_enhanced_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_qemu_enhanced_disk_create_archive_records_reading(Abstractformat *self,
                                                     const xx_list_s *options,
                                                     xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_qemu_enhanced_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qemu_enhanced_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qemu_enhanced_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qemu_enhanced_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Write the whole guest disk to output. Unallocated clusters are written as
 *  zeros; a backing file is never opened. Needs handle_base_info first. */
XXFC_API bool xx_qemu_enhanced_disk_unpack_to_device(
    xx_qemu_enhanced_disk *archive, xx_io_device *output, xx_pd_struct *pd);

XXFC_API uint64_t xx_qemu_enhanced_disk_get_image_size(
    const xx_qemu_enhanced_disk *archive);
XXFC_API uint32_t xx_qemu_enhanced_disk_get_cluster_size(
    const xx_qemu_enhanced_disk *archive);
XXFC_API uint32_t xx_qemu_enhanced_disk_get_table_size(
    const xx_qemu_enhanced_disk *archive);
XXFC_API uint64_t xx_qemu_enhanced_disk_get_features(
    const xx_qemu_enhanced_disk *archive);
/** The backing file name, or NULL. Owned by the reader. */
XXFC_API const char *xx_qemu_enhanced_disk_get_backing_file(
    const xx_qemu_enhanced_disk *archive);

static inline Abstractformat *xx_qemu_enhanced_disk_to_format(
    xx_qemu_enhanced_disk *archive) {
    return archive ? &archive->format : NULL;
}
static inline void XQemuEnhancedDisk_init(xx_qemu_enhanced_disk *archive,
                                          xx_io_device *dev,
                                          int64_t base_address) {
    xx_qemu_enhanced_disk_init(archive, dev, base_address);
}
static inline xx_qemu_enhanced_disk *XQemuEnhancedDisk_create(
    xx_io_device *dev, int64_t base_address) {
    return xx_qemu_enhanced_disk_create(dev, base_address);
}
static inline void XQemuEnhancedDisk_free(xx_qemu_enhanced_disk *archive) {
    xx_qemu_enhanced_disk_free(archive);
}
static inline bool XQemuEnhancedDisk_is_valid(xx_qemu_enhanced_disk *archive,
                                              xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QEMU_ENHANCED_DISK_H */

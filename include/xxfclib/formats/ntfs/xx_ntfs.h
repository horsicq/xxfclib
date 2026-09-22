/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ntfs.h @brief NTFS volume archive reader (MFT walker, read-only).
 *
 * Bounded, read-only extraction of user files from a single NTFS volume that
 * begins at the format's base address. The volume is never mounted: the boot
 * sector supplies the geometry, $MFT record 0 supplies the MFT run list, and
 * every other record is decoded straight out of that stream.
 *
 * Deliberately unsupported (the parse fails, or the record is published with a
 * reason attached rather than extracted): attribute-list extensions, reparse
 * and compressed-provider files, compressed or encrypted $DATA, a sparse or
 * partially initialised $MFT, and named data streams. No LZNT1 decompressor is
 * part of this reader.
 */

#ifndef XXFCLIB_FORMAT_NTFS_H
#define XXFCLIB_FORMAT_NTFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ntfs xx_ntfs;
typedef struct xx_ntfs xx_ntfs_t;
typedef struct xx_ntfs XNtfs;

struct xx_ntfs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t bytes_per_sector;
    uint32_t bytes_per_cluster;
    uint32_t file_record_size;
    uint64_t mft_cluster;
    uint64_t volume_size;
    int64_t volume_end;
    void *internal;
};

XXFC_API void xx_ntfs_init(xx_ntfs *ntfs, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_ntfs *xx_ntfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ntfs_destroy(xx_ntfs *ntfs);
XXFC_API void xx_ntfs_free(xx_ntfs *ntfs);

XXFC_API bool xx_ntfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ntfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ntfs_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_ntfs_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ntfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ntfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ntfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ntfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ntfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ntfs_get_number_of_records(const xx_ntfs *ntfs);
XXFC_API uint64_t xx_ntfs_get_number_of_members(const xx_ntfs *ntfs);
XXFC_API uint32_t xx_ntfs_get_bytes_per_sector(const xx_ntfs *ntfs);
XXFC_API uint32_t xx_ntfs_get_bytes_per_cluster(const xx_ntfs *ntfs);
XXFC_API uint32_t xx_ntfs_get_file_record_size(const xx_ntfs *ntfs);
XXFC_API uint64_t xx_ntfs_get_mft_cluster(const xx_ntfs *ntfs);
XXFC_API uint64_t xx_ntfs_get_volume_size(const xx_ntfs *ntfs);
XXFC_API int64_t xx_ntfs_get_volume_end(const xx_ntfs *ntfs);

static inline Abstractformat *xx_ntfs_to_format(xx_ntfs *ntfs) {
    return ntfs ? &ntfs->format : NULL;
}
static inline void XNtfs_init(xx_ntfs *ntfs, xx_io_device *dev,
                              int64_t base_address) {
    xx_ntfs_init(ntfs, dev, base_address);
}
static inline xx_ntfs *XNtfs_create(xx_io_device *dev, int64_t base_address) {
    return xx_ntfs_create(dev, base_address);
}
static inline void XNtfs_free(xx_ntfs *ntfs) { xx_ntfs_free(ntfs); }
static inline bool XNtfs_is_valid(xx_ntfs *ntfs, xx_pd_struct *pd) {
    return ntfs ? xx_format_is_valid(&ntfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NTFS_H */

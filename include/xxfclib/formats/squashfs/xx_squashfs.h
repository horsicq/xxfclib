/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_squashfs.h @brief SquashFS filesystem archive reader (v1..v4). */

#ifndef XXFCLIB_FORMAT_SQUASHFS_H
#define XXFCLIB_FORMAT_SQUASHFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registration of a shared XX_FILE_TYPE_SQUASHFS enumerator in xxfc_defs.h is
 * still pending (215 is the next free value).  This local alias keeps the
 * reader buildable in the meantime; once the enumerator exists, drop the
 * fallback and let the #ifndef pick the real constant up.
 */
#ifndef XX_SQUASHFS_FILE_TYPE
#define XX_SQUASHFS_FILE_TYPE XX_FILE_TYPE_SQUASHFS
#endif

/** SquashFS compressor ids, exactly as the v4 superblock stores them. */
#define XX_SQUASHFS_COMPRESSOR_GZIP 1U
#define XX_SQUASHFS_COMPRESSOR_LZMA 2U
#define XX_SQUASHFS_COMPRESSOR_LZO 3U
#define XX_SQUASHFS_COMPRESSOR_XZ 4U
#define XX_SQUASHFS_COMPRESSOR_LZ4 5U
#define XX_SQUASHFS_COMPRESSOR_ZSTD 6U

typedef struct xx_squashfs xx_squashfs;
typedef struct xx_squashfs xx_squashfs_t;
typedef struct xx_squashfs XSquashfs;

struct xx_squashfs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t compressor;
    uint32_t block_size;
    uint64_t inode_count;
    uint64_t fragment_count;
    int64_t bytes_used;
    bool big_endian;
    void *internal;
};

XXFC_API void xx_squashfs_init(xx_squashfs *squashfs, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_squashfs *xx_squashfs_create(xx_io_device *dev,
                                         int64_t base_address);
XXFC_API void xx_squashfs_destroy(xx_squashfs *squashfs);
XXFC_API void xx_squashfs_free(xx_squashfs *squashfs);

XXFC_API bool xx_squashfs_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_squashfs_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_squashfs_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_squashfs_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_squashfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_squashfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_squashfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_squashfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_squashfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_squashfs_get_number_of_records(const xx_squashfs *squashfs);
XXFC_API uint64_t xx_squashfs_get_number_of_members(const xx_squashfs *squashfs);
XXFC_API uint32_t xx_squashfs_get_version_major(const xx_squashfs *squashfs);
XXFC_API uint32_t xx_squashfs_get_version_minor(const xx_squashfs *squashfs);
XXFC_API uint32_t xx_squashfs_get_compressor(const xx_squashfs *squashfs);
XXFC_API uint32_t xx_squashfs_get_block_size(const xx_squashfs *squashfs);
XXFC_API int64_t xx_squashfs_get_bytes_used(const xx_squashfs *squashfs);

/** Display name of a SquashFS compressor id ("GZIP", "XZ", ... or "Unknown"). */
XXFC_API const char *xx_squashfs_compressor_to_string(uint32_t compressor);

static inline Abstractformat *xx_squashfs_to_format(xx_squashfs *squashfs) {
    return squashfs ? &squashfs->format : NULL;
}
static inline void XSquashfs_init(xx_squashfs *squashfs, xx_io_device *dev,
                                  int64_t base_address) {
    xx_squashfs_init(squashfs, dev, base_address);
}
static inline xx_squashfs *XSquashfs_create(xx_io_device *dev,
                                            int64_t base_address) {
    return xx_squashfs_create(dev, base_address);
}
static inline void XSquashfs_free(xx_squashfs *squashfs) {
    xx_squashfs_free(squashfs);
}
static inline bool XSquashfs_is_valid(xx_squashfs *squashfs, xx_pd_struct *pd) {
    return squashfs ? xx_format_is_valid(&squashfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQUASHFS_H */

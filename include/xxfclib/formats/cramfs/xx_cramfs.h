/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cramfs.h @brief cramfs compressed read-only filesystem reader. */

/* cramfs - the "Compressed ROM filesystem", the small read-only image format
 * that predates SquashFS and is still common in embedded firmware.  Images
 * exist in both byte orders: the image is written with the host's native
 * layout, so a big-endian image is not merely the little-endian one with the
 * words swapped - the bitfields inside an inode are packed from the opposite
 * end of each word as well, and both differences have to be undone together.
 *
 *   superblock, 76 bytes
 *     +0   u32  magic, 0x28cd3d45 read in the image's own byte order
 *     +4   u32  size, the number of bytes the image occupies
 *     +8   u32  flags
 *     +12  u32  future, reserved and zero
 *     +16  char signature[16], "Compressed ROMFS"
 *     +32  u32  crc            \
 *     +36  u32  edition         | the fsid block
 *     +40  u32  blocks          |
 *     +44  u32  files          /
 *     +48  char name[16], the volume name
 *     +64  the root inode, 12 bytes
 *
 *   inode, 12 bytes, three words of packed bitfields
 *     little endian            big endian
 *       w0: mode:16 uid:16       w0: mode in the high 16, uid in the low 16
 *       w1: size:24 gid:8        w1: size in the high 24, gid in the low 8
 *       w2: namelen:6 offset:26  w2: namelen in the high 6, offset in the low 26
 *     namelen counts 4-byte units, offset is a byte offset divided by 4.  The
 *     26-bit offset is what caps a cramfs image at 256 MiB.
 *
 * A directory's payload is `size` bytes of back-to-back entries, each an inode
 * followed by namelen*4 name bytes padded with NULs.  A regular file's payload
 * begins with ceil(size / 4096) u32 block pointers; pointer i holds the END
 * offset of block i, so block i runs from pointer i-1 (or from the end of the
 * pointer array, for block 0) up to pointer i.  Each block is an RFC 1950 zlib
 * stream that expands to 4096 bytes, or fewer for the last one.  A zero-length
 * block is a hole and expands to zeros.
 *
 * Every offset in the image is an unchecked 26-bit number that a hostile image
 * can aim anywhere, including back at a directory already being walked, so the
 * walker keeps a visited set of directory payload offsets and caps both the
 * recursion depth and the total entry count.
 */

#ifndef XXFCLIB_FORMAT_CRAMFS_H
#define XXFCLIB_FORMAT_CRAMFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Superblock flag bits, as defined by include/uapi/linux/cramfs_fs.h. */
#define XX_CRAMFS_FLAG_FSID_VERSION_2 0x00000001U
#define XX_CRAMFS_FLAG_SORTED_DIRS 0x00000002U
#define XX_CRAMFS_FLAG_HOLES 0x00000100U
#define XX_CRAMFS_FLAG_WRONG_SIGNATURE 0x00000200U
#define XX_CRAMFS_FLAG_SHIFTED_ROOT_OFFSET 0x00000400U
#define XX_CRAMFS_FLAG_EXT_BLOCK_POINTERS 0x00000800U

typedef struct xx_cramfs xx_cramfs;
typedef struct xx_cramfs xx_cramfs_t;
typedef struct xx_cramfs XCramfs;

struct xx_cramfs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t image_size;   /**< The superblock's "size" field. */
    uint32_t flags;        /**< The superblock's flag word. */
    uint32_t crc;          /**< The fsid CRC, stored but not verified. */
    uint32_t edition;      /**< The fsid edition number. */
    uint32_t block_count;  /**< The fsid block count, informational. */
    uint32_t file_count;   /**< The fsid file count, informational. */
    int64_t archive_end;   /**< base_address + image_size, or -1. */
    bool big_endian;       /**< True when the image is big endian. */
    void *internal;
};

XXFC_API void xx_cramfs_init(xx_cramfs *cramfs, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_cramfs *xx_cramfs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_cramfs_destroy(xx_cramfs *cramfs);
XXFC_API void xx_cramfs_free(xx_cramfs *cramfs);

XXFC_API bool xx_cramfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cramfs_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_cramfs_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cramfs_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cramfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cramfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cramfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cramfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cramfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_cramfs_get_number_of_records(const xx_cramfs *cramfs);
XXFC_API uint64_t xx_cramfs_get_number_of_members(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_image_size(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_flags(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_crc(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_edition(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_block_count(const xx_cramfs *cramfs);
XXFC_API uint32_t xx_cramfs_get_file_count(const xx_cramfs *cramfs);
XXFC_API int64_t xx_cramfs_get_archive_end(const xx_cramfs *cramfs);
XXFC_API bool xx_cramfs_is_big_endian(const xx_cramfs *cramfs);

static inline Abstractformat *xx_cramfs_to_format(xx_cramfs *cramfs) {
    return cramfs ? &cramfs->format : NULL;
}
static inline void XCramfs_init(xx_cramfs *cramfs, xx_io_device *dev,
                                int64_t base_address) {
    xx_cramfs_init(cramfs, dev, base_address);
}
static inline xx_cramfs *XCramfs_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_cramfs_create(dev, base_address);
}
static inline void XCramfs_free(xx_cramfs *cramfs) { xx_cramfs_free(cramfs); }
static inline bool XCramfs_is_valid(xx_cramfs *cramfs, xx_pd_struct *pd) {
    return cramfs ? xx_format_is_valid(&cramfs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CRAMFS_H */

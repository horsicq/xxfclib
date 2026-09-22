/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qnx6.h @brief QNX6 (QNX Neutrino "power-safe") filesystem reader. */

/* QNX6 is the power-safe filesystem of QNX Neutrino 6.4 and later. This
 * reader implements the QNX6 FILESYSTEM, the one whose superblock magic is
 * 0x68191122. It does NOT implement the QNX IFS image-filesystem startup
 * container, which is a different format with a different header, and it has
 * nothing to do with xxfclib's existing "qnxbase" reader, which handles the
 * unrelated QNX archive format.
 *
 *   image layout
 *     0x0000  boot block, 0x2000 bytes
 *     0x2000  superblock area, 0x1000 bytes; the superblock is at its start
 *     0x3000  the block area; physical block n begins here, n * blocksize in
 *     end     a second superblock, not consulted by this reader
 *
 *   superblock (both a little-endian and a big-endian variant exist; the
 *   magic disambiguates them)
 *     +0   u32  0x68191122
 *     +4   u32  checksum, not verified here
 *     +8   u64  serial
 *     +16  u32  ctime          +20  u32  atime
 *     +24  u32  flags          +28  u16  version1   +30 u16 version2
 *     +32  u8[16] volume id
 *     +48  u32  blocksize      +52  u32  num_inodes  +56 u32 free_inodes
 *     +60  u32  num_blocks     +64  u32  free_blocks +68 u32 allocgroup
 *     +72  root node: Inode
 *     +152 root node: Bitmap
 *     +232 root node: Longfile
 *     +312 root node: Unknown
 *
 *   root node, 80 bytes
 *     +0   u64  size          +8   u32[16] block pointers
 *     +72  u8   levels        +73  u8  mode      +74 u8[6] spare
 *
 *   inode entry, 128 bytes, living in the "inode file" the Inode root node
 *   describes; inode numbers are 1-based and the root directory is inode 1
 *     +0   u64  size          +8   u32 uid   +12 u32 gid
 *     +16  u32  ftime  +20 u32 mtime  +24 u32 atime  +28 u32 ctime
 *     +32  u16  mode          +34  u16 ext_mode
 *     +36  u32[16] block pointers
 *     +100 u8   file levels   +101 u8  status  +102 u8[2] spare
 *
 *   directory entry, 32 bytes
 *     +0   u32  inode number, 0 for a free slot
 *     +4   u8   name length 1..27, or 0xff for the long-name form
 *     +5   char[27] name
 *   In the long form the slot is instead
 *     +0   u32  inode   +4 u8 0xff   +5 u8[3] unknown
 *     +8   u32  long-name block index into the "longfile" file
 *     +12  u32  checksum
 *   and the named block of the longfile file starts with a u16 length
 *   followed by up to 510 name bytes.
 *
 * A block pointer array is read through `levels` of indirection, each level
 * costing one block read; depth, node count and entry count are all capped,
 * and a visited-inode set keeps a directory that contains itself from
 * looping.
 */

#ifndef XXFCLIB_FORMAT_QNX6_H
#define XXFCLIB_FORMAT_QNX6_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_qnx6 xx_qnx6;
typedef struct xx_qnx6 xx_qnx6_t;
typedef struct xx_qnx6 XQnx6;

struct xx_qnx6 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t serial;
    uint32_t blocksize;
    uint32_t num_inodes;
    uint32_t free_inodes;
    uint32_t num_blocks;
    uint32_t free_blocks;
    uint32_t checksum;    /**< The superblock's field, NOT verified. */
    uint16_t version1;
    uint16_t version2;
    int64_t superblock_offset; /**< Device offset of the superblock. */
    int64_t archive_end;
    bool big_endian;      /**< True for the big-endian superblock variant. */
    void *internal;
};

XXFC_API void xx_qnx6_init(xx_qnx6 *qnx6, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_qnx6 *xx_qnx6_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_qnx6_destroy(xx_qnx6 *qnx6);
XXFC_API void xx_qnx6_free(xx_qnx6 *qnx6);

XXFC_API bool xx_qnx6_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qnx6_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qnx6_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_qnx6_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qnx6_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qnx6_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qnx6_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qnx6_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qnx6_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_qnx6_get_number_of_records(const xx_qnx6 *qnx6);
XXFC_API uint32_t xx_qnx6_get_blocksize(const xx_qnx6 *qnx6);
XXFC_API uint32_t xx_qnx6_get_num_blocks(const xx_qnx6 *qnx6);
XXFC_API bool xx_qnx6_is_big_endian(const xx_qnx6 *qnx6);

static inline Abstractformat *xx_qnx6_to_format(xx_qnx6 *qnx6) {
    return qnx6 ? &qnx6->format : NULL;
}
static inline void XQnx6_init(xx_qnx6 *qnx6, xx_io_device *dev,
                              int64_t base_address) {
    xx_qnx6_init(qnx6, dev, base_address);
}
static inline xx_qnx6 *XQnx6_create(xx_io_device *dev, int64_t base_address) {
    return xx_qnx6_create(dev, base_address);
}
static inline void XQnx6_free(xx_qnx6 *qnx6) { xx_qnx6_free(qnx6); }
static inline bool XQnx6_is_valid(xx_qnx6 *qnx6, xx_pd_struct *pd) {
    return qnx6 ? xx_format_is_valid(&qnx6->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QNX6_H */

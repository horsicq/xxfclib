/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ubifs.h @brief UBIFS journaling flash filesystem reader. */

/* UBIFS is the filesystem that normally lives inside a UBI volume. This
 * reader takes the already reassembled volume image, in which logical erase
 * block N starts at N * leb_size; chain it after xx_ubi, which does that
 * reassembly, or point it at an image that a tool such as ubireader already
 * extracted.
 *
 * Everything is little endian. Every structure starts with a common header:
 *
 *   ubifs_ch, 24 bytes
 *     +0   u32  0x06101831
 *     +4   u32  crc, over bytes 8 .. len-1 of the whole node
 *     +8   u64  sqnum
 *     +16  u32  len, the full node length
 *     +20  u8   node_type
 *     +21  u8   group_type
 *
 * The CRC is seeded with 0xFFFFFFFF and not finally complemented, the same
 * convention UBI uses.
 *
 * Three structures anchor the filesystem:
 *
 *   superblock, node type 6, LEB 0 offset 0, 4096 bytes. Carries the
 *   geometry: leb_size, leb_cnt, fanout, default_compr, fmt_version and the
 *   feature flags.
 *
 *   master node, node type 7, LEB 1 and LEB 2, 512 bytes. Rewritten on every
 *   commit at increasing offsets inside its block, so the newest valid copy
 *   is the live one. It names the root of the index: root_lnum, root_offs,
 *   root_len.
 *
 *   index node, node type 9. A B-tree node: a 28-byte header holding
 *   child_cnt and level, then child_cnt branches. A branch is 12 bytes -
 *   lnum, offs, len - followed by the 8-byte on-flash key, so the stride is
 *   20 bytes, NOT the 28 that the padded 16-byte key fields inside ino, dent
 *   and data nodes might suggest. Level 0 branches point at leaf nodes.
 *
 * A key is two little-endian words: the inode number, then
 * (type << 29) | value, where type is 0 inode, 1 data, 2 directory entry,
 * 3 extended attribute. For a data key the value is the 4 KiB block number;
 * for a directory entry it is a name hash and the key's inode number is the
 * PARENT directory.
 *
 * What this reader does and does not do is spelled out in xx_ubifs.c; in
 * short, it walks the committed index and ignores the journal, so nodes
 * written since the last commit are not visible.
 */

#ifndef XXFCLIB_FORMAT_UBIFS_H
#define XXFCLIB_FORMAT_UBIFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ubifs xx_ubifs;
typedef struct xx_ubifs xx_ubifs_t;
typedef struct xx_ubifs XUbifs;

struct xx_ubifs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t leb_size;
    uint32_t leb_cnt;
    uint32_t min_io_size;
    uint32_t fanout;
    uint32_t fmt_version;
    uint32_t sb_flags;
    uint16_t default_compr;
    uint64_t highest_inum;
    uint64_t leaf_count;   /**< Level-0 index branches found. */
    int64_t archive_end;   /**< base_address + leb_cnt * leb_size, or -1. */
    void *internal;
};

XXFC_API void xx_ubifs_init(xx_ubifs *ubifs, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_ubifs *xx_ubifs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ubifs_destroy(xx_ubifs *ubifs);
XXFC_API void xx_ubifs_free(xx_ubifs *ubifs);

XXFC_API bool xx_ubifs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ubifs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ubifs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_ubifs_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ubifs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ubifs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ubifs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ubifs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ubifs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ubifs_get_number_of_records(const xx_ubifs *ubifs);
XXFC_API uint64_t xx_ubifs_get_number_of_members(const xx_ubifs *ubifs);
XXFC_API uint32_t xx_ubifs_get_leb_size(const xx_ubifs *ubifs);
XXFC_API uint32_t xx_ubifs_get_leb_count(const xx_ubifs *ubifs);
XXFC_API uint32_t xx_ubifs_get_format_version(const xx_ubifs *ubifs);
XXFC_API uint16_t xx_ubifs_get_default_compression(const xx_ubifs *ubifs);
XXFC_API int64_t xx_ubifs_get_archive_end(const xx_ubifs *ubifs);
/** @brief "None", "LZO", "zlib", "zstd" or "Unknown". */
XXFC_API const char *xx_ubifs_compression_to_string(uint32_t compr_type);

static inline Abstractformat *xx_ubifs_to_format(xx_ubifs *ubifs) {
    return ubifs ? &ubifs->format : NULL;
}
static inline void XUbifs_init(xx_ubifs *ubifs, xx_io_device *dev,
                               int64_t base_address) {
    xx_ubifs_init(ubifs, dev, base_address);
}
static inline xx_ubifs *XUbifs_create(xx_io_device *dev, int64_t base_address) {
    return xx_ubifs_create(dev, base_address);
}
static inline void XUbifs_free(xx_ubifs *ubifs) { xx_ubifs_free(ubifs); }
static inline bool XUbifs_is_valid(xx_ubifs *ubifs, xx_pd_struct *pd) {
    return ubifs ? xx_format_is_valid(&ubifs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UBIFS_H */

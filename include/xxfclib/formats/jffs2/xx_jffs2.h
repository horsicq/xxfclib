/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_jffs2.h @brief JFFS2 journalling flash filesystem reader. */

/* JFFS2 - the journalling flash filesystem, version 2. Unlike romfs or
 * squashfs there is no superblock and no directory table: the image is a
 * bare log, a sequence of self-describing nodes laid down in the order the
 * filesystem wrote them. The state of the tree is whatever replaying that
 * log produces, and a later node silently supersedes an earlier one that
 * described the same thing.
 *
 * Every node opens with the same 12-byte header:
 *
 *     +0   u16  magic, 0x1985
 *     +2   u16  nodetype
 *     +4   u32  totlen, the whole node including this header
 *     +8   u32  hdr_crc, over the first 8 bytes
 *
 * The header is stored in the endianness the image was built with, which is
 * not recorded anywhere; it is recovered by trying both and keeping the one
 * whose magic and header CRC agree. Nodes are padded to a 4-byte boundary,
 * and the gaps between them - erase-block tails, padding nodes, unwritten
 * flash - are skipped by resynchronising on the next plausible header.
 *
 * Two node types carry the filesystem:
 *
 *   DIRENT (0xE001), 40 bytes plus the name
 *     +12  u32  pino, the inode number of the containing directory
 *     +16  u32  version, monotonic per name within that directory
 *     +20  u32  ino, the inode this name resolves to; ZERO means the name
 *               was unlinked, which is how deletion is recorded
 *     +24  u32  mctime
 *     +28  u8   nsize
 *     +29  u8   type, a DT_* constant
 *     +32  u32  node_crc, over the first 32 bytes
 *     +36  u32  name_crc, over the name
 *     +40  the name, nsize bytes, not NUL terminated
 *
 *   INODE (0xE002), 68 bytes plus the payload
 *     +12  u32  ino
 *     +16  u32  version, monotonic per inode
 *     +20  u32  mode
 *     +24  u16  uid
 *     +26  u16  gid
 *     +28  u32  isize, the file's size AFTER this node is applied
 *     +32  u32  atime
 *     +36  u32  mtime
 *     +40  u32  ctime
 *     +44  u32  offset, where the payload lands in the file
 *     +48  u32  csize, the payload's stored size
 *     +52  u32  dsize, the payload's size once decompressed
 *     +56  u8   compr
 *     +57  u8   usercompr
 *     +58  u16  flags
 *     +60  u32  data_crc, over the csize stored bytes
 *     +64  u32  node_crc, over the first 60 bytes
 *     +68  the payload, csize bytes
 *
 * CLEANMARKER (0x2003) and PADDING (0x2004) carry no content and are only
 * stepped over. XATTR (0xE008) and XREF (0xE009) are not interpreted.
 *
 * Reconstruction therefore has two halves. A name is resolved by taking the
 * highest-version DIRENT for a given (pino, name) pair and honouring it,
 * deletion included. A file is resolved by replaying every INODE node for
 * its inode number in version order, each one writing dsize bytes at its
 * offset and then declaring the file to be isize bytes long - so a later
 * node both overwrites and truncates.
 *
 * Everything in a node header is attacker controlled. The walker resyncs on
 * a 4-byte step whenever a header fails to validate, so the scan offset
 * always advances; the node, name, entry and reconstructed-size budgets are
 * all capped; and directory recursion carries a visited-inode set so a
 * dirent naming an ancestor cannot loop.
 */

#ifndef XXFCLIB_FORMAT_JFFS2_H
#define XXFCLIB_FORMAT_JFFS2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node types. */
#define XX_JFFS2_NODETYPE_DIRENT 0xE001U
#define XX_JFFS2_NODETYPE_INODE 0xE002U
#define XX_JFFS2_NODETYPE_CLEANMARKER 0x2003U
#define XX_JFFS2_NODETYPE_PADDING 0x2004U
#define XX_JFFS2_NODETYPE_SUMMARY 0x2006U
#define XX_JFFS2_NODETYPE_XATTR 0xE008U
#define XX_JFFS2_NODETYPE_XREF 0xE009U

/* Compression identifiers carried in the inode node's compr byte. */
#define XX_JFFS2_COMPR_NONE 0U
#define XX_JFFS2_COMPR_ZERO 1U
#define XX_JFFS2_COMPR_RTIME 2U
#define XX_JFFS2_COMPR_RUBINMIPS 3U
#define XX_JFFS2_COMPR_COPY 4U
#define XX_JFFS2_COMPR_DYNRUBIN 5U
#define XX_JFFS2_COMPR_ZLIB 6U
#define XX_JFFS2_COMPR_LZO 7U
#define XX_JFFS2_COMPR_LZMA 8U /**< Not upstream; an OpenWrt extension. */

typedef struct xx_jffs2 xx_jffs2;
typedef struct xx_jffs2 xx_jffs2_t;
typedef struct xx_jffs2 XJffs2;

struct xx_jffs2 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t number_of_nodes;    /**< Validated nodes of any type. */
    uint64_t number_of_dirents;  /**< Validated DIRENT nodes. */
    uint64_t number_of_inodes;   /**< Validated INODE nodes. */
    int64_t archive_end;         /**< One past the last validated node. */
    uint32_t compression_mask;   /**< Bit n set when compr n was observed. */
    bool is_big_endian;
    void *internal;
};

XXFC_API void xx_jffs2_init(xx_jffs2 *jffs2, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_jffs2 *xx_jffs2_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_jffs2_destroy(xx_jffs2 *jffs2);
XXFC_API void xx_jffs2_free(xx_jffs2 *jffs2);

XXFC_API bool xx_jffs2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jffs2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_jffs2_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_jffs2_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jffs2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jffs2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jffs2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jffs2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jffs2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_jffs2_get_number_of_records(const xx_jffs2 *jffs2);
XXFC_API uint64_t xx_jffs2_get_number_of_members(const xx_jffs2 *jffs2);
XXFC_API uint64_t xx_jffs2_get_number_of_nodes(const xx_jffs2 *jffs2);
XXFC_API uint64_t xx_jffs2_get_number_of_dirents(const xx_jffs2 *jffs2);
XXFC_API uint64_t xx_jffs2_get_number_of_inodes(const xx_jffs2 *jffs2);
XXFC_API int64_t xx_jffs2_get_archive_end(const xx_jffs2 *jffs2);
XXFC_API uint32_t xx_jffs2_get_compression_mask(const xx_jffs2 *jffs2);
XXFC_API bool xx_jffs2_get_is_big_endian(const xx_jffs2 *jffs2);

/** @brief Readable name for a compr byte, "Unknown" when unrecognised. */
XXFC_API const char *xx_jffs2_compression_to_string(uint32_t compression);

static inline Abstractformat *xx_jffs2_to_format(xx_jffs2 *jffs2) {
    return jffs2 ? &jffs2->format : NULL;
}
static inline void XJffs2_init(xx_jffs2 *jffs2, xx_io_device *dev,
                               int64_t base_address) {
    xx_jffs2_init(jffs2, dev, base_address);
}
static inline xx_jffs2 *XJffs2_create(xx_io_device *dev, int64_t base_address) {
    return xx_jffs2_create(dev, base_address);
}
static inline void XJffs2_free(xx_jffs2 *jffs2) { xx_jffs2_free(jffs2); }
static inline bool XJffs2_is_valid(xx_jffs2 *jffs2, xx_pd_struct *pd) {
    return jffs2 ? xx_format_is_valid(&jffs2->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JFFS2_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_yaffs.h @brief YAFFS1 / YAFFS2 flash filesystem reader. */

/* YAFFS ("Yet Another Flash File System") is a log structured filesystem for
 * raw NAND.  An image is a flat sequence of fixed size chunks; each chunk is
 * one NAND page of data immediately followed by that page's out-of-band
 * "spare" area, which carries the tags that say what the page is:
 *
 *   chunk = page_size bytes of page data + spare_size bytes of spare/OOB
 *
 * YAFFS2 tags (struct yaffs_packed_tags2_tags_only), at the start of the
 * spare (mkyaffs2image, followed by a 12 byte tag ECC) - or two bytes in,
 * after the bad block marker, as yaffs2utils and the kernel's MTD layout
 * place them - or, with in-band tags, in the last 16 bytes of the page:
 *
 *     +0   u32  sequence number of the erase block, 0x1000..0xEFFFFF00
 *     +4   u32  object id this chunk belongs to
 *     +8   u32  chunk id - 0 marks an OBJECT HEADER, 1..n a data chunk
 *     +12  u32  number of valid bytes in the page
 *
 * A header written by the Linux driver sets bit 31 of the chunk id and
 * stores the parent id in its low 28 bits, and the object type in the top
 * nibble of the object id. Sequence 0x21 marks a checkpoint block.
 *
 * YAFFS1 tags (struct yaffs_tags) are eight bytes of bitfields scattered
 * through a sixteen byte struct yaffs_spare, around the page/block status
 * bytes and the two three-byte ECCs:
 *
 *     spare[0..3]   tag bytes 0..3      spare[4]      page status (0: deleted)
 *     spare[5]      block status        spare[6..7]   tag bytes 4..5
 *     spare[8..10]  ECC of bytes 0..255 spare[11..12] tag bytes 6..7
 *     spare[13..15] ECC of bytes 256..511
 *
 *   chunk_id : 20, serial_number : 2, byte_count : 10, object_id : 18,
 *   ecc : 12, 2 spare bits - packed by the compiler, so the bit order follows
 *   the target's endianness. The two-bit serial tells the newer of two copies
 *   of a chunk.
 *
 * An object header chunk holds a 512 byte struct yaffs_obj_hdr:
 *
 *     +0    u32   type: 0 unknown, 1 file, 2 symlink, 3 directory,
 *                 4 hardlink, 5 special
 *     +4    u32   parent object id (1 is the root directory)
 *     +8    u16   name checksum, no longer used, always 0xFFFF
 *     +10   char  name[256], at most 255 bytes, NUL terminated below that
 *     +268  u32   mode        +272 uid     +276 gid
 *     +280  u32   atime       +284 mtime   +288 ctime
 *     +292  u32   file size, low 32 bits
 *     +296  u32   equivalent object id, for hardlinks
 *     +300  char  alias[160], the target of a symlink
 *     +460  u32   rdev        +464 six words of WinCE timestamps
 *     +488  u32   inband shadowed object id   +492 u32 inband is-shrink
 *     +496  u32   file size, high 32 bits, 0xFFFFFFFF when unused
 *
 * YAFFS HAS NO MAGIC NUMBER.  Detection is structural: chunk 0 must hold the
 * first object header an image builder writes - an object in the root
 * directory: type 1, 2, 3 or 5, parent 1, checksum 0xFFFF, a printable name
 * (empty only for the root directory itself) - and the tags of chunk 0 must
 * describe an object header. Page size, spare size, spare offset, in-band
 * tags, endianness and the YAFFS1/YAFFS2 tag layout are all unknown up
 * front, so the reader brute-forces that product space and scores each
 * candidate over the first 64 chunks; a candidate whose following chunks
 * hold more non-YAFFS tags than YAFFS ones is rejected. See xx_yaffs_detect()
 * in the implementation for the order and the scoring.
 *
 * A file's data lives in chunks scattered through the image and is addressed
 * by (object id, chunk id); the newest copy of each wins. The tree is rebuilt
 * from each object's parent id. Both are attacker controlled, so the parent
 * chain is resolved iteratively with an explicit in-progress marker that
 * turns a self-parenting or mutually parenting set of objects into an orphan
 * (listed under lost+found) rather than a hang. Objects in the unlinked or
 * deleted pseudo-directories are not listed. Names are made host-safe and
 * unique within their directory - compared the way a case-insensitive host
 * compares them, non-ASCII letters included, a later clash becoming
 * "name~N" - and full paths are rebuilt from the leaves per record rather
 * than stored per object. Symlink targets are reported in
 * XX_META_ID_LINK_TARGET; a hard link extracts a copy of its target file,
 * all such copies together bounded by the image size.
 */

#ifndef XXFCLIB_FORMAT_YAFFS_H
#define XXFCLIB_FORMAT_YAFFS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_YAFFS lands the alias is
 * defined and this picks it up with no further change. */
#ifdef YAFFS
#define XX_YAFFS_FILE_TYPE XX_FILE_TYPE_YAFFS
#else
#define XX_YAFFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Object types, as stored in the first word of an object header. */
#define XX_YAFFS_OBJECT_TYPE_UNKNOWN 0U
#define XX_YAFFS_OBJECT_TYPE_FILE 1U
#define XX_YAFFS_OBJECT_TYPE_SYMLINK 2U
#define XX_YAFFS_OBJECT_TYPE_DIRECTORY 3U
#define XX_YAFFS_OBJECT_TYPE_HARDLINK 4U
#define XX_YAFFS_OBJECT_TYPE_SPECIAL 5U

/** Reserved object ids. */
#define XX_YAFFS_OBJECTID_ROOT 1U
#define XX_YAFFS_OBJECTID_LOSTNFOUND 2U
#define XX_YAFFS_OBJECTID_UNLINKED 3U
#define XX_YAFFS_OBJECTID_DELETED 4U

typedef struct xx_yaffs xx_yaffs;
typedef struct xx_yaffs xx_yaffs_t;
typedef struct xx_yaffs XYaffs;

struct xx_yaffs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t page_size;   /**< Detected NAND page size in bytes. */
    uint32_t spare_size;  /**< Detected spare/OOB size, 0 when not present. */
    uint32_t tag_offset;  /**< Byte offset of the tags inside the spare. */
    uint32_t version;     /**< 1 for YAFFS1 tags, 2 for YAFFS2 tags. */
    bool big_endian;      /**< True when the image was built big endian. */
    bool has_spare;       /**< False for in-band tags and the tag-less
                               layout (an image whose spare was stripped). */
    int64_t archive_end;  /**< base_address + whole chunks, or -1. */
    void *internal;
};

XXFC_API void xx_yaffs_init(xx_yaffs *yaffs, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_yaffs *xx_yaffs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_yaffs_destroy(xx_yaffs *yaffs);
XXFC_API void xx_yaffs_free(xx_yaffs *yaffs);

XXFC_API bool xx_yaffs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_yaffs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_yaffs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_yaffs_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_yaffs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_yaffs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_yaffs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_yaffs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_yaffs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_yaffs_get_number_of_records(const xx_yaffs *yaffs);
XXFC_API uint64_t xx_yaffs_get_number_of_members(const xx_yaffs *yaffs);
XXFC_API uint32_t xx_yaffs_get_page_size(const xx_yaffs *yaffs);
XXFC_API uint32_t xx_yaffs_get_spare_size(const xx_yaffs *yaffs);
XXFC_API uint32_t xx_yaffs_get_version(const xx_yaffs *yaffs);
XXFC_API bool xx_yaffs_get_big_endian(const xx_yaffs *yaffs);
XXFC_API int64_t xx_yaffs_get_archive_end(const xx_yaffs *yaffs);

static inline Abstractformat *xx_yaffs_to_format(xx_yaffs *yaffs) {
    return yaffs ? &yaffs->format : NULL;
}
static inline void XYaffs_init(xx_yaffs *yaffs, xx_io_device *dev,
                               int64_t base_address) {
    xx_yaffs_init(yaffs, dev, base_address);
}
static inline xx_yaffs *XYaffs_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_yaffs_create(dev, base_address);
}
static inline void XYaffs_free(xx_yaffs *yaffs) { xx_yaffs_free(yaffs); }
static inline bool XYaffs_is_valid(xx_yaffs *yaffs, xx_pd_struct *pd) {
    return yaffs ? xx_format_is_valid(&yaffs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_YAFFS_H */

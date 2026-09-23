/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dmsfw.h @brief DMS firmware image (halfword-swapped) reader. */

/* A "DMS firmware image" is stored with the two 16-bit halves of every
 * 32-bit word exchanged, the way a big endian image looks after it has been
 * written through a flash bus wired for the opposite half-word order.  Read
 * back in that order ("un-swapped"), bytes ABCD of every 4-byte group become
 * CDAB, and the image then starts with a plain big endian header.
 *
 * This is unrelated to the Amiga DiskMasher "DMS!" disk archive handled by
 * src/formats/dms; only the three letters are shared.
 *
 *   un-swapped header (16 bytes, BIG endian)
 *     +0x00  u16  unknown
 *     +0x02  u16  0x4D47 ("MG")
 *     +0x04  u32  0x3C31303E ("<10>")
 *     +0x08  u32  unknown
 *     +0x0C  u32  image size, counted from the first header byte
 *
 *   the same bytes as they sit in the file
 *     +0x00  "MG"   +0x02 unknown u16   +0x04 "0><1"
 *     +0x08  low half of the unknown u32, then its high half
 *     +0x0C  low half of the image size,  then its high half
 *
 * Source: binwalk's src/signatures/dms.rs (magic "0><1" four bytes into the
 * image, 0x100 bytes required, CONFIDENCE_MEDIUM, carve length = image size),
 * src/structures/dms.rs (the header above, parsed big endian after the swap)
 * and src/extractors/swapped.rs (byte_swap with n = 2, which is what
 * "every 2 bytes swapped" means there: the halves of each 4-byte chunk are
 * exchanged and a trailing partial chunk is dropped).  No vendor
 * documentation was found and there is no checksum in the header.
 *
 * The reader publishes one record, "swapped.bin": the image un-swapped.  Its
 * length is the image size rounded down to a multiple of four, exactly what
 * byte_swap yields for the carved image, because a tail shorter than one
 * 4-byte group has no partner half to exchange with.
 */

#ifndef XXFCLIB_FORMAT_DMSFW_H
#define XXFCLIB_FORMAT_DMSFW_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Raw file bytes at +0x00 and +0x04 (before un-swapping). */
#define XX_DMSFW_RAW_TAG "MG"
#define XX_DMSFW_RAW_TAG_SIZE 2U
#define XX_DMSFW_RAW_MAGIC "0><1"
#define XX_DMSFW_RAW_MAGIC_OFFSET 4U
#define XX_DMSFW_RAW_MAGIC_SIZE 4U
/** Values of the un-swapped big endian header words. */
#define XX_DMSFW_MAGIC_P1 0x4D47U
#define XX_DMSFW_MAGIC_P2 0x3C31303EUL
#define XX_DMSFW_HEADER_SIZE 16U
/** binwalk's MIN_SIZE: bytes that must be present from the image start. */
#define XX_DMSFW_MIN_SIZE 0x100U
/** Size of the unit whose two halves are exchanged. */
#define XX_DMSFW_SWAP_UNIT 4U

typedef struct xx_dmsfw xx_dmsfw;
typedef struct xx_dmsfw xx_dmsfw_t;
typedef struct xx_dmsfw XDmsfw;

struct xx_dmsfw {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t image_size;     /**< Header word at un-swapped +0x0C. */
    uint32_t unswapped_size; /**< image_size rounded down to 4. */
    uint16_t unknown1;       /**< Header word at un-swapped +0x00. */
    uint32_t unknown2;       /**< Header word at un-swapped +0x08. */
    int64_t image_end;       /**< base_address + image_size, or -1. */
    void *internal;
};

XXFC_API void xx_dmsfw_init(xx_dmsfw *dmsfw, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_dmsfw *xx_dmsfw_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dmsfw_destroy(xx_dmsfw *dmsfw);
XXFC_API void xx_dmsfw_free(xx_dmsfw *dmsfw);

XXFC_API bool xx_dmsfw_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dmsfw_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_dmsfw_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_dmsfw_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dmsfw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dmsfw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dmsfw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dmsfw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dmsfw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Writes the un-swapped image (unswapped_size bytes) to @p output. */
XXFC_API bool xx_dmsfw_unpack_to_device(xx_dmsfw *dmsfw, xx_io_device *output,
                                        xx_pd_struct *pd);

XXFC_API uint64_t xx_dmsfw_get_number_of_records(const xx_dmsfw *dmsfw);
XXFC_API uint64_t xx_dmsfw_get_number_of_members(const xx_dmsfw *dmsfw);
XXFC_API uint32_t xx_dmsfw_get_image_size(const xx_dmsfw *dmsfw);
XXFC_API uint32_t xx_dmsfw_get_unswapped_size(const xx_dmsfw *dmsfw);
XXFC_API int64_t xx_dmsfw_get_image_end(const xx_dmsfw *dmsfw);

static inline Abstractformat *xx_dmsfw_to_format(xx_dmsfw *dmsfw) {
    return dmsfw ? &dmsfw->format : NULL;
}
static inline void XDmsfw_init(xx_dmsfw *dmsfw, xx_io_device *dev,
                               int64_t base_address) {
    xx_dmsfw_init(dmsfw, dev, base_address);
}
static inline xx_dmsfw *XDmsfw_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_dmsfw_create(dev, base_address);
}
static inline void XDmsfw_free(xx_dmsfw *dmsfw) { xx_dmsfw_free(dmsfw); }
static inline bool XDmsfw_is_valid(xx_dmsfw *dmsfw, xx_pd_struct *pd) {
    return dmsfw ? xx_format_is_valid(&dmsfw->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DMSFW_H */

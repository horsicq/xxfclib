/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_chk.h @brief NETGEAR CHK firmware container reader. */

/* CHK is the flash image wrapper NETGEAR wraps around an OpenWrt style
 * kernel + rootfs pair.  It names exactly two payload regions, both of which
 * are formats this library already decodes - typically an LZMA or uImage
 * kernel and a squashfs rootfs - so this reader validates the header, verifies
 * all four checksums and publishes the two regions as archive records for the
 * caller to recurse into.
 *
 * Everything is BIG endian.
 *
 *   header (40 fixed bytes, then the board id)
 *     +0x00  u32  magic, 0x2A23245E
 *     +0x04  u32  header_len == 40 + strlen(board_id)
 *     +0x08  8 reserved bytes
 *     +0x10  u32  kernel_chksum
 *     +0x14  u32  rootfs_chksum
 *     +0x18  u32  kernel_len
 *     +0x1C  u32  rootfs_len
 *     +0x20  u32  image_chksum, over the kernel and rootfs together
 *     +0x24  u32  header_chksum, over header_len bytes with this field zero
 *     +0x28  the board id, NOT NUL terminated, running to header_len
 *
 * The checksum is not a CRC.  It is NETGEAR's own Fletcher-like pair: c0
 * accumulates the bytes, c1 accumulates c0, each is folded twice into sixteen
 * bits and the result is (c1 << 16) | c0.  See netgear_checksum_fini() in
 * OpenWrt's tools/firmware-utils/src/mkchkimg.c, which is the generator this
 * reader was written against.
 *
 * Note that binwalk's src/structures/chk.rs lists rootfs_len before
 * kernel_len; mkchkimg, which writes the file, has kernel_len first at 0x18.
 * This reader follows mkchkimg.
 */

#ifndef XXFCLIB_FORMAT_CHK_H
#define XXFCLIB_FORMAT_CHK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_CHK_MAGIC UINT32_C(0x2A23245E)
#define XX_CHK_FIXED_HEADER_SIZE 40U
/** A board id is a short model string; this bounds the header allocation. */
#define XX_CHK_MAX_HEADER_SIZE 256U
#define XX_CHK_MAX_BOARD_ID (XX_CHK_MAX_HEADER_SIZE - XX_CHK_FIXED_HEADER_SIZE)

typedef struct xx_chk xx_chk;
typedef struct xx_chk xx_chk_t;
typedef struct xx_chk XChk;

struct xx_chk {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t header_size;    /**< The header_len field. */
    uint32_t kernel_size;
    uint32_t rootfs_size;
    uint32_t kernel_checksum;
    uint32_t rootfs_checksum;
    uint32_t image_checksum;
    uint32_t header_checksum;
    int64_t archive_end;     /**< End of the rootfs, or of the kernel. */
    char board_id[XX_CHK_MAX_BOARD_ID + 1U];
    void *internal;
};

XXFC_API void xx_chk_init(xx_chk *chk, xx_io_device *dev, int64_t base_address);
XXFC_API xx_chk *xx_chk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_chk_destroy(xx_chk *chk);
XXFC_API void xx_chk_free(xx_chk *chk);

XXFC_API bool xx_chk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_chk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_chk_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_chk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_chk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_chk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_chk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_chk_archive_record_move_to_next(Abstractformat *self,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd);
XXFC_API void xx_chk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_chk_get_number_of_records(const xx_chk *chk);
XXFC_API uint64_t xx_chk_get_number_of_members(const xx_chk *chk);
XXFC_API uint32_t xx_chk_get_kernel_size(const xx_chk *chk);
XXFC_API uint32_t xx_chk_get_rootfs_size(const xx_chk *chk);
XXFC_API const char *xx_chk_get_board_id(const xx_chk *chk);
XXFC_API int64_t xx_chk_get_archive_end(const xx_chk *chk);

static inline Abstractformat *xx_chk_to_format(xx_chk *chk) {
    return chk ? &chk->format : NULL;
}
static inline void XChk_init(xx_chk *chk, xx_io_device *dev,
                             int64_t base_address) {
    xx_chk_init(chk, dev, base_address);
}
static inline xx_chk *XChk_create(xx_io_device *dev, int64_t base_address) {
    return xx_chk_create(dev, base_address);
}
static inline void XChk_free(xx_chk *chk) { xx_chk_free(chk); }
static inline bool XChk_is_valid(xx_chk *chk, xx_pd_struct *pd) {
    return chk ? xx_format_is_valid(&chk->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CHK_H */

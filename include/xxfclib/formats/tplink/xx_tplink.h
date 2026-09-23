/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tplink.h @brief TP-Link firmware header reader. */

/*
 * TP-Link ships two unrelated firmware wrappers under the same brand, and
 * binwalk treats them as two signatures.  Both are handled here, selected by
 * xx_tplink_variant_t, because they share a vendor and a caller asking for
 * "tplink" wants whichever one is in front of it.
 *
 * ---------------------------------------------------------------- variant 1
 * The Linux/OpenWrt header, produced by tools/firmware-utils/mktplinkfw.c and
 * described field-for-field by firmware-mod-kit's tpl-tool Image_layout, which
 * is also the map binwalk uses (src/structures/tplink.rs).  It is a fixed
 * 0x200-byte header in front of a concatenated kernel and rootfs:
 *
 *   +0x00  u32   version, 0x01000000 (bytes 01 00 00 00)
 *   +0x04  char  vendor_name[24],  "TP-LINK Technologies"
 *   +0x1C  char  fw_version[36],   e.g. "ver. 1.0"
 *   +0x40  u32   hw_id
 *   +0x44  u32   hw_rev
 *   +0x48  u32   region_code       0, or 1 for US builds
 *   +0x4C  u8    md5sum1[16]       over the whole image, see below
 *   +0x5C  u32   unk2, zero
 *   +0x60  u8    md5sum2[16]       over the vendor's boot image
 *   +0x70  u32   unk3, zero
 *   +0x74  u32   kernel_load_address
 *   +0x78  u32   kernel_entry_point
 *   +0x7C  u32   fw_length         whole image INCLUDING this header
 *   +0x80  u32   kernel_offset     from the start of this header
 *   +0x84  u32   kernel_length
 *   +0x88  u32   rootfs_offset
 *   +0x8C  u32   rootfs_length
 *   +0x90  u32   bootloader_offset
 *   +0x94  u32   bootloader_length
 *   +0x98  u16   ver_hi, ver_mid, ver_lo
 *   +0x9E        padding, region strings, padding to 0x200
 *
 * md5sum1 is an MD5 over the entire image with the md5sum1 field itself
 * replaced by a fixed 16-byte salt.  mktplinkfw.c uses one salt for images
 * without a bootloader and another for images with one, and vendor-modified
 * builds use others again.  This reader therefore tries the two mktplinkfw
 * salts (from handle_base_info only), records which one matched (if any) in
 * xx_tplink_get_md5_salt_index(), and NEVER rejects an image on the basis
 * of the digest.  See the comment on the salt table in xx_tplink.c: the salt
 * constants are the one thing in this reader that could not be checked against
 * a local copy of their source, and making the check advisory means a wrong
 * constant cannot cause a false negative.
 *
 * Byte order: mktplinkfw.c writes every field with htonl()/htons(), i.e. big
 * endian (the version word 0x01000000 is what produces the bytes 01 00 00 00
 * binwalk matches), while binwalk's structure parser reads the block little
 * endian.  This reader decodes big endian first and falls back to little
 * endian, and keeps the first order whose fw_length AND kernel/rootfs offset
 * table both fit the device.  The choice is reported by
 * xx_tplink_get_header_big_endian().
 *
 * kernel and rootfs spans are strict (declared means it must fit); the
 * bootloader span is published only when it fits after the header and is
 * otherwise skipped.  All offsets are read as FILE offsets from the start of
 * this header.
 *
 * Unverified: stock "_up_boot" images.  The offset convention of TP-Link's
 * stock firmware files that carry a bootloader (names ending "_up_boot") has
 * NOT been checked against a real image; none was available when this reader
 * was written.  The reader treats every offset as a file offset, and that is
 * a guess.  One recalled recipe for stripping these files ("dd skip=257
 * bs=512", i.e. drop 0x20200 bytes) suggests the layout is: this 0x200
 * header, a 0x20000 bootloader, then an inner image with its own TP-Link
 * header at file offset 0x20200, with the outer offsets counted from the END
 * of the outer header (flash offsets).  If that is right, this reader, on
 * such a file:
 *   - skips the bootloader silently (bootloader_offset 0 lies inside the
 *     header), and
 *   - accepts the image and extracts kernel.bin and rootfs.bin from positions
 *     0x200 bytes too early, so kernel.bin starts with the inner header.
 * No error is reported in that case.  It cannot crash or read or write out of
 * bounds, since every span is still checked against fw_length and the
 * device.  The behaviour is deliberately left as is until a real image
 * confirms the layout.
 *
 * ---------------------------------------------------------------- variant 2
 * The RTOS header, per binwalk's parse_tplink_rtos_header().  Big endian, a
 * 0x94-byte header, and a second magic "IMG0" at +20 that carries most of the
 * evidence:
 *
 *   +0   u32   magic1, 0x00142FC0
 *   +4   u64   unknown
 *   +12  u64   unknown
 *   +20  u32   magic2, 0x494D4730 == "IMG0"
 *   +24  u32   data_size
 *   +28  u16   model_number
 *   +30  u8    hardware_revision_major
 *   +31  u8    hardware_revision_minor
 *
 * Total size is data_size + 20.  binwalk does not bound data_size against the
 * file at all; this reader does.
 */

#ifndef XXFCLIB_FORMAT_TPLINK_H
#define XXFCLIB_FORMAT_TPLINK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Vendor string at +4, the strongest fixed evidence in the Linux header. */
#define XX_TPLINK_VENDOR_STRING "TP-LINK Technologies"
#define XX_TPLINK_VENDOR_OFFSET 4U
#define XX_TPLINK_VENDOR_LENGTH 20U
/** Fixed header length of the Linux variant. */
#define XX_TPLINK_HEADER_SIZE 0x200U
/** Offset of the binary field block inside the Linux header. */
#define XX_TPLINK_STRUCTURE_OFFSET 0x40U
/** md5sum1 field: offset inside the header and length. */
#define XX_TPLINK_MD5SUM1_OFFSET 0x4CU
#define XX_TPLINK_MD5_SIZE 16U

/** RTOS variant: leading magic, "IMG0" at +20, header length, size bias. */
#define XX_TPLINK_RTOS_MAGIC1 UINT32_C(0x00142FC0)
#define XX_TPLINK_RTOS_MAGIC2 UINT32_C(0x494D4730)
#define XX_TPLINK_RTOS_HEADER_SIZE 0x94U
#define XX_TPLINK_RTOS_SIZE_BIAS 20U

/** kernel, rootfs, bootloader. */
#define XX_TPLINK_MAX_RECORDS 3U

/** Which of the two TP-Link wrappers was recognised. */
typedef enum xx_tplink_variant_e {
    XX_TPLINK_VARIANT_NONE = 0,
    XX_TPLINK_VARIANT_LINUX = 1, /**< mktplinkfw, 0x200 header. */
    XX_TPLINK_VARIANT_RTOS = 2   /**< "IMG0", 0x94 header. */
} xx_tplink_variant_t;

typedef struct xx_tplink xx_tplink;
typedef struct xx_tplink xx_tplink_t;
typedef struct xx_tplink XTpLink;

struct xx_tplink {
    Abstractformat format;
    xx_tplink_variant_t variant;
    uint64_t number_of_records;
    uint32_t header_size;
    uint32_t image_size;          /**< fw_length, or data_size + 20 for RTOS. */
    uint32_t hardware_id;         /**< Linux variant only. */
    uint32_t hardware_revision;   /**< Linux variant only. */
    uint32_t kernel_load_address; /**< Linux variant only. */
    uint32_t kernel_entry_point;  /**< Linux variant only. */
    uint32_t kernel_offset;
    uint32_t kernel_length;
    uint32_t rootfs_offset;
    uint32_t rootfs_length;
    uint32_t bootloader_offset;
    uint32_t bootloader_length;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t model_number;       /**< RTOS variant only. */
    uint8_t hardware_rev_major;  /**< RTOS variant only. */
    uint8_t hardware_rev_minor;  /**< RTOS variant only. */
    uint8_t md5sum1[XX_TPLINK_MD5_SIZE]; /**< Stored digest, Linux variant. */
    bool md5_checked; /**< The image digest was recomputed. */
    bool md5_valid;   /**< ...and one of the known salts reproduced it. */
    int md5_salt_index; /**< Which salt matched, or -1.  Advisory. */
    bool header_big_endian; /**< Linux variant field decode order. */
    int64_t archive_end;    /**< base_address + image size, or -1. */
    void *internal;
};

XXFC_API void xx_tplink_init(xx_tplink *tplink, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_tplink *xx_tplink_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_tplink_destroy(xx_tplink *tplink);
XXFC_API void xx_tplink_free(xx_tplink *tplink);

XXFC_API bool xx_tplink_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tplink_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_tplink_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_tplink_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tplink_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tplink_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tplink_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tplink_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tplink_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_tplink_get_number_of_records(const xx_tplink *tplink);
XXFC_API xx_tplink_variant_t xx_tplink_get_variant(const xx_tplink *tplink);
XXFC_API uint32_t xx_tplink_get_image_size(const xx_tplink *tplink);
XXFC_API uint32_t xx_tplink_get_hardware_id(const xx_tplink *tplink);
/** fw_version as stored, or NULL.  Linux variant only. */
XXFC_API const char *xx_tplink_get_firmware_version(const xx_tplink *tplink);
/** vendor_name as stored, or NULL.  Linux variant only. */
XXFC_API const char *xx_tplink_get_vendor_name(const xx_tplink *tplink);
/** true when one of the known salts reproduced the stored md5sum1. */
XXFC_API bool xx_tplink_get_md5_valid(const xx_tplink *tplink);
/** Index into the known-salt table, or -1 when nothing matched. */
XXFC_API int xx_tplink_get_md5_salt_index(const xx_tplink *tplink);
/** true when the Linux header decoded as big endian. */
XXFC_API bool xx_tplink_get_header_big_endian(const xx_tplink *tplink);

static inline Abstractformat *xx_tplink_to_format(xx_tplink *tplink) {
    return tplink ? &tplink->format : NULL;
}
static inline void XTpLink_init(xx_tplink *tplink, xx_io_device *dev,
                                int64_t base_address) {
    xx_tplink_init(tplink, dev, base_address);
}
static inline xx_tplink *XTpLink_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_tplink_create(dev, base_address);
}
static inline void XTpLink_free(xx_tplink *tplink) { xx_tplink_free(tplink); }
static inline bool XTpLink_is_valid(xx_tplink *tplink, xx_pd_struct *pd) {
    return tplink ? xx_format_is_valid(&tplink->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TPLINK_H */

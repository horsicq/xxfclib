/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_jboot.h @brief JBOOT firmware headers: SCH2, STAG and ARM. */

/* JBOOT is the bootloader Broadcom/DSL-era D-Link and Alpha Networks devices
 * ship (dsl-274x, DAP-13xx, DIR-6xx, ...).  One module in binwalk emits THREE
 * distinct signatures, and all three are handled here as variants of a single
 * reader because they are the same bootloader's image headers and never occur
 * in the same place:
 *
 *   SCH2 - the header in front of a compressed kernel.  40 bytes, little
 *          endian, CRC-protected both for itself and for the kernel.
 *   STAG - a 16-byte staging header, little endian, in front of a kernel.
 *   ARM  - an 80-byte flash-section header, little endian, that names an
 *          erase region and a data region.
 *
 * Everything is LITTLE endian, which is unusual for a vendor firmware wrapper
 * and worth stating explicitly: the D-Link/NETGEAR big-endian wrappers in this
 * library (chk, seama, dlob) are not a guide here.
 *
 * ---------------------------------------------------------------- SCH2 ----
 *   +0   u16  magic, 0x2124, i.e. the bytes "$!"
 *   +2   u8   compression_type, 0 none / 1 jz / 2 gzip / 3 lzma
 *   +3   u8   version, must be 2
 *   +4   u32  ram_entry_address
 *   +8   u32  kernel_image_size
 *   +12  u32  kernel_image_crc
 *   +16  u32  ram_start_address
 *   +20  u32  rootfs_flash_address
 *   +24  u32  rootfs_size
 *   +28  u32  rootfs_crc
 *   +32  u32  header_crc
 *   +36  u16  header_size, must be 40
 *   +38  u16  cmd_line_size
 *
 * The header CRC is the ordinary ISO-HDLC CRC-32 over the whole 40-byte
 * header with the header_crc field itself zeroed; the kernel CRC is the same
 * CRC over kernel_image_size bytes immediately after the header.  Both are
 * verified here, so a JBOOT SCH2 image that this reader accepts is one the
 * bootloader would also accept.  NOTE: this is NOT the JAMCRC variant that
 * TRX uses - no final complement is applied.
 *
 * rootfs_flash_address is a FLASH address, not an offset into the file, and
 * nothing in the header says where the flash window starts.  It is published
 * as metadata but no rootfs record is emitted; guessing the mapping would
 * mean publishing a record that points at the wrong bytes.
 *
 * ---------------------------------------------------------------- STAG ----
 *   +0   u8   cmark
 *   +1   u8   id
 *   +2   u16  magic, 0x2B24
 *   +4   u32  timestamp
 *   +8   u32  image_size
 *   +12  u16  image_checksum
 *   +14  u16  header_checksum
 *
 * cmark == 0xFF marks a factory image; cmark == id marks a system-upgrade
 * image.  Anything else is rejected.  The two 16-bit checksums are NOT
 * verified: binwalk does not verify them either and the exact JBOOT sum
 * algorithm could not be established from a source available here, so a
 * "verification" would have been a guess.  image_size is taken as the size of
 * the payload FOLLOWING the header, which is how binwalk bounds it.
 *
 * ----------------------------------------------------------------- ARM ----
 *   +0   char[12] rom_id, a NUL-padded board string
 *   +12  u16  drange
 *   +14  u16  image_checksum
 *   +16  u32  block_size
 *   +20  u32  reserved2, must be 0
 *   +24  u16  reserved3, must be 0
 *   +26  u8   lpvs, must be 1
 *   +27  u8   mbz, must be 0
 *   +28  u32  timestamp
 *   +32  u32  erase_start
 *   +36  u32  erase_size
 *   +40  u32  data_start
 *   +44  u32  data_size
 *   +48  u32  reserved4, must be 0
 *   +52  u32  reserved5, must be 0
 *   +56  u32  reserved6, must be 0
 *   +60  u32  reserved7, must be 0
 *   +64  u16  header_id, must be 0x4842 ("BH")
 *   +66  u16  header_version, must be <= 4
 *   +68  u16  reserved8, must be 0
 *   +70  u8   section_id
 *   +71  u8   image_info_type
 *   +72  u32  image_info_offset
 *   +76  u16  family
 *   +78  u16  header_checksum
 *
 * The distinguishing magic for the ARM variant sits at offset 64, past the
 * 64-byte prefilter window, so a dispatcher has to probe the device for it.
 * The sixteen must-be-zero reserved bytes at +48 plus lpvs/mbz/header_id
 * together make the match specific enough to be safe.
 *
 * data_start is a FLASH offset for the section, not a file offset; the data
 * itself follows the header in the file.  data_size is attacker-controlled
 * and is bounded against the device before any record is published.
 */

#ifndef XXFCLIB_FORMAT_JBOOT_H
#define XXFCLIB_FORMAT_JBOOT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_JBOOT_SCH2_MAGIC UINT32_C(0x2124) /**< u16 LE, bytes "$!". */
#define XX_JBOOT_STAG_MAGIC UINT32_C(0x2B24) /**< u16 LE at +2. */
#define XX_JBOOT_ARM_MAGIC UINT32_C(0x4842)  /**< u16 LE at +64, "BH". */

#define XX_JBOOT_SCH2_HEADER_SIZE 40U
#define XX_JBOOT_STAG_HEADER_SIZE 16U
#define XX_JBOOT_ARM_HEADER_SIZE 80U

/** Offset of the ARM rom_id field's length, i.e. where the structure starts. */
#define XX_JBOOT_ARM_ROM_ID_SIZE 12U

/** Which of the three headers was recognised. */
typedef enum {
    XX_JBOOT_VARIANT_NONE = 0,
    XX_JBOOT_VARIANT_SCH2 = 1,
    XX_JBOOT_VARIANT_STAG = 2,
    XX_JBOOT_VARIANT_ARM = 3
} xx_jboot_variant_t;

/** SCH2 compression_type values. */
typedef enum {
    XX_JBOOT_COMPRESSION_NONE = 0,
    XX_JBOOT_COMPRESSION_JZ = 1,
    XX_JBOOT_COMPRESSION_GZIP = 2,
    XX_JBOOT_COMPRESSION_LZMA = 3
} xx_jboot_compression_t;

typedef struct xx_jboot xx_jboot;
typedef struct xx_jboot xx_jboot_t;
typedef struct xx_jboot XJBoot;

struct xx_jboot {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t variant;     /**< xx_jboot_variant_t. */
    uint32_t header_size; /**< 40, 16 or 80. */
    uint32_t payload_size;
    int64_t archive_end; /**< base_address + header + payload, or -1. */

    /* SCH2 */
    uint32_t compression;
    uint32_t kernel_size;
    uint32_t kernel_crc;
    uint32_t kernel_entry_point;
    uint32_t rootfs_flash_address;
    uint32_t rootfs_size;
    uint32_t rootfs_crc;
    uint32_t cmd_line_size;

    /* STAG */
    uint32_t stag_cmark;
    uint32_t stag_id;
    bool is_factory_image;
    bool is_sysupgrade_image;

    /* ARM */
    char rom_id[XX_JBOOT_ARM_ROM_ID_SIZE + 1U];
    uint32_t erase_start;
    uint32_t erase_size;
    uint32_t data_start;
    uint32_t header_version;
    uint32_t section_id;
    uint32_t family;

    uint32_t timestamp; /**< SCH2 has none; STAG and ARM both carry one. */
    void *internal;
};

XXFC_API void xx_jboot_init(xx_jboot *jboot, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_jboot *xx_jboot_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_jboot_destroy(xx_jboot *jboot);
XXFC_API void xx_jboot_free(xx_jboot *jboot);

XXFC_API bool xx_jboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jboot_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_jboot_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_jboot_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jboot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jboot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jboot_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jboot_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jboot_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint32_t xx_jboot_get_variant(const xx_jboot *jboot);
XXFC_API const char *xx_jboot_get_variant_name(const xx_jboot *jboot);
XXFC_API uint32_t xx_jboot_get_header_size(const xx_jboot *jboot);
XXFC_API uint32_t xx_jboot_get_payload_size(const xx_jboot *jboot);
XXFC_API const char *xx_jboot_get_rom_id(const xx_jboot *jboot);
XXFC_API int64_t xx_jboot_get_archive_end(const xx_jboot *jboot);

static inline Abstractformat *xx_jboot_to_format(xx_jboot *jboot) {
    return jboot ? &jboot->format : NULL;
}
static inline void XJBoot_init(xx_jboot *jboot, xx_io_device *dev,
                               int64_t base_address) {
    xx_jboot_init(jboot, dev, base_address);
}
static inline xx_jboot *XJBoot_create(xx_io_device *dev, int64_t base_address) {
    return xx_jboot_create(dev, base_address);
}
static inline void XJBoot_free(xx_jboot *jboot) { xx_jboot_free(jboot); }
static inline bool XJBoot_is_valid(xx_jboot *jboot, xx_pd_struct *pd) {
    return jboot ? xx_format_is_valid(&jboot->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JBOOT_H */

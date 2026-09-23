/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * @file xx_linuxarm64.h
 * @brief Linux kernel ARM64 boot image ("Image") header reader.
 *
 * Layout (Documentation/arch/arm64/booting.rst, all fields little endian):
 *
 *     0x00  u32  code0        executable code (the EFI stub puts "MZ" here)
 *     0x04  u32  code1        executable code
 *     0x08  u64  text_offset  image load offset
 *     0x10  u64  image_size   effective image size (memory footprint, BSS
 *                             included; zero before Linux 3.17)
 *     0x18  u64  flags        bit 0 kernel endianness (1 = big),
 *                             bits 1-2 page size, bit 3 physical placement,
 *                             bits 4-63 reserved (must be zero)
 *     0x20  u64  res2         reserved, zero
 *     0x28  u64  res3         reserved, zero
 *     0x30  u64  res4         reserved, zero
 *     0x38  u32  magic        "ARM\x64"
 *     0x3C  u32  res5         offset of the PE header ("PE\0\0")
 *
 * Source: binwalk's src/signatures/linux.rs (linux_arm64_boot_image_*) and
 * src/structures/linux.rs (parse_linux_arm64_boot_image_header).  binwalk
 * validates the three reserved words, the reserved flag bits and a "PE"
 * signature at res5, reports the 64-byte header as the carved result
 * (result.size == 64) and extracts nothing, so this is a non-archive reader
 * whose format size is the 64-byte header.  image_size is NOT a file length:
 * it includes BSS and page tables and exceeds the Image file on real kernels;
 * it is exposed only as information, exactly as binwalk shows it.
 */

#ifndef XXFCLIB_FORMAT_LINUXARM64_H
#define XXFCLIB_FORMAT_LINUXARM64_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LINUXARM64_HEADER_SIZE       64U
#define XX_LINUXARM64_MAGIC             "ARM\x64"
#define XX_LINUXARM64_MAGIC_SIZE        4U
#define XX_LINUXARM64_MAGIC_OFFSET      0x38U
#define XX_LINUXARM64_PE_OFFSET_FIELD   0x3CU
#define XX_LINUXARM64_RESERVED_OFFSET   0x20U
#define XX_LINUXARM64_RESERVED_SIZE     24U
#define XX_LINUXARM64_FLAGS_RESERVED    UINT64_C(0xFFFFFFFFFFFFFFF0)
#define XX_LINUXARM64_FLAG_BE           UINT64_C(0x1)
#define XX_LINUXARM64_FLAG_PHYS_ANY     UINT64_C(0x8)

typedef struct xx_linuxarm64 xx_linuxarm64;
typedef struct xx_linuxarm64 xx_linuxarm64_t;
typedef struct xx_linuxarm64 XLinuxArm64;

struct xx_linuxarm64 {
    Abstractformat format;  /**< Base format structure (first member) */
    uint32_t code0;         /**< First instruction word (0x00). */
    uint32_t code1;         /**< Second instruction word (0x04). */
    uint64_t text_offset;   /**< Image load offset (0x08). */
    uint64_t image_size;    /**< Effective image size (0x10), informational. */
    uint64_t flags;         /**< Kernel flags (0x18). */
    uint32_t pe_offset;     /**< Offset of the "PE\0\0" signature (0x3C). */
    uint32_t page_size;     /**< From flags bits 1-2: 0 unspecified, 4096,
                                 16384 or 65536. */
    bool kernel_big_endian; /**< flags bit 0. */
    bool phys_placement_anywhere; /**< flags bit 3. */
    bool has_mz;            /**< The EFI stub's "MZ" occupies code0. */
};

XXFC_API void xx_linuxarm64_init(xx_linuxarm64 *image, xx_io_device *dev,
                                 int64_t base_address);
XXFC_API xx_linuxarm64 *xx_linuxarm64_create(xx_io_device *dev,
                                             int64_t base_address);
XXFC_API void xx_linuxarm64_destroy(xx_linuxarm64 *image);
XXFC_API void xx_linuxarm64_free(xx_linuxarm64 *image);

XXFC_API bool xx_linuxarm64_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_linuxarm64_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_linuxarm64_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);

XXFC_API uint64_t xx_linuxarm64_get_text_offset(const xx_linuxarm64 *image);
XXFC_API uint64_t xx_linuxarm64_get_image_size(const xx_linuxarm64 *image);
XXFC_API uint64_t xx_linuxarm64_get_flags(const xx_linuxarm64 *image);
XXFC_API uint32_t xx_linuxarm64_get_pe_offset(const xx_linuxarm64 *image);
XXFC_API uint32_t xx_linuxarm64_get_page_size(const xx_linuxarm64 *image);
XXFC_API bool xx_linuxarm64_is_kernel_big_endian(const xx_linuxarm64 *image);

static inline Abstractformat *xx_linuxarm64_to_format(xx_linuxarm64 *image) {
    return image ? &image->format : NULL;
}
static inline void XLinuxArm64_init(xx_linuxarm64 *image, xx_io_device *dev,
                                    int64_t base_address) {
    xx_linuxarm64_init(image, dev, base_address);
}
static inline xx_linuxarm64 *XLinuxArm64_create(xx_io_device *dev,
                                                int64_t base_address) {
    return xx_linuxarm64_create(dev, base_address);
}
static inline void XLinuxArm64_free(xx_linuxarm64 *image) {
    xx_linuxarm64_free(image);
}
static inline bool XLinuxArm64_is_valid(xx_linuxarm64 *image,
                                        xx_pd_struct *pd) {
    return image ? xx_format_is_valid(&image->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LINUXARM64_H */

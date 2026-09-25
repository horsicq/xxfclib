/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_linuxzimage.h
 * @brief Linux ARM (32-bit) compressed kernel image, "zImage".
 *
 * Layout (arch/arm/boot/compressed/head.S and vmlinux.lds.S):
 *
 *     0x00  8 x u32  "mov r0, r0" (0xE1A00000), the legacy a.out patch area
 *     0x20  u32      branch over the header ("b 1f")
 *     0x24  u32      magic 0x016F2818
 *     0x28  u32      start: absolute load/run address of the zImage
 *     0x2C  u32      end:   address of _edata, the end of the zImage
 *     0x30  u32      (v3.x+) endianness flag 0x04030201, stored natively
 *     0x34  u32      (v4.x+) 0x45454545
 *     0x38  u32      (v4.x+) offset of the additional data table
 *
 * Byte order.  A little-endian kernel stores every word little-endian.  A
 * BE32 kernel (ARMv5 and older) stores the instructions and the header words
 * big-endian.  A BE8 kernel (ARMv6+) stores the instructions little-endian;
 * its header words are big-endian before ZIMAGE_MAGIC() (Linux 3.11) and
 * byte-swapped back to little-endian after it.  The magic, start and end
 * words are always stored in one byte order, so start and end are read in
 * the byte order in which the magic matched.
 *
 * Source: binwalk's src/signatures/linux.rs (linux_arm_zimage_*) and
 * src/structures/linux.rs (parse_linux_arm_zimage_header).  binwalk matches
 * 0x016F2818 in either byte order at offset 36 and requires the eight words
 * at offset 0, read little-endian, to be identical and equal to 0xE1A00000
 * ("little") or 0x0000A0E1 ("big").  It reports no size (result.size == 0,
 * so it carves to the next signature or EOF) and extracts nothing.  This is
 * a non-archive reader; it is stricter than binwalk in requiring
 * start < end, end - start >= 0x30 and end - start <= the bytes available,
 * and its format size is end - start, the zImage length that the kernel's
 * appended-DTB code and U-Boot's bootz both use.  Anything after it (an
 * appended DTB, flash padding) is overlay.
 */

#ifndef XXFCLIB_FORMAT_LINUXZIMAGE_H
#define XXFCLIB_FORMAT_LINUXZIMAGE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LINUXZIMAGE_NOP_COUNT            8U
#define XX_LINUXZIMAGE_NOP_LE               UINT32_C(0xE1A00000)
#define XX_LINUXZIMAGE_NOP_BE               UINT32_C(0x0000A0E1)
#define XX_LINUXZIMAGE_MAGIC                UINT32_C(0x016F2818)
#define XX_LINUXZIMAGE_BRANCH_OFFSET        0x20U
#define XX_LINUXZIMAGE_MAGIC_OFFSET         0x24U
#define XX_LINUXZIMAGE_START_OFFSET         0x28U
#define XX_LINUXZIMAGE_END_OFFSET           0x2CU
#define XX_LINUXZIMAGE_HEADER_SIZE          0x30U
#define XX_LINUXZIMAGE_ENDIAN_FLAG_OFFSET   0x30U
#define XX_LINUXZIMAGE_ENDIAN_FLAG          UINT32_C(0x04030201)
#define XX_LINUXZIMAGE_TABLE_MAGIC_OFFSET   0x34U
#define XX_LINUXZIMAGE_TABLE_MAGIC          UINT32_C(0x45454545)
#define XX_LINUXZIMAGE_TABLE_OFFSET_FIELD   0x38U
#define XX_LINUXZIMAGE_EXTENDED_HEADER_SIZE 0x3CU

typedef struct xx_linuxzimage xx_linuxzimage;
typedef struct xx_linuxzimage xx_linuxzimage_t;
typedef struct xx_linuxzimage XLinuxZImage;

struct xx_linuxzimage {
    Abstractformat format;    /**< Base format structure (first member) */
    uint32_t start_address;   /**< Load/run address of the zImage (0x28). */
    uint32_t end_address;     /**< End address, _edata (0x2C). */
    uint32_t branch;          /**< Instruction word at 0x20, as stored in the
                                   instruction byte order. */
    uint32_t table_offset;    /**< Offset of the additional data table (0x38)
                                   when has_table_magic, else 0. */
    bool code_big_endian;     /**< Instructions are BE32; binwalk's
                                   "big endian". */
    bool header_big_endian;   /**< magic/start/end are stored big-endian. */
    bool has_endian_flag;     /**< 0x04030201 is present at 0x30. */
    bool has_table_magic;     /**< 0x45454545 is present at 0x34. */
    bool kernel_big_endian;   /**< From the endianness flag when present,
                                   otherwise from the header byte order. */
};

XXFC_API void xx_linuxzimage_init(xx_linuxzimage *image, xx_io_device *dev,
                                  int64_t base_address);
XXFC_API xx_linuxzimage *xx_linuxzimage_create(xx_io_device *dev,
                                               int64_t base_address);
XXFC_API void xx_linuxzimage_destroy(xx_linuxzimage *image);
XXFC_API void xx_linuxzimage_free(xx_linuxzimage *image);

XXFC_API bool xx_linuxzimage_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_linuxzimage_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_linuxzimage_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);

XXFC_API uint32_t xx_linuxzimage_get_start_address(const xx_linuxzimage *image);
XXFC_API uint32_t xx_linuxzimage_get_end_address(const xx_linuxzimage *image);
XXFC_API uint32_t xx_linuxzimage_get_table_offset(const xx_linuxzimage *image);
XXFC_API bool xx_linuxzimage_is_code_big_endian(const xx_linuxzimage *image);
XXFC_API bool xx_linuxzimage_is_header_big_endian(const xx_linuxzimage *image);
XXFC_API bool xx_linuxzimage_is_kernel_big_endian(const xx_linuxzimage *image);
XXFC_API bool xx_linuxzimage_has_endian_flag(const xx_linuxzimage *image);

static inline Abstractformat *xx_linuxzimage_to_format(xx_linuxzimage *image) {
    return image ? &image->format : NULL;
}
static inline void XLinuxZImage_init(xx_linuxzimage *image, xx_io_device *dev,
                                     int64_t base_address) {
    xx_linuxzimage_init(image, dev, base_address);
}
static inline xx_linuxzimage *XLinuxZImage_create(xx_io_device *dev,
                                                  int64_t base_address) {
    return xx_linuxzimage_create(dev, base_address);
}
static inline void XLinuxZImage_free(xx_linuxzimage *image) {
    xx_linuxzimage_free(image);
}
static inline bool XLinuxZImage_is_valid(xx_linuxzimage *image,
                                         xx_pd_struct *pd) {
    return image ? xx_format_is_valid(&image->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LINUXZIMAGE_H */

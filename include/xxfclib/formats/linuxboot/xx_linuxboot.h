/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_linuxboot.h @brief x86 Linux kernel boot image (zImage/bzImage). */

/* The classic x86 Linux kernel image as the kernel build writes it: a 512-byte
 * floppy boot sector, the real-mode "setup" code, then the protected-mode
 * system (the self-decompressing kernel).  binwalk calls it "Linux kernel
 * boot image" and recognises it by the first sixteen bytes of the old
 * arch/i386/boot/bootsect.S,
 *
 *     b8 c0 07  movw $BOOTSEG,%ax      8e d8  movw %ax,%ds
 *     b8 00 90  movw $INITSEG,%ax      8e c0  movw %ax,%es
 *     b9 00 01  movw $256,%cx          29 f6  subw %si,%si
 *     29        (first byte of subw %di,%di)
 *
 * i.e. the opening of the floppy loader that the 2.x kernels carried until
 * 2.6.0 replaced it with a stub (modern bzImages start with "MZ" or that stub
 * and are not this signature).  The setup header documented in
 * Documentation/i386/boot.txt then sits at fixed offsets:
 *
 *   0x1F1  u8   setup_sects       setup length in sectors, 0 means 4
 *   0x1F2  u16  root_flags
 *   0x1F4  u16  syssize           system length in 16-byte paragraphs
 *                                 (u32 from boot protocol 2.04 on)
 *   0x1F6  u16  swap_dev          0x1F8 u16 ram_size   0x1FA u16 vid_mode
 *   0x1FC  u16  root_dev
 *   0x1FE  u16  boot_flag         0xAA55
 *   0x200  jump                   short jump over the header
 *   0x202  "HdrS"                 boot protocol >= 2.00 (kernel 1.3.73)
 *   0x206  u16  version           0x0200 .. 0x0203 for these kernels
 *   0x20E  u16  kernel_version    version string pointer, less 0x200
 *   0x211  u8   loadflags         bit 0: loaded high (bzImage)
 *   0x214  u32  code32_start
 *
 * The file is boot sector + setup_sects * 512 + the system.  Kernels before
 * 2.6 write the system unpadded, so syssize = ceil(system / 16) pins the end
 * only to within fifteen bytes, and for a bzImage below protocol 2.04 the
 * 16-bit field wraps once the system passes 1 MiB.  The reader resolves both
 * from the gzip "piggy" the build appends last (see xx_linuxboot.c).
 *
 * Not an archive: binwalk extracts nothing for this signature.  The
 * setup/system split is exposed through the getters below instead.
 */

#ifndef XXFCLIB_FORMAT_LINUXBOOT_H
#define XXFCLIB_FORMAT_LINUXBOOT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LINUXBOOT_SECTOR_SIZE 512U
#define XX_LINUXBOOT_MAGIC_SIZE 16U
/** The sixteen bootsect.S bytes binwalk matches at offset 0. */
#define XX_LINUXBOOT_MAGIC \
    "\xB8\xC0\x07\x8E\xD8\xB8\x00\x90\x8E\xC0\xB9\x00\x01\x29\xF6\x29"
#define XX_LINUXBOOT_HDRS_OFFSET 0x202U
#define XX_LINUXBOOT_KERNEL_VERSION_MAX 128U

typedef struct xx_linuxboot xx_linuxboot;
typedef struct xx_linuxboot xx_linuxboot_t;
typedef struct xx_linuxboot XLinuxboot;

struct xx_linuxboot {
    Abstractformat format;
    uint16_t protocol_version;  /**< 0x206, e.g. 0x0203. */
    uint8_t setup_sects;        /**< 0x1F1 as stored (0 means 4). */
    uint8_t loadflags;          /**< 0x211. */
    uint16_t root_flags;        /**< 0x1F2. */
    uint16_t swap_dev;          /**< 0x1F6. */
    uint16_t ram_size;          /**< 0x1F8. */
    uint16_t vid_mode;          /**< 0x1FA. */
    uint16_t root_dev;          /**< 0x1FC. */
    uint16_t kernel_version_offset; /**< 0x20E as stored, 0 if absent. */
    uint32_t syssize;           /**< 0x1F4 as stored (16 or 32 bits). */
    uint32_t code32_start;      /**< 0x214. */
    uint32_t system_paragraphs; /**< syssize after wrap resolution. */
    int64_t setup_offset;       /**< base + 512. */
    int64_t setup_size;         /**< effective setup_sects * 512. */
    int64_t system_offset;      /**< First byte of the protected-mode system. */
    int64_t system_size;        /**< Bytes of system inside the format size. */
    int64_t payload_offset;     /**< The gzip piggy, or -1 when not located. */
    int64_t payload_size;       /**< Its input_len word, or 0. */
    bool is_bzimage;            /**< loadflags bit 0 (LOADED_HIGH). */
    bool size_exact;            /**< End pinned by the piggy or by EOF. */
    char kernel_version[XX_LINUXBOOT_KERNEL_VERSION_MAX];
};

XXFC_API void xx_linuxboot_init(xx_linuxboot *image, xx_io_device *dev,
                                int64_t base_address);
XXFC_API xx_linuxboot *xx_linuxboot_create(xx_io_device *dev,
                                           int64_t base_address);
XXFC_API void xx_linuxboot_destroy(xx_linuxboot *image);
XXFC_API void xx_linuxboot_free(xx_linuxboot *image);

XXFC_API bool xx_linuxboot_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_linuxboot_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_linuxboot_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);

XXFC_API uint16_t xx_linuxboot_get_protocol_version(const xx_linuxboot *image);
XXFC_API const char *xx_linuxboot_get_kernel_version(const xx_linuxboot *image);
XXFC_API int64_t xx_linuxboot_get_setup_size(const xx_linuxboot *image);
XXFC_API int64_t xx_linuxboot_get_system_offset(const xx_linuxboot *image);
XXFC_API int64_t xx_linuxboot_get_system_size(const xx_linuxboot *image);
XXFC_API int64_t xx_linuxboot_get_payload_offset(const xx_linuxboot *image);
XXFC_API bool xx_linuxboot_is_bzimage(const xx_linuxboot *image);

static inline Abstractformat *xx_linuxboot_to_format(xx_linuxboot *image) {
    return image ? &image->format : NULL;
}
static inline void XLinuxboot_init(xx_linuxboot *image, xx_io_device *dev,
                                   int64_t base_address) {
    xx_linuxboot_init(image, dev, base_address);
}
static inline xx_linuxboot *XLinuxboot_create(xx_io_device *dev,
                                              int64_t base_address) {
    return xx_linuxboot_create(dev, base_address);
}
static inline void XLinuxboot_free(xx_linuxboot *image) {
    xx_linuxboot_free(image);
}
static inline bool XLinuxboot_is_valid(xx_linuxboot *image, xx_pd_struct *pd) {
    return image ? xx_format_is_valid(&image->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LINUXBOOT_H */

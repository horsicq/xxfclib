/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_androidboot.h @brief Android boot image reader. */

/* The Android boot image is the container a device's boot partition holds:
 * a header followed by page-aligned blobs.  Everything is LITTLE endian and
 * the magic is "ANDROID!" at offset 0.  Layout facts come from the AOSP
 * public specification, system/tools/mkbootimg/include/bootimg/bootimg.h.
 *
 * header_version lives at offset 40 in EVERY version, which is the only
 * field the two structural families share.
 *
 *   v0 (1632 bytes), v1 (1648), v2 (1660)
 *     +0    "ANDROID!"
 *     +8    u32 kernel size      +12  u32 kernel address
 *     +16   u32 ramdisk size     +20  u32 ramdisk address
 *     +24   u32 second size      +28  u32 second address
 *     +32   u32 tags address
 *     +36   u32 page size        (a power of two, 2048..65536)
 *     +40   u32 header version   (in v0 this doubles as "unused")
 *     +44   u32 os version/patch level
 *     +48   char name[16]
 *     +64   char cmdline[512]
 *     +576  u32 id[8]            (SHA-1 of the payload, unverified here)
 *     +608  char extra cmdline[1024]
 *     +1632 u32 recovery dtbo size   (v1+)
 *     +1636 u64 recovery dtbo offset (v1+)
 *     +1644 u32 header size          (v1+, must equal the version's size)
 *     +1648 u32 dtb size             (v2+)
 *     +1652 u64 dtb address          (v2+)
 *
 *   v3 (1580 bytes), v4 (1584) - a different structure, not an extension:
 *   the load addresses, the second stage, the board name and the page size
 *   field are all gone, the page size is fixed at 4096, and header_size is
 *   mandatory.
 *     +0    "ANDROID!"
 *     +8    u32 kernel size      +12  u32 ramdisk size
 *     +16   u32 os version/patch level
 *     +20   u32 header size      (must equal the version's size)
 *     +24   u32 reserved[4]
 *     +40   u32 header version
 *     +44   char cmdline[1536]
 *     +1580 u32 signature size   (v4 only)
 *
 * The payload blobs follow the header, each starting on a page boundary:
 * kernel, ramdisk, second, recovery dtbo, dtb for v0-v2, and kernel,
 * ramdisk, boot signature for v3-v4.  They are published verbatim as
 * records, so a caller can feed the ramdisk (customarily gzip or lz4 over
 * cpio) straight back into the format detector.  The reassembled kernel
 * command line is published alongside them as a synthesised "cmdline.txt".
 *
 * Every declared size is an attacker-controlled u32.  A size is refused at
 * PARSE time against both an absolute ceiling and the device's own size, so
 * a 12-byte header can never make a caller walk a terabyte.
 */

#ifndef XXFCLIB_FORMAT_ANDROIDBOOT_H
#define XXFCLIB_FORMAT_ANDROIDBOOT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_androidboot xx_androidboot;
typedef struct xx_androidboot xx_androidboot_t;
typedef struct xx_androidboot XAndroidBoot;

struct xx_androidboot {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t header_version; /**< 0..4, from offset 40. */
    uint32_t page_size;      /**< Declared for v0-v2, fixed 4096 for v3-v4. */
    uint32_t header_size;    /**< Declared size of the header structure. */
    int64_t archive_end;     /**< base_address + the last blob's page end. */
    void *internal;
};

XXFC_API void xx_androidboot_init(xx_androidboot *image, xx_io_device *dev,
                                  int64_t base_address);
XXFC_API xx_androidboot *xx_androidboot_create(xx_io_device *dev,
                                               int64_t base_address);
XXFC_API void xx_androidboot_destroy(xx_androidboot *image);
XXFC_API void xx_androidboot_free(xx_androidboot *image);

XXFC_API bool xx_androidboot_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_androidboot_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_androidboot_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_androidboot_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_androidboot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_androidboot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_androidboot_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_androidboot_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_androidboot_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_androidboot_get_number_of_records(
    const xx_androidboot *image);
XXFC_API uint64_t xx_androidboot_get_number_of_members(
    const xx_androidboot *image);
XXFC_API uint32_t xx_androidboot_get_header_version(
    const xx_androidboot *image);
XXFC_API uint32_t xx_androidboot_get_page_size(const xx_androidboot *image);
XXFC_API uint32_t xx_androidboot_get_header_size(const xx_androidboot *image);
XXFC_API int64_t xx_androidboot_get_archive_end(const xx_androidboot *image);

static inline Abstractformat *xx_androidboot_to_format(
    xx_androidboot *image) {
    return image ? &image->format : NULL;
}
static inline void XAndroidBoot_init(xx_androidboot *image, xx_io_device *dev,
                                     int64_t base_address) {
    xx_androidboot_init(image, dev, base_address);
}
static inline xx_androidboot *XAndroidBoot_create(xx_io_device *dev,
                                                  int64_t base_address) {
    return xx_androidboot_create(dev, base_address);
}
static inline void XAndroidBoot_free(xx_androidboot *image) {
    xx_androidboot_free(image);
}
static inline bool XAndroidBoot_is_valid(xx_androidboot *image,
                                         xx_pd_struct *pd) {
    return image ? xx_format_is_valid(&image->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ANDROIDBOOT_H */

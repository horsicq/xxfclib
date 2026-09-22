/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uimage.h @brief U-Boot legacy uImage reader. */

/* The legacy U-Boot image is a fixed 64-byte header followed by its payload.
 * Every field is BIG endian, whatever the target's own byte order.
 *
 *   +0   u32   ih_magic 0x27051956
 *   +4   u32   ih_hcrc, CRC32 of these 64 bytes WITH ih_hcrc ITSELF ZEROED
 *   +8   u32   ih_time, creation timestamp
 *   +12  u32   ih_size, payload size in bytes, the header excluded
 *   +16  u32   ih_load, load address
 *   +20  u32   ih_ep, entry point
 *   +24  u32   ih_dcrc, CRC32 of the payload
 *   +28  u8    ih_os
 *   +29  u8    ih_arch
 *   +30  u8    ih_type
 *   +31  u8    ih_comp
 *   +32  char  ih_name[32], NUL padded, not necessarily NUL terminated
 *
 * ih_type 4 is IH_TYPE_MULTI.  Its payload opens with a list of BIG endian
 * u32 component sizes terminated by a zero word; the components follow in
 * order, each padded up to a 4-byte boundary.  That list is what lets a
 * multi-file image be split into separate members.
 *
 * ih_comp names the compressor applied to the payload as a whole: 0 none,
 * 1 gzip, 2 bzip2, 3 LZMA, 4 LZO, 5 LZ4, 6 zstd.  Only an IH_COMP_NONE
 * multi-file image is split, because the component size list is itself part
 * of the compressed stream otherwise.
 *
 * The modern replacement for this container is the FIT image, which is a
 * flattened device tree rather than a fixed header; it is read by xx_dtb.
 *
 * ih_size is attacker controlled and 32-bit.  It is checked against the real
 * device size before anything is read, the component size list is bounded by
 * the payload it lives in, and no buffer is ever sized from a declared
 * length alone.
 */

#ifndef XXFCLIB_FORMAT_UIMAGE_H
#define XXFCLIB_FORMAT_UIMAGE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ih_comp values, as defined by U-Boot's include/image.h. */
typedef enum xx_uimage_comp_e {
    XX_UIMAGE_COMP_NONE = 0,
    XX_UIMAGE_COMP_GZIP = 1,
    XX_UIMAGE_COMP_BZIP2 = 2,
    XX_UIMAGE_COMP_LZMA = 3,
    XX_UIMAGE_COMP_LZO = 4,
    XX_UIMAGE_COMP_LZ4 = 5,
    XX_UIMAGE_COMP_ZSTD = 6
} xx_uimage_comp_t;

/** The ih_type values this reader treats specially. */
#define XX_UIMAGE_TYPE_MULTI 4U

typedef struct xx_uimage xx_uimage;
typedef struct xx_uimage xx_uimage_t;
typedef struct xx_uimage XUimage;

struct xx_uimage {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t header_crc;      /**< ih_hcrc as stored. */
    uint32_t data_crc;        /**< ih_dcrc as stored. */
    uint32_t timestamp;       /**< ih_time. */
    uint32_t data_size;       /**< ih_size. */
    uint32_t load_address;    /**< ih_load. */
    uint32_t entry_point;     /**< ih_ep. */
    uint8_t os;               /**< ih_os. */
    uint8_t cpu_arch;         /**< ih_arch. */
    uint8_t image_type;       /**< ih_type. */
    uint8_t compression;      /**< ih_comp. */
    char name[33];            /**< ih_name, NUL terminated here. */
    int64_t archive_end;      /**< base_address + 64 + ih_size, or -1. */
    void *internal;
};

XXFC_API void xx_uimage_init(xx_uimage *uimage, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_uimage *xx_uimage_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_uimage_destroy(xx_uimage *uimage);
XXFC_API void xx_uimage_free(xx_uimage *uimage);

XXFC_API bool xx_uimage_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uimage_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_uimage_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_uimage_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_uimage_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_uimage_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_uimage_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_uimage_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_uimage_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Human-readable name for an ih_comp value; never NULL. */
XXFC_API const char *xx_uimage_compression_to_string(uint8_t compression);

XXFC_API uint64_t xx_uimage_get_number_of_records(const xx_uimage *uimage);
XXFC_API uint64_t xx_uimage_get_number_of_members(const xx_uimage *uimage);
XXFC_API uint32_t xx_uimage_get_data_size(const xx_uimage *uimage);
XXFC_API uint32_t xx_uimage_get_data_crc(const xx_uimage *uimage);
XXFC_API uint32_t xx_uimage_get_header_crc(const xx_uimage *uimage);
XXFC_API uint8_t xx_uimage_get_compression(const xx_uimage *uimage);
XXFC_API uint8_t xx_uimage_get_image_type(const xx_uimage *uimage);
XXFC_API const char *xx_uimage_get_name(const xx_uimage *uimage);
XXFC_API int64_t xx_uimage_get_archive_end(const xx_uimage *uimage);

static inline Abstractformat *xx_uimage_to_format(xx_uimage *uimage) {
    return uimage ? &uimage->format : NULL;
}
static inline void XUimage_init(xx_uimage *uimage, xx_io_device *dev,
                                int64_t base_address) {
    xx_uimage_init(uimage, dev, base_address);
}
static inline xx_uimage *XUimage_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_uimage_create(dev, base_address);
}
static inline void XUimage_free(xx_uimage *uimage) { xx_uimage_free(uimage); }
static inline bool XUimage_is_valid(xx_uimage *uimage, xx_pd_struct *pd) {
    return uimage ? xx_format_is_valid(&uimage->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UIMAGE_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uefi_fv.h @brief UEFI PI firmware volume reader. */

/* A firmware volume is the container a PI-conformant platform stores its
 * boot firmware in. Everything is little endian.
 *
 *   EFI_FIRMWARE_VOLUME_HEADER
 *     +0   ZeroVector[16]  reserved, normally zero
 *     +16  FileSystemGuid  the FFS the volume is formatted with
 *     +32  u64  FvLength, the whole volume including this header
 *     +40  u32  Signature, "_FVH" (0x4856465F)   <- the detection key
 *     +44  u32  Attributes
 *     +48  u16  HeaderLength
 *     +50  u16  Checksum, a 16-bit sum over HeaderLength bytes to zero
 *     +52  u16  ExtHeaderOffset
 *     +54  u8   Reserved
 *     +55  u8   Revision
 *     +56  the block map, pairs of u32 NumBlocks/Length ended by 0,0
 *
 * The volume does NOT start with its signature - the first sixteen bytes are
 * the zero vector - so a scan has to key on "_FVH" at +40 and then walk back.
 *
 *   EFI_FFS_FILE_HEADER, 8-byte aligned inside the volume
 *     +0   Name GUID
 *     +16  u16  IntegrityCheck
 *     +18  u8   Type
 *     +19  u8   Attributes
 *     +20  u24  Size, header included      <- 24 bits, the hazard here
 *     +23  u8   State
 *   When Attributes carries FFS_ATTRIB_LARGE_FILE and Size reads 0xFFFFFF the
 *   header is an EFI_FFS_FILE_HEADER2 with a u64 ExtendedSize at +24.
 *
 *   EFI_COMMON_SECTION_HEADER, 4-byte aligned inside a file
 *     +0   u24  Size
 *     +3   u8   Type
 *   A Size of 0xFFFFFF selects EFI_COMMON_SECTION_HEADER2, whose real u32
 *   size lives at +4 and whose header is eight bytes.
 *
 * Both the 24-bit sizes can be zero (which would make a walk stand still) and
 * can exceed their container, so the file walk and the section walk each keep
 * their own bound, re-align every step and refuse a step that does not move
 * forward. Volumes nest - a section of type FIRMWARE_VOLUME_IMAGE holds a
 * whole volume - so the recursion is depth-capped and remembers the volume
 * offsets it has already entered.
 */

#ifndef XXFCLIB_FORMAT_UEFI_FV_H
#define XXFCLIB_FORMAT_UEFI_FV_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Compression of one published record, reported as XX_META_ID_COMPRESSION_METHOD. */
#define XX_UEFI_FV_METHOD_STORE 0U     /**< Bytes lie on the device as-is. */
#define XX_UEFI_FV_METHOD_LZMA 1U      /**< LZMA_CUSTOM_DECOMPRESS payload. */
#define XX_UEFI_FV_METHOD_TIANO 2U     /**< EFI/Tiano compression, no codec. */
#define XX_UEFI_FV_METHOD_UNKNOWN 3U   /**< Unrecognised GUID-defined codec. */

typedef struct xx_uefi_fv xx_uefi_fv;
typedef struct xx_uefi_fv xx_uefi_fv_t;
typedef struct xx_uefi_fv XUefiFv;

struct xx_uefi_fv {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t number_of_volumes;  /**< Volumes entered, the nested ones too. */
    uint64_t number_of_files;    /**< FFS files found across all volumes. */
    uint64_t fv_length;          /**< FvLength of the outermost volume. */
    uint16_t header_length;      /**< HeaderLength of the outermost volume. */
    uint16_t checksum;           /**< Its Checksum field; verified at parse. */
    uint8_t revision;
    int64_t archive_end;         /**< base_address + fv_length, or -1. */
    void *internal;
};

XXFC_API void xx_uefi_fv_init(xx_uefi_fv *fv, xx_io_device *dev,
                              int64_t base_address);
XXFC_API xx_uefi_fv *xx_uefi_fv_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_uefi_fv_destroy(xx_uefi_fv *fv);
XXFC_API void xx_uefi_fv_free(xx_uefi_fv *fv);

XXFC_API bool xx_uefi_fv_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uefi_fv_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_uefi_fv_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_uefi_fv_get_number_of_archive_records(Abstractformat *self,
                                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_uefi_fv_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_uefi_fv_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_uefi_fv_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_uefi_fv_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_uefi_fv_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_uefi_fv_get_number_of_records(const xx_uefi_fv *fv);
XXFC_API uint64_t xx_uefi_fv_get_number_of_members(const xx_uefi_fv *fv);
XXFC_API uint64_t xx_uefi_fv_get_number_of_volumes(const xx_uefi_fv *fv);
XXFC_API uint64_t xx_uefi_fv_get_number_of_files(const xx_uefi_fv *fv);
XXFC_API uint64_t xx_uefi_fv_get_fv_length(const xx_uefi_fv *fv);
XXFC_API uint16_t xx_uefi_fv_get_checksum(const xx_uefi_fv *fv);
XXFC_API uint8_t xx_uefi_fv_get_revision(const xx_uefi_fv *fv);
XXFC_API int64_t xx_uefi_fv_get_archive_end(const xx_uefi_fv *fv);

static inline Abstractformat *xx_uefi_fv_to_format(xx_uefi_fv *fv) {
    return fv ? &fv->format : NULL;
}
static inline void XUefiFv_init(xx_uefi_fv *fv, xx_io_device *dev,
                                int64_t base_address) {
    xx_uefi_fv_init(fv, dev, base_address);
}
static inline xx_uefi_fv *XUefiFv_create(xx_io_device *dev,
                                         int64_t base_address) {
    return xx_uefi_fv_create(dev, base_address);
}
static inline void XUefiFv_free(xx_uefi_fv *fv) { xx_uefi_fv_free(fv); }
static inline bool XUefiFv_is_valid(xx_uefi_fv *fv, xx_pd_struct *pd) {
    return fv ? xx_format_is_valid(&fv->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UEFI_FV_H */

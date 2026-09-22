/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uefi_capsule.h @brief UEFI capsule reader. */

/* A capsule is the container the UEFI firmware update mechanism takes its
 * payload in. Everything is little endian.
 *
 *   EFI_CAPSULE_HEADER
 *     +0   CapsuleGuid       what the body is, and who consumes it
 *     +16  u32  HeaderSize   where the body starts, header included
 *     +20  u32  Flags        bit 16 persist across reset, 17 populate system
 *                            table, 18 initiate reset; the low 16 bits are
 *                            defined by CapsuleGuid
 *     +24  u32  CapsuleImageSize   the whole capsule, header included
 *
 * When CapsuleGuid is EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID the body opens
 * with an EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER:
 *
 *     +0   u32  Version
 *     +4   u16  EmbeddedDriverCount
 *     +6   u16  PayloadItemCount
 *     +8   u64  ItemOffsetList[EmbeddedDriverCount + PayloadItemCount],
 *               each relative to the start of this header; the drivers come
 *               first, the payloads after them
 *
 * and every payload item points at an
 * EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER:
 *
 *     +0   u32  Version, 1 to 3
 *     +4   UpdateImageTypeId
 *     +20  u8   UpdateImageIndex
 *     +21  u8   reserved[3]
 *     +24  u32  UpdateImageSize
 *     +28  u32  UpdateVendorCodeSize
 *     +32  u64  UpdateHardwareInstance   (version 2 and later)
 *     +40  u64  ImageCapsuleSupport      (version 3 and later)
 *
 * followed by UpdateImageSize image bytes and UpdateVendorCodeSize vendor
 * bytes. The image is normally a firmware volume, so it is published as a
 * record of its own for a caller to recurse into.
 */

#ifndef XXFCLIB_FORMAT_UEFI_CAPSULE_H
#define XXFCLIB_FORMAT_UEFI_CAPSULE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CAPSULE_FLAGS_*, the three bits the UEFI specification defines itself. */
#define XX_UEFI_CAPSULE_FLAG_PERSIST_ACROSS_RESET UINT32_C(0x00010000)
#define XX_UEFI_CAPSULE_FLAG_POPULATE_SYSTEM_TABLE UINT32_C(0x00020000)
#define XX_UEFI_CAPSULE_FLAG_INITIATE_RESET UINT32_C(0x00040000)

typedef struct xx_uefi_capsule xx_uefi_capsule;
typedef struct xx_uefi_capsule xx_uefi_capsule_t;
typedef struct xx_uefi_capsule XUefiCapsule;

struct xx_uefi_capsule {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t header_size;       /**< EFI_CAPSULE_HEADER.HeaderSize. */
    uint32_t flags;             /**< EFI_CAPSULE_HEADER.Flags. */
    uint32_t capsule_image_size;/**< EFI_CAPSULE_HEADER.CapsuleImageSize. */
    uint32_t driver_count;      /**< Embedded drivers, zero when not an FMP. */
    uint32_t payload_count;     /**< Payload items, zero when not an FMP. */
    bool is_firmware_management;/**< The body is an FMP capsule. */
    int64_t archive_end;        /**< base_address + capsule_image_size, or -1. */
    void *internal;
};

XXFC_API void xx_uefi_capsule_init(xx_uefi_capsule *capsule, xx_io_device *dev,
                                   int64_t base_address);
XXFC_API xx_uefi_capsule *xx_uefi_capsule_create(xx_io_device *dev,
                                                 int64_t base_address);
XXFC_API void xx_uefi_capsule_destroy(xx_uefi_capsule *capsule);
XXFC_API void xx_uefi_capsule_free(xx_uefi_capsule *capsule);

XXFC_API bool xx_uefi_capsule_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_uefi_capsule_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_uefi_capsule_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_uefi_capsule_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_uefi_capsule_create_archive_records_reading(Abstractformat *self,
                                               const xx_list_s *options,
                                               xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_uefi_capsule_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_uefi_capsule_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_uefi_capsule_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_uefi_capsule_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_uefi_capsule_get_number_of_records(
    const xx_uefi_capsule *capsule);
XXFC_API uint64_t xx_uefi_capsule_get_number_of_members(
    const xx_uefi_capsule *capsule);
XXFC_API uint32_t xx_uefi_capsule_get_header_size(
    const xx_uefi_capsule *capsule);
XXFC_API uint32_t xx_uefi_capsule_get_flags(const xx_uefi_capsule *capsule);
XXFC_API uint32_t xx_uefi_capsule_get_image_size(
    const xx_uefi_capsule *capsule);
XXFC_API bool xx_uefi_capsule_is_firmware_management(
    const xx_uefi_capsule *capsule);
XXFC_API int64_t xx_uefi_capsule_get_archive_end(
    const xx_uefi_capsule *capsule);

static inline Abstractformat *xx_uefi_capsule_to_format(
    xx_uefi_capsule *capsule) {
    return capsule ? &capsule->format : NULL;
}
static inline void XUefiCapsule_init(xx_uefi_capsule *capsule,
                                     xx_io_device *dev, int64_t base_address) {
    xx_uefi_capsule_init(capsule, dev, base_address);
}
static inline xx_uefi_capsule *XUefiCapsule_create(xx_io_device *dev,
                                                   int64_t base_address) {
    return xx_uefi_capsule_create(dev, base_address);
}
static inline void XUefiCapsule_free(xx_uefi_capsule *capsule) {
    xx_uefi_capsule_free(capsule);
}
static inline bool XUefiCapsule_is_valid(xx_uefi_capsule *capsule,
                                         xx_pd_struct *pd) {
    return capsule ? xx_format_is_valid(&capsule->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UEFI_CAPSULE_H */

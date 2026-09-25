/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rpm.h @brief RPM package reader (one member: the payload). */

#ifndef XXFCLIB_FORMAT_RPM_H
#define XXFCLIB_FORMAT_RPM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RPM package: lead, signature, header, payload.
 *
 * Every multi-byte field is big endian.
 *
 *   lead, 96 bytes at +0
 *     +0x00  u8[4]   ED AB EE DB
 *     +0x04  u8      major version (3; 4 is accepted too)
 *     +0x05  u8      minor version
 *     +0x06  u16     package type: 0 binary, 1 source
 *     +0x08  u16     architecture number
 *     +0x0A  char[66] "name-version-release", NUL padded
 *     +0x4C  u16     operating system number (1 = Linux)
 *     +0x4E  u16     signature type: 0 none, 1 a fixed 256-byte PGP block,
 *                    5 a header structure (every package since RPM 3)
 *     +0x50  u8[16]  reserved
 *
 *   header structure (the signature for type 5, then the main header)
 *     +0x00  u8[8]   8E AD E8 01 00 00 00 00
 *     +0x08  u32     index entry count  (1..0xFFFF)
 *     +0x0C  u32     data store size    (< 0x10000000)
 *     +0x10  entries of 16 bytes: u32 tag, u32 type (0..9), u32 offset into
 *            the data store, u32 count
 *     then the data store
 *
 *   The signature structure is padded with zeros to a multiple of 8 bytes
 *   from the start of the package; the main header follows at once and the
 *   payload (a cpio archive, normally compressed with gzip, bzip2, xz, lzma
 *   or zstd) starts right after the main header's data store.
 *
 *   Signature tag 1000 (INT32), or 270 (INT64) in packages over 4 GiB, is
 *   the size of the main header plus the payload.  When it is present and
 *   consistent it ends the package; otherwise the package runs to the end
 *   of the device.
 *
 * The reader publishes the payload as one member, byte for byte, the way
 * 7-Zip (-tRpm), unar and Deark do; the compressed cpio inside is left to
 * the gz / bz2 / xz / lzma / zstd and cpio readers.  The member is named
 * NAME-VERSION-RELEASE.ARCH.FORMAT.EXT from the main header tags 1000, 1001,
 * 1002, 1022 and 1124 ("src" instead of ARCH for a source package; the lead
 * name when tag 1000 is missing), EXT from the payload's own magic, else tag
 * 1125.  Every character outside printable ASCII or illegal in a file name
 * becomes '_', and a Windows device stem gets a '_' prefix.
 */
typedef struct xx_rpm {
    Abstractformat format;
    uint64_t number_of_records;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t package_type;      /**< Lead +0x06: 0 binary, 1 source. */
    uint16_t signature_type;    /**< Lead +0x4E: 0, 1 or 5. */
    int64_t signature_offset;   /**< Absolute; -1 without a signature header. */
    int64_t header_offset;      /**< Absolute offset of the main header. */
    int64_t header_size;        /**< Main header, intro to end of store. */
    int64_t payload_offset;     /**< Absolute. */
    int64_t payload_size;       /**< Bytes present, up to the package end. */
    int64_t declared_size;      /**< Package size (lead to payload end) from
                                     signature tag 1000/270, INT64_MAX when
                                     it overflows; -1 if none or ignored. */
    bool truncated;             /**< The declared end lies past the device. */
    char member_name[256];      /**< Name the payload member is published as. */
} xx_rpm;

typedef xx_rpm xx_rpm_t;

/** Lead size; the first header structure starts here. */
#define XX_RPM_LEAD_SIZE 96

XXFC_API void xx_rpm_init(xx_rpm *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_rpm *xx_rpm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rpm_destroy(xx_rpm *archive);
XXFC_API void xx_rpm_free(xx_rpm *archive);

XXFC_API bool xx_rpm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rpm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rpm_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_rpm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rpm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rpm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rpm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rpm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rpm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RPM_H */

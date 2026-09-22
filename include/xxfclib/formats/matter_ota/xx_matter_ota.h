/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_matter_ota.h @brief Matter OTA software image container reader. */

/*
 * The Connectivity Standards Alliance's Matter specification defines an "OTA
 * Software Image File Format" for the images distributed through the OTA
 * Provider cluster.  It is a fixed 16-byte preamble followed by a TLV-encoded
 * header and then one opaque payload - the payload is whatever the vendor's
 * bootloader understands, so this reader's job is to validate the preamble,
 * walk the TLV header, and publish the payload as a single archive record so a
 * caller can hand it back to the detector and recurse.
 *
 * Everything is LITTLE endian, which makes it the odd one out among the
 * firmware wrappers in this library.
 *
 *   +0    u32  magic, 0x1BEEF11E, stored as the bytes 1E F1 EE 1B
 *   +4    u64  total_size, the whole file: preamble + TLV header + payload
 *   +12   u32  header_size, the length of the TLV header that follows
 *   +16   ...  TLV header, header_size bytes
 *   +16+header_size  payload, total_size - 16 - header_size bytes
 *
 * The TLV header is an anonymous structure holding context-specific tags:
 *
 *   0  VendorID                      unsigned
 *   1  ProductID                     unsigned
 *   2  SoftwareVersion               unsigned
 *   3  SoftwareVersionString         UTF-8 string
 *   4  PayloadSize                   unsigned
 *   5  MinApplicableSoftwareVersion  unsigned
 *   6  MaxApplicableSoftwareVersion  unsigned
 *   7  ReleaseNotesURL               UTF-8 string
 *   8  ImageDigestType               unsigned
 *   9  ImageDigest                   octet string
 *
 * ImageDigestType is an index into the IANA "Named Information Hash Algorithm"
 * registry, where 1 is sha-256.  When the header says sha-256 and carries a
 * 32-byte digest, this reader hashes the payload and records whether it
 * matched.  The result is REPORTED, not enforced: the digest covers only the
 * payload, and an image whose digest is wrong is still a structurally valid
 * OTA file that a caller may well want to look inside.
 *
 * The only structural invariant that is enforced is the one the specification
 * makes unambiguous and that binwalk also checks:
 *
 *     16 + header_size + PayloadSize == total_size
 *
 * total_size, header_size and PayloadSize all come straight out of the file
 * and are all bounded against the device before anything is allocated or
 * seeked; header_size in particular is read into memory, so it is capped
 * independently of the device size as well.
 */

#ifndef XXFCLIB_FORMAT_MATTER_OTA_H
#define XXFCLIB_FORMAT_MATTER_OTA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "\x1e\xf1\xee\x1b" read as a little endian u32. */
#define XX_MATTER_OTA_MAGIC UINT32_C(0x1BEEF11E)
/** Magic + total_size + header_size. */
#define XX_MATTER_OTA_PREAMBLE_SIZE 16U
/** Refuse to buffer a TLV header larger than this; real ones are < 1 KiB. */
#define XX_MATTER_OTA_MAX_HEADER_SIZE 65536U
/** Longest SoftwareVersionString / ReleaseNotesURL kept, in bytes. */
#define XX_MATTER_OTA_MAX_STRING 1024U
/** ImageDigestType value for sha-256 in the IANA hash algorithm registry. */
#define XX_MATTER_OTA_DIGEST_SHA256 1U

typedef struct xx_matter_ota xx_matter_ota;
typedef struct xx_matter_ota xx_matter_ota_t;
typedef struct xx_matter_ota XMatterOta;

struct xx_matter_ota {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t total_size;   /**< The header's total_size field. */
    uint32_t header_size;  /**< The header's header_size field. */
    uint64_t payload_size; /**< TLV tag 4. */
    uint64_t vendor_id;    /**< TLV tag 0. */
    uint64_t product_id;   /**< TLV tag 1. */
    uint64_t software_version;      /**< TLV tag 2. */
    uint64_t min_applicable_version; /**< TLV tag 5. */
    uint64_t max_applicable_version; /**< TLV tag 6. */
    uint64_t image_digest_type;      /**< TLV tag 8, IANA registry index. */
    uint32_t image_digest_size;      /**< Bytes actually present in tag 9. */
    uint8_t image_digest[64];        /**< TLV tag 9, truncated to 64 bytes. */
    bool digest_checked; /**< A sha-256 digest was present and recomputed. */
    bool digest_valid;   /**< ...and it matched.  Informational only. */
    int64_t payload_offset; /**< Absolute, or -1. */
    int64_t archive_end;    /**< base_address + total_size, or -1. */
    void *internal;
};

XXFC_API void xx_matter_ota_init(xx_matter_ota *ota, xx_io_device *dev,
                                 int64_t base_address);
XXFC_API xx_matter_ota *xx_matter_ota_create(xx_io_device *dev,
                                             int64_t base_address);
XXFC_API void xx_matter_ota_destroy(xx_matter_ota *ota);
XXFC_API void xx_matter_ota_free(xx_matter_ota *ota);

XXFC_API bool xx_matter_ota_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_matter_ota_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_matter_ota_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_matter_ota_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_matter_ota_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_matter_ota_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_matter_ota_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_matter_ota_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_matter_ota_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_matter_ota_get_total_size(const xx_matter_ota *ota);
XXFC_API uint32_t xx_matter_ota_get_header_size(const xx_matter_ota *ota);
XXFC_API uint64_t xx_matter_ota_get_payload_size(const xx_matter_ota *ota);
XXFC_API uint64_t xx_matter_ota_get_vendor_id(const xx_matter_ota *ota);
XXFC_API uint64_t xx_matter_ota_get_product_id(const xx_matter_ota *ota);
XXFC_API uint64_t xx_matter_ota_get_software_version(const xx_matter_ota *ota);
/** SoftwareVersionString (TLV tag 3), or NULL when absent. */
XXFC_API const char *xx_matter_ota_get_version_string(
    const xx_matter_ota *ota);
/** ReleaseNotesURL (TLV tag 7), or NULL when absent. */
XXFC_API const char *xx_matter_ota_get_release_notes_url(
    const xx_matter_ota *ota);
XXFC_API bool xx_matter_ota_get_digest_valid(const xx_matter_ota *ota);

static inline Abstractformat *xx_matter_ota_to_format(xx_matter_ota *ota) {
    return ota ? &ota->format : NULL;
}
static inline void XMatterOta_init(xx_matter_ota *ota, xx_io_device *dev,
                                   int64_t base_address) {
    xx_matter_ota_init(ota, dev, base_address);
}
static inline xx_matter_ota *XMatterOta_create(xx_io_device *dev,
                                               int64_t base_address) {
    return xx_matter_ota_create(dev, base_address);
}
static inline void XMatterOta_free(xx_matter_ota *ota) {
    xx_matter_ota_free(ota);
}
static inline bool XMatterOta_is_valid(xx_matter_ota *ota, xx_pd_struct *pd) {
    return ota ? xx_format_is_valid(&ota->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MATTER_OTA_H */

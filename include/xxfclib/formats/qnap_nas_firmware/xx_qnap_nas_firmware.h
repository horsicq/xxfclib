/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qnap_nas_firmware.h @brief QNAP NAS (QTS) firmware image reader. */

#ifndef XXFCLIB_FORMAT_QNAP_NAS_FIRMWARE_H
#define XXFCLIB_FORMAT_QNAP_NAS_FIRMWARE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A QNAP NAS firmware image (QTS / QuTS ".img" update file).
 *
 *   0x000000  encrypted_len bytes of a gzip stream (the firmware tarball),
 *             enciphered with QNAP's PC1-derived byte stream cipher keyed by
 *             "QNAPNASVERSION<major>"
 *   ........  the rest of the gzip stream, stored in the clear
 *   size-74   the 74-byte footer, little-endian:
 *     +0x00  char  magic[6]          "icpnas"
 *     +0x06  u32   encrypted_len     length of the enciphered prefix
 *     +0x0A  char  device_id[16]     model, e.g. "TS-X53B"
 *     +0x1A  char  file_version[16]  e.g. "5.2.6"
 *     +0x2A  char  firmware_date[16] e.g. "20240712"
 *     +0x3A  char  revision[16]
 *
 * The cipher only ever uses the first 2*floor(len/2) key bytes, so the
 * trailing major-version digit of the secret never takes part and every
 * NAS image is keyed by "QNAPNASVERSION".  The payload is always gzip with
 * no optional header fields, so the first four stored bytes are the
 * enciphered 1F 8B 08 00: F5 7B 47 03.
 *
 * The reader exposes one member, the deciphered gzip stream ("firmware.tgz",
 * everything in front of the footer).  QNAP networking images, which share
 * the footer but key the cipher by their device_id, are not handled here.
 */
typedef struct xx_qnap_nas_firmware {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t payload_size;   /**< Bytes in front of the footer. */
    uint32_t encrypted_len;  /**< Enciphered prefix of the payload. */
    char device_id[17];
    char file_version[17];
    char firmware_date[17];
    char revision[17];
} xx_qnap_nas_firmware;

typedef xx_qnap_nas_firmware xx_qnap_nas_firmware_t;

XXFC_API void xx_qnap_nas_firmware_init(xx_qnap_nas_firmware *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_qnap_nas_firmware *xx_qnap_nas_firmware_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_qnap_nas_firmware_destroy(xx_qnap_nas_firmware *archive);
XXFC_API void xx_qnap_nas_firmware_free(xx_qnap_nas_firmware *archive);

XXFC_API bool xx_qnap_nas_firmware_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_qnap_nas_firmware_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_qnap_nas_firmware_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_qnap_nas_firmware_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_qnap_nas_firmware_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_qnap_nas_firmware_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qnap_nas_firmware_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qnap_nas_firmware_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qnap_nas_firmware_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_qnap_nas_firmware_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_qnap_nas_firmware_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_qnap_nas_firmware_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_qnap_nas_firmware_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_qnap_nas_firmware_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_QNAP_NAS_FIRMWARE_H */

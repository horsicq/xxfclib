/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Encrypted Apple disk image: the CDSA encryption wrapper hdiutil puts
 * around a disk image created with -encryption.
 *
 * Version 2 ("encrcdsa", Mac OS X 10.3 and later) starts with a big-endian
 * header:
 *   0x00  char[8]  "encrcdsa"
 *   0x08  u32      version (2)
 *   0x0C  u32      block IV length (16)
 *   0x10  u32      block mode (5 = CSSM_ALGMODE_CBC_IV8)
 *   0x14  u32      block algorithm (0x80000001 = CSSM_ALGID_AES)
 *   0x18  u32      key bits (128 or 256)
 *   0x1C  u32      IV-key algorithm (0x5B = CSSM_ALGID_SHA1HMAC)
 *   0x20  u32      IV-key bits (160)
 *   0x24  u8[16]   UUID
 *   0x34  u32      block size (512)
 *   0x38  u64      plaintext data length
 *   0x40  u64      data offset (from the start of the header)
 *   0x48  u32      number of key pointers
 *   0x4C  key pointers, 20 bytes each: u32 type, u64 offset, u64 size
 * A key pointer of type 1 locates a passphrase-wrapped key blob (PBKDF2
 * parameters, then a 3DES- or AES-wrapped image key).
 *
 * Version 1 ("cdsaencr", Mac OS X 10.2) keeps a 0x4FC-byte header at the END
 * of the file, whose last eight bytes are "cdsaencr"; the ciphertext runs
 * from the start of the file up to that trailer.
 *
 * NO DECRYPTION IS ATTEMPTED. The reader identifies the wrapper, publishes
 * what the cleartext header says, lists the ciphertext as one encrypted
 * record, and refuses to unpack it (the LUKS reader's shape).
 */

#ifndef XXFCLIB_FORMAT_ENCRYPTED_APPLE_DISK_IMAGE_H
#define XXFCLIB_FORMAT_ENCRYPTED_APPLE_DISK_IMAGE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_encrypted_apple_disk_image xx_encrypted_apple_disk_image;
typedef struct xx_encrypted_apple_disk_image xx_encrypted_apple_disk_image_t;
typedef struct xx_encrypted_apple_disk_image XEncryptedAppleDiskImage;

struct xx_encrypted_apple_disk_image {
    Abstractformat format;
    uint64_t number_of_records;  /**< Always 1: the encrypted payload. */
    uint64_t payload_offset;     /**< Absolute device offset of ciphertext. */
    uint64_t payload_size;       /**< Ciphertext bytes present in the file. */
    uint64_t data_length;        /**< v2: plaintext length; v1: 0 (unknown). */
    uint32_t version;            /**< 1 or 2. */
    uint32_t key_bits;           /**< v2: AES key size; v1: 0. */
    uint32_t block_size;         /**< v2 block size; v1: 0. */
    uint32_t number_of_keys;     /**< v2 key pointers; v1: 1. */
    uint32_t kdf_iterations;     /**< PBKDF2 iterations of the first
                                      passphrase key, 0 when none. */
    uint8_t uuid[16];            /**< v2 only. */
    bool truncated;              /**< Ciphertext shorter than declared. */
    void *internal;
};

XXFC_API void xx_encrypted_apple_disk_image_init(
    xx_encrypted_apple_disk_image *image, xx_io_device *dev,
    int64_t base_address);
XXFC_API xx_encrypted_apple_disk_image *xx_encrypted_apple_disk_image_create(
    xx_io_device *dev, int64_t base_address);
XXFC_API void xx_encrypted_apple_disk_image_destroy(
    xx_encrypted_apple_disk_image *image);
XXFC_API void xx_encrypted_apple_disk_image_free(
    xx_encrypted_apple_disk_image *image);

XXFC_API bool xx_encrypted_apple_disk_image_check_is_valid(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API bool xx_encrypted_apple_disk_image_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_encrypted_apple_disk_image_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_encrypted_apple_disk_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_encrypted_apple_disk_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_encrypted_apple_disk_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_encrypted_apple_disk_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_encrypted_apple_disk_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_encrypted_apple_disk_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint32_t xx_encrypted_apple_disk_image_get_version(
    const xx_encrypted_apple_disk_image *image);

static inline Abstractformat *xx_encrypted_apple_disk_image_to_format(
    xx_encrypted_apple_disk_image *image) {
    return image ? &image->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_encrypted_apple_disk_image_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_encrypted_apple_disk_image_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_encrypted_apple_disk_image_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_ENCRYPTED_APPLE_DISK_IMAGE_H */

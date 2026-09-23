/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mh01.h @brief D-Link MH01 signed/encrypted firmware reader. */

/*
 * MH01 is the wrapper D-Link puts around recent encrypted firmware images.
 * The layout is not documented by the vendor; the field map used here is
 * binwalk's (src/structures/mh01.rs), which is the only description of it that
 * exists in public source.
 *
 * It is two sixteen-byte headers back to back, both starting with the same
 * "MH01" magic - the first describes the detached signature, the second the
 * encrypted image.  Everything is LITTLE endian.
 *
 *   +0   u32  magic1, "MH01"
 *   +4   u32  signature_offset, measured from the END of the first header
 *   +8   u32  signature_size
 *   +12  u32  unknown1
 *   +16  u32  magic2, "MH01" again
 *   +20  u32  iv_size, the length of the ASCII-hex IV that follows
 *   +24  u32  encrypted_data_size
 *   +28  u32  unknown2
 *   +32       IV, iv_size bytes of ASCII hex, NOT NUL terminated
 *             encrypted image, encrypted_data_size bytes, OpenSSL "Salted__"
 *             signature, signature_size bytes
 *
 * The IV field is the output of `openssl rand -hex 16`: 32 hex digits and a
 * trailing newline, so iv_size is 33 on genuine images and the payload starts
 * at 0x41 (delink's src/mh01.rs hard-codes exactly those offsets; binwalk
 * trim()s the field).  The reader accepts hex digits followed by ASCII
 * whitespace; iv.bin carries the raw field and xx_mh01_get_iv() the digits.
 *
 * signature_offset is relative to offset 16, so the signature sits at
 * 16 + signature_offset and the file ends at 16 + signature_offset +
 * signature_size.  In every sample binwalk was built against the signature
 * follows the encrypted image immediately, but nothing in the header forces
 * that, so this reader derives both regions from their own fields and
 * requires that each one is inside the file and that the signature does not
 * start before the end of the encrypted image.
 *
 * Nothing here is checksummed - the integrity check is the RSA signature over
 * the encrypted image, which cannot be verified without the vendor key - so
 * validation rests on the doubled magic, the self-consistency of the three
 * regions, and the OpenSSL "Salted__" magic at the start of the payload.  That
 * last check is what stops a file that merely begins with "MH01" from being
 * accepted; it is the same check binwalk makes.
 *
 * The payload cannot be decrypted here: the AES key lives in the device's
 * bootloader.  The records are therefore published as-is and the encrypted one
 * is flagged, so a caller gets the ciphertext, the IV and the signature rather
 * than nothing.
 */

#ifndef XXFCLIB_FORMAT_MH01_H
#define XXFCLIB_FORMAT_MH01_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "MH01" read as a little endian u32. */
#define XX_MH01_MAGIC UINT32_C(0x3130484D)
/** One sub-header; the pair in front of the IV is twice this. */
#define XX_MH01_SUBHEADER_SIZE 16U
/** magic1..unknown2, i.e. both sub-headers. */
#define XX_MH01_HEADER_SIZE 32U
/** Longest IV accepted.  A 16-byte AES IV renders as 32 hex characters. */
#define XX_MH01_MAX_IV_SIZE 256U
/** Records published: iv, encrypted, signature. */
#define XX_MH01_MAX_RECORDS 3U

typedef struct xx_mh01 xx_mh01;
typedef struct xx_mh01 xx_mh01_t;
typedef struct xx_mh01 XMh01;

struct xx_mh01 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t signature_offset;   /**< Raw field, relative to offset 16. */
    uint32_t signature_size;
    uint32_t iv_size;
    uint32_t encrypted_data_size;
    uint32_t unknown1;
    uint32_t unknown2;
    int64_t iv_offset;            /**< Absolute, or -1. */
    int64_t encrypted_data_offset;/**< Absolute, or -1. */
    int64_t signature_data_offset;/**< Absolute, or -1. */
    int64_t archive_end;          /**< base_address + total size, or -1. */
    void *internal;
};

XXFC_API void xx_mh01_init(xx_mh01 *mh01, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_mh01 *xx_mh01_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_mh01_destroy(xx_mh01 *mh01);
XXFC_API void xx_mh01_free(xx_mh01 *mh01);

XXFC_API bool xx_mh01_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mh01_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mh01_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_mh01_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mh01_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mh01_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mh01_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mh01_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mh01_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_mh01_get_number_of_records(const xx_mh01 *mh01);
XXFC_API uint32_t xx_mh01_get_iv_size(const xx_mh01 *mh01);
XXFC_API uint32_t xx_mh01_get_encrypted_data_size(const xx_mh01 *mh01);
XXFC_API uint32_t xx_mh01_get_signature_size(const xx_mh01 *mh01);
/** The ASCII-hex IV digits (trailing whitespace removed) as a NUL terminated
 *  string, or NULL before parsing. */
XXFC_API const char *xx_mh01_get_iv(const xx_mh01 *mh01);
XXFC_API int64_t xx_mh01_get_archive_end(const xx_mh01 *mh01);

static inline Abstractformat *xx_mh01_to_format(xx_mh01 *mh01) {
    return mh01 ? &mh01->format : NULL;
}
static inline void XMh01_init(xx_mh01 *mh01, xx_io_device *dev,
                              int64_t base_address) {
    xx_mh01_init(mh01, dev, base_address);
}
static inline xx_mh01 *XMh01_create(xx_io_device *dev, int64_t base_address) {
    return xx_mh01_create(dev, base_address);
}
static inline void XMh01_free(xx_mh01 *mh01) { xx_mh01_free(mh01); }
static inline bool XMh01_is_valid(xx_mh01 *mh01, xx_pd_struct *pd) {
    return mh01 ? xx_format_is_valid(&mh01->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MH01_H */

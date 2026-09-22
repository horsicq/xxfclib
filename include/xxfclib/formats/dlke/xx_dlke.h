/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dlke.h @brief D-Link DLKE signed-and-encrypted firmware wrapper. */

/* DLKE is the wrapper D-Link puts around an encrypted firmware payload on
 * several JBOOT-based boards.  It is not a format of its own: it is TWO
 * back-to-back JBOOT ARM headers (see formats/jboot/xx_jboot.h for the field
 * layout), distinguished only by the ROM ID string the first one carries.
 *
 *   +0                    JBOOT ARM header #1, 80 bytes
 *                           rom_id is "DLK6E8202001" or "DLK6E6110002"
 *                           data_size is the size of the image SIGNATURE
 *   +80                   the signature blob, data_size bytes
 *   +80+sig               JBOOT ARM header #2, 80 bytes
 *                           data_size is the size of the ENCRYPTED payload
 *   +160+sig              the encrypted payload, data_size bytes
 *
 * Both headers are LITTLE endian and are validated in full: the sixteen
 * must-be-zero reserved bytes, lpvs == 1, mbz == 0, header_id == 0x4842 and
 * header_version <= 4.  Two independently valid ARM headers in a row, the
 * first carrying one of two exact ROM ID strings, is a very specific shape;
 * false positives are not a realistic concern.
 *
 * NO DECRYPTION IS ATTEMPTED.  binwalk hands this format to the same
 * `delink` decryptor it uses for encfw; that key material is not reproduced
 * here and no key recovery of any kind is performed.  The payload region is
 * published as one record flagged XX_META_ID_IS_ENCRYPTED and unpacking it
 * FAILS.  The signature blob in front of it is NOT ciphertext - it is an
 * RSA signature over the image - so that record is carved out verbatim on
 * request, which is the only thing a caller could usefully do with it.
 *
 * The two data_size fields are bare 32-bit values straight out of the file
 * and are bounded against the device at PARSE time, before any record exists
 * and long before anything reads a byte of payload.
 */

#ifndef XXFCLIB_FORMAT_DLKE_H
#define XXFCLIB_FORMAT_DLKE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Each DLKE ARM header is 12 bytes of ROM ID plus a 68-byte structure. */
#define XX_DLKE_HEADER_SIZE 80U
#define XX_DLKE_ROM_ID_SIZE 12U

/** The signature blob plus the encrypted payload. */
#define XX_DLKE_RECORD_COUNT 2U

typedef struct xx_dlke xx_dlke;
typedef struct xx_dlke xx_dlke_t;
typedef struct xx_dlke XDlke;

struct xx_dlke {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t signature_size; /**< Header #1's data_size. */
    uint32_t payload_size;   /**< Header #2's data_size, all ciphertext. */
    uint32_t timestamp;      /**< Header #1's timestamp. */
    int64_t signature_offset;
    int64_t payload_offset;
    int64_t archive_end; /**< base_address + the whole container, or -1. */
    char rom_id[XX_DLKE_ROM_ID_SIZE + 1U];
    void *internal;
};

XXFC_API void xx_dlke_init(xx_dlke *dlke, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_dlke *xx_dlke_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dlke_destroy(xx_dlke *dlke);
XXFC_API void xx_dlke_free(xx_dlke *dlke);

XXFC_API bool xx_dlke_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dlke_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dlke_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_dlke_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dlke_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dlke_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dlke_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dlke_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dlke_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_dlke_get_rom_id(const xx_dlke *dlke);
XXFC_API uint32_t xx_dlke_get_signature_size(const xx_dlke *dlke);
XXFC_API uint32_t xx_dlke_get_payload_size(const xx_dlke *dlke);
XXFC_API int64_t xx_dlke_get_archive_end(const xx_dlke *dlke);

static inline Abstractformat *xx_dlke_to_format(xx_dlke *dlke) {
    return dlke ? &dlke->format : NULL;
}
static inline void XDlke_init(xx_dlke *dlke, xx_io_device *dev,
                              int64_t base_address) {
    xx_dlke_init(dlke, dev, base_address);
}
static inline xx_dlke *XDlke_create(xx_io_device *dev, int64_t base_address) {
    return xx_dlke_create(dev, base_address);
}
static inline void XDlke_free(xx_dlke *dlke) { xx_dlke_free(dlke); }
static inline bool XDlke_is_valid(xx_dlke *dlke, xx_pd_struct *pd) {
    return dlke ? xx_format_is_valid(&dlke->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DLKE_H */

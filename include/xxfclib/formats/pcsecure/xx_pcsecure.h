/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pcsecure.h @brief Central Point PCSECURE protected file reader. */

#ifndef XXFCLIB_FORMAT_PCSECURE_H
#define XXFCLIB_FORMAT_PCSECURE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PC Tools PCSECURE protected file: a 68-byte ENCRYPTED header and
 *        one member, DES-ECB and optionally LZW under the encryption.
 *
 * The key is usually not a secret -- PCSECURE ships four built-in product keys
 * and a file saved with a user password carries its key in the header's
 * verifier -- so the codec recovers it from the file itself. A file whose key
 * is genuinely not recoverable is still LISTED, with its encrypted flag set
 * and the payload's own sizes standing in for the member's; extracting it is
 * refused rather than guessed at.
 */
typedef struct xx_pcsecure {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t signature;  /**< "PCT5" / "PCT6" / "PCT7" / "AfoS", LE u32 */
    bool key_found;      /**< a candidate key opened the header */
    bool user_password;  /**< the verifier at header +60 was non-zero */
    bool compressed;     /**< the payload is LZW-compressed under the cipher */
    int32_t rounds;      /**< DES rounds for the payload, 0..16 */
} xx_pcsecure;

typedef xx_pcsecure xx_pcsecure_t;

XXFC_API void xx_pcsecure_init(xx_pcsecure *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_pcsecure *xx_pcsecure_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_pcsecure_destroy(xx_pcsecure *archive);
XXFC_API void xx_pcsecure_free(xx_pcsecure *archive);

XXFC_API bool xx_pcsecure_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_pcsecure_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_pcsecure_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_pcsecure_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pcsecure_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pcsecure_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pcsecure_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pcsecure_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pcsecure_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCSECURE_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sfx_nss.h @brief Norton Secret Stuff 1.0 DOS SFX reader.
 *
 * Lists members and decodes them with the original password-derived Blowfish
 * CBC layer followed by the SFX's LZW and RLE codecs. Set the ordinary
 * XX_META_ID_OPT_PASSWORD parameter to supply a password; without one the
 * format's two built-in keys and an empty password are tried. A wrong
 * password fails before writing a file.
 */
#ifndef XXFCLIB_FORMAT_SFX_NSS_H
#define XXFCLIB_FORMAT_SFX_NSS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sfx_nss {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sfx_nss;
typedef xx_sfx_nss xx_sfx_nss_t;

XXFC_API void xx_sfx_nss_init(xx_sfx_nss *, xx_io_device *, int64_t);
XXFC_API xx_sfx_nss *xx_sfx_nss_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_nss_destroy(xx_sfx_nss *);
XXFC_API void xx_sfx_nss_free(xx_sfx_nss *);
XXFC_API bool xx_sfx_nss_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_nss_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_sfx_nss_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_sfx_nss_get_number_of_archive_records(
    Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_sfx_nss_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_sfx_nss_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_sfx_nss_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_sfx_nss_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_sfx_nss_free_archive_records_reading(
    Abstractformat *, xx_archive_record_state *);

static inline Abstractformat *xx_sfx_nss_to_format(xx_sfx_nss *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif
#endif

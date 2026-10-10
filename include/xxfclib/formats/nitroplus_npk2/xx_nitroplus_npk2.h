/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nitroplus_npk2.h @brief Native Nitroplus/Mware NPK2 reader. */
#ifndef XXFCLIB_FORMAT_NITROPLUS_NPK2_H
#define XXFCLIB_FORMAT_NITROPLUS_NPK2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/** NPK2 encrypted resource archive, with UTF-8 names and stored/raw-Deflate
 * segments. The encrypted index and every segment independently use
 * AES-CBC with the header IV and PKCS#7 padding. Whole-file SHA-256 hashes,
 * all sizes, and every individual segment range are checked on extraction.
 *
 * A correct raw AES key is required for listing/extraction. Supply a 16-,
 * 24-, or 32-byte key with xx_nitroplus_npk2_set_key before parsing, or pass
 * XX_META_ID_OPT_PASSWORD as bytes or a 32/48/64 hexadecimal-character string.
 * Operation-specific password options take priority over format-wide extra
 * parameters, which take priority over the explicit raw-key setter. Format-
 * wide passwords are available to base parsing and validity checks as well
 * as record creation. No game-key database is embedded. Header recognition
 * without a supplied key does not verify encrypted contents.
 *
 * The reader accepts empty files and segmented files. Backslashes in names
 * become '/', '%' becomes "%25", and ASCII-case duplicate names receive
 * "%_<record index>" before their extension. UTF-8 must be well formed.
 * Unsafe paths remain listable but are refused for filesystem extraction.
 * Input cursors are preserved when the device can report them. Filesystem
 * output uses exclusive short sibling stages and is published only after
 * successful hash verification. Extraction options resolve operation values
 * first, then format-wide parameters;
 * existing files require an explicit XX_META_ID_OPT_OVERWRITE option.
 *
 * Bounds: 0xFFFFF files, 64 MiB encrypted index, 260 bytes/name, 16 MiB
 * encrypted data per segment, 256 MiB decoded data per segment. Larger files
 * can contain multiple segments and are decoded without a whole-file buffer.
 */
typedef struct xx_nitroplus_npk2 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t encrypted_index_size;
    uint8_t iv[16];
    uint8_t key[32];
    size_t key_size;
} xx_nitroplus_npk2;
typedef xx_nitroplus_npk2 xx_nitroplus_npk2_t;

XXFC_API void xx_nitroplus_npk2_init(xx_nitroplus_npk2 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_nitroplus_npk2 *xx_nitroplus_npk2_create(xx_io_device *device, int64_t base_address);
/** Copy a raw key, or clear it with NULL and a zero size. Parsed format
 * state is invalidated. Change keys only while no archive iterator exists. */
XXFC_API bool xx_nitroplus_npk2_set_key(xx_nitroplus_npk2 *archive, const uint8_t *key, size_t key_size);
XXFC_API void xx_nitroplus_npk2_destroy(xx_nitroplus_npk2 *archive);
XXFC_API void xx_nitroplus_npk2_free(xx_nitroplus_npk2 *archive);
XXFC_API bool xx_nitroplus_npk2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nitroplus_npk2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nitroplus_npk2_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_nitroplus_npk2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_nitroplus_npk2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nitroplus_npk2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nitroplus_npk2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Verify the current complete file before copying it to destination.
 * NULL verifies only. Destination must differ from the input device. */
XXFC_API bool xx_nitroplus_npk2_unpack_current_archive_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination,
                                                                        xx_pd_struct *pd);
XXFC_API bool xx_nitroplus_npk2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nitroplus_npk2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);
#ifdef __cplusplus
}
#endif
#endif

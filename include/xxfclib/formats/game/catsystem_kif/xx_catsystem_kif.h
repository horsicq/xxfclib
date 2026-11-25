/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_catsystem_kif.h @brief Native CatSystem KIF/INT archive reader. */
#ifndef XXFCLIB_FORMAT_CATSYSTEM_KIF_H
#define XXFCLIB_FORMAT_CATSYSTEM_KIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * CatSystem/Frontwing KIF archives (.int): "KIF\0", u32 count,
 * then count fixed records containing 32- or 64-byte CP932 names and
 * little-endian u32 payload offset/size. The name width is inferred by full
 * directory validation. Both NUL-terminated and full-width names are valid;
 * payloads, including empty members, are stored and copied byte-for-byte.
 * All offsets are relative to the archive's caller-provided base address.
 *
 * CP932 bytes >=128 and '%' are escaped as "%XX". A double-byte character's
 * trail backslash remains escaped; other backslashes become '/'. ASCII
 * case-insensitive duplicate names receive "%_<index>" before an extension.
 * Unsafe paths remain listable but are refused for filesystem extraction.
 *
 * Encrypted archives start with a reserved "__key__.dat" record, omitted
 * from enumeration. They require a title-specific 32-bit main key supplied
 * by the setter or an eight-hex-digit XX_META_ID_OPT_PASSWORD string.
 * Operation options override format-wide parameters, then the setter.
 * Native legacy-seeded MT19937 deciphers filenames and derives the archive
 * Blowfish key; directory words and aligned eight-byte payload blocks use
 * Blowfish with little-endian words. The final 0..7 payload bytes are stored.
 * No game key database or passphrase encoder is embedded. KIF has no payload
 * checksum/authentication: a wrong main key can yield plausible wrong names,
 * so successful parsing does not prove that filename key was correct.
 * The keyless validity predicate only recognizes a bounded encrypted header;
 * encrypted base parsing, listing and extraction fail without a supplied key.
 * Limit: 0xFFFFF records and 64 MiB expanded names. Calls preserve a known
 * input-device cursor. Key changes require creating a new record iterator.
 */
typedef struct xx_catsystem_kif {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t name_size;
    uint64_t index_size;
    uint32_t main_key;
    bool has_main_key;
    bool is_encrypted;
} xx_catsystem_kif;
typedef xx_catsystem_kif xx_catsystem_kif_t;
XXFC_API void xx_catsystem_kif_init(xx_catsystem_kif *, xx_io_device *, int64_t);
XXFC_API xx_catsystem_kif *xx_catsystem_kif_create(xx_io_device *, int64_t);
XXFC_API void xx_catsystem_kif_destroy(xx_catsystem_kif *);
XXFC_API void xx_catsystem_kif_free(xx_catsystem_kif *);
XXFC_API bool xx_catsystem_kif_set_key(xx_catsystem_kif *, uint32_t);
XXFC_API void xx_catsystem_kif_clear_key(xx_catsystem_kif *);
XXFC_API bool xx_catsystem_kif_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_catsystem_kif_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_catsystem_kif_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_catsystem_kif_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_catsystem_kif_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_catsystem_kif_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_catsystem_kif_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** Copy a member to a caller-owned device; NULL verifies it only. */
XXFC_API bool xx_catsystem_kif_unpack_current_archive_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API bool xx_catsystem_kif_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_catsystem_kif_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
#endif

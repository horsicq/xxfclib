/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nitroplus_npa.h @brief Native Nitroplus NPA archive reader. */
#ifndef XXFCLIB_FORMAT_NITROPLUS_NPA_H
#define XXFCLIB_FORMAT_NITROPLUS_NPA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NPA01 archives with a 41-byte header and variable CP932 filename records.
 * Filename obfuscation is undone even for plain archives. Folder records are
 * validated but file enumeration returns leaf members only. Full counts,
 * index consumption, folder IDs and member extents are checked first.
 * Stored and RFC1950 zlib data are extracted byte-exactly; zlib checks exact
 * output/consumption and Adler-32. AUTO treats equal stored/plain lengths as
 * stored, otherwise the global compression flag selects zlib. NPA omits a
 * per-member flag and GARbro uses an external image extension catalog, so
 * equal-size compressed members need explicit ZLIB; title-specific stored
 * images can use explicit STORED. No failed zlib stream falls back to stored.
 *
 * Encrypted archives require a caller-supplied profile, uint32 NameKey and a
 * final 256-byte permutation decrypt table. STANDARD uses header key product,
 * size-dependent file key and indexed subtraction over 4096+raw-name bytes.
 * LAMENTO uses header key sum, name-only key and a 4096-byte constant prefix.
 * All remaining bytes are stored. For composed-table schemes such as Totono,
 * supply the final table; no order/base/title key database is embedded.
 * Stored encryption has no authentication: wrong schemes may produce plausible
 * names/output. Keyless encrypted validity is structural recognition only;
 * listing and extraction require the explicit scheme.
 *
 * Setters are a fallback after operation and format-wide OPT_PASSWORD. Bytes
 * are exactly261: profile(0/1), LE uint32 NameKey, final table256. Narrow text
 * is s:XXXXXXXX:<512 hex table digits> or l:XXXXXXXX:<512 hex table digits>.
 * Iterators snapshot keys, independent of later setter changes. CP932 bytes
 * and '%' use reversible "%XX" escapes; true backslashes become '/', CP932
 * trail backslashes stay escaped. Duplicate and file/directory-prefix leaf
 * aliases use "%_<index>"; unsafe paths remain listable but cannot create files.
 * Known cursors are preserved. Files stage to exclusive short sibling paths
 * before publication; overwrite is explicit. Limits:0xFFFFF total records,
 * 4096 raw name bytes,64MiB index/expanded file names,256MiB packed zlib member.
 * Extraction options resolve operation then format-wide values and cap member
 * output plus dynamic packed/buffer workspace. Plain stored output streams.
 */
typedef enum xx_nitroplus_npa_profile {
    XX_NITROPLUS_NPA_STANDARD = 0,
    XX_NITROPLUS_NPA_LAMENTO = 1
} xx_nitroplus_npa_profile;
typedef enum xx_nitroplus_npa_payload_mode {
    XX_NITROPLUS_NPA_AUTO = 0,
    XX_NITROPLUS_NPA_STORED = 1,
    XX_NITROPLUS_NPA_ZLIB = 2
} xx_nitroplus_npa_payload_mode;
/** Operation/format option with values xx_nitroplus_npa_payload_mode. */
#define XX_NITROPLUS_NPA_OPT_PAYLOAD_MODE XX_META_ID_COMPRESSION_METHOD
typedef struct xx_nitroplus_npa {
    Abstractformat format;
    uint64_t number_of_records, number_of_directories;
    uint32_t key1, key2;
    bool compressed, encrypted, has_scheme;
    xx_nitroplus_npa_profile profile;
    uint32_t name_key;
    uint8_t decrypt_table[256];
} xx_nitroplus_npa;

typedef xx_nitroplus_npa xx_nitroplus_npa_t;
typedef xx_nitroplus_npa XNitroplusNpa;

XXFC_API void xx_nitroplus_npa_init(xx_nitroplus_npa *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_nitroplus_npa *xx_nitroplus_npa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nitroplus_npa_destroy(xx_nitroplus_npa *archive);
XXFC_API void xx_nitroplus_npa_free(xx_nitroplus_npa *archive);
/** Table must be a permutation. Invalid parameters leave the old key intact. */
XXFC_API bool xx_nitroplus_npa_set_scheme(xx_nitroplus_npa *archive, xx_nitroplus_npa_profile profile, uint32_t name_key, const uint8_t decrypt_table[256]);
XXFC_API void xx_nitroplus_npa_clear_scheme(xx_nitroplus_npa *archive);
XXFC_API bool xx_nitroplus_npa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nitroplus_npa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nitroplus_npa_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_nitroplus_npa_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_nitroplus_npa_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nitroplus_npa_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nitroplus_npa_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Copy the current member to a caller-owned device; NULL verifies it only.
 * Destination must differ from the input device. Options limiting member
 * size and extraction buffers are also honored by this direct C API. */
XXFC_API bool xx_nitroplus_npa_unpack_current_archive_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API bool xx_nitroplus_npa_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nitroplus_npa_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif
#endif

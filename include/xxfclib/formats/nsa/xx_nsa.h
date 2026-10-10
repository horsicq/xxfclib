/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nsa.h @brief NScripter NSA archive (arc.nsa) reader. */

#ifndef XXFCLIB_FORMAT_NSA_H
#define XXFCLIB_FORMAT_NSA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An NScripter / ONScripter NSA resource archive (arc.nsa, arc1.nsa,
 * ...; some games name it *.dat).
 *
 * Every field is BIG endian:
 *   0x00  u16 number of entries (1..65535)
 *   0x02  u32 base: offset of the data area from the header start, which is
 *         also the end of the index
 *   0x06  the index, one entry per member:
 *           char[] name, NUL terminated; Shift-JIS bytes with '\\' as the
 *                  directory separator
 *           u8     codec: 0 stored, 1 SPB, 2 LZSS, 4 NBZ
 *           u32    data offset, relative to base
 *           u32    packed size
 *           u32    unpacked size
 *   base  the member data
 *
 * A variant (opened by GARbro) prefixes the header with two zero bytes; the
 * base is then counted from offset 2.  Encrypted (password) archives are not
 * supported: they are indistinguishable from noise without the key.
 *
 * Codecs:
 *   - SPB: BE u16 width, BE u16 height, then three planes (B, G, R) of
 *     predicted, serpentine-ordered samples; the output is a bottom-up
 *     24-bit BMP with a 54-byte header, whatever the unpacked-size field says.
 *   - LZSS: 256-byte ring, zero filled, write cursor 239; MSB-first bits,
 *     1 = 8-bit literal, 0 = 8-bit ring position + 4-bit (length - 2).  The
 *     output is exactly the unpacked-size field long.
 *   - NBZ: BE u32 output size, then a bzip2 stream.  A member whose name ends
 *     in ".nbz" is NBZ whatever its codec byte says (as in GARbro).
 *
 * There is no magic, so the index walk is also the detection probe (run
 * late, after every signature-gated format): the base must lie inside the
 * file and leave room for the index; every name is 1..1024 bytes with no
 * control byte; every codec is one of 0, 1, 2, 4; an LZSS member cannot
 * claim more output than its packed bits can encode; the entries must end
 * exactly at base; member offsets must not decrease (a member may share an
 * earlier one's data); and the furthest member end must be exactly EOF.
 *
 * Member names follow the sar_ns reader's rules: '\\' becomes '/', Shift-JIS
 * double-byte characters and other bytes >= 0x80 become "%XX", '%' becomes
 * "%25", a repeated name (case-insensitive) gets "%_<entry index>" before its
 * extension, and unsafe names (absolute, drive, "..", device names, control
 * or reserved characters) are listed but refused on extraction.
 */
typedef struct xx_nsa {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t data_base; /**< Absolute offset of the data area. */
} xx_nsa;

typedef xx_nsa xx_nsa_t;

XXFC_API void xx_nsa_init(xx_nsa *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_nsa *xx_nsa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nsa_destroy(xx_nsa *archive);
XXFC_API void xx_nsa_free(xx_nsa *archive);

XXFC_API bool xx_nsa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nsa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nsa_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_nsa_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nsa_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nsa_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nsa_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nsa_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nsa_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nsa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nsa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nsa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nsa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nsa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_NSA_H */

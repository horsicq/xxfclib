/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_diet_compression.h @brief DIET-compressed data file reader. */

#ifndef XXFCLIB_FORMAT_DIET_COMPRESSION_H
#define XXFCLIB_FORMAT_DIET_COMPRESSION_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A data file compressed by Teddy Matsumoto's DIET (not the COM/EXE
 * variants, which are executable packers).  Three header layouts exist:
 *
 *   v1.00, 1.00d        B4 4C CD 21 9D 89   u16 LE CRC   stream to EOF
 *   v1.02b..1.20        9D 89 'dlz'          8-byte field block, stream
 *   v1.44, 1.45f        B4 4C CD 21 9D 89 'dlz'  8-byte field block, stream
 *
 * The field block after 'dlz' is
 *   +0  u8   flags (high nibble; 0x80 = a following block, unsupported)
 *            | compressed length bits 16..19 (low nibble)
 *   +1  u16  compressed length bits 0..15
 *   +3  u16  CRC-16/ARC of the compressed stream
 *   +5  u8   original length bits 16..21 (in bits 2..7)
 *   +6  u16  original length bits 0..15
 *
 * The stream is an LZ77 code with an 8 KiB window: 16-bit little-endian
 * control words read least significant bit first (the next word is loaded
 * as soon as the last bit of the current one is taken), interleaved with
 * literal, offset-low and length bytes.  It ends with a stop code.
 *
 * B4 4C CD 21 is "mov ah,4Ch / int 21h": such a file is also a DOS COM
 * program that exits immediately.  The one member is named "payload".
 */
typedef struct xx_diet_compression {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;
    uint32_t version;      /**< 100, 102 or 144 (header layout, see above). */
    uint16_t crc_stored;   /**< CRC-16/ARC stored in the header. */
    uint16_t crc_computed; /**< Over the compressed stream as parsed. */
} xx_diet_compression;

typedef xx_diet_compression xx_diet_compression_t;

XXFC_API void xx_diet_compression_init(xx_diet_compression *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_diet_compression *xx_diet_compression_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_diet_compression_destroy(xx_diet_compression *archive);
XXFC_API void xx_diet_compression_free(xx_diet_compression *archive);

XXFC_API bool xx_diet_compression_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_diet_compression_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_diet_compression_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_diet_compression_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_diet_compression_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_diet_compression_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_diet_compression_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_diet_compression_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_diet_compression_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DIET_COMPRESSION_H */

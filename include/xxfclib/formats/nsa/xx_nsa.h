/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nsa.h @brief NScripter NSA resource archive reader. */

#ifndef XXFCLIB_FORMAT_NSA_H
#define XXFCLIB_FORMAT_NSA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An NScripter / ONScripter ".nsa" archive (arc.nsa, arc1.nsa, ...).
 *
 * All integers are big endian.  There is no magic:
 *
 *   0x00  u16  member count (1..65535)
 *   0x02  u32  base: offset of the data area, from the header start
 *   0x06  count entries, packed back to back up to base:
 *           name   NUL-terminated CP932 path, '\' separated
 *           u8     codec: 0 stored, 1 SPB, 2 LZSS, 4 NBZ
 *           u32    member offset, from base
 *           u32    packed size
 *           u32    unpacked size (meaningful for LZSS)
 *   base  member data
 *
 * A header whose count reads 0 is the layout GARbro also opens with two
 * leading zero bytes: the real header then starts at offset 2 and every
 * offset is relative to it.
 *
 * Codecs: LZSS is Okumura's 8/4-bit variant (256-byte ring cleared to zero,
 * write cursor 239, MSB-first flag bits, 1 = literal); SPB is the engine's
 * planar delta image codec and is emitted as the 24-bit bottom-up BMP the
 * engine builds from it; NBZ is a BE u32 plain size followed by one bzip2
 * stream.  A codec-0 member whose name ends in ".nbz" is NBZ as well.
 *
 * Recognition is structural: the entry table must end exactly at base, every
 * codec must be one of the four, offsets must not decrease, every member
 * must lie inside the data area, an LZSS member cannot claim more than 11
 * times its packed size, and the members together must end exactly at EOF.
 */
typedef struct xx_nsa {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t header_offset; /**< Absolute offset of the count field. */
    int64_t data_offset;   /**< Absolute offset of the data area. */
} xx_nsa;

typedef xx_nsa xx_nsa_t;

/** Codec byte values of an NSA entry. */
#define XX_NSA_CODEC_STORED 0U
#define XX_NSA_CODEC_SPB 1U
#define XX_NSA_CODEC_LZSS 2U
#define XX_NSA_CODEC_NBZ 4U

XXFC_API void xx_nsa_init(xx_nsa *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_nsa *xx_nsa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nsa_destroy(xx_nsa *archive);
XXFC_API void xx_nsa_free(xx_nsa *archive);

XXFC_API bool xx_nsa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nsa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nsa_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_nsa_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nsa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nsa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nsa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nsa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nsa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one NScripter LZSS stream held in memory.
 * @return true when exactly @p output_size bytes were produced before the
 *         input ran out.
 */
XXFC_API bool xx_nsa_lzss_decode_memory(const uint8_t *packed,
                                        size_t packed_size, uint8_t *output,
                                        size_t output_size);

/**
 * @brief Size of the BMP an SPB stream decodes to (54 + stride * height),
 *        from its first four bytes; 0 when the dimensions are refused.
 */
XXFC_API uint64_t xx_nsa_spb_output_size(const uint8_t *packed,
                                         size_t packed_size);

/**
 * @brief Decode one SPB stream held in memory into a 24-bit BMP.
 * @param output      receives xx_nsa_spb_output_size() bytes
 * @return true when all three planes decode inside the input.
 */
XXFC_API bool xx_nsa_spb_decode_memory(const uint8_t *packed,
                                       size_t packed_size, uint8_t *output,
                                       size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NSA_H */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xpak.h @brief XPAK single-member container reader. */

#ifndef XXFCLIB_FORMAT_XPAK_H
#define XXFCLIB_FORMAT_XPAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An XPAK container: one 25-byte header and one packed stream.
 *
 * This is the Software Toolworks "XPAK" wrapper (Star Wars Chess, 1993), not
 * Gentoo's "XPAKPACK" metadata block.
 *
 *   0x00  char[4] "XPAK"
 *   0x04  u32 LE  archive size, counting this header
 *   0x08  char[13] member name, NUL padded to the full field (8.3 plus the
 *                  terminator); bytes after the first NUL are all zero
 *   0x15  u32 LE  unpacked size of the member
 *   0x19  the packed stream, running to the end of the archive
 *
 * The packed stream is a commercial LZ77 + adaptive-Huffman codec whose
 * decoder survives in 3Com's COMSLINK INST.EXE (the same stream format is
 * wrapped there in a ".SAC" container):
 *
 *   +0  u8      encoder hash-size parameter (ignored by the decoder)
 *   +1  u16 LE  0xFEFF
 *   +3  u16 LE  window size          0x200..0x8000
 *   +5  u16 LE  longest match        0x100..0x4000
 *   +7  u16 LE  model rescale limit  0x200..0x8000
 *   +9  u8      0xFF, or a count N of bytes to skip whose last one is 0xFF
 *   then an LSB-first bit stream of adaptive-Huffman symbols: 0..255
 *   literals, 256 end of stream, 257.. match classes that carry their own
 *   distance and length extra bits.
 *
 * Every one of the 64 complete archives of the reference corpus decodes to
 * exactly its declared size with the end symbol in the archive's last byte.
 */
typedef struct xx_xpak {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;
    int64_t declared_size; /**< The u32 at 0x04, before truncation clamping. */
    bool truncated;
} xx_xpak;

typedef xx_xpak xx_xpak_t;

/** Length of the fixed, NUL-padded member name field at 0x08. */
#define XX_XPAK_NAME_FIELD 13

XXFC_API void xx_xpak_init(xx_xpak *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_xpak *xx_xpak_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xpak_destroy(xx_xpak *archive);
XXFC_API void xx_xpak_free(xx_xpak *archive);

XXFC_API bool xx_xpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xpak_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_xpak_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode the member into @p destination (NULL only verifies).
 *
 * Succeeds only when the stream reaches its end symbol inside the archive
 * after producing exactly the declared unpacked size.
 */
XXFC_API bool xx_xpak_unpack_to_device(xx_xpak *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

/**
 * @brief Decode one packed stream (codec header included) held in memory.
 *
 * @param stream       the stream, starting at its 9-byte codec header
 * @param stream_size  bytes available
 * @param output       receives exactly @p output_size bytes
 * @param consumed     optional; bytes of @p stream the decoder used
 * @return true when the end symbol arrives after exactly @p output_size bytes
 */
XXFC_API bool xx_xpak_decode_memory(const uint8_t *stream, size_t stream_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XPAK_H */

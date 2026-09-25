/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_sydex_diskette_image.h
 *  @brief Sydex self-extracting diskette image (the 1995 "WB"/"SXD"
 *         generation: an MZ + NE extractor with the image in its DOS
 *         overlay). */

#ifndef XXFCLIB_FORMAT_SFX_SYDEX_DISKETTE_IMAGE_H
#define XXFCLIB_FORMAT_SFX_SYDEX_DISKETTE_IMAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Sydex self-extracting diskette image.
 *
 * The extractor is an ordinary MZ (+ NE) program.  Its payload starts at the
 * end of the DOS image, (e_cp - 1) * 512 + (e_cblp ? e_cblp : 512), and has
 * two stages.  Nothing in the stub is executed or emulated; only the two
 * MZ size words are read.
 *
 * Stage 1, the extractor's own UI text:
 *
 *   +0  char[2] "WB"
 *   +2  u16 LE  bytes in the last 512-byte page (< 0x200)
 *   +4  u16 LE  number of pages (non-zero)
 *   +6  u16 LE  decoded length of the text (non-zero)
 *   +8  LZHUF stream; its first word is 0xE4D5 or 0xE5CC
 *
 * The two size words give the stage length the way e_cblp / e_cp do for an
 * MZ image, which is where the stage-2 header sits.  If that spot does not
 * hold a valid header, the first 128 KiB of the overlay are scanned for one,
 * as the reference readers do; the header is self-checking either way.
 *
 * Stage 2, the 33-byte image header:
 *
 *   +0x00  char[3] "SXD"
 *   +0x03  u16 LE  bytes per track (128..0x8000)
 *   +0x05  u8      sectors per track
 *   +0x06  u8      heads
 *   +0x07  u8      cylinders of the medium
 *   +0x08  u8      cylinders stored (1..cylinders); heads * this = tracks
 *   +0x0F  i16 LE  non-zero: encrypted image (refused)
 *   +0x13  i16 LE  length of the description text behind the header
 *   +0x15  u16 LE  CRC-16/ARC of that text (not required)
 *   +0x17  char[4] "SNUM" then a u32 serial
 *   +0x1F  u16 LE  CRC-16/ARC (poly 0xA001 reflected, init 0) of +0..+0x1E
 *
 * Then one block per track, cylinder-major:
 *
 *   +0  u16 LE  CRC-16/ARC of the decoded track
 *   +2  i16 LE  > 0: that many bytes of LZHUF; < 0: -n bytes of RLE; 0 is
 *               invalid
 *
 * LZHUF is Yoshizaki's LZSS + adaptive Huffman with the LHA -lh1- parameters
 * (4 KiB window, F = 60, THRESHOLD = 2, MAX_FREQ 0x8000 with rebuild, no end
 * symbol) and the ring preset to 0x00; the model restarts for every track.
 * RLE is CopyQM's: an i16 n > 0 copies n literal bytes, n < 0 repeats the
 * next byte -n times.  Every track must decode to exactly the track size and
 * match its CRC.
 *
 * The output is one member, "image.img": the stored tracks back to back.
 */
typedef struct xx_sfx_sydex_diskette_image {
    Abstractformat format;
    int64_t overlay_offset;  /**< Device offset of "WB". */
    int64_t header_offset;   /**< Device offset of "SXD". */
    int64_t data_offset;     /**< Device offset of the first track block. */
    int64_t payload_end;     /**< Device offset behind the last block. */
    uint64_t image_size;     /**< tracks * bytes per track. */
    uint64_t number_of_records;
    uint32_t track_size;
    uint32_t track_count;
    uint32_t comment_size;
    uint8_t sectors;
    uint8_t heads;
    uint8_t cylinders;
    uint8_t stored_cylinders;
    bool header_scanned;     /**< Found by the scan, not by the WB sizes. */
    bool truncated;          /**< The block walk ran off the file. */
} xx_sfx_sydex_diskette_image;

typedef xx_sfx_sydex_diskette_image xx_sfx_sydex_diskette_image_t;

XXFC_API void xx_sfx_sydex_diskette_image_init(
    xx_sfx_sydex_diskette_image *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_sydex_diskette_image *xx_sfx_sydex_diskette_image_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_sydex_diskette_image_destroy(
    xx_sfx_sydex_diskette_image *archive);
XXFC_API void xx_sfx_sydex_diskette_image_free(
    xx_sfx_sydex_diskette_image *archive);

XXFC_API bool xx_sfx_sydex_diskette_image_check_is_valid(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API bool xx_sfx_sydex_diskette_image_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_sydex_diskette_image_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_sydex_diskette_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_sydex_diskette_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_sydex_diskette_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_sydex_diskette_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_sydex_diskette_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_sydex_diskette_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode the whole image into @p destination (NULL only verifies).
 *
 * Succeeds only when every track decodes to exactly the track size and
 * matches its CRC.
 */
XXFC_API bool xx_sfx_sydex_diskette_image_unpack_to_device(
    xx_sfx_sydex_diskette_image *archive, xx_io_device *destination,
    xx_pd_struct *pd);

/**
 * @brief Decode one LZHUF track block (ring preset to 0x00).
 *
 * @param input        the block, exactly as stored
 * @param input_size   its length
 * @param output       receives exactly @p output_size bytes
 * @param output_size  the track size (at most 0x8000)
 * @return true when @p output_size bytes were produced without reading a
 *         single bit past the block
 */
XXFC_API bool xx_sfx_sydex_diskette_image_lzhuf_decode(const uint8_t *input,
                                                       size_t input_size,
                                                       uint8_t *output,
                                                       size_t output_size);

/** @brief CRC-16/ARC (reflected 0xA001, init 0), as the format uses it. */
XXFC_API uint16_t xx_sfx_sydex_diskette_image_crc16(const uint8_t *data,
                                                    size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_SYDEX_DISKETTE_IMAGE_H */

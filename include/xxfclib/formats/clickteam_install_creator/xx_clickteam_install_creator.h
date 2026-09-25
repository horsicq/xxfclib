/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_clickteam_install_creator.h
 * @brief Clickteam Install Creator installers (1.x and 2.x).
 *
 * An Install Creator setup is a Windows PE stub with its payload appended
 * as the PE overlay.  The payload is a chain of chunks
 *
 *     u16 id, u16 flags, u32 size, size bytes of body
 *
 * ended by a data chunk {0x7F7F, 0, u32 size} followed by a repeated
 * u32 size and then `size` bytes of member data.
 *
 * - Install Creator 2.x opens the overlay with the tag "wwgT)H"
 *   (77 77 67 54 29 48).  A compressed body is {u32 unpacked, u8 method,
 *   stream} with method 0 stored, 1 zlib, 2 bzip2.  Chunk 0x143A is the file
 *   list: u16 count, u16 0, then `count` nodes whose layout depends on the
 *   builder version (20, 24, 30, 35, 40).  A member is stored at its node's
 *   offset inside the data region as {u8 method, stream}.
 * - Install Creator 1.x carries no tag; the first chunk (id 0x1239, 0x1241
 *   or 0x1242, flags 1) starts right at the overlay.  A body with flags 1 is
 *   {u32 unpacked, Clickteam-Deflate stream}.  Chunk 0x1243 is the file list:
 *   u32 count, then entries {u32 size, .., u8 flags @0x0D, u32 unpacked @0x12,
 *   u32 packed @0x16, [0x14 bytes if flags & 6][0x18 bytes if flags & 8],
 *   NUL-terminated relative path}.  Members follow each other in list order
 *   inside the data region, each one a Clickteam-Deflate stream.
 *
 * Clickteam-Deflate is RFC 1951 with a different block header: 3 bits of
 * type then 1 bit "final", type 5 = fixed Huffman, 6 = dynamic, 7 = stored
 * (16-bit length only), and the code-length alphabet order 18,17,16,0..15.
 */

#ifndef XXFCLIB_FORMAT_CLICKTEAM_INSTALL_CREATOR_H
#define XXFCLIB_FORMAT_CLICKTEAM_INSTALL_CREATOR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_clickteam_install_creator {
    Abstractformat format;
    uint32_t generation;      /**< 1 = Install Creator 1.x, 2 = 2.x */
    uint32_t list_version;    /**< 2.x file-list layout (20..40), 0 for 1.x */
    int64_t overlay_offset;   /**< Device offset of the PE overlay */
    int64_t data_offset;      /**< Device offset of the member region */
    int64_t data_size;        /**< Size of the member region */
    uint64_t number_of_records;
    uint64_t unpacked_size;   /**< Sum of the members' unpacked sizes */
} xx_clickteam_install_creator;

typedef xx_clickteam_install_creator xx_clickteam_install_creator_t;

/** Compression method ids published in XX_META_ID_COMPRESSION_METHOD. */
#define XX_CLICKTEAM_METHOD_STORED 0U
#define XX_CLICKTEAM_METHOD_ZLIB 1U
#define XX_CLICKTEAM_METHOD_BZIP2 2U
#define XX_CLICKTEAM_METHOD_CTDEFLATE 3U

XXFC_API void xx_clickteam_install_creator_init(
    xx_clickteam_install_creator *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_clickteam_install_creator *xx_clickteam_install_creator_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_clickteam_install_creator_destroy(
    xx_clickteam_install_creator *archive);
XXFC_API void xx_clickteam_install_creator_free(
    xx_clickteam_install_creator *archive);

XXFC_API bool xx_clickteam_install_creator_check_is_valid(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API bool xx_clickteam_install_creator_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_clickteam_install_creator_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_clickteam_install_creator_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_clickteam_install_creator_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_clickteam_install_creator_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_clickteam_install_creator_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_clickteam_install_creator_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_clickteam_install_creator_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one Clickteam-Deflate stream held in memory.
 *
 * Fails unless the stream ends (final block) within @p stream_size and
 * produces exactly @p output_size bytes.  @p consumed receives the number of
 * input bytes used through the final block.
 */
XXFC_API bool xx_clickteam_install_creator_inflate_memory(
    const uint8_t *stream, size_t stream_size, uint8_t *output,
    size_t output_size, size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CLICKTEAM_INSTALL_CREATOR_H */

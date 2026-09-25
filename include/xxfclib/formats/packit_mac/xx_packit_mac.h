/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_packit_mac.h @brief Macintosh PackIt (.pit) archive reader. */

#ifndef XXFCLIB_FORMAT_PACKIT_MAC_H
#define XXFCLIB_FORMAT_PACKIT_MAC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Macintosh PackIt archive (Harry Chesley's PackIt I/II/III, the
 * predecessor of StuffIt).  Not the DOS "PACKIT by MJP" bundle, which is
 * xx_packit.
 *
 * The archive is a chain of members followed by an end marker; everything
 * is big-endian:
 *
 *   member
 *     0   char[4]  "PMag" stored, "PMa4" Huffman compressed,
 *                  "PMa5" / "PMa6" encrypted (stored / compressed)
 *     4   the member body; for "PMa4" the body is one Huffman stream that
 *         decodes to exactly the bytes a stored body holds, and the next
 *         member starts at the first whole byte after the stream's last bit
 *   end   char[4]  "PEnd"
 *
 *   member body
 *     0   u8       name length, 1..63
 *     1   char[63] name (Mac Roman)
 *     64  char[4]  Mac file type
 *     68  char[4]  Mac creator
 *     72  u16      Finder flags
 *     74  u16      "locked"
 *     76  u32      data fork length
 *     80  u32      resource fork length
 *     84  u32      creation time (seconds since 1904)
 *     88  u32      modification time
 *     92  u16      CRC-16/XMODEM of body bytes 0..91
 *     94  data fork, then resource fork
 *     ..  u16      CRC-16/XMODEM of both forks together
 *
 * Huffman stream (MSB-first bits): a pre-order code tree (bit 1 = leaf
 * followed by its 8-bit value, bit 0 = internal node, left subtree first),
 * then one code per output byte.  The stream has no end code; the output
 * length comes from the decoded body header.
 *
 * Each member publishes its data fork as "<name>" and a non-empty resource
 * fork as "<name>.rsrc"; the data fork is omitted only when it is empty and
 * a resource fork exists.  An encrypted member cannot be walked without the
 * password (its body header is encrypted too), so it ends the walk and is
 * published as one placeholder record that refuses to unpack.
 */
typedef struct xx_packit_mac {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    int64_t archive_size;   /**< Bytes from the base address to the end. */
    bool has_end_marker;    /**< The walk stopped at a "PEnd" marker. */
    bool has_encrypted;     /**< The walk stopped at an encrypted member. */
} xx_packit_mac;

typedef xx_packit_mac xx_packit_mac_t;

XXFC_API void xx_packit_mac_init(xx_packit_mac *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_packit_mac *xx_packit_mac_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_packit_mac_destroy(xx_packit_mac *archive);
XXFC_API void xx_packit_mac_free(xx_packit_mac *archive);

XXFC_API bool xx_packit_mac_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_packit_mac_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_packit_mac_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_packit_mac_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_packit_mac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_packit_mac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_packit_mac_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_packit_mac_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_packit_mac_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one PackIt / StuffIt-method-3 Huffman stream held in memory.
 *
 * @param stream       the stream, starting at its code tree
 * @param stream_size  bytes available
 * @param output       receives exactly @p output_size bytes
 * @param output_size  bytes to produce (the stream carries no end code)
 * @param consumed     optional; whole bytes of @p stream used
 * @return true when the tree is well formed and @p output_size bytes were
 *         decoded without running past @p stream_size
 */
XXFC_API bool xx_packit_mac_huffman_decode_memory(const uint8_t *stream,
                                                  size_t stream_size,
                                                  uint8_t *output,
                                                  size_t output_size,
                                                  size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PACKIT_MAC_H */

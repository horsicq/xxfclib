/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xpk_compressed_file.h @brief Amiga XPK ("XPKF") packed file. */

#ifndef XXFCLIB_FORMAT_XPK_COMPRESSED_FILE_H
#define XXFCLIB_FORMAT_XPK_COMPRESSED_FILE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A file packed through the Amiga xpkmaster.library.  All fields are
 * big-endian.
 *
 *   0x00  'XPKF'
 *   0x04  u32 packed length (file length - 8)
 *   0x08  4CC sub-packer ("MASH", "NONE", "SQSH", "NUKE", ...)
 *   0x0c  u32 unpacked length
 *   0x10  16 bytes: the first 16 bytes of the plaintext
 *   0x20  u8  flags: 1 long chunk headers, 2 password, 4 extended header
 *   0x21  u8  header check: the XOR of the 36 header bytes is 0
 *   0x22  u8  sub-packer version, 0x23 u8 master version
 *   [flag 4: u16 length + that many bytes of extended header]
 *   then chunks, each starting 4-aligned from the file start:
 *     u8 type (0 raw, 1 packed, 0x0f end), u8 header check (XOR of the
 *     chunk header is 0), u16 data check (XOR of the big-endian 16-bit
 *     words of the data padded to 4), then u16 clen + u16 ulen (short
 *     headers, 8 bytes) or u32 clen + u32 ulen (long headers, 12 bytes),
 *     then clen bytes of data.
 *
 * The file is a single member. Sub-packers decoded: NONE, MASH, BZP2,
 * GZIP (raw Deflate or zlib without preset dictionaries), RLEN, CBR0, CBR1,
 * FRLE, DLTA (byte delta, reset per packed chunk), FAST, SQSH, BLZW, SMPL,
 * NUKE, DUKE, LIN1/LIN2/LIN3/LIN4, RDCN, ILZR and ZENO.
 * SQSH validates its BE16 chunk output size and adaptive signed-delta/copy
 * stream; copy and delta runs are clipped at the declared final byte.
 * BLZW validates its9..20-bit dictionary limit, bounded prefix stack, explicit
 * reset/width controls and next-code references (at most5MiB dictionary plus
 * 65540 stack bytes). SMPL validates explicit1..30-bit Huffman prefix codes
 * and applies byte deltas, resetting its table/accumulator per packed chunk.
 * NUKE/DUKE validate shared forward-word/reverse-literal cursor boundaries,
 * four independent bit reservoirs and exact history-copy lengths. DUKE byte
 * deltas reset per chunk; these two methods need no additional workspace.
 * LINO methods validate zero password markers, chunk-local history and linked
 * literal/control cursor boundaries, including partial nibble reservoirs.
 * Their original producer's oversized final match is clipped to the declared
 * output length. LIN2/LIN4 use the explicit terminal literal table; all four
 * require no additional workspace and reset history/reservoirs per chunk.
 * RDCN uses shared BE16 control/literal input, RLE and chunk-local LZ copies;
 * ILZR validates its nonzero BE16 output size and absolute history positions.
 * Both reject output-overrun runs rather than clipping them, need no additional
 * workspace and preserve no history between chunks.
 * ZENO validates the unencrypted 9..20-bit LZW dictionary and bounded 5000-byte
 * expansion stack; its reset code retains the previous token as on the wire.
 * Chunk-local ZENO workspace is at most 5 MiB plus stack and output buffers.
 * Other sub-packers and password-protected streams are listed but not
 * extracted. Packed and newly buffered decoded chunks are capped at 16 MiB.
 */
typedef struct xx_xpk_compressed_file {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< The header's unpacked length. */
    uint32_t method;        /**< Sub-packer 4CC, big-endian value. */
    uint32_t chunk_count;   /**< Data chunks (END not counted). */
    uint8_t flags;
} xx_xpk_compressed_file;

typedef xx_xpk_compressed_file xx_xpk_compressed_file_t;

XXFC_API void xx_xpk_compressed_file_init(xx_xpk_compressed_file *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_xpk_compressed_file *xx_xpk_compressed_file_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xpk_compressed_file_destroy(xx_xpk_compressed_file *archive);
XXFC_API void xx_xpk_compressed_file_free(xx_xpk_compressed_file *archive);

XXFC_API bool xx_xpk_compressed_file_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xpk_compressed_file_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xpk_compressed_file_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_xpk_compressed_file_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xpk_compressed_file_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xpk_compressed_file_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xpk_compressed_file_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xpk_compressed_file_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xpk_compressed_file_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/** Decode the whole stream into @p destination (NULL: verify only). */
XXFC_API bool xx_xpk_compressed_file_unpack_to_device(xx_xpk_compressed_file *archive, xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XPK_COMPRESSED_FILE_H */

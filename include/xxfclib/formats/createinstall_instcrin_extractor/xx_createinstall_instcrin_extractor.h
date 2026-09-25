/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_createinstall_instcrin_extractor.h
 *  @brief CreateInstall "instcrin" self-extractor (pre-Gentee CreateInstall). */

#ifndef XXFCLIB_FORMAT_CREATEINSTALL_INSTCRIN_EXTRACTOR_H
#define XXFCLIB_FORMAT_CREATEINSTALL_INSTCRIN_EXTRACTOR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A setup program made by the pre-Gentee CreateInstall builder.
 *
 * The carrier is a small PE32 stub (image end 0x2000, or 0x5200 for an older
 * stub).  Everything the installer carries sits in the PE overlay:
 *
 *   +0        the installer runtime "instcrin.dll" as one compressed stream.
 *             The runtime is a PE and the coder starts from a fixed state, so
 *             the first eight bytes are always 61 57 41 57 AE 40 60 1B.
 *   ...       ASCII '0' padding up to a multiple of 100 bytes
 *   prelude   0x12 bytes {u32 carrier size, u32, u32 script size, ...}, then
 *             0x0c or 0x0e bytes, then 0x2f bytes, then the builder script
 *             (script size bytes)
 *   records   17-byte headers {u8 type, u8 flag, u32 attributes, u64 FILETIME,
 *             u8 method (0 packed, 1 stored), i16 name length}, the ANSI name,
 *             and for type 1 (file) an i32 size and the member data.  Type 2
 *             enters a directory, type 3 leaves it, type 4 ends the archive.
 *
 * A packed member is a chain of streams of the codec below, each producing at
 * most 4,000,000 bytes; nothing records a compressed length, so the extent of
 * a member is only known after decoding it.
 *
 * Codec: LZ77 over an adaptive Huffman coder of 629 symbols (0..255 literal,
 * 256 end of stream, 257..628 a length 3..64 and one of six distance slots),
 * 32 KiB window, bits read MSB first; every stream restarts the model.
 *
 * Member 0 is the runtime, "instcrin.dll"; then one member per type 1 record,
 * in container order, named by the directory records around it.
 */
typedef struct xx_createinstall_instcrin_extractor {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t container_offset;    /**< PE overlay, relative to base_address. */
    int64_t runtime_size;        /**< Decoded size of instcrin.dll. */
    int64_t runtime_packed_size; /**< Bytes of the runtime stream. */
    int64_t records_offset;      /**< First record header, relative to base. */
    void *walk;                  /**< Member table cached by handle_base_info. */
} xx_createinstall_instcrin_extractor;

typedef xx_createinstall_instcrin_extractor
    xx_createinstall_instcrin_extractor_t;

/** Compressed form of the runtime's MZ header: the overlay's first bytes. */
#define XX_CREATEINSTALL_INSTCRIN_SIGNATURE_SIZE 8

XXFC_API void xx_createinstall_instcrin_extractor_init(
    xx_createinstall_instcrin_extractor *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_createinstall_instcrin_extractor *
xx_createinstall_instcrin_extractor_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_createinstall_instcrin_extractor_destroy(
    xx_createinstall_instcrin_extractor *archive);
XXFC_API void xx_createinstall_instcrin_extractor_free(
    xx_createinstall_instcrin_extractor *archive);

XXFC_API bool xx_createinstall_instcrin_extractor_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_createinstall_instcrin_extractor_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_createinstall_instcrin_extractor_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t
xx_createinstall_instcrin_extractor_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_createinstall_instcrin_extractor_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_createinstall_instcrin_extractor_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_createinstall_instcrin_extractor_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_createinstall_instcrin_extractor_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_createinstall_instcrin_extractor_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode a chain of codec streams held in memory.
 *
 * Streams are decoded one after another, each from a fresh model, until
 * exactly @p output_size bytes have been produced.  An @p output_size of 0
 * consumes nothing.
 *
 * @param packed       the first stream of the chain
 * @param packed_size  bytes available
 * @param output       receives exactly @p output_size bytes
 * @param consumed     optional; bytes of @p packed the chain used
 * @return true when every stream ends with its end symbol and the chain
 *         produces exactly @p output_size bytes
 */
XXFC_API bool xx_createinstall_instcrin_extractor_decode_memory(
    const uint8_t *packed, size_t packed_size, uint8_t *output,
    size_t output_size, size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CREATEINSTALL_INSTCRIN_EXTRACTOR_H */

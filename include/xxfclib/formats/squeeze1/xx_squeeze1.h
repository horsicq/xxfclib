/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_squeeze1.h @brief Classic CP/M "Squeeze" (SQ, 0xFF76) reader. */

#ifndef XXFCLIB_FORMAT_SQUEEZE1_H
#define XXFCLIB_FORMAT_SQUEEZE1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Greenlaw/Huffman "squeeze": a single stored file carrying its original CP/M
 * name, a static signed-child Huffman tree and a 0x90 repeat escape.  There is
 * no stored plaintext length -- the stream ends on the SPEOF symbol and the
 * result is validated against the 16-bit header checksum. */
typedef struct xx_squeeze1 {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t tree_offset;
    uint32_t data_offset;
    uint16_t checksum;
    uint16_t node_count;
    char file_name[256];
} xx_squeeze1;

typedef xx_squeeze1 xx_squeeze1_t;

XXFC_API void xx_squeeze1_init(xx_squeeze1 *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_squeeze1 *xx_squeeze1_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_squeeze1_destroy(xx_squeeze1 *archive);
XXFC_API void xx_squeeze1_free(xx_squeeze1 *archive);

XXFC_API bool xx_squeeze1_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_squeeze1_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_squeeze1_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_squeeze1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_squeeze1_unpack_to_device(xx_squeeze1 *archive,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_squeeze1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_squeeze1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_squeeze1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_squeeze1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_squeeze1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_squeeze1_get_uncompressed_size(
    const xx_squeeze1 *archive);
XXFC_API int64_t xx_squeeze1_get_stream_end(const xx_squeeze1 *archive);
XXFC_API uint16_t xx_squeeze1_get_checksum(const xx_squeeze1 *archive);

static inline Abstractformat *xx_squeeze1_to_format(xx_squeeze1 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQUEEZE1_H */

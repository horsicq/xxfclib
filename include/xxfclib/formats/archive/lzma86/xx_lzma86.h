/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_lzma86.h
 * @brief LZMA86 (.lzma86) reader: one "payload" record.
 *
 * The LZMA SDK's Lzma86 container (Lzma86Enc / Lzma86Dec), which 7-Zip
 * opens as the "lzma86" type:
 *
 *   +0   u8               filter: 0 = none, 1 = x86 BCJ
 *   +1   u8               LZMA properties (pb * 5 + lp) * 9 + lc, below 225
 *   +2   u32 LE           dictionary size
 *   +6   u64 LE           uncompressed size; all ones = unknown
 *   +14  range-coded LZMA data
 *
 * That is an LZMA-alone header with one filter byte in front of it.  With
 * filter 1 the decoded bytes are run through the x86 BCJ decoder (start
 * address 0) before they are written out.  Consecutive streams decode as
 * one payload, each with its own header and a fresh filter state, as in
 * 7-Zip.  format_size is the exact extent of the streams; anything after
 * them is reported as overlay.
 *
 * The container has no signature.  Detection rests on the header grammar
 * 7-Zip applies (filter byte, properties range, 7-Zip's dictionary-size
 * test, size limit, a zero first range-coder byte) plus a trial decode.
 */

#ifndef XXFCLIB_FORMAT_LZMA86_H
#define XXFCLIB_FORMAT_LZMA86_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LZMA86_HEADER_SIZE 14U

typedef struct xx_lzma86 {
    Abstractformat format;
    uint64_t uncompressed_size; /**< All streams, after handle_base_info. */
    int64_t stream_end;         /**< Device offset past the last stream. */
    uint64_t stream_count;
    bool filtered; /**< The first stream uses the x86 filter. */
} xx_lzma86;

typedef xx_lzma86 xx_lzma86_t;

/** Header grammar test on the first bytes of a stream (needs >= 16 bytes:
 *  the 14-byte header and the first two range-coder bytes). */
XXFC_API bool xx_lzma86_has_header(const uint8_t *data, size_t size);

XXFC_API void xx_lzma86_init(xx_lzma86 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_lzma86 *xx_lzma86_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_lzma86_destroy(xx_lzma86 *archive);
XXFC_API void xx_lzma86_free(xx_lzma86 *archive);

XXFC_API bool xx_lzma86_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzma86_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_lzma86_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_lzma86_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

/** Decode the payload (every stream measured by handle_base_info, in
 *  order, filtered where the stream says so) to a caller-provided device. */
XXFC_API bool xx_lzma86_unpack_to_device(xx_lzma86 *archive, xx_io_device *destination, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzma86_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzma86_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzma86_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzma86_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzma86_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_lzma86_to_format(xx_lzma86 *archive)
{
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZMA86_H */

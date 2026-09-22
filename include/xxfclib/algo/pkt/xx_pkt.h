/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_PKT_H
#define XXFCLIB_ALGO_PKT_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Render one FidoNet .PKT message record as plain text.
 *
 * The "codec" is a renderer: a packed message record is a 14-byte binary
 * header followed by five NUL-terminated strings (Date, To, From, Subject,
 * Body).  The rendering is
 *
 *     "Date    = " <date> CR
 *     "To      = " <to>   CR
 *     "From    = " <from> CR
 *     "Subject = " <subj> CR
 *     CR
 *     <body> CR
 *
 * @param input       The whole message record, header included.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output.
 * @param written     Receives the rendered length (0 on failure).
 * @return true only when the record was complete and fully rendered.
 */
XXFC_API bool xx_pkt_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief Measure the rendering of a record without producing it.
 *
 * The .PKT container stores no rendered length -- the reader computes it with
 * the codec's own measuring helper -- so a reader cannot allocate without
 * this.  Shares the single core routine with the decoder, so the measure and
 * the decode can never disagree.
 *
 * @param input       The whole message record, header included.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a record that would render to more than this.
 * @param consumed    Receives the input bytes the record occupies (== the
 *                    record size, since a record must end on its last NUL).
 *                    May be NULL.
 * @param produced    Receives the rendered length. May be NULL.
 * @return true when the record is well formed and fits within @p max_output.
 */
XXFC_API bool xx_pkt_scan_memory(const uint8_t *input, size_t input_size,
                                 size_t max_output, size_t *consumed,
                                 size_t *produced);

#ifdef __cplusplus
}
#endif
#endif

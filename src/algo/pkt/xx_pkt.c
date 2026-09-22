/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FidoNet .PKT message renderer, ported from XArchive/Algos/xpktdecoder.cpp.
 *
 * A packed message record is a 14-byte binary header followed by exactly five
 * NUL-terminated strings.  The reference splits the record with pktSplit(),
 * measures with renderedSize() and renders with decode(); here one core
 * routine does all three, with the output pointer NULL when only the length
 * is wanted, so the measure and the decode can never disagree.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/pkt/xx_pkt.h"

#define PKT_MESSAGE_HEADER_SIZE 14U
#define PKT_FIELD_COUNT 5U
#define PKT_LINE_TERMINATOR 0x0d

/* Field 4 (the body) is rendered without a label; the reference stores that
 * as a NULL entry in its label table and loops PKT_FIELD_COUNT - 1 times. */
static const char *const pkt_labels[PKT_FIELD_COUNT] = {
    "Date    = ", "To      = ", "From    = ", "Subject = ", 0
};

static size_t pkt_label_length(const char *label)
{
    size_t n = 0;
    if (!label) return 0;
    while (label[n]) ++n;
    return n;
}

/* Core: split, then measure and (when output != NULL) render.
 *
 * Deliberate, matching the reference:
 *  - The body keeps its own embedded CRs verbatim and still gets a trailing
 *    CR of its own.
 *  - The blank separator line is a bare CR emitted between the last labelled
 *    header and the body.
 *
 * WHERE THE RECORD ENDS.  The reference's pktSplit() is handed exactly one
 * record and asserts `nPosition == baRecord.size()`.  A .PKT reader cannot
 * do that: nothing before the fifth NUL states a record's length, so it must
 * hand this routine a speculative window covering the REST OF THE PACKET and
 * learn the length from the split.  Requiring the fifth NUL to fall on the
 * last byte of that window therefore only ever held for the final record,
 * and every packet with more than one message was rejected at its first.
 * The end position is now reported through `consumed` instead, and only the
 * rendering path - which is called with the exact extent the measuring path
 * returned - still insists the record fill its input. */
static bool pkt_run(const uint8_t *input, size_t input_size, uint8_t *output,
                    size_t output_size, size_t *produced, size_t *consumed)
{
    size_t starts[PKT_FIELD_COUNT];
    size_t lengths[PKT_FIELD_COUNT];
    size_t position;
    size_t total;
    size_t i;
    size_t j;

    if (produced) *produced = 0;
    if (consumed) *consumed = 0;
    if (!input) return false;
    if (input_size < PKT_MESSAGE_HEADER_SIZE) return false;

    position = PKT_MESSAGE_HEADER_SIZE;
    for (i = 0; i < PKT_FIELD_COUNT; ++i) {
        size_t end = position;
        while ((end < input_size) && (input[end] != 0)) ++end;
        if (end >= input_size) return false; /* unterminated field */
        starts[i] = position;
        lengths[i] = end - position;
        position = end + 1;
    }
    if (output && position != input_size) return false;
    if (consumed) *consumed = position;

    total = 0;
    for (i = 0; i < PKT_FIELD_COUNT; ++i) {
        size_t label = pkt_label_length(pkt_labels[i]);
        if (label > (size_t)-1 - total) return false;
        total += label;
        if (lengths[i] > (size_t)-1 - total) return false;
        total += lengths[i];
        if (total == (size_t)-1) return false;
        total += 1; /* line terminator */
    }
    if (total == (size_t)-1) return false;
    total += 1; /* the blank line between the headers and the body */

    if (produced) *produced = total;
    if (total > output_size) return false;
    if (!output) return true;

    position = 0;
    for (i = 0; i + 1 < PKT_FIELD_COUNT; ++i) {
        const char *label = pkt_labels[i];
        for (j = 0; label[j]; ++j) output[position++] = (uint8_t)label[j];
        if (lengths[i]) {
            xx_rt_memcpy(output + position, input + starts[i], lengths[i]);
            position += lengths[i];
        }
        output[position++] = (uint8_t)PKT_LINE_TERMINATOR;
    }
    output[position++] = (uint8_t)PKT_LINE_TERMINATOR; /* blank line */
    if (lengths[PKT_FIELD_COUNT - 1]) {
        xx_rt_memcpy(output + position, input + starts[PKT_FIELD_COUNT - 1],
                     lengths[PKT_FIELD_COUNT - 1]);
        position += lengths[PKT_FIELD_COUNT - 1];
    }
    output[position++] = (uint8_t)PKT_LINE_TERMINATOR;

    return position == total;
}

bool xx_pkt_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written)
{
    size_t produced = 0;
    bool ok;

    if (written) *written = 0;
    if (!output) return false;
    ok = pkt_run(input, input_size, output, output_size, &produced, 0);
    if (!ok) return false;
    if (written) *written = produced;
    return true;
}

bool xx_pkt_scan_memory(const uint8_t *input, size_t input_size,
                        size_t max_output, size_t *consumed, size_t *produced)
{
    size_t measured = 0;
    size_t used = 0;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;
    if (!pkt_run(input, input_size, 0, max_output, &measured, &used))
        return false;
    /* The record's true end, not the size of the window it was found in. */
    if (consumed) *consumed = used;
    if (produced) *produced = measured;
    return true;
}

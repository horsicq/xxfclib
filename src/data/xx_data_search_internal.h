/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_DATA_SEARCH_INTERNAL_H
#define XX_DATA_SEARCH_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Private immutable-engine search support; the public finders stay stateless.
 * Fusion is appropriate only when the enabled native AVX2 filter already
 * selects the first two bytes. Other backends retain their normal search. */
bool xx_data_can_fuse_literal_prefix(const uint8_t *pat, size_t pattern_size);

/* Return ordered, overlapping two-byte anchors from the first nonempty batch
 * (at most 128 starts). Every anchor through size-2 is eligible. When capacity
 * fills, next is last emitted offset+1, preserving any remaining mask bits on
 * resumption. Otherwise next is past the scanned batch. Zero means exhausted;
 * invalid inputs return zero and set a non-NULL next to size. No allocation. */
size_t xx_data_collect_prefixes_buffer(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], size_t *positions, size_t capacity, size_t *next);

typedef struct XXDataLiteralDualBatch {
    size_t adjacent[128];
    size_t skip[128];
    size_t adjacent_count;
    size_t skip_count;
    size_t next;
} XXDataLiteralDualBatch;

/* Collect both A,B and A,?,B raw starts, with no encoding assumptions. Emit
 * every ordered, overlapping candidate from the first nonempty batch in
 * either channel (at most 128 starts). next is past all scanned starts, even
 * when a caller returns early after inspecting one candidate. Preserve the
 * adjacent anchor at size-2 and the spaced anchor at size-3 independently.
 * False means exhausted or invalid; counts are zero and next is size or
 * size-1 when batch is non-NULL. Arrays beyond each count are unspecified.
 * No allocation, cancellation, or change to the public stateless finders. */
bool xx_data_collect_literal_dual_buffer(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], XXDataLiteralDualBatch *batch);

#endif

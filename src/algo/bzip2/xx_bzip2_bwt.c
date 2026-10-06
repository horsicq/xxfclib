/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_bzip2_internal.h"
#include "xxfclib/rt/xx_rt.h"

/* length is capped at BZ2_MAX_BLOCK_SIZE, so the four-array scratch size
 * computed below cannot overflow size_t. */
_Static_assert((size_t)BZ2_MAX_BLOCK_SIZE <= SIZE_MAX / (4U * sizeof(uint32_t)),
               "bzip2 BWT scratch size overflows size_t");

/* Sort circular suffixes by doubling the compared prefix. Each round is a
 * stable counting sort of integer classes, rather than repeatedly comparing
 * circular byte strings. The shifted-order array becomes the new class array
 * after sorting, keeping scratch storage to four uint32_t arrays.
 *
 * If doubling does not split any class, each class has a unique continuation
 * class at that prefix distance. All later doubling rounds therefore leave
 * the partition unchanged: tied rotations are identical for their complete
 * length. This permits early termination for periodic input, not just when
 * every rotation becomes unique.
 */
bool xx_bzip2_bwt_transform(const uint8_t *src, int length,
                             uint8_t *bwt, int *orig_ptr)
{
    uint32_t histogram[256] = {0};
    uint32_t *workspace;
    uint32_t *order, *classes_at, *temporary, *counts;
    uint32_t n, classes, span, i;
    size_t scratch_size;

    if (!src || !bwt || !orig_ptr || length <= 0 ||
        length > BZ2_MAX_BLOCK_SIZE) return false;
    n = (uint32_t)length;
    scratch_size = (size_t)n * 4U * sizeof(uint32_t);

    for (i = 0; i < n; ++i) histogram[src[i]]++;
    classes = 0;
    for (i = 0; i < 256U; ++i) if (histogram[i] != 0U) classes++;
    if (classes == 1U) {
        xx_rt_memset(bwt, src[0], (size_t)n);
        *orig_ptr = 0;
        return true;
    }

    workspace = (uint32_t *)xx_mem_alloc(scratch_size);
    if (!workspace) return false;
    order = workspace;
    classes_at = order + n;
    temporary = classes_at + n;
    counts = temporary + n;

    /* Initial order uses one byte, with ascending source indices for ties. */
    for (i = 1; i < 256U; ++i) histogram[i] += histogram[i - 1U];
    for (i = n; i > 0U; --i) order[--histogram[src[i - 1U]]] = i - 1U;
    classes = 1U;
    classes_at[order[0]] = 0U;
    for (i = 1; i < n; ++i) {
        if (src[order[i]] != src[order[i - 1U]]) classes++;
        classes_at[order[i]] = classes - 1U;
    }

    for (span = 1U; span < n && classes < n; span <<= 1U) {
        uint32_t total = 0U;
        uint32_t new_classes = 1U;
        uint32_t *swap;

        /* The old order already sorts the second half of each new prefix.
         * Shift its indices to the first half, then stably sort that half. */
        for (i = 0; i < n; ++i) {
            temporary[i] = order[i] >= span ? order[i] - span :
                           order[i] + n - span;
        }
        xx_rt_memset(counts, 0, (size_t)classes * sizeof(uint32_t));
        for (i = 0; i < n; ++i) counts[classes_at[temporary[i]]]++;
        for (i = 0; i < classes; ++i) {
            total += counts[i];
            counts[i] = total;
        }
        for (i = n; i > 0U; --i) {
            uint32_t index = temporary[i - 1U];
            order[--counts[classes_at[index]]] = index;
        }

        /* The shifted indices are no longer needed. Reuse their array for
         * the new classes while the previous class array remains readable. */
        temporary[order[0]] = 0U;
        for (i = 1; i < n; ++i) {
            uint32_t current = order[i];
            uint32_t previous = order[i - 1U];
            uint32_t current_second = current + span;
            uint32_t previous_second = previous + span;
            if (current_second >= n) current_second -= n;
            if (previous_second >= n) previous_second -= n;
            if (classes_at[current] != classes_at[previous] ||
                classes_at[current_second] != classes_at[previous_second])
                new_classes++;
            temporary[current] = new_classes - 1U;
        }
        swap = classes_at;
        classes_at = temporary;
        temporary = swap;
        if (new_classes == classes) break;
        classes = new_classes;
    }

    /* Equivalent periodic rotations have identical preceding bytes. Order
     * their source indices increasingly for a deterministic original row. */
    if (classes < n) {
        uint32_t total = 0U;
        xx_rt_memset(counts, 0, (size_t)classes * sizeof(uint32_t));
        for (i = 0; i < n; ++i) counts[classes_at[i]]++;
        for (i = 0; i < classes; ++i) {
            uint32_t count = counts[i];
            counts[i] = total;
            total += count;
        }
        for (i = 0; i < n; ++i) order[counts[classes_at[i]]++] = i;
    }

    for (i = 0; i < n; ++i) {
        uint32_t index = order[i];
        bwt[i] = src[index != 0U ? index - 1U : n - 1U];
        if (index == 0U) *orig_ptr = (int)i;
    }
    xx_mem_free(workspace);
    return true;
}

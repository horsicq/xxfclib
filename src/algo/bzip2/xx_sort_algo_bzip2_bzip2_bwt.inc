/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Private sorting implementation, included where its format types are visible.
 * Keep the exact ordering and tie behavior of the original implementation.
 */

static void xx_bzip2_bwt_insertion_sort(uint32_t *arr, uint32_t len, const uint32_t *classes_at, uint32_t span, uint32_t n)
{
    uint32_t i;
    for (i = 1U; i < len; ++i) {
        uint32_t tmp = arr[i];
        uint32_t s2 = tmp + span;
        uint32_t key;
        int32_t j;
        if (s2 >= n) s2 -= n;
        key = classes_at[s2];
        j = (int32_t)i - 1;
        while (j >= 0) {
            uint32_t js2 = arr[j] + span;
            if (js2 >= n) js2 -= n;
            if (classes_at[js2] <= key) break;
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = tmp;
    }
}

static void xx_bzip2_bwt_quicksort_group(uint32_t *arr, int32_t left, int32_t right, const uint32_t *classes_at, uint32_t span, uint32_t n)
{
    if (right - left <= 16) {
        xx_bzip2_bwt_insertion_sort(arr + left, (uint32_t)(right - left + 1), classes_at, span, n);
        return;
    }
    int32_t mid = left + (right - left) / 2;
    uint32_t pivot_idx = arr[mid];
    uint32_t ps2 = pivot_idx + span;
    uint32_t pivot_key;
    int32_t i = left, j = right;
    if (ps2 >= n) ps2 -= n;
    pivot_key = classes_at[ps2];

    while (i <= j) {
        uint32_t is2 = arr[i] + span;
        if (is2 >= n) is2 -= n;
        while (classes_at[is2] < pivot_key) {
            i++;
            is2 = arr[i] + span;
            if (is2 >= n) is2 -= n;
        }
        uint32_t js2 = arr[j] + span;
        if (js2 >= n) js2 -= n;
        while (classes_at[js2] > pivot_key) {
            j--;
            js2 = arr[j] + span;
            if (js2 >= n) js2 -= n;
        }
        if (i <= j) {
            uint32_t tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            i++;
            j--;
        }
    }
    if (left < j) xx_bzip2_bwt_quicksort_group(arr, left, j, classes_at, span, n);
    if (i < right) xx_bzip2_bwt_quicksort_group(arr, i, right, classes_at, span, n);
}

bool xx_bzip2_bwt_transform(const uint8_t *src, int length, uint8_t *bwt, int *orig_ptr)
{
    uint32_t histogram[256] = {0};
    uint32_t *workspace;
    uint32_t *order, *classes_at, *temporary, *counts;
    uint32_t n, classes, span, i;
    size_t scratch_size;

    if (!src || !bwt || !orig_ptr || length <= 0 || length > BZ2_MAX_BLOCK_SIZE) return false;
    n = (uint32_t)length;
    scratch_size = (size_t)n * 4U * sizeof(uint32_t);

    for (i = 0; i < n; ++i) histogram[src[i]]++;
    classes = 0;
    for (i = 0; i < 256U; ++i)
        if (histogram[i] != 0U) classes++;
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
        /* Once the majority of elements are in singleton classes, sorting
         * only the tied groups in-place avoids iterating and counting over
         * all n elements and eliminates massive cache misses. */
        if (classes > (n >> 3)) {
            uint32_t grp_start = 0U;
            for (i = 1U; i <= n; ++i) {
                if (i == n || classes_at[order[i]] != classes_at[order[grp_start]]) {
                    if (i - grp_start > 1U) {
                        xx_bzip2_bwt_quicksort_group(order, (int32_t)grp_start, (int32_t)(i - 1U), classes_at, span, n);
                    }
                    grp_start = i;
                }
            }

            uint32_t new_classes = 1U;
            uint32_t prev_c1 = classes_at[order[0]];
            uint32_t s0 = order[0] + span;
            uint32_t prev_c2;
            uint32_t *swap;

            if (s0 >= n) s0 -= n;
            prev_c2 = classes_at[s0];
            temporary[order[0]] = 0U;

            for (i = 1U; i < n; ++i) {
                uint32_t curr = order[i];
                uint32_t c1 = classes_at[curr];
                uint32_t cs = curr + span;
                uint32_t c2;
                if (cs >= n) cs -= n;
                c2 = classes_at[cs];
                if (c1 != prev_c1 || c2 != prev_c2) {
                    new_classes++;
                    prev_c1 = c1;
                    prev_c2 = c2;
                }
                temporary[curr] = new_classes - 1U;
            }
            swap = classes_at;
            classes_at = temporary;
            temporary = swap;
            if (new_classes == classes) break;
            classes = new_classes;
            continue;
        }

        uint32_t total = 0U;
        uint32_t new_classes = 1U;
        uint32_t *swap;
        uint32_t prev_class;

        /* The old order already sorts the second half of each new prefix.
         * Shift its indices to the first half, then stably sort that half. */
        for (i = 0; i < n; ++i) {
            uint32_t idx = order[i];
            temporary[i] = idx - span + (idx < span ? n : 0U);
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
        prev_class = classes_at[order[0]];
        for (i = 1; i < n; ++i) {
            uint32_t current = order[i];
            uint32_t cur_class = classes_at[current];
            if (cur_class != prev_class) {
                new_classes++;
                prev_class = cur_class;
            } else {
                uint32_t previous = order[i - 1U];
                uint32_t current_second = current + span;
                uint32_t previous_second = previous + span;
                if (current_second >= n) current_second -= n;
                if (previous_second >= n) previous_second -= n;
                if (classes_at[current_second] != classes_at[previous_second]) {
                    new_classes++;
                }
            }
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

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/sort/xx_sort_u32.h"

static void xx_sort_u32_sift_down(uint32_t *items, size_t start, size_t end)
{
    size_t root = start;

    for (;;) {
        size_t child = root * 2 + 1;
        uint32_t swap;

        if (child >= end) {
            break;
        }

        if ((child + 1 < end) && (items[child] < items[child + 1])) {
            child++;
        }

        if (items[root] >= items[child]) {
            break;
        }

        swap = items[root];
        items[root] = items[child];
        items[child] = swap;

        root = child;
    }
}

void xx_sort_u32(uint32_t *items, size_t count)
{
    size_t index;

    if ((!items) || (count < 2)) {
        return;
    }

    for (index = count / 2; index > 0; index--) {
        xx_sort_u32_sift_down(items, index - 1, count);
    }

    for (index = count - 1; index > 0; index--) {
        uint32_t swap = items[0];
        items[0] = items[index];
        items[index] = swap;
        xx_sort_u32_sift_down(items, 0, index);
    }
}

size_t xx_sort_u32_unique(uint32_t *items, size_t count)
{
    size_t index;
    size_t out;

    if ((!items) || (count < 2)) {
        return items ? count : 0;
    }

    xx_sort_u32(items, count);

    out = 1;

    for (index = 1; index < count; index++) {
        if (items[index] != items[out - 1]) {
            items[out++] = items[index];
        }
    }

    return out;
}

size_t xx_sorted_u32_lower_bound(const uint32_t *items, size_t count, uint32_t value)
{
    size_t low = 0;
    size_t high = count;

    if (!items) {
        return 0;
    }

    while (low < high) {
        size_t middle = low + (high - low) / 2;

        if (items[middle] < value) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    return low;
}

bool xx_sorted_u32_contains(const uint32_t *items, size_t count, uint32_t value)
{
    size_t position = xx_sorted_u32_lower_bound(items, count, value);

    return items && (position < count) && (items[position] == value);
}

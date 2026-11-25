/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Stable dynamic array list sorting with an allocation-free fallback. */

#include "xxfclib/list/xx_list.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_sort_internal.h"

typedef struct xx_list_sort_context_s {
    char *data;
    size_t elem_size;
    xx_elem_compare_fn compare;
    xx_sort_swap_fn swap;
    xx_sort_copy_fn copy;
    uint8_t *buffer;
} xx_list_sort_context;

static char *xx_list_sort_item(const xx_list_sort_context *context, size_t index)
{
    return context->data + index * context->elem_size;
}

static void xx_list_sort_reverse(const xx_list_sort_context *context, size_t first, size_t last)
{
    while (first < last) {
        --last;
        if (first == last) {
            return;
        }
        context->swap(xx_list_sort_item(context, first), xx_list_sort_item(context, last), context->elem_size);
        ++first;
    }
}

/* Rotate the adjacent groups without changing either group's final order. */
static void xx_list_sort_rotate(const xx_list_sort_context *context, size_t first, size_t middle, size_t last)
{
    if (first == middle || middle == last) {
        return;
    }
    xx_list_sort_reverse(context, first, middle);
    xx_list_sort_reverse(context, middle, last);
    xx_list_sort_reverse(context, first, last);
}

static size_t xx_list_sort_lower_bound(const xx_list_sort_context *context, size_t first, size_t last, const void *pivot)
{
    while (first < last) {
        size_t middle = first + (last - first) / 2;

        if (context->compare(xx_list_sort_item(context, middle), pivot) < 0) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return first;
}

static size_t xx_list_sort_upper_bound(const xx_list_sort_context *context, size_t first, size_t last, const void *pivot)
{
    while (first < last) {
        size_t middle = first + (last - first) / 2;

        if (context->compare(pivot, xx_list_sort_item(context, middle)) < 0) {
            last = middle;
        } else {
            first = middle + 1;
        }
    }
    return first;
}

/* Split the longer sorted group, find its stable cut in the other group,
 * and rotate the middle groups into place. Lower/upper bounds keep equal
 * elements from the left group before equal elements from the right group.
 * The longer group is halved at every split, bounding the recursive stack. */
static void xx_list_sort_merge(const xx_list_sort_context *context, size_t first, size_t middle, size_t last)
{
    while (first < middle && middle < last) {
        size_t first_cut;
        size_t second_cut;
        size_t new_middle;

        if (context->compare(xx_list_sort_item(context, middle - 1), xx_list_sort_item(context, middle)) <= 0) {
            return;
        }

        if (last - first == 2) {
            context->swap(xx_list_sort_item(context, first), xx_list_sort_item(context, middle), context->elem_size);
            return;
        }

        if (middle - first > last - middle) {
            first_cut = first + (middle - first) / 2;
            second_cut = xx_list_sort_lower_bound(context, middle, last, xx_list_sort_item(context, first_cut));
        } else {
            second_cut = middle + (last - middle) / 2;
            first_cut = xx_list_sort_upper_bound(context, first, middle, xx_list_sort_item(context, second_cut));
        }

        new_middle = first_cut + (second_cut - middle);
        xx_list_sort_rotate(context, first_cut, middle, second_cut);
        xx_list_sort_merge(context, first, first_cut, new_middle);
        first = new_middle;
        middle = second_cut;
    }
}

/* Comparator arguments always point into the list. Finish reading both
 * sorted groups before copying the merged output back over their storage. */
static void xx_list_sort_merge_buffered(const xx_list_sort_context *context, size_t first, size_t middle, size_t last)
{
    size_t left = first;
    size_t right = middle;
    uint8_t *output = context->buffer;

    while (left < middle && right < last) {
        size_t selected;

        if (context->compare(xx_list_sort_item(context, left), xx_list_sort_item(context, right)) <= 0) {
            selected = left++;
        } else {
            selected = right++;
        }
        context->copy(output, (const uint8_t *)xx_list_sort_item(context, selected), context->elem_size);
        output += context->elem_size;
    }

    if (left < middle) {
        context->copy(output, (const uint8_t *)xx_list_sort_item(context, left), (middle - left) * context->elem_size);
    } else if (right < last) {
        context->copy(output, (const uint8_t *)xx_list_sort_item(context, right), (last - right) * context->elem_size);
    }
    context->copy((uint8_t *)xx_list_sort_item(context, first), context->buffer, (last - first) * context->elem_size);
}

static void xx_list_sort_range(const xx_list_sort_context *context, size_t first, size_t last)
{
    size_t middle;

    if (last - first <= 11) {
        size_t i;

        for (i = first + 1; i < last; ++i) {
            size_t j = i;

            while (j > first && context->compare(xx_list_sort_item(context, j), xx_list_sort_item(context, j - 1)) < 0) {
                context->swap(xx_list_sort_item(context, j), xx_list_sort_item(context, j - 1), context->elem_size);
                --j;
            }
        }
        return;
    }

    middle = first + (last - first) / 2;
    xx_list_sort_range(context, first, middle);
    xx_list_sort_range(context, middle, last);
    if (context->compare(xx_list_sort_item(context, middle - 1), xx_list_sort_item(context, middle)) <= 0) {
        return;
    }
    if (context->buffer) {
        xx_list_sort_merge_buffered(context, first, middle, last);
    } else {
        xx_list_sort_merge(context, first, middle, last);
    }
}

void xx_list_sort(xx_list_t *list, xx_elem_compare_fn cmp)
{
    xx_list_sort_context context;
    xx_sort_movement movement;

    if (!list || !cmp || list->count < 2 || !list->data || list->elem_size == 0 || list->count > ((size_t)-1) / list->elem_size) {
        return;
    }

    /* Capture both movement functions once. Large lists use one reusable
     * buffer for O(n log n) sorting. Small lists and allocation failures
     * use O(n log squared n) in-place sorting. Both paths retain equal-key
     * input order, use O(log n) stack space, and leave ownership unchanged. */
    movement = xx_sort_select_movement(list->elem_size);
    context.data = (char *)list->data;
    context.elem_size = list->elem_size;
    context.compare = cmp;
    context.swap = movement.swap;
    context.copy = movement.copy;
    context.buffer = list->count >= 64 ? (uint8_t *)xx_mem_alloc(list->count * list->elem_size) : NULL;
    xx_list_sort_range(&context, 0, list->count);
    if (context.buffer) {
        xx_mem_free(context.buffer);
    }
}

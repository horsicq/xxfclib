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

/**
 * @file xx_list.c
 * @brief Implementation of dynamic array list container.
 */

#include "xxfclib/list/xx_list.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_LIST_INITIAL_CAPACITY 8

/* ========================================================================= */
/* --- Internal Helpers                                                  --- */
/* ========================================================================= */

static inline uint8_t* xx_list_elem_ptr(const xx_list_t *list, size_t index) {
    return list->data + (index * list->elem_size);
}

static bool xx_list_grow_if_needed(xx_list_t *list, size_t needed) {
    if (list->capacity >= needed) {
        return true;
    }

    size_t new_cap = list->capacity ? list->capacity : XX_LIST_INITIAL_CAPACITY;
    while (new_cap < needed) {
        new_cap = new_cap + (new_cap >> 1); /* 1.5x amortized growth */
        if (new_cap < needed) {
            new_cap = needed;
        }
    }

    return xx_list_reserve(list, new_cap);
}

/* ========================================================================= */
/* --- Creation, Destruction & Copying                                   --- */
/* ========================================================================= */

xx_list_t* xx_list_create(size_t elem_size, xx_elem_free_fn elem_free) {
    if (elem_size == 0) {
        return NULL;
    }
    xx_list_t *list = (xx_list_t*)xx_mem_alloc(sizeof(xx_list_t));
    if (!list) {
        return NULL;
    }
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
    list->elem_size = elem_size;
    list->elem_free = elem_free;
    return list;
}

bool xx_list_init(xx_list_t *list, size_t elem_size, xx_elem_free_fn elem_free) {
    if (!list || elem_size == 0) {
        return false;
    }
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
    list->elem_size = elem_size;
    list->elem_free = elem_free;
    return true;
}

void xx_list_clear(xx_list_t *list) {
    if (!list) {
        return;
    }
    if (list->elem_free && list->data) {
        for (size_t i = 0; i < list->count; ++i) {
            list->elem_free(xx_list_elem_ptr(list, i));
        }
    }
    list->count = 0;
}

void xx_list_cleanup(xx_list_t *list) {
    if (!list) {
        return;
    }
    xx_list_clear(list);
    if (list->data) {
        xx_mem_free(list->data);
        list->data = NULL;
    }
    list->capacity = 0;
}

void xx_list_destroy(xx_list_t *list) {
    if (!list) {
        return;
    }
    xx_list_cleanup(list);
    xx_mem_free(list);
}

xx_list_t* xx_list_clone(const xx_list_t *list) {
    if (!list) {
        return NULL;
    }
    xx_list_t *copy = xx_list_create(list->elem_size, list->elem_free);
    if (!copy) {
        return NULL;
    }
    if (list->count > 0) {
        if (!xx_list_reserve(copy, list->count)) {
            xx_list_destroy(copy);
            return NULL;
        }
        xx_mem_copy(copy->data, list->data, list->count * list->elem_size);
        copy->count = list->count;
    }
    return copy;
}

/* ========================================================================= */
/* --- Capacity and Size                                                 --- */
/* ========================================================================= */

size_t xx_list_count(const xx_list_t *list) {
    return list ? list->count : 0;
}

size_t xx_list_size(const xx_list_t *list) {
    return list ? list->count : 0;
}

size_t xx_list_length(const xx_list_t *list) {
    return list ? list->count : 0;
}

bool xx_list_is_empty(const xx_list_t *list) {
    return !list || (list->count == 0);
}

size_t xx_list_capacity(const xx_list_t *list) {
    return list ? list->capacity : 0;
}

size_t xx_list_elem_size(const xx_list_t *list) {
    return list ? list->elem_size : 0;
}

bool xx_list_reserve(xx_list_t *list, size_t new_capacity) {
    if (!list) {
        return false;
    }
    if (new_capacity <= list->capacity) {
        return true;
    }
    uint8_t *new_data = (uint8_t*)xx_mem_realloc(list->data, new_capacity * list->elem_size);
    if (!new_data) {
        return false;
    }
    list->data = new_data;
    list->capacity = new_capacity;
    return true;
}

bool xx_list_squeeze(xx_list_t *list) {
    if (!list) {
        return false;
    }
    if (list->capacity == list->count) {
        return true;
    }
    if (list->count == 0) {
        if (list->data) {
            xx_mem_free(list->data);
            list->data = NULL;
        }
        list->capacity = 0;
        return true;
    }
    uint8_t *new_data = (uint8_t*)xx_mem_realloc(list->data, list->count * list->elem_size);
    if (!new_data) {
        return false;
    }
    list->data = new_data;
    list->capacity = list->count;
    return true;
}

/* ========================================================================= */
/* --- Element Access                                                    --- */
/* ========================================================================= */

void* xx_list_at(const xx_list_t *list, size_t index) {
    if (!list || index >= list->count) {
        return NULL;
    }
    return xx_list_elem_ptr(list, index);
}

bool xx_list_get(const xx_list_t *list, size_t index, void *out_element) {
    void *elem = xx_list_at(list, index);
    if (!elem || !out_element) {
        return false;
    }
    xx_mem_copy(out_element, elem, list->elem_size);
    return true;
}

bool xx_list_set(xx_list_t *list, size_t index, const void *element) {
    if (!list || !element || index >= list->count) {
        return false;
    }
    uint8_t *slot = xx_list_elem_ptr(list, index);
    if (list->elem_free) {
        list->elem_free(slot);
    }
    xx_mem_copy(slot, element, list->elem_size);
    return true;
}

void* xx_list_first(const xx_list_t *list) {
    return xx_list_at(list, 0);
}

void* xx_list_last(const xx_list_t *list) {
    if (!list || list->count == 0) {
        return NULL;
    }
    return xx_list_elem_ptr(list, list->count - 1);
}

bool xx_list_value(const xx_list_t *list, size_t index, const void *default_val, void *out_element) {
    if (!out_element) {
        return false;
    }
    if (list && index < list->count) {
        xx_mem_copy(out_element, xx_list_elem_ptr(list, index), list->elem_size);
        return true;
    }
    if (list && default_val) {
        xx_mem_copy(out_element, default_val, list->elem_size);
        return true;
    }
    return false;
}

/* ========================================================================= */
/* --- Insertion / Appending / Prepending                                --- */
/* ========================================================================= */

bool xx_list_append(xx_list_t *list, const void *element) {
    if (!list || !element) {
        return false;
    }
    if (!xx_list_grow_if_needed(list, list->count + 1)) {
        return false;
    }
    xx_mem_copy(xx_list_elem_ptr(list, list->count), element, list->elem_size);
    list->count++;
    return true;
}

bool xx_list_insert(xx_list_t *list, size_t index, const void *element) {
    if (!list || !element || index > list->count) {
        return false;
    }
    if (index == list->count) {
        return xx_list_append(list, element);
    }
    if (!xx_list_grow_if_needed(list, list->count + 1)) {
        return false;
    }

    uint8_t *src = xx_list_elem_ptr(list, index);
    uint8_t *dst = src + list->elem_size;
    size_t bytes_to_shift = (list->count - index) * list->elem_size;
    xx_mem_move(dst, src, bytes_to_shift);

    xx_mem_copy(src, element, list->elem_size);
    list->count++;
    return true;
}

bool xx_list_prepend(xx_list_t *list, const void *element) {
    return xx_list_insert(list, 0, element);
}

bool xx_list_append_list(xx_list_t *list, const xx_list_t *other) {
    if (!list || !other || other->count == 0) {
        return list != NULL;
    }
    if (list->elem_size != other->elem_size) {
        return false;
    }
    if (!xx_list_grow_if_needed(list, list->count + other->count)) {
        return false;
    }
    xx_mem_copy(xx_list_elem_ptr(list, list->count), other->data, other->count * other->elem_size);
    list->count += other->count;
    return true;
}

/* ========================================================================= */
/* --- Removal / Taking                                                  --- */
/* ========================================================================= */

bool xx_list_remove_at(xx_list_t *list, size_t index) {
    if (!list || index >= list->count) {
        return false;
    }
    uint8_t *ptr = xx_list_elem_ptr(list, index);
    if (list->elem_free) {
        list->elem_free(ptr);
    }
    size_t trailing = list->count - index - 1;
    if (trailing > 0) {
        xx_mem_move(ptr, ptr + list->elem_size, trailing * list->elem_size);
    }
    list->count--;
    return true;
}

bool xx_list_remove_first(xx_list_t *list) {
    return xx_list_remove_at(list, 0);
}

bool xx_list_remove_last(xx_list_t *list) {
    if (!list || list->count == 0) {
        return false;
    }
    return xx_list_remove_at(list, list->count - 1);
}

bool xx_list_take_at(xx_list_t *list, size_t index, void *out_element) {
    if (!list || index >= list->count) {
        return false;
    }
    uint8_t *ptr = xx_list_elem_ptr(list, index);
    if (out_element) {
        xx_mem_copy(out_element, ptr, list->elem_size);
    }
    /* Do NOT call elem_free when taking ownership */
    size_t trailing = list->count - index - 1;
    if (trailing > 0) {
        xx_mem_move(ptr, ptr + list->elem_size, trailing * list->elem_size);
    }
    list->count--;
    return true;
}

bool xx_list_take_first(xx_list_t *list, void *out_element) {
    return xx_list_take_at(list, 0, out_element);
}

bool xx_list_take_last(xx_list_t *list, void *out_element) {
    if (!list || list->count == 0) {
        return false;
    }
    return xx_list_take_at(list, list->count - 1, out_element);
}

bool xx_list_remove_one(xx_list_t *list, const void *element, xx_elem_compare_fn cmp) {
    int64_t idx = xx_list_index_of(list, element, 0, cmp);
    if (idx < 0) {
        return false;
    }
    return xx_list_remove_at(list, (size_t)idx);
}

size_t xx_list_remove_all(xx_list_t *list, const void *element, xx_elem_compare_fn cmp) {
    if (!list || !element || list->count == 0) {
        return 0;
    }
    size_t removed = 0;
    size_t i = 0;
    while (i < list->count) {
        uint8_t *cur = xx_list_elem_ptr(list, i);
        bool match = (cmp != NULL) ? (cmp(cur, element) == 0) : (xx_mem_compare(cur, element, list->elem_size) == 0);
        if (match) {
            xx_list_remove_at(list, i);
            removed++;
        } else {
            i++;
        }
    }
    return removed;
}

/* ========================================================================= */
/* --- Searching                                                         --- */
/* ========================================================================= */

int64_t xx_list_index_of(const xx_list_t *list, const void *element, size_t from_index, xx_elem_compare_fn cmp) {
    if (!list || !element || from_index >= list->count) {
        return -1;
    }
    for (size_t i = from_index; i < list->count; ++i) {
        const uint8_t *cur = xx_list_elem_ptr(list, i);
        if (cmp ? (cmp(cur, element) == 0) : (xx_mem_compare(cur, element, list->elem_size) == 0)) {
            return (int64_t)i;
        }
    }
    return -1;
}

int64_t xx_list_last_index_of(const xx_list_t *list, const void *element, size_t from_index, xx_elem_compare_fn cmp) {
    if (!list || !element || list->count == 0) {
        return -1;
    }
    size_t start = (from_index >= list->count) ? list->count - 1 : from_index;
    for (size_t i = start + 1; i > 0; --i) {
        size_t idx = i - 1;
        const uint8_t *cur = xx_list_elem_ptr(list, idx);
        if (cmp ? (cmp(cur, element) == 0) : (xx_mem_compare(cur, element, list->elem_size) == 0)) {
            return (int64_t)idx;
        }
    }
    return -1;
}

bool xx_list_contains(const xx_list_t *list, const void *element, xx_elem_compare_fn cmp) {
    return xx_list_index_of(list, element, 0, cmp) >= 0;
}

size_t xx_list_count_value(const xx_list_t *list, const void *element, xx_elem_compare_fn cmp) {
    if (!list || !element || list->count == 0) {
        return 0;
    }
    size_t matches = 0;
    for (size_t i = 0; i < list->count; ++i) {
        const uint8_t *cur = xx_list_elem_ptr(list, i);
        if (cmp ? (cmp(cur, element) == 0) : (xx_mem_compare(cur, element, list->elem_size) == 0)) {
            matches++;
        }
    }
    return matches;
}

/* ========================================================================= */
/* --- Reordering / Slicing                                              --- */
/* ========================================================================= */

bool xx_list_swap_items_at(xx_list_t *list, size_t i, size_t j) {
    if (!list || i >= list->count || j >= list->count) {
        return false;
    }
    if (i == j) {
        return true;
    }

    uint8_t temp_stack[64];
    uint8_t *temp = temp_stack;
    if (list->elem_size > sizeof(temp_stack)) {
        temp = (uint8_t*)xx_mem_alloc(list->elem_size);
        if (!temp) {
            return false;
        }
    }

    uint8_t *pi = xx_list_elem_ptr(list, i);
    uint8_t *pj = xx_list_elem_ptr(list, j);

    xx_mem_copy(temp, pi, list->elem_size);
    xx_mem_copy(pi, pj, list->elem_size);
    xx_mem_copy(pj, temp, list->elem_size);

    if (temp != temp_stack) {
        xx_mem_free(temp);
    }
    return true;
}

bool xx_list_move(xx_list_t *list, size_t from_index, size_t to_index) {
    if (!list || from_index >= list->count || to_index >= list->count) {
        return false;
    }
    if (from_index == to_index) {
        return true;
    }

    uint8_t temp_stack[64];
    uint8_t *temp = temp_stack;
    if (list->elem_size > sizeof(temp_stack)) {
        temp = (uint8_t*)xx_mem_alloc(list->elem_size);
        if (!temp) {
            return false;
        }
    }

    uint8_t *p_from = xx_list_elem_ptr(list, from_index);
    xx_mem_copy(temp, p_from, list->elem_size);

    if (from_index < to_index) {
        size_t shift_count = to_index - from_index;
        xx_mem_move(p_from, p_from + list->elem_size, shift_count * list->elem_size);
    } else {
        size_t shift_count = from_index - to_index;
        uint8_t *p_to = xx_list_elem_ptr(list, to_index);
        xx_mem_move(p_to + list->elem_size, p_to, shift_count * list->elem_size);
    }

    uint8_t *p_target = xx_list_elem_ptr(list, to_index);
    xx_mem_copy(p_target, temp, list->elem_size);

    if (temp != temp_stack) {
        xx_mem_free(temp);
    }
    return true;
}

bool xx_list_replace(xx_list_t *list, size_t index, const void *element) {
    return xx_list_set(list, index, element);
}

bool xx_list_reverse(xx_list_t *list) {
    if (!list || list->count <= 1) {
        return list != NULL;
    }
    size_t i = 0;
    size_t j = list->count - 1;
    while (i < j) {
        xx_list_swap_items_at(list, i, j);
        i++;
        j--;
    }
    return true;
}

/* Quicksort internal implementation (zero CRT) */
static void xx_list_qsort_internal(xx_list_t *list, int64_t low, int64_t high, xx_elem_compare_fn cmp) {
    if (low >= high) {
        return;
    }

    /* Small partition optimization: insertion sort */
    if (high - low < 10) {
        for (int64_t i = low + 1; i <= high; ++i) {
            int64_t j = i;
            while (j > low && cmp(xx_list_elem_ptr(list, (size_t)j), xx_list_elem_ptr(list, (size_t)(j - 1))) < 0) {
                xx_list_swap_items_at(list, (size_t)j, (size_t)(j - 1));
                j--;
            }
        }
        return;
    }

    /* Pivot: median of low, mid, high */
    int64_t mid = low + (high - low) / 2;
    if (cmp(xx_list_elem_ptr(list, (size_t)mid), xx_list_elem_ptr(list, (size_t)low)) < 0) {
        xx_list_swap_items_at(list, (size_t)low, (size_t)mid);
    }
    if (cmp(xx_list_elem_ptr(list, (size_t)high), xx_list_elem_ptr(list, (size_t)low)) < 0) {
        xx_list_swap_items_at(list, (size_t)low, (size_t)high);
    }
    if (cmp(xx_list_elem_ptr(list, (size_t)high), xx_list_elem_ptr(list, (size_t)mid)) < 0) {
        xx_list_swap_items_at(list, (size_t)mid, (size_t)high);
    }

    xx_list_swap_items_at(list, (size_t)mid, (size_t)high);

    int64_t i = low;
    for (int64_t j = low; j < high; ++j) {
        if (cmp(xx_list_elem_ptr(list, (size_t)j), xx_list_elem_ptr(list, (size_t)high)) < 0) {
            xx_list_swap_items_at(list, (size_t)i, (size_t)j);
            i++;
        }
    }
    xx_list_swap_items_at(list, (size_t)i, (size_t)high);

    if (i > low + 1) {
        xx_list_qsort_internal(list, low, i - 1, cmp);
    }
    if (i + 1 < high) {
        xx_list_qsort_internal(list, i + 1, high, cmp);
    }
}

void xx_list_sort(xx_list_t *list, xx_elem_compare_fn cmp) {
    if (!list || list->count <= 1 || !cmp) {
        return;
    }
    xx_list_qsort_internal(list, 0, (int64_t)list->count - 1, cmp);
}

xx_list_t* xx_list_mid(const xx_list_t *list, size_t pos, int64_t length) {
    if (!list || pos >= list->count) {
        return list ? xx_list_create(list->elem_size, list->elem_free) : NULL;
    }

    size_t available = list->count - pos;
    size_t take = (length < 0 || (size_t)length > available) ? available : (size_t)length;

    xx_list_t *sub = xx_list_create(list->elem_size, list->elem_free);
    if (!sub) {
        return NULL;
    }
    if (take > 0) {
        if (!xx_list_reserve(sub, take)) {
            xx_list_destroy(sub);
            return NULL;
        }
        xx_mem_copy(sub->data, xx_list_elem_ptr(list, pos), take * list->elem_size);
        sub->count = take;
    }
    return sub;
}

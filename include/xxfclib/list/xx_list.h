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
 * @file xx_list.h
 * @brief Dynamic array list container operations.
 */

#ifndef XX_LIST_H
#define XX_LIST_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Element destructor callback function pointer.
 */
typedef void (*xx_elem_free_fn)(void *element);

/**
 * @brief Element comparison callback function pointer.
 * @return < 0 if a < b, 0 if a == b, > 0 if a > b.
 */
typedef int (*xx_elem_compare_fn)(const void *a, const void *b);

/**
 * @brief Generic dynamic list container.
 */
typedef struct xx_list_s {
    uint8_t *data;              /**< Contiguous buffer holding elements */
    size_t count;               /**< Number of active elements in the list */
    size_t capacity;            /**< Total allocated element capacity */
    size_t elem_size;           /**< Byte size of a single element */
    xx_elem_free_fn elem_free;  /**< Optional element destructor */
} xx_list_t;

typedef struct xx_list_s xx_list_s;

/* ========================================================================= */
/* --- Creation, Destruction & Copying                                   --- */
/* ========================================================================= */

/**
 * @brief Create a new empty list.
 * @param elem_size Size of each element in bytes (must be > 0).
 * @param elem_free Optional destructor called on removed elements (can be NULL).
 * @return Pointer to newly allocated xx_list_t, or NULL on error.
 */
XXFC_API xx_list_t* xx_list_create(size_t elem_size, xx_elem_free_fn elem_free);

/**
 * @brief Initialize an existing xx_list_t on stack or embedded struct.
 */
XXFC_API bool xx_list_init(xx_list_t *list, size_t elem_size, xx_elem_free_fn elem_free);

/**
 * @brief Free all elements and the list container itself.
 */
XXFC_API void xx_list_destroy(xx_list_t *list);

/**
 * @brief Free all internal element storage without freeing the container pointer itself.
 */
XXFC_API void xx_list_cleanup(xx_list_t *list);

/**
 * @brief Remove all elements from the list.
 */
XXFC_API void xx_list_clear(xx_list_t *list);

/**
 * @brief Create a shallow clone of the list.
 */
XXFC_API xx_list_t* xx_list_clone(const xx_list_t *list);

/* ========================================================================= */
/* --- Capacity and Size                                                 --- */
/* ========================================================================= */

XXFC_API size_t xx_list_count(const xx_list_t *list);
XXFC_API size_t xx_list_size(const xx_list_t *list);
XXFC_API size_t xx_list_length(const xx_list_t *list);
XXFC_API bool   xx_list_is_empty(const xx_list_t *list);
XXFC_API size_t xx_list_capacity(const xx_list_t *list);
XXFC_API size_t xx_list_elem_size(const xx_list_t *list);

XXFC_API bool   xx_list_reserve(xx_list_t *list, size_t new_capacity);
XXFC_API bool   xx_list_squeeze(xx_list_t *list);

/* ========================================================================= */
/* --- Element Access                                                    --- */
/* ========================================================================= */

/**
 * @brief Get pointer to element at specified index.
 * @return Direct pointer to element in internal buffer, or NULL if out of bounds.
 */
XXFC_API void* xx_list_at(const xx_list_t *list, size_t index);

/**
 * @brief Copy element at index into out_element.
 */
XXFC_API bool xx_list_get(const xx_list_t *list, size_t index, void *out_element);

/**
 * @brief Set/replace element at index. Calls elem_free on old element if set.
 */
XXFC_API bool xx_list_set(xx_list_t *list, size_t index, const void *element);

/**
 * @brief Pointer to first element, or NULL if list is empty.
 */
XXFC_API void* xx_list_first(const xx_list_t *list);

/**
 * @brief Pointer to last element, or NULL if list is empty.
 */
XXFC_API void* xx_list_last(const xx_list_t *list);

/**
 * @brief Get element at index, or copy defaultValue if index is out of bounds.
 */
XXFC_API bool xx_list_value(const xx_list_t *list, size_t index, const void *default_val, void *out_element);

/* ========================================================================= */
/* --- Insertion / Appending / Prepending                                --- */
/* ========================================================================= */

/**
 * @brief Append an element to the end of the list.
 */
XXFC_API bool xx_list_append(xx_list_t *list, const void *element);

/**
 * @brief Prepend an element to the beginning of the list.
 */
XXFC_API bool xx_list_prepend(xx_list_t *list, const void *element);

/**
 * @brief Insert an element at the specified index.
 */
XXFC_API bool xx_list_insert(xx_list_t *list, size_t index, const void *element);

/**
 * @brief Append all elements from other list to the end of list.
 */
XXFC_API bool xx_list_append_list(xx_list_t *list, const xx_list_t *other);

/* ========================================================================= */
/* --- Removal / Taking                                                  --- */
/* ========================================================================= */

/**
 * @brief Remove element at index. Invokes elem_free on it.
 */
XXFC_API bool xx_list_remove_at(xx_list_t *list, size_t index);

/**
 * @brief Remove first element in the list.
 */
XXFC_API bool xx_list_remove_first(xx_list_t *list);

/**
 * @brief Remove last element in the list.
 */
XXFC_API bool xx_list_remove_last(xx_list_t *list);

/**
 * @brief Remove element at index and copy its content into out_element before removal.
 * Does NOT call elem_free on the taken item (transfers ownership to caller).
 */
XXFC_API bool xx_list_take_at(xx_list_t *list, size_t index, void *out_element);

/**
 * @brief Take first element, transferring ownership to caller without calling elem_free.
 */
XXFC_API bool xx_list_take_first(xx_list_t *list, void *out_element);

/**
 * @brief Take last element, transferring ownership to caller without calling elem_free.
 */
XXFC_API bool xx_list_take_last(xx_list_t *list, void *out_element);

/**
 * @brief Remove the first occurrence of element matching value using comparator (or memcmp if NULL).
 * @return true if an element was found and removed.
 */
XXFC_API bool xx_list_remove_one(xx_list_t *list, const void *element, xx_elem_compare_fn cmp);

/**
 * @brief Remove all occurrences of element matching value.
 * @return Number of elements removed.
 */
XXFC_API size_t xx_list_remove_all(xx_list_t *list, const void *element, xx_elem_compare_fn cmp);

/* ========================================================================= */
/* --- Searching                                                         --- */
/* ========================================================================= */

/**
 * @brief Check if list contains matching element.
 */
XXFC_API bool xx_list_contains(const xx_list_t *list, const void *element, xx_elem_compare_fn cmp);

/**
 * @brief Find index of first occurrence of element (searching from from_index forward).
 * @return Element index, or -1 if not found.
 */
XXFC_API int64_t xx_list_index_of(const xx_list_t *list, const void *element, size_t from_index, xx_elem_compare_fn cmp);

/**
 * @brief Find index of last occurrence of element (searching backward from from_index).
 * If from_index >= count, searches backwards from the last element.
 * @return Element index, or -1 if not found.
 */
XXFC_API int64_t xx_list_last_index_of(const xx_list_t *list, const void *element, size_t from_index, xx_elem_compare_fn cmp);

/**
 * @brief Count number of occurrences of element matching value.
 */
XXFC_API size_t xx_list_count_value(const xx_list_t *list, const void *element, xx_elem_compare_fn cmp);

/* ========================================================================= */
/* --- Reordering / Slicing                                              --- */
/* ========================================================================= */

/**
 * @brief Swap items at indices i and j.
 */
XXFC_API bool xx_list_swap_items_at(xx_list_t *list, size_t i, size_t j);

/**
 * @brief Move element from from_index to to_index.
 */
XXFC_API bool xx_list_move(xx_list_t *list, size_t from_index, size_t to_index);

/**
 * @brief Replace element at index with new element.
 */
XXFC_API bool xx_list_replace(xx_list_t *list, size_t index, const void *element);

/**
 * @brief Reverse the order of elements in list.
 */
XXFC_API bool xx_list_reverse(xx_list_t *list);

/**
 * @brief Sort elements in-place using comparator function.
 */
XXFC_API void xx_list_sort(xx_list_t *list, xx_elem_compare_fn cmp);

/**
 * @brief Extract a sub-list starting at pos with length len.
 * If length < 0, copies all elements from pos to end of list.
 */
XXFC_API xx_list_t* xx_list_mid(const xx_list_t *list, size_t pos, int64_t length);

/* ========================================================================= */
/* --- Type-Safe Helper Macros                                           --- */
/* ========================================================================= */

#define xx_list_at_as(Type, list, index) (*((Type*)xx_list_at((list), (index))))
#define xx_list_first_as(Type, list)     (*((Type*)xx_list_first((list))))
#define xx_list_last_as(Type, list)      (*((Type*)xx_list_last((list))))

#define xx_list_append_val(list, Type, val) do { \
    Type _tmp_val = (val); \
    xx_list_append((list), &_tmp_val); \
} while(0)

#define xx_list_prepend_val(list, Type, val) do { \
    Type _tmp_val = (val); \
    xx_list_prepend((list), &_tmp_val); \
} while(0)

#define xx_list_insert_val(list, index, Type, val) do { \
    Type _tmp_val = (val); \
    xx_list_insert((list), (index), &_tmp_val); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* XX_LIST_H */

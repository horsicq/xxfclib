/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/list/xx_u64_set.h"

#include "xxfclib/memory/xx_mem_array.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_U64_SET_INITIAL_CAPACITY 256U
#define XX_U64_SET_KEEP_CAPACITY 4096U

uint64_t xx_hash_mix64(uint64_t value)
{
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33;

    return value;
}

uint64_t xx_hash_bytes64(const void *data, size_t length, uint64_t seed)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t hash = UINT64_C(0xcbf29ce484222325) ^ seed;
    size_t index;

    for (index = 0; index < length; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(0x100000001b3);
    }

    return xx_hash_mix64(hash);
}

void xx_u64_set_init(xx_u64_set_t *set)
{
    if (!set) {
        return;
    }

    set->keys = NULL;
    set->used = NULL;
    set->capacity = 0;
    set->count = 0;
    set->failed = false;
}

void xx_u64_set_free(xx_u64_set_t *set)
{
    if (!set) {
        return;
    }

    xx_mem_free(set->keys);
    xx_mem_free(set->used);
    xx_u64_set_init(set);
}

void xx_u64_set_clear(xx_u64_set_t *set)
{
    if (!set) {
        return;
    }

    if (set->capacity > XX_U64_SET_KEEP_CAPACITY) {
        /* A big burst is rare; do not pay for its table on every small one. */
        xx_u64_set_free(set);
    } else if (set->capacity) {
        xx_mem_set(set->used, 0, set->capacity);
        set->count = 0;
    }

    set->failed = false;
}

bool xx_u64_set_contains(const xx_u64_set_t *set, uint64_t key)
{
    size_t mask;
    size_t position;

    if ((!set) || (!set->capacity)) {
        return false;
    }

    mask = set->capacity - 1;
    position = (size_t)xx_hash_mix64(key) & mask;

    while (set->used[position]) {
        if (set->keys[position] == key) {
            return true;
        }

        position = (position + 1) & mask;
    }

    return false;
}

static bool xx_u64_set_grow(xx_u64_set_t *set)
{
    size_t capacity = set->capacity ? set->capacity * 2 : XX_U64_SET_INITIAL_CAPACITY;
    uint64_t *keys = NULL;
    uint8_t *used = NULL;
    size_t index;

    if (capacity > set->capacity) { /* no wrap */
        keys = (uint64_t *)xx_mem_alloc_array(capacity, sizeof(uint64_t));
        used = (uint8_t *)xx_mem_calloc(capacity, 1);
    }

    if ((!keys) || (!used)) {
        xx_mem_free(keys);
        xx_mem_free(used);
        set->failed = true;
        return false;
    }

    for (index = 0; index < set->capacity; index++) {
        if (set->used[index]) {
            size_t position = (size_t)xx_hash_mix64(set->keys[index]) & (capacity - 1);

            while (used[position]) {
                position = (position + 1) & (capacity - 1);
            }

            used[position] = 1;
            keys[position] = set->keys[index];
        }
    }

    xx_mem_free(set->keys);
    xx_mem_free(set->used);
    set->keys = keys;
    set->used = used;
    set->capacity = capacity;

    return true;
}

bool xx_u64_set_insert(xx_u64_set_t *set, uint64_t key)
{
    size_t mask;
    size_t position;

    if (!set) {
        return false;
    }

    if ((set->count + 1) * 2 > set->capacity) {
        if (!xx_u64_set_grow(set)) {
            return false;
        }
    }

    mask = set->capacity - 1;
    position = (size_t)xx_hash_mix64(key) & mask;

    while (set->used[position]) {
        if (set->keys[position] == key) {
            return false;
        }

        position = (position + 1) & mask;
    }

    set->used[position] = 1;
    set->keys[position] = key;
    set->count++;

    return true;
}

size_t xx_u64_set_count(const xx_u64_set_t *set)
{
    return set ? set->count : 0;
}

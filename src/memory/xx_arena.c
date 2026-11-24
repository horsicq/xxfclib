/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/memory/xx_arena.h"

#include "xxfclib/memory/xx_mem_array.h"
#include "xxfclib/memory/xx_memory.h"

struct xx_arena_chunk_s {
    xx_arena_chunk *next;
    size_t capacity;
    size_t used;
    size_t reserved; /* keeps the payload 8 byte aligned on 32 bit targets too */
    /* payload follows */
};

static char *xx_arena_chunk_data(xx_arena_chunk *chunk)
{
    return (char *)(chunk + 1);
}

void xx_arena_init(xx_arena_t *arena)
{
    xx_arena_init_ex(arena, 0);
}

void xx_arena_init_ex(xx_arena_t *arena, size_t chunk_size)
{
    if (!arena) {
        return;
    }

    arena->head = NULL;
    arena->current = NULL;
    arena->chunk_size = chunk_size;
    arena->failed = false;
}

void xx_arena_free(xx_arena_t *arena)
{
    xx_arena_chunk *chunk;

    if (!arena) {
        return;
    }

    chunk = arena->head;

    while (chunk) {
        xx_arena_chunk *next = chunk->next;
        xx_mem_free(chunk);
        chunk = next;
    }

    xx_arena_init_ex(arena, arena->chunk_size);
}

void xx_arena_reset(xx_arena_t *arena)
{
    if (!arena) {
        return;
    }

    /* Keep the first chunk only, so one big burst does not pin its memory. */
    if (arena->head) {
        xx_arena_chunk *chunk = arena->head->next;

        while (chunk) {
            xx_arena_chunk *next = chunk->next;
            xx_mem_free(chunk);
            chunk = next;
        }

        arena->head->next = NULL;
        arena->head->used = 0;
    }

    arena->current = arena->head;
    arena->failed = false;
}

void *xx_arena_alloc(xx_arena_t *arena, size_t size)
{
    xx_arena_chunk *chunk;
    void *result;

    if (!arena) {
        return NULL;
    }

    chunk = arena->current;

    if (size > ((size_t)-1) - 64 - sizeof(xx_arena_chunk)) {
        arena->failed = true;
        return NULL;
    }

    size = (size + 7) & ~(size_t)7;

    if (size == 0) {
        size = 8;
    }

    if ((!chunk) || ((chunk->capacity - chunk->used) < size)) {
        size_t chunk_size = arena->chunk_size ? arena->chunk_size : XX_ARENA_DEFAULT_CHUNK_SIZE;
        size_t capacity = (size > chunk_size) ? size : chunk_size;
        xx_arena_chunk *fresh = (xx_arena_chunk *)xx_mem_alloc(sizeof(xx_arena_chunk) + capacity);

        if (!fresh) {
            arena->failed = true;
            return NULL;
        }

        fresh->next = NULL;
        fresh->capacity = capacity;
        fresh->used = 0;
        fresh->reserved = 0;

        if (chunk) {
            /* chunk is the last one: current only ever moves forward */
            fresh->next = chunk->next;
            chunk->next = fresh;
        } else {
            arena->head = fresh;
        }

        arena->current = fresh;
        chunk = fresh;
    }

    result = xx_arena_chunk_data(chunk) + chunk->used;
    chunk->used += size;
    xx_mem_set(result, 0, size);

    return result;
}

void *xx_arena_alloc_array(xx_arena_t *arena, size_t count, size_t size)
{
    size_t bytes;

    if (!xx_size_mul(count, size, &bytes)) {
        if (arena) {
            arena->failed = true;
        }

        return NULL;
    }

    return xx_arena_alloc(arena, bytes);
}

char *xx_arena_strndup(xx_arena_t *arena, const char *text, size_t length)
{
    char *result;

    if (length == (size_t)-1) {
        if (arena) {
            arena->failed = true;
        }

        return NULL;
    }

    result = (char *)xx_arena_alloc(arena, length + 1);

    if (result) {
        if (length) {
            xx_mem_copy(result, text, length);
        }

        result[length] = 0;
    }

    return result;
}

char *xx_arena_strdup(xx_arena_t *arena, const char *text)
{
    size_t length = 0;

    if (!text) {
        text = "";
    }

    while (text[length]) {
        length++;
    }

    return xx_arena_strndup(arena, text, length);
}

bool xx_arena_ok(const xx_arena_t *arena)
{
    return arena && !arena->failed;
}

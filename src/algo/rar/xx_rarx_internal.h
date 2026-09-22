/* Copyright (c) 2026 hors<horsicq@gmail.com>. MIT license. */
#ifndef XX_RARX_INTERNAL_H
#define XX_RARX_INTERNAL_H

#include "xxfclib/algo/rar/xx_rarx.h"
#include "xxfclib/memory/xx_memory.h"

/* Decoder-owned storage can contain decrypted data even when it only holds
 * compressed bytes or adaptive model state.  Do not release it through realloc,
 * which can discard the original allocation without first clearing it. */
static inline void xx_rarx_clear_free(void *data, size_t size) {
    xx_mem_zero(data, size);
    xx_mem_free(data);
}

static inline void *xx_rarx_clear_resize(void *data, size_t old_size,
                                        size_t new_size) {
    void *replacement = xx_mem_alloc(new_size);
    if (!replacement) return NULL;
    if (data) xx_mem_copy(replacement, data,
                          old_size < new_size ? old_size : new_size);
    xx_rarx_clear_free(data, old_size);
    return replacement;
}

typedef struct xx_rarx15_state xx_rarx15_state;
typedef struct xx_rarx20_state xx_rarx20_state;
typedef struct xx_rarx29_state xx_rarx29_state;
typedef struct xx_rarx50_state xx_rarx50_state;

xx_rarx15_state *xx_rarx15_create(void);
void xx_rarx15_destroy(xx_rarx15_state *state);
void xx_rarx15_reset(xx_rarx15_state *state);
xx_rarx_status_t xx_rarx15_decode(xx_rarx15_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used, xx_pd_struct *progress);

xx_rarx20_state *xx_rarx20_create(void);
void xx_rarx20_destroy(xx_rarx20_state *state);
void xx_rarx20_reset(xx_rarx20_state *state);
xx_rarx_status_t xx_rarx20_decode(xx_rarx20_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used, xx_pd_struct *progress);

xx_rarx29_state *xx_rarx29_create(void);
void xx_rarx29_destroy(xx_rarx29_state *state);
void xx_rarx29_reset(xx_rarx29_state *state);
xx_rarx_status_t xx_rarx29_decode(xx_rarx29_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, size_t allocation_limit,
                                  bool solid,
                                  size_t *source_used, xx_pd_struct *progress);

xx_rarx50_state *xx_rarx50_create(void);
void xx_rarx50_destroy(xx_rarx50_state *state);
void xx_rarx50_reset(xx_rarx50_state *state);
xx_rarx_status_t xx_rarx50_decode(xx_rarx50_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used, xx_pd_struct *progress);

#endif /* XX_RARX_INTERNAL_H */

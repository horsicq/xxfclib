/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SPIS_KEY_INTERNAL_H
#define XX_SPIS_KEY_INTERNAL_H
#include "xxfclib/xxfc_defs.h"
/* Distinct authenticated candidate keys, not syntactic decoder successes.
 * Saturation prevents wrap; a second distinct key permanently disqualifies
 * the entire recovery. The caller must still finish its bounded scan. */
typedef struct xx_spis_key_selection_s {
    uint32_t key;
    unsigned matches;
} xx_spis_key_selection;
static inline void xx_spis_key_selection_add(xx_spis_key_selection *s, uint32_t key)
{
    if (s->matches == 0U) {
        s->key = key;
        s->matches = 1U;
    } else if (key != s->key) s->matches = 2U;
}
static inline bool xx_spis_key_selection_unique(const xx_spis_key_selection *s)
{
    return s->matches == 1U;
}
#endif

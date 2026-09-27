/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef DIE_ENGINE_SEARCH_H
#define DIE_ENGINE_SEARCH_H
#include "xxfclib/formats/xx_data_signature.h"
#define DIE_SIGNATURE_BATCH_MAX 128u
#define DIE_SIGNATURE_BATCH_TEXT_MAX 65536u
/* Same parser, matcher, clamping and first-offset semantics as find_text.
 * false means invalid batch limits or allocation failure; results are -1. */
bool die_find_signatures(const void *data, size_t data_size,
    int64_t offset, int64_t length, const char *const *patterns,
    size_t count, const xx_data_sig_context *context, int64_t *results);
#endif

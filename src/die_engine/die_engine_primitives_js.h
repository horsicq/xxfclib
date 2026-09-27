/* SPDX-License-Identifier: MIT */
#ifndef DIE_ENGINE_PRIMITIVES_JS_H
#define DIE_ENGINE_PRIMITIVES_JS_H
#include "xxfclib/js/xx_js.h"
#include <stdint.h>
#include <stddef.h>
int die_js_safe_unsigned(JSVal value, uint64_t maximum, uint64_t *result);
JSVal die_js_find_any_bytes(JSCtx *ctx, const void *data, size_t data_size, int argc, JSVal *argv);
JSVal die_js_find_byte_relation_candidates(JSCtx *ctx, const void *data, size_t data_size, int argc, JSVal *argv);
#endif

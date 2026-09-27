/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "die_engine_primitives_js.h"
#include "die_engine_primitives.h"
#include "../js/xx_js_internal.h"
#include "xxfclib/memory/xx_memory.h"

int die_js_safe_unsigned(JSVal value, uint64_t maximum, uint64_t *result)
{
    if (value.tag != JT_NUM || !(value.u.n >= 0 && value.u.n <= (double)maximum)) return 0;
    *result = (uint64_t)value.u.n;
    return (double)*result == value.u.n;
}
static int sequence(JSVal value)
{
    return value.tag == JT_OBJ && value.u.o
        && (value.u.o->cls == JCLASS_ARRAY || value.u.o->cls == JCLASS_UINT32_ARRAY);
}
JSVal die_js_find_any_bytes(JSCtx *ctx, const void *data, size_t data_size, int argc, JSVal *argv)
{
    uint64_t offset, length;
    int64_t count, i;
    size_t used = 0;
    uint8_t *bytes;
    DieBytePattern patterns[DIE_BYTES_PATTERN_MAX];
    DieBytesMatch match;
    JSVal result = js_null();
    if (argc < 3 || !die_js_safe_unsigned(argv[0], UINT64_C(9007199254740991), &offset)
        || !die_js_safe_unsigned(argv[1], UINT64_C(9007199254740991), &length)
        || !sequence(argv[2])) return result;
    count = js_array_length(ctx, argv[2]);
    if (count < 0 || count > DIE_BYTES_PATTERN_MAX) return result;
    bytes = count ? xx_mem_alloc(DIE_BYTES_PATTERN_BUDGET) : NULL;
    if (count && !bytes) return result;
    for (i = 0; i < count; ++i) {
        JSVal pattern = js_get_index(ctx, argv[2], i);
        int64_t size = sequence(pattern) ? js_array_length(ctx, pattern) : -1;
        int64_t j;
        if (size <= 0 || (uint64_t)size > DIE_BYTES_PATTERN_BUDGET - used) {
            js_release(ctx, pattern);
            goto done;
        }
        patterns[i].data = bytes + used;
        patterns[i].size = (size_t)size;
        for (j = 0; j < size; ++j) {
            JSVal byte = js_get_index(ctx, pattern, j);
            uint64_t value;
            int valid = die_js_safe_unsigned(byte, 255, &value);
            js_release(ctx, byte);
            if (!valid) { js_release(ctx, pattern); goto done; }
            bytes[used++] = (uint8_t)value;
        }
        js_release(ctx, pattern);
    }
    if (!die_find_any_bytes(data, data_size, offset, length, patterns, (size_t)count, &match)
        || !match.found || match.offset > UINT64_C(9007199254740991)) goto done;
    result = js_new_object(ctx);
    js_set(ctx, result, "offset", js_num((double)match.offset));
    js_set(ctx, result, "patternIndex", js_num((double)match.pattern_index));
done:
    xx_mem_free(bytes);
    return result;
}
JSVal die_js_find_byte_relation_candidates(JSCtx *ctx, const void *data, size_t data_size, int argc, JSVal *argv)
{
    uint64_t offset, length, tail;
    int64_t count, i;
    size_t used = 0, value_count = 0;
    uint32_t *values = NULL;
    DieByteRelationGroup groups[DIE_RELATION_GROUP_MAX];
    DieByteRelationPair pairs[DIE_RELATION_PAIR_MAX];
    JSVal result = js_null();
    if (argc < 4 || !die_js_safe_unsigned(argv[0], UINT64_C(9007199254740991), &offset)
        || !die_js_safe_unsigned(argv[1], DIE_RELATION_RANGE_MAX, &length)
        || !die_js_safe_unsigned(argv[3], UINT32_MAX, &tail) || !sequence(argv[2])) return result;
    count = js_array_length(ctx, argv[2]);
    if (count < 0 || count > DIE_RELATION_GROUP_MAX) return result;
    for (i = 0; i < count; ++i) {
        JSVal group = js_get_index(ctx, argv[2], i);
        int64_t size = sequence(group) ? js_array_length(ctx, group) : -1, j;
        if (size <= 0 || (uint64_t)size > DIE_RELATION_PAIR_MAX - used) {
            js_release(ctx, group);
            return result;
        }
        groups[i].pairs = pairs + used;
        groups[i].count = (size_t)size;
        for (j = 0; j < size; ++j) {
            JSVal pair = js_get_index(ctx, group, j), a = js_undefined(), b = js_undefined();
            uint64_t av, bv;
            int valid = sequence(pair) && js_array_length(ctx, pair) == 2;
            if (valid) {
                a = js_get_index(ctx, pair, 0); b = js_get_index(ctx, pair, 1);
                valid = die_js_safe_unsigned(a, tail, &av) && die_js_safe_unsigned(b, tail, &bv);
            }
            js_release(ctx, a); js_release(ctx, b); js_release(ctx, pair);
            if (!valid) { js_release(ctx, group); return result; }
            pairs[used].a = (uint32_t)av; pairs[used++].b = (uint32_t)bv;
        }
        js_release(ctx, group);
    }
    if (die_find_byte_relation_candidates(data, data_size, offset, length, groups,
            (size_t)count, tail, &values, &value_count))
        result = js_new_uint32_array(ctx, values, value_count);
    die_byte_relation_results_free(values);
    return result;
}

/* SPDX-License-Identifier: MIT. Checked primitives for additional containers. */
#ifndef XX_NUMERIC_VALUES_H
#define XX_NUMERIC_VALUES_H
#include "xx_scientific_numbers.h"
static bool numeric_finite64(uint64_t b)
{
    return ((b >> 52) & 2047U) != 2047U;
}
static XXFC_MAYBE_UNUSED bool numeric_positive64(uint64_t b)
{
    return !(b >> 63) && (b & UINT64_C(0x7fffffffffffffff)) && numeric_finite64(b);
}
static XXFC_MAYBE_UNUSED uint64_t numeric_ordered64(uint64_t x)
{
    if (!(x & UINT64_C(0x7fffffffffffffff))) x = 0;
    return x >> 63 ? ~x : x ^ UINT64_C(0x8000000000000000);
}
static XXFC_MAYBE_UNUSED unsigned numeric_tokens(char *p, char **v, unsigned cap)
{
    unsigned n = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (n == cap) return cap + 1;
        v[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            ++p;
        }
        if (*p) *p++ = 0;
    }
    return n;
}
#endif

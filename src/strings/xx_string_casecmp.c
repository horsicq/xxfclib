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

/* YARA-X 1.23 lib/src/wasm/string.rs: LowercaseChars / LowercaseCharsRev
 * and the *_case_insensitive operators, with exact ASCII fast paths. */

#include <xxfclib/strings/xx_string_casecmp.h>
#include <xxfclib/strings/xx_string_casefold.h>
#include <xxfclib/algo/literal_nocase/xx_literal_nocase.h>
#include <xxfclib/algo/byteset/xx_byteset.h>

#include <stdlib.h>
#include <string.h>

/* Items of malformed bytes are kept apart from every scalar. */
#define XX_CASECMP_MALFORMED 0x80000000u
#define XX_CASECMP_HIGH_BITS UINT64_C(0x8080808080808080)

typedef struct {
    const uint8_t *at;
    const uint8_t *end;
    uint32_t pending; /* U+0307 after the "i" of U+0130, or 0. */
} xx_casecmp_forward;

typedef struct {
    const uint8_t *begin;
    const uint8_t *end;
    uint32_t pending; /* "i" before the U+0307 of U+0130, or 0. */
} xx_casecmp_backward;

static uint8_t xx_casecmp_ascii_lower(uint8_t ch)
{
    return ch >= 'A' && ch <= 'Z' ? (uint8_t)(ch + ('a' - 'A')) : ch;
}

/* Eight ASCII bytes with 'A'..'Z' lowercased. b + 0x3f has its top bit set
 * for b >= 'A', b + 0x25 for b > 'Z'; neither sum carries for b < 0x80. */
static uint64_t xx_casecmp_lower8(uint64_t word)
{
    uint64_t upper = (word + UINT64_C(0x3f3f3f3f3f3f3f3f)) &
                     ~(word + UINT64_C(0x2525252525252525)) &
                     XX_CASECMP_HIGH_BITS;
    return word | (upper >> 2);
}

static bool xx_casecmp_is_ascii(const uint8_t *bytes, size_t length)
{
    size_t i = 0;
    for (; length - i >= 8; i += 8) {
        uint64_t word;
        memcpy(&word, bytes + i, sizeof(word));
        if (word & XX_CASECMP_HIGH_BITS) return false;
    }
    for (; i < length; ++i)
        if (bytes[i] >= 0x80) return false;
    return true;
}

/* Width of the well-formed UTF-8 scalar that starts a non-ASCII byte
 * sequence, or 0, as bstr::decode_utf8 validates it. */
static size_t xx_casecmp_decode(const uint8_t *bytes, size_t length,
                                uint32_t *scalar)
{
    uint8_t lead = bytes[0];
    size_t width;
    uint32_t value;
    if (lead >= 0xc2 && lead <= 0xdf) {
        width = 2;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        width = 3;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        width = 4;
        value = lead & 0x07u;
    } else {
        return 0;
    }
    if (length < width) return 0;
    for (size_t i = 1; i < width; ++i) {
        if ((bytes[i] & 0xc0u) != 0x80u) return 0;
        value = (value << 6) | (bytes[i] & 0x3fu);
    }
    if ((width == 3 && value < 0x800u) || (width == 4 && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu)
        return 0;
    *scalar = value;
    return width;
}

/* LowercaseChars::next. */
static bool xx_casecmp_next(xx_casecmp_forward *it, uint32_t *item)
{
    uint32_t scalar;
    size_t width;
    if (it->pending) {
        *item = it->pending;
        it->pending = 0;
        return true;
    }
    if (it->at == it->end) return false;
    if (*it->at < 0x80) {
        *item = xx_casecmp_ascii_lower(*it->at++);
        return true;
    }
    width = xx_casecmp_decode(it->at, (size_t)(it->end - it->at), &scalar);
    if (!width) {
        *item = XX_CASECMP_MALFORMED | *it->at++;
        return true;
    }
    it->at += width;
    if (scalar == 0x0130u) {
        *item = 'i';
        it->pending = 0x0307u;
        return true;
    }
    *item = xx_string_scalar_to_lower(scalar);
    return true;
}

/* LowercaseCharsRev::next, with bstr::decode_last_utf8: the last scalar
 * starts at most three continuation bytes back and must end the slice. */
static bool xx_casecmp_prev(xx_casecmp_backward *it, uint32_t *item)
{
    size_t length, start, limit, width;
    uint32_t scalar;
    if (it->pending) {
        *item = it->pending;
        it->pending = 0;
        return true;
    }
    if (it->end == it->begin) return false;
    if (it->end[-1] < 0x80) {
        *item = xx_casecmp_ascii_lower(*--it->end);
        return true;
    }
    length = (size_t)(it->end - it->begin);
    start = length - 1;
    limit = length > 4 ? length - 4 : 0;
    while (start > limit && (it->begin[start] & 0xc0u) == 0x80u) --start;
    width = xx_casecmp_decode(it->begin + start, length - start, &scalar);
    if (!width || start + width != length) {
        *item = XX_CASECMP_MALFORMED | *--it->end;
        return true;
    }
    it->end -= width;
    if (scalar == 0x0130u) {
        *item = 0x0307u;
        it->pending = 'i';
        return true;
    }
    *item = xx_string_scalar_to_lower(scalar);
    return true;
}

/* Number of leading byte pairs that are ASCII and equal ignoring ASCII
 * case (ascii_case_insensitive_mismatch). */
static size_t xx_casecmp_ascii_prefix(const uint8_t *lhs, const uint8_t *rhs,
                                      size_t length)
{
    size_t i = 0;
    for (; length - i >= 8; i += 8) {
        uint64_t a, b;
        memcpy(&a, lhs + i, sizeof(a));
        memcpy(&b, rhs + i, sizeof(b));
        if (((a | b) & XX_CASECMP_HIGH_BITS) ||
            xx_casecmp_lower8(a) != xx_casecmp_lower8(b)) break;
    }
    for (; i < length; ++i)
        if (lhs[i] >= 0x80 ||
            xx_casecmp_ascii_lower(lhs[i]) != xx_casecmp_ascii_lower(rhs[i]))
            break;
    return i;
}

/* The same count over trailing byte pairs; ends point past the data. */
static size_t xx_casecmp_ascii_suffix(const uint8_t *lhs_end,
                                      const uint8_t *rhs_end, size_t length)
{
    size_t i = 0;
    for (; length - i >= 8; i += 8) {
        uint64_t a, b;
        memcpy(&a, lhs_end - i - 8, sizeof(a));
        memcpy(&b, rhs_end - i - 8, sizeof(b));
        if (((a | b) & XX_CASECMP_HIGH_BITS) ||
            xx_casecmp_lower8(a) != xx_casecmp_lower8(b)) break;
    }
    for (; i < length; ++i) {
        uint8_t a = lhs_end[-1 - (ptrdiff_t)i];
        uint8_t b = rhs_end[-1 - (ptrdiff_t)i];
        if (a >= 0x80 || xx_casecmp_ascii_lower(a) != xx_casecmp_ascii_lower(b))
            break;
    }
    return i;
}

/* Only U+212A KELVIN SIGN (lowercase "k") and U+0130 ("i" + U+0307) give
 * ASCII items from non-ASCII bytes. Without them an ASCII needle can only
 * match ASCII bytes, and the byte search below is exact. */
static bool xx_casecmp_has_ascii_lowercase_source(const uint8_t *bytes,
                                                  size_t length)
{
    const uint8_t *at, *end = bytes + length;
    if (xx_casecmp_is_ascii(bytes, length)) return false;
    for (at = bytes; (size_t)(end - at) >= 2 &&
         (at = (const uint8_t *)memchr(at, 0xc4, (size_t)(end - at))) != NULL;
         ++at)
        if (end - at >= 2 && at[1] == 0xb0) return true;
    for (at = bytes; (size_t)(end - at) >= 3 &&
         (at = (const uint8_t *)memchr(at, 0xe2, (size_t)(end - at))) != NULL;
         ++at)
        if (end - at >= 3 && at[1] == 0x84 && at[2] == 0xaa) return true;
    return false;
}

/* Whether the haystack items from byte position at onwards start with the
 * needle's first item and continue with the rest of it. */
static bool xx_casecmp_matches_at(const uint8_t *at, const uint8_t *end,
                                  uint32_t first,
                                  const xx_casecmp_forward *rest)
{
    xx_casecmp_forward candidate = {at, end, 0}, expected = *rest;
    uint32_t item, want;
    if (!xx_casecmp_next(&candidate, &item) || item != first) return false;
    while (xx_casecmp_next(&expected, &want))
        if (!xx_casecmp_next(&candidate, &item) || item != want) return false;
    return true;
}

/* contains_unicode_case_insensitive: every item position of the haystack,
 * including the one inside the lowercase of U+0130. */
static bool xx_casecmp_contains_items(const uint8_t *haystack,
                                      size_t haystack_length,
                                      const uint8_t *needle,
                                      size_t needle_length)
{
    xx_casecmp_forward position = {haystack, haystack + haystack_length, 0};
    xx_casecmp_forward rest = {needle, needle + needle_length, 0};
    uint32_t first, item;
    if (!xx_casecmp_next(&rest, &first)) return true;
    if (first < 0x80) {
        /* An ASCII first item comes from that byte in either case, or
         * from U+212A ('k') or U+0130 ('i'). ASCII bytes and lead bytes are
         * always item boundaries, so only these bytes can start a match. */
        const uint8_t *end = haystack + haystack_length;
        uint8_t lower = (uint8_t)first;
        uint8_t upper = lower >= 'a' && lower <= 'z' ? (uint8_t)(lower - 32) : lower;
        uint8_t lead = lower == 'k' ? 0xe2 : lower == 'i' ? 0xc4 : lower;
        for (const uint8_t *at = haystack; at < end; ++at)
            if ((*at == lower || *at == upper || *at == lead) &&
                xx_casecmp_matches_at(at, end, first, &rest)) return true;
        return false;
    }
    for (;;) {
        xx_casecmp_forward probe = position;
        if (!xx_casecmp_next(&probe, &item)) return false;
        if (item == first) {
            xx_casecmp_forward candidate = probe, expected = rest;
            uint32_t want;
            bool matched = true;
            while (xx_casecmp_next(&expected, &want)) {
                if (!xx_casecmp_next(&candidate, &item) || item != want) {
                    matched = false;
                    break;
                }
            }
            if (matched) return true;
        }
        position = probe;
    }
}

/* Needles up to this many bytes keep their KMP tables on the stack. */
#define XX_CASECMP_KMP_STACK 64
/* ASCII needles up to this many bytes try the SIMD literal search first. */
#define XX_CASECMP_LITERAL_MAX 32

/* The same answer as xx_casecmp_contains_items in O(n + m) item steps:
 * Knuth-Morris-Pratt over the item streams. A needle byte yields at most
 * one item (U+0130's two items take its two bytes), so needle_length bounds
 * the item count. Returns false, with no answer, only when the tables
 * cannot be allocated; the caller then keeps the allocation-free search. */
static bool xx_casecmp_contains_kmp(const uint8_t *haystack,
                                    size_t haystack_length,
                                    const uint8_t *needle,
                                    size_t needle_length, bool *result)
{
    size_t stack_fail[XX_CASECMP_KMP_STACK];
    uint32_t stack_items[XX_CASECMP_KMP_STACK];
    size_t *fail = stack_fail;
    uint32_t *items = stack_items;
    void *block = NULL;
    xx_casecmp_forward it = {needle, needle + needle_length, 0};
    size_t count = 0, q, k;
    uint32_t item;
    xx_byteset starts;
    bool skip;
    if (needle_length > XX_CASECMP_KMP_STACK) {
        if (needle_length > SIZE_MAX / (sizeof(size_t) + sizeof(uint32_t)))
            return false;
        block = malloc(needle_length * (sizeof(size_t) + sizeof(uint32_t)));
        if (!block) return false;
        fail = (size_t *)block;
        items = (uint32_t *)(fail + needle_length);
    }
    while (count < needle_length && xx_casecmp_next(&it, &item))
        items[count++] = item;
    fail[0] = 0;
    for (q = 1, k = 0; q < count; ++q) {
        while (k && items[q] != items[k]) k = fail[k - 1];
        if (items[q] == items[k]) ++k;
        fail[q] = k;
    }
    /* An ASCII first item comes only from that byte in either case, or
     * from U+212A ('k') or U+0130 ('i'). ASCII and lead bytes always start
     * an item (decoding never consumes them as continuations), so with no
     * partial match the search may jump to the next such byte: the skipped
     * items could not have advanced it. Each byte is still visited at most
     * once, so the bound stays O(n + m). */
    skip = items[0] < 0x80;
    if (skip) {
        uint8_t lower = (uint8_t)items[0];
        xx_byteset_clear(&starts);
        xx_byteset_add(&starts, lower);
        if (lower >= 'a' && lower <= 'z')
            xx_byteset_add(&starts, (uint8_t)(lower - ('a' - 'A')));
        if (lower == 'k') xx_byteset_add(&starts, 0xe2);
        if (lower == 'i') xx_byteset_add(&starts, 0xc4);
        xx_byteset_prepare(&starts);
    }
    *result = false;
    it.at = haystack;
    it.end = haystack + haystack_length;
    it.pending = 0;
    for (q = 0;;) {
        if (skip && !q && !it.pending) {
            size_t from = (size_t)(it.at - haystack);
            size_t next = xx_byteset_find(&starts, haystack, haystack_length, from);
            if (next >= haystack_length) break;
            it.at = haystack + next;
        }
        if (!xx_casecmp_next(&it, &item)) break;
        while (q && item != items[q]) q = fail[q - 1];
        if (item == items[q] && ++q == count) {
            *result = true;
            break;
        }
    }
    free(block);
    return true;
}

XXFC_API bool xx_string_unicode_icontains(const uint8_t *haystack,
                                          size_t haystack_length,
                                          const uint8_t *needle,
                                          size_t needle_length)
{
    bool found;
    if ((!haystack && haystack_length) || (!needle && needle_length))
        return false;
    if (!needle_length) return true;
    if (!haystack) haystack = (const uint8_t *)"";
    if (xx_casecmp_is_ascii(needle, needle_length)) {
        /* An ASCII needle has one item per byte, a haystack at most as many
         * items as bytes (U+0130's two items take its two bytes). */
        if (needle_length > haystack_length) return false;
        /* The SIMD literal search verifies each pivot hit in full, at most
         * needle_length steps per haystack byte; short needles keep it as
         * the fast path (linear with a constant factor below the cap), long
         * ones go straight to KMP, which gives the same answer. */
        if (needle_length <= XX_CASECMP_LITERAL_MAX) {
            if (xx_literal_nocase_find(haystack, haystack_length, 0, needle,
                                       needle_length) != SIZE_MAX) return true;
            if (!xx_casecmp_has_ascii_lowercase_source(haystack, haystack_length))
                return false;
        }
    }
    /* Linear in the worst case; if its tables cannot be allocated, the
     * allocation-free search gives the same answer more slowly. */
    if (xx_casecmp_contains_kmp(haystack, haystack_length, needle,
                                needle_length, &found))
        return found;
    return xx_casecmp_contains_items(haystack, haystack_length, needle,
                                     needle_length);
}

XXFC_API bool xx_string_unicode_istarts_with(const uint8_t *haystack,
                                             size_t haystack_length,
                                             const uint8_t *prefix,
                                             size_t prefix_length)
{
    size_t shorter, at;
    if ((!haystack && haystack_length) || (!prefix && prefix_length))
        return false;
    if (!haystack) haystack = (const uint8_t *)"";
    if (!prefix) prefix = (const uint8_t *)"";
    shorter = haystack_length < prefix_length ? haystack_length : prefix_length;
    at = xx_casecmp_ascii_prefix(haystack, prefix, shorter);
    if (at == shorter) return haystack_length >= prefix_length;
    if (haystack[at] < 0x80 && prefix[at] < 0x80) return false;
    {
        xx_casecmp_forward text = {haystack + at, haystack + haystack_length, 0};
        xx_casecmp_forward start = {prefix + at, prefix + prefix_length, 0};
        uint32_t want, item;
        while (xx_casecmp_next(&start, &want))
            if (!xx_casecmp_next(&text, &item) || item != want) return false;
    }
    return true;
}

XXFC_API bool xx_string_unicode_iends_with(const uint8_t *haystack,
                                           size_t haystack_length,
                                           const uint8_t *suffix,
                                           size_t suffix_length)
{
    size_t shorter, matched, haystack_end, suffix_end;
    if ((!haystack && haystack_length) || (!suffix && suffix_length))
        return false;
    if (!haystack) haystack = (const uint8_t *)"";
    if (!suffix) suffix = (const uint8_t *)"";
    shorter = haystack_length < suffix_length ? haystack_length : suffix_length;
    matched = xx_casecmp_ascii_suffix(haystack + haystack_length,
                                      suffix + suffix_length, shorter);
    if (matched == shorter) return haystack_length >= suffix_length;
    haystack_end = haystack_length - matched;
    suffix_end = suffix_length - matched;
    if (haystack[haystack_end - 1] < 0x80 && suffix[suffix_end - 1] < 0x80)
        return false;
    {
        xx_casecmp_backward text = {haystack, haystack + haystack_end, 0};
        xx_casecmp_backward end = {suffix, suffix + suffix_end, 0};
        uint32_t want, item;
        while (xx_casecmp_prev(&end, &want))
            if (!xx_casecmp_prev(&text, &item) || item != want) return false;
    }
    return true;
}

XXFC_API bool xx_string_unicode_iequals(const uint8_t *lhs, size_t lhs_length,
                                        const uint8_t *rhs, size_t rhs_length)
{
    size_t shorter, at;
    if ((!lhs && lhs_length) || (!rhs && rhs_length)) return false;
    if (!lhs) lhs = (const uint8_t *)"";
    if (!rhs) rhs = (const uint8_t *)"";
    shorter = lhs_length < rhs_length ? lhs_length : rhs_length;
    at = xx_casecmp_ascii_prefix(lhs, rhs, shorter);
    if (at == shorter) return lhs_length == rhs_length;
    if (lhs[at] < 0x80 && rhs[at] < 0x80) return false;
    {
        xx_casecmp_forward left = {lhs + at, lhs + lhs_length, 0};
        xx_casecmp_forward right = {rhs + at, rhs + rhs_length, 0};
        for (;;) {
            uint32_t a, b;
            bool has_a = xx_casecmp_next(&left, &a);
            bool has_b = xx_casecmp_next(&right, &b);
            if (has_a != has_b) return false;
            if (!has_a) return true;
            if (a != b) return false;
        }
    }
}

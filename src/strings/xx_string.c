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
 * @file xx_string.c
 * @brief String handling subsystem implementation.
 */

#include "xxfclib/strings/xx_string.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "platforms/xx_string_platform.h"

/* --- Helpers --- */

static inline char xx_tolower_a(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline wchar_t xx_tolower_w(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + (L'a' - L'A')) : c;
}

/* The forked xx_rt_str* primitives live in platforms/; the wide-character
 * ones below have no platform fork and stay here. */

size_t xx_rt_wcslen(const wchar_t *pString)
{
    const wchar_t *p = pString;

    while (*p != L'\0') {
        p++;
    }

    return (size_t)(p - pString);
}

int xx_rt_wcscmp(const wchar_t *pLeft, const wchar_t *pRight)
{
    while ((*pLeft != L'\0') && (*pLeft == *pRight)) {
        pLeft++;
        pRight++;
    }

    if (*pLeft == *pRight) {
        return 0;
    }

    return (*pLeft < *pRight) ? -1 : 1;
}

int xx_rt_wcsncmp(const wchar_t *pLeft, const wchar_t *pRight,
                  size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        if (pLeft[i] != pRight[i]) {
            return (pLeft[i] < pRight[i]) ? -1 : 1;
        }

        if (pLeft[i] == L'\0') {
            break;
        }
    }

    return 0;
}

wchar_t *xx_rt_wcschr(const wchar_t *pString, wchar_t nChar)
{
    for (;;) {
        if (*pString == nChar) {
            return (wchar_t *)pString;
        }

        if (*pString == L'\0') {
            return NULL;
        }

        pString++;
    }
}

int xx_rt_ascii_tolower(int nChar)
{
    if ((nChar >= 'A') && (nChar <= 'Z')) {
        return nChar + ('a' - 'A');
    }

    return nChar;
}

/* --- Raw ANSI String Implementation --- */

size_t xx_str_len(const char *s) {
    if (!s) {
        return 0;
    }
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

size_t xx_str_get_length(const char *s) {
    return xx_str_len(s);
}

char* xx_str_create(const char *s) {
    if (!s) {
        char *res = (char*)xx_mem_alloc(1);
        if (res) {
            res[0] = '\0';
        }
        return res;
    }
    return xx_str_dup(s);
}

char* xx_str_create_len(size_t len) {
    char *res = (char*)xx_mem_alloc(len + 1);
    if (!res) {
        return NULL;
    }
    xx_mem_zero(res, len + 1);
    return res;
}

void xx_str_free(char *s) {
    xx_mem_free(s);
}

char* xx_str_concat(const char *s1, const char *s2) {
    size_t len1 = xx_str_len(s1);
    size_t len2 = xx_str_len(s2);
    char *res = (char*)xx_mem_alloc(len1 + len2 + 1);
    if (!res) {
        return NULL;
    }
    if (len1 > 0) {
        xx_mem_copy(res, s1, len1);
    }
    if (len2 > 0) {
        xx_mem_copy(res + len1, s2, len2);
    }
    res[len1 + len2] = '\0';
    return res;
}

char* xx_str_concat3(const char *s1, const char *s2, const char *s3) {
    size_t len1 = xx_str_len(s1);
    size_t len2 = xx_str_len(s2);
    size_t len3 = xx_str_len(s3);
    char *res = (char*)xx_mem_alloc(len1 + len2 + len3 + 1);
    if (!res) {
        return NULL;
    }
    if (len1 > 0) {
        xx_mem_copy(res, s1, len1);
    }
    if (len2 > 0) {
        xx_mem_copy(res + len1, s2, len2);
    }
    if (len3 > 0) {
        xx_mem_copy(res + len1 + len2, s3, len3);
    }
    res[len1 + len2 + len3] = '\0';
    return res;
}

int xx_str_cmp(const char *a, const char *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

int xx_str_icmp(const char *a, const char *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && (xx_tolower_a(*a) == xx_tolower_a(*b))) {
        a++;
        b++;
    }
    return (int)((unsigned char)xx_tolower_a(*a)) - (int)((unsigned char)xx_tolower_a(*b));
}

int xx_str_ncmp(const char *a, const char *b, size_t n) {
    if (n == 0 || a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (n-- && *a) {
        if (*a != *b) {
            return (int)((unsigned char)*a) - (int)((unsigned char)*b);
        }
        a++;
        b++;
    }
    return 0;
}

int xx_str_nicmp(const char *a, const char *b, size_t n) {
    if (n == 0 || a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (n-- && *a) {
        if (xx_tolower_a(*a) != xx_tolower_a(*b)) {
            return (int)((unsigned char)xx_tolower_a(*a)) - (int)((unsigned char)xx_tolower_a(*b));
        }
        a++;
        b++;
    }
    return 0;
}

char* xx_str_dup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = xx_str_len(s);
    char *dup = (char*)xx_mem_alloc(len + 1);
    if (!dup) {
        return NULL;
    }
    xx_mem_copy(dup, s, len + 1);
    return dup;
}

char* xx_str_chr(const char *s, int c) {
    if (!s) {
        return NULL;
    }
    char target = (char)c;
    while (*s) {
        if (*s == target) {
            return (char*)s;
        }
        s++;
    }
    return (target == '\0') ? (char*)s : NULL;
}

char* xx_str_rchr(const char *s, int c) {
    if (!s) {
        return NULL;
    }
    char target = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == target) {
            last = s;
        }
        s++;
    }
    if (target == '\0') {
        return (char*)s;
    }
    return (char*)last;
}

char* xx_str_str(const char *haystack, const char *needle) {
    if (!haystack || !needle) {
        return NULL;
    }
    if (!*needle) {
        return (char*)haystack;
    }
    size_t nlen = xx_str_len(needle);
    while (*haystack) {
        if (*haystack == *needle && xx_str_ncmp(haystack, needle, nlen) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return NULL;
}

bool xx_str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) {
        return false;
    }
    size_t plen = xx_str_len(prefix);
    return xx_str_ncmp(s, prefix, plen) == 0;
}

bool xx_str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) {
        return false;
    }
    size_t slen = xx_str_len(s);
    size_t xlen = xx_str_len(suffix);
    if (xlen > slen) {
        return false;
    }
    return xx_str_cmp(s + (slen - xlen), suffix) == 0;
}

bool xx_str_equals(const char *a, const char *b) {
    return xx_str_cmp(a, b) == 0;
}

bool xx_str_iequals(const char *a, const char *b) {
    return xx_str_icmp(a, b) == 0;
}

/* --- Raw Unicode (Wide) String Implementation --- */

size_t xx_str_wlen(const wchar_t *w) {
    if (!w) {
        return 0;
    }
    size_t len = 0;
    while (w[len]) {
        len++;
    }
    return len;
}

size_t xx_str_wget_length(const wchar_t *w) {
    return xx_str_wlen(w);
}

wchar_t* xx_str_wcreate(const wchar_t *w) {
    if (!w) {
        wchar_t *res = (wchar_t*)xx_mem_alloc(sizeof(wchar_t));
        if (res) {
            res[0] = L'\0';
        }
        return res;
    }
    return xx_str_wdup(w);
}

wchar_t* xx_str_wcreate_len(size_t len) {
    wchar_t *res = (wchar_t*)xx_mem_alloc((len + 1) * sizeof(wchar_t));
    if (!res) {
        return NULL;
    }
    xx_mem_zero(res, (len + 1) * sizeof(wchar_t));
    return res;
}

void xx_str_wfree(wchar_t *w) {
    xx_mem_free(w);
}

wchar_t* xx_str_wconcat(const wchar_t *w1, const wchar_t *w2) {
    size_t len1 = xx_str_wlen(w1);
    size_t len2 = xx_str_wlen(w2);
    wchar_t *res = (wchar_t*)xx_mem_alloc((len1 + len2 + 1) * sizeof(wchar_t));
    if (!res) {
        return NULL;
    }
    if (len1 > 0) {
        xx_mem_copy(res, w1, len1 * sizeof(wchar_t));
    }
    if (len2 > 0) {
        xx_mem_copy(res + len1, w2, len2 * sizeof(wchar_t));
    }
    res[len1 + len2] = L'\0';
    return res;
}

wchar_t* xx_str_wconcat3(const wchar_t *w1, const wchar_t *w2, const wchar_t *w3) {
    size_t len1 = xx_str_wlen(w1);
    size_t len2 = xx_str_wlen(w2);
    size_t len3 = xx_str_wlen(w3);
    wchar_t *res = (wchar_t*)xx_mem_alloc((len1 + len2 + len3 + 1) * sizeof(wchar_t));
    if (!res) {
        return NULL;
    }
    if (len1 > 0) {
        xx_mem_copy(res, w1, len1 * sizeof(wchar_t));
    }
    if (len2 > 0) {
        xx_mem_copy(res + len1, w2, len2 * sizeof(wchar_t));
    }
    if (len3 > 0) {
        xx_mem_copy(res + len1 + len2, w3, len3 * sizeof(wchar_t));
    }
    res[len1 + len2 + len3] = L'\0';
    return res;
}

int xx_str_wcmp(const wchar_t *a, const wchar_t *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(*a) - (int)(*b);
}

int xx_str_wicmp(const wchar_t *a, const wchar_t *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && (xx_tolower_w(*a) == xx_tolower_w(*b))) {
        a++;
        b++;
    }
    return (int)xx_tolower_w(*a) - (int)xx_tolower_w(*b);
}

int xx_str_wncmp(const wchar_t *a, const wchar_t *b, size_t n) {
    if (n == 0 || a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (n-- && *a) {
        if (*a != *b) {
            return (int)(*a) - (int)(*b);
        }
        a++;
        b++;
    }
    return 0;
}

int xx_str_wnicmp(const wchar_t *a, const wchar_t *b, size_t n) {
    if (n == 0 || a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (n-- && *a) {
        if (xx_tolower_w(*a) != xx_tolower_w(*b)) {
            return (int)xx_tolower_w(*a) - (int)xx_tolower_w(*b);
        }
        a++;
        b++;
    }
    return 0;
}

wchar_t* xx_str_wdup(const wchar_t *w) {
    if (!w) {
        return NULL;
    }
    size_t len = xx_str_wlen(w);
    wchar_t *dup = (wchar_t*)xx_mem_alloc((len + 1) * sizeof(wchar_t));
    if (!dup) {
        return NULL;
    }
    xx_mem_copy(dup, w, (len + 1) * sizeof(wchar_t));
    return dup;
}

wchar_t* xx_str_wchr(const wchar_t *w, wchar_t c) {
    if (!w) {
        return NULL;
    }
    while (*w) {
        if (*w == c) {
            return (wchar_t*)w;
        }
        w++;
    }
    return (c == L'\0') ? (wchar_t*)w : NULL;
}

wchar_t* xx_str_wrchr(const wchar_t *w, wchar_t c) {
    if (!w) {
        return NULL;
    }
    const wchar_t *last = NULL;
    while (*w) {
        if (*w == c) {
            last = w;
        }
        w++;
    }
    if (c == L'\0') {
        return (wchar_t*)w;
    }
    return (wchar_t*)last;
}

wchar_t* xx_str_wstr(const wchar_t *haystack, const wchar_t *needle) {
    if (!haystack || !needle) {
        return NULL;
    }
    if (!*needle) {
        return (wchar_t*)haystack;
    }
    size_t nlen = xx_str_wlen(needle);
    while (*haystack) {
        if (*haystack == *needle && xx_str_wncmp(haystack, needle, nlen) == 0) {
            return (wchar_t*)haystack;
        }
        haystack++;
    }
    return NULL;
}

bool xx_str_w_starts_with(const wchar_t *w, const wchar_t *prefix) {
    if (!w || !prefix) {
        return false;
    }
    size_t plen = xx_str_wlen(prefix);
    return xx_str_wncmp(w, prefix, plen) == 0;
}

bool xx_str_w_ends_with(const wchar_t *w, const wchar_t *suffix) {
    if (!w || !suffix) {
        return false;
    }
    size_t slen = xx_str_wlen(w);
    size_t xlen = xx_str_wlen(suffix);
    if (xlen > slen) {
        return false;
    }
    return xx_str_wcmp(w + (slen - xlen), suffix) == 0;
}

bool xx_str_w_equals(const wchar_t *a, const wchar_t *b) {
    return xx_str_wcmp(a, b) == 0;
}

bool xx_str_w_iequals(const wchar_t *a, const wchar_t *b) {
    return xx_str_wicmp(a, b) == 0;
}

/* --- Encoding / Conversion Implementation --- */

wchar_t* xx_str_ansi_to_unicode(const char *ansi) {
    return xx_string_platform_mb_to_wide(ansi, XX_CODEPAGE_ANSI);
}

char* xx_str_unicode_to_ansi(const wchar_t *wstr) {
    return xx_string_platform_wide_to_mb(wstr, XX_CODEPAGE_ANSI);
}

wchar_t* xx_str_utf8_to_unicode(const char *utf8) {
    return xx_string_platform_mb_to_wide(utf8, XX_CODEPAGE_UTF8);
}

char* xx_str_unicode_to_utf8(const wchar_t *wstr) {
    return xx_string_platform_wide_to_mb(wstr, XX_CODEPAGE_UTF8);
}

void xx_str_free_unicode(wchar_t *wstr) {
    xx_mem_free(wstr);
}

void xx_str_free_ansi(char *str) {
    xx_mem_free(str);
}

/* --- Dynamic String Struct Implementation (ANSI) --- */

xx_str_a_t* xx_str_a_create(const char *raw) {
    xx_str_a_t *s = (xx_str_a_t*)xx_mem_alloc(sizeof(xx_str_a_t));
    if (!s) {
        return NULL;
    }
    if (!xx_str_a_init_copy(s, raw)) {
        xx_mem_free(s);
        return NULL;
    }
    return s;
}

void xx_str_a_destroy(xx_str_a_t *s) {
    if (!s) {
        return;
    }
    xx_str_a_free(s);
    xx_mem_free(s);
}

bool xx_str_a_concat(xx_str_a_t *s, const char *append_str) {
    return xx_str_a_append(s, append_str);
}

size_t xx_str_a_get_length(const xx_str_a_t *s) {
    return s ? s->length : 0;
}

bool xx_str_a_init(xx_str_a_t *s) {
    if (!s) {
        return false;
    }
    s->data     = NULL;
    s->length   = 0;
    s->capacity = 0;
    s->is_view  = false;
    return true;
}

bool xx_str_a_init_view(xx_str_a_t *s, const char *raw, size_t len) {
    if (!s) {
        return false;
    }
    s->data     = (char*)raw;
    s->length   = len;
    s->capacity = len;
    s->is_view  = true;
    return true;
}

bool xx_str_a_init_copy(xx_str_a_t *s, const char *raw) {
    if (!s) {
        return false;
    }
    xx_str_a_init(s);
    if (!raw) {
        return true;
    }
    size_t len = xx_str_len(raw);
    if (!xx_str_a_reserve(s, len + 1)) {
        return false;
    }
    xx_mem_copy(s->data, raw, len);
    s->data[len] = '\0';
    s->length    = len;
    return true;
}

void xx_str_a_free(xx_str_a_t *s) {
    if (!s) {
        return;
    }
    if (!s->is_view && s->data) {
        xx_mem_free(s->data);
    }
    s->data     = NULL;
    s->length   = 0;
    s->capacity = 0;
    s->is_view  = false;
}

bool xx_str_a_reserve(xx_str_a_t *s, size_t capacity) {
    if (!s || s->is_view) {
        return false;
    }
    if (capacity <= s->capacity) {
        return true;
    }

    size_t new_cap = (s->capacity == 0) ? 16 : s->capacity;
    while (new_cap < capacity) {
        size_t grown = new_cap + (new_cap >> 1); /* 1.5x growth */
        /* Growing past the point where 1.5x wraps would leave new_cap smaller
         * than it started, so the loop would never reach capacity and would
         * spin forever. Take the exact request instead. */
        if (grown <= new_cap) {
            new_cap = capacity;
            break;
        }
        new_cap = grown;
    }

    char *new_data = (char*)xx_mem_realloc(s->data, new_cap);
    if (!new_data) {
        return false;
    }

    s->data     = new_data;
    s->capacity = new_cap;
    return true;
}

bool xx_str_a_append(xx_str_a_t *s, const char *append_str) {
    if (!s || s->is_view || !append_str) {
        return false;
    }
    size_t app_len = xx_str_len(append_str);
    if (app_len == 0) {
        return true;
    }
    size_t needed = s->length + app_len + 1;
    if (!xx_str_a_reserve(s, needed)) {
        return false;
    }
    xx_mem_copy(s->data + s->length, append_str, app_len);
    s->length += app_len;
    s->data[s->length] = '\0';
    return true;
}

bool xx_str_a_append_char(xx_str_a_t *s, char c) {
    char temp[2];
    temp[0] = c;
    temp[1] = '\0';
    return xx_str_a_append(s, temp);
}

void xx_str_a_clear(xx_str_a_t *s) {
    if (!s || s->is_view) {
        return;
    }
    s->length = 0;
    if (s->data) {
        s->data[0] = '\0';
    }
}

const char* xx_str_a_cstr(const xx_str_a_t *s) {
    return (s && s->data) ? s->data : "";
}

/* --- Dynamic String Struct Implementation (Unicode) --- */

xx_str_w_t* xx_str_w_create(const wchar_t *raw) {
    xx_str_w_t *s = (xx_str_w_t*)xx_mem_alloc(sizeof(xx_str_w_t));
    if (!s) {
        return NULL;
    }
    if (!xx_str_w_init_copy(s, raw)) {
        xx_mem_free(s);
        return NULL;
    }
    return s;
}

void xx_str_w_destroy(xx_str_w_t *s) {
    if (!s) {
        return;
    }
    xx_str_w_free(s);
    xx_mem_free(s);
}

bool xx_str_w_concat(xx_str_w_t *s, const wchar_t *append_str) {
    return xx_str_w_append(s, append_str);
}

size_t xx_str_w_get_length(const xx_str_w_t *s) {
    return s ? s->length : 0;
}

bool xx_str_w_init(xx_str_w_t *s) {
    if (!s) {
        return false;
    }
    s->data     = NULL;
    s->length   = 0;
    s->capacity = 0;
    s->is_view  = false;
    return true;
}

bool xx_str_w_init_view(xx_str_w_t *s, const wchar_t *raw, size_t len) {
    if (!s) {
        return false;
    }
    s->data     = (wchar_t*)raw;
    s->length   = len;
    s->capacity = len;
    s->is_view  = true;
    return true;
}

bool xx_str_w_init_copy(xx_str_w_t *s, const wchar_t *raw) {
    if (!s) {
        return false;
    }
    xx_str_w_init(s);
    if (!raw) {
        return true;
    }
    size_t len = xx_str_wlen(raw);
    if (!xx_str_w_reserve(s, len + 1)) {
        return false;
    }
    xx_mem_copy(s->data, raw, len * sizeof(wchar_t));
    s->data[len] = L'\0';
    s->length    = len;
    return true;
}

void xx_str_w_free(xx_str_w_t *s) {
    if (!s) {
        return;
    }
    if (!s->is_view && s->data) {
        xx_mem_free(s->data);
    }
    s->data     = NULL;
    s->length   = 0;
    s->capacity = 0;
    s->is_view  = false;
}

bool xx_str_w_reserve(xx_str_w_t *s, size_t capacity) {
    if (!s || s->is_view) {
        return false;
    }
    if (capacity <= s->capacity) {
        return true;
    }

    size_t new_cap = (s->capacity == 0) ? 16 : s->capacity;
    while (new_cap < capacity) {
        new_cap = new_cap + (new_cap >> 1);
    }

    wchar_t *new_data = (wchar_t*)xx_mem_realloc(s->data, new_cap * sizeof(wchar_t));
    if (!new_data) {
        return false;
    }

    s->data     = new_data;
    s->capacity = new_cap;
    return true;
}

bool xx_str_w_append(xx_str_w_t *s, const wchar_t *append_str) {
    if (!s || s->is_view || !append_str) {
        return false;
    }
    size_t app_len = xx_str_wlen(append_str);
    if (app_len == 0) {
        return true;
    }
    size_t needed = s->length + app_len + 1;
    if (!xx_str_w_reserve(s, needed)) {
        return false;
    }
    xx_mem_copy(s->data + s->length, append_str, app_len * sizeof(wchar_t));
    s->length += app_len;
    s->data[s->length] = L'\0';
    return true;
}

bool xx_str_w_append_char(xx_str_w_t *s, wchar_t c) {
    wchar_t temp[2];
    temp[0] = c;
    temp[1] = L'\0';
    return xx_str_w_append(s, temp);
}

void xx_str_w_clear(xx_str_w_t *s) {
    if (!s || s->is_view) {
        return;
    }
    s->length = 0;
    if (s->data) {
        s->data[0] = L'\0';
    }
}

const wchar_t* xx_str_w_cstr(const xx_str_w_t *s) {
    return (s && s->data) ? s->data : L"";
}

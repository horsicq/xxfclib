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
 * @file xx_string.h
 * @brief String handling subsystem supporting raw ANSI, raw Unicode, dynamic structs and conversions.
 */

#ifndef XX_STRING_H
#define XX_STRING_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- String Structures --- */

/**
 * @brief Represents a dynamic or non-owning view of an ANSI/UTF-8 character string.
 */
typedef struct xx_str_a_s {
    char   *data;       /**< Pointer to null-terminated char buffer */
    size_t  length;     /**< String length in bytes/chars (excluding null terminator) */
    size_t  capacity;   /**< Total allocated capacity in bytes */
    bool    is_view;    /**< True if this is a non-owning read-only reference */
} xx_str_a_t;

/**
 * @brief Represents a dynamic or non-owning view of a wide/Unicode character string.
 */
typedef struct xx_str_w_s {
    wchar_t *data;      /**< Pointer to null-terminated wchar_t buffer */
    size_t   length;    /**< String length in characters (excluding null terminator) */
    size_t   capacity;  /**< Total allocated capacity in wchar_t units */
    bool     is_view;   /**< True if this is a non-owning read-only reference */
} xx_str_w_t;

/* Type aliases */
typedef xx_str_a_t xx_string_a;
typedef xx_str_w_t xx_string_w;
typedef xx_str_a_t str_a;
typedef xx_str_w_t str_w;

/* --- Raw ANSI String Functions --- */

XXFC_API size_t xx_str_len(const char *s);
XXFC_API size_t xx_str_get_length(const char *s);
XXFC_API char*  xx_str_create(const char *s);
XXFC_API char*  xx_str_create_len(size_t len);
XXFC_API void   xx_str_free(char *s);
XXFC_API char*  xx_str_concat(const char *s1, const char *s2);
XXFC_API char*  xx_str_concat3(const char *s1, const char *s2, const char *s3);
XXFC_API int    xx_str_cmp(const char *a, const char *b);
XXFC_API int    xx_str_icmp(const char *a, const char *b);
XXFC_API int    xx_str_ncmp(const char *a, const char *b, size_t n);
XXFC_API int    xx_str_nicmp(const char *a, const char *b, size_t n);
XXFC_API char*  xx_str_dup(const char *s);
XXFC_API char*  xx_str_chr(const char *s, int c);
XXFC_API char*  xx_str_rchr(const char *s, int c);
XXFC_API char*  xx_str_str(const char *haystack, const char *needle);
XXFC_API bool   xx_str_starts_with(const char *s, const char *prefix);
XXFC_API bool   xx_str_ends_with(const char *s, const char *suffix);
XXFC_API bool   xx_str_equals(const char *a, const char *b);
XXFC_API bool   xx_str_iequals(const char *a, const char *b);

/* --- Raw Unicode (Wide) String Functions --- */

XXFC_API size_t   xx_str_wlen(const wchar_t *w);
XXFC_API size_t   xx_str_wget_length(const wchar_t *w);
XXFC_API wchar_t* xx_str_wcreate(const wchar_t *w);
XXFC_API wchar_t* xx_str_wcreate_len(size_t len);
XXFC_API void     xx_str_wfree(wchar_t *w);
XXFC_API wchar_t* xx_str_wconcat(const wchar_t *w1, const wchar_t *w2);
XXFC_API wchar_t* xx_str_wconcat3(const wchar_t *w1, const wchar_t *w2, const wchar_t *w3);
XXFC_API int      xx_str_wcmp(const wchar_t *a, const wchar_t *b);
XXFC_API int      xx_str_wicmp(const wchar_t *a, const wchar_t *b);
XXFC_API int      xx_str_wncmp(const wchar_t *a, const wchar_t *b, size_t n);
XXFC_API int      xx_str_wnicmp(const wchar_t *a, const wchar_t *b, size_t n);
XXFC_API wchar_t* xx_str_wdup(const wchar_t *w);
XXFC_API wchar_t* xx_str_wchr(const wchar_t *w, wchar_t c);
XXFC_API wchar_t* xx_str_wrchr(const wchar_t *w, wchar_t c);
XXFC_API wchar_t* xx_str_wstr(const wchar_t *haystack, const wchar_t *needle);
XXFC_API bool     xx_str_w_starts_with(const wchar_t *w, const wchar_t *prefix);
XXFC_API bool     xx_str_w_ends_with(const wchar_t *w, const wchar_t *suffix);
XXFC_API bool     xx_str_w_equals(const wchar_t *a, const wchar_t *b);
XXFC_API bool     xx_str_w_iequals(const wchar_t *a, const wchar_t *b);

/* Unicode Raw String inline aliases */
static inline wchar_t* xx_str_create_w(const wchar_t *w) { return xx_str_wcreate(w); }
static inline wchar_t* xx_str_create_wide(const wchar_t *w) { return xx_str_wcreate(w); }
static inline wchar_t* xx_str_create_w_len(size_t len) { return xx_str_wcreate_len(len); }
static inline wchar_t* xx_str_create_wide_len(size_t len) { return xx_str_wcreate_len(len); }
static inline void     xx_str_free_w(wchar_t *w) { xx_str_wfree(w); }
static inline void     xx_str_free_wide(wchar_t *w) { xx_str_wfree(w); }
static inline wchar_t* xx_str_concat_w(const wchar_t *w1, const wchar_t *w2) { return xx_str_wconcat(w1, w2); }
static inline wchar_t* xx_str_concat_wide(const wchar_t *w1, const wchar_t *w2) { return xx_str_wconcat(w1, w2); }
static inline wchar_t* xx_str_concat3_w(const wchar_t *w1, const wchar_t *w2, const wchar_t *w3) { return xx_str_wconcat3(w1, w2, w3); }
static inline size_t   xx_str_get_length_w(const wchar_t *w) { return xx_str_wget_length(w); }
static inline size_t   xx_str_get_length_wide(const wchar_t *w) { return xx_str_wget_length(w); }

/* --- Character Encoding / Conversion Functions --- */

/**
 * @brief Convert ANSI string (current system codepage) to newly allocated Unicode string.
 * @return Allocated wide string (free with xx_str_free_unicode or xx_str_wfree), or NULL on error.
 */
XXFC_API wchar_t* xx_str_ansi_to_unicode(const char *ansi);

/**
 * @brief Convert Unicode string to newly allocated ANSI string (current system codepage).
 * @return Allocated string (free with xx_str_free_ansi or xx_str_free), or NULL on error.
 */
XXFC_API char* xx_str_unicode_to_ansi(const wchar_t *wstr);

/**
 * @brief Convert UTF-8 string to newly allocated Unicode string.
 * @return Allocated wide string (free with xx_str_free_unicode or xx_str_wfree), or NULL on error.
 */
XXFC_API wchar_t* xx_str_utf8_to_unicode(const char *utf8);

/**
 * @brief Convert Unicode string to newly allocated UTF-8 string.
 * @return Allocated string (free with xx_str_free_ansi or xx_str_free), or NULL on error.
 */
XXFC_API char* xx_str_unicode_to_utf8(const wchar_t *wstr);

/**
 * @brief Free a wide string allocated by conversion functions.
 */
XXFC_API void xx_str_free_unicode(wchar_t *wstr);

/**
 * @brief Free an ANSI or UTF-8 string allocated by conversion functions.
 */
XXFC_API void xx_str_free_ansi(char *str);

/* --- Dynamic String Struct APIs (ANSI) --- */

XXFC_API xx_str_a_t* xx_str_a_create(const char *raw);
XXFC_API void        xx_str_a_destroy(xx_str_a_t *s);
XXFC_API bool        xx_str_a_init(xx_str_a_t *s);
XXFC_API bool        xx_str_a_init_view(xx_str_a_t *s, const char *raw, size_t len);
XXFC_API bool        xx_str_a_init_copy(xx_str_a_t *s, const char *raw);
XXFC_API void        xx_str_a_free(xx_str_a_t *s);
XXFC_API bool        xx_str_a_reserve(xx_str_a_t *s, size_t capacity);
XXFC_API bool        xx_str_a_append(xx_str_a_t *s, const char *append_str);
XXFC_API bool        xx_str_a_concat(xx_str_a_t *s, const char *append_str);
XXFC_API bool        xx_str_a_append_char(xx_str_a_t *s, char c);
XXFC_API void        xx_str_a_clear(xx_str_a_t *s);
XXFC_API size_t      xx_str_a_get_length(const xx_str_a_t *s);
XXFC_API const char* xx_str_a_cstr(const xx_str_a_t *s);

/* --- Dynamic String Struct APIs (Unicode) --- */

XXFC_API xx_str_w_t*    xx_str_w_create(const wchar_t *raw);
XXFC_API void           xx_str_w_destroy(xx_str_w_t *s);
XXFC_API bool           xx_str_w_init(xx_str_w_t *s);
XXFC_API bool           xx_str_w_init_view(xx_str_w_t *s, const wchar_t *raw, size_t len);
XXFC_API bool           xx_str_w_init_copy(xx_str_w_t *s, const wchar_t *raw);
XXFC_API void           xx_str_w_free(xx_str_w_t *s);
XXFC_API bool           xx_str_w_reserve(xx_str_w_t *s, size_t capacity);
XXFC_API bool           xx_str_w_append(xx_str_w_t *s, const wchar_t *append_str);
XXFC_API bool           xx_str_w_concat(xx_str_w_t *s, const wchar_t *append_str);
XXFC_API bool           xx_str_w_append_char(xx_str_w_t *s, wchar_t c);
XXFC_API void           xx_str_w_clear(xx_str_w_t *s);
XXFC_API size_t         xx_str_w_get_length(const xx_str_w_t *s);
XXFC_API const wchar_t* xx_str_w_cstr(const xx_str_w_t *s);

/* --- Inline Convenience Aliases --- */

static inline char*    str_create(const char *s) { return xx_str_create(s); }
static inline char*    str_create_len(size_t len) { return xx_str_create_len(len); }
static inline void     str_free(char *s) { xx_str_free(s); }
static inline char*    str_concat(const char *s1, const char *s2) { return xx_str_concat(s1, s2); }
static inline char*    str_concat3(const char *s1, const char *s2, const char *s3) { return xx_str_concat3(s1, s2, s3); }
static inline size_t   str_get_length(const char *s) { return xx_str_get_length(s); }
static inline size_t   str_len(const char *s) { return xx_str_len(s); }
static inline int      str_cmp(const char *a, const char *b) { return xx_str_cmp(a, b); }
static inline int      str_icmp(const char *a, const char *b) { return xx_str_icmp(a, b); }
static inline char*    str_dup(const char *s) { return xx_str_dup(s); }

static inline wchar_t* str_wcreate(const wchar_t *w) { return xx_str_wcreate(w); }
static inline wchar_t* str_create_w(const wchar_t *w) { return xx_str_wcreate(w); }
static inline wchar_t* str_create_wide(const wchar_t *w) { return xx_str_wcreate(w); }
static inline wchar_t* str_wcreate_len(size_t len) { return xx_str_wcreate_len(len); }
static inline void     str_wfree(wchar_t *w) { xx_str_wfree(w); }
static inline void     str_free_w(wchar_t *w) { xx_str_wfree(w); }
static inline void     str_free_wide(wchar_t *w) { xx_str_wfree(w); }
static inline wchar_t* str_wconcat(const wchar_t *w1, const wchar_t *w2) { return xx_str_wconcat(w1, w2); }
static inline wchar_t* str_concat_w(const wchar_t *w1, const wchar_t *w2) { return xx_str_wconcat(w1, w2); }
static inline wchar_t* str_wconcat3(const wchar_t *w1, const wchar_t *w2, const wchar_t *w3) { return xx_str_wconcat3(w1, w2, w3); }
static inline size_t   str_wget_length(const wchar_t *w) { return xx_str_wget_length(w); }
static inline size_t   str_get_length_w(const wchar_t *w) { return xx_str_wget_length(w); }
static inline size_t   str_get_length_wide(const wchar_t *w) { return xx_str_wget_length(w); }
static inline size_t   str_wlen(const wchar_t *w) { return xx_str_wlen(w); }
static inline int      str_wcmp(const wchar_t *a, const wchar_t *b) { return xx_str_wcmp(a, b); }
static inline int      str_wicmp(const wchar_t *a, const wchar_t *b) { return xx_str_wicmp(a, b); }
static inline wchar_t* str_wdup(const wchar_t *w) { return xx_str_wdup(w); }

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_H */

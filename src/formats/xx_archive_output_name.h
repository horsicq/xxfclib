/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_ARCHIVE_OUTPUT_NAME_H
#define XX_ARCHIVE_OUTPUT_NAME_H

/* Private filename boundary for legacy readers. Metadata retains the stored
 * name. Never consult the host locale: each reader supplies its wire charset.
 * %HH escapes are reversible (literal '%' and the generated-name marker '~'
 * are escaped too). Root stripping is an explicit reader policy, never the
 * default; a traversal component is rejected before any conversion.
 * CRT-free on every platform (xx_rt_* only): die_engine compiles it into the
 * /NODEFAULTLIB cdie console, where strcmp/snprintf are unresolved. */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/codepages/xx_codepage.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#ifdef _WIN32
#include <windows.h>
#endif

#define XX_ARCHIVE_NAME_ROOTED 1U
#define XX_ARCHIVE_NAME_DRIVE 2U
#define XX_ARCHIVE_NAME_NATIVE_TAR 4U
/* A DOS drive prefix becomes a folder named after its letter: "C:\WINDOWS\x"
 * and "C:/x" are written as "C_/WINDOWS/x" and "C_/x" (the reference
 * readers' layout; Inno Setup builds its names the same way). */
#define XX_ARCHIVE_NAME_DRIVE_FOLDER 8U

/* The ASCII letter of a "C:" drive prefix. */
static inline bool xx_archive_name_drive_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* A component that names the folder itself or its parent, or that Windows
 * aliases to one by dropping trailing dots and spaces: empty, or made only
 * of dots and spaces ("." "..", ".. ", ". .", "..."). */
static inline bool xx_archive_name_dot_component(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (s[i] != '.' && s[i] != ' ') return false;
    return true;
}

/* Member names of formats without folders, in place: '/', '\\' and ':'
 * become '_', and a name that is empty or only dots and spaces is made of
 * '_' ("..": "__", ".. ": "___"; Windows would trim a kept trailing space),
 * so every name is an ordinary file in the output folder. The buffer must
 * hold at least 2 bytes. */
static inline void xx_archive_flat_name(char *name)
{
    size_t i, n = xx_rt_strlen(name);
    for (i = 0; i < n; ++i)
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':') name[i] = '_';
    if (!n) {
        name[0] = '_';
        name[1] = 0;
    } else if (xx_archive_name_dot_component(name, n)) {
        for (i = 0; i < n; ++i) name[i] = '_';
    }
}

/* A relative path a reader built from folder components ('/' or '\\'
 * separated): no root, no drive prefix or ':' anywhere, no control
 * character and no empty, ".", ".." or other dot-and-space-only component. */
static inline bool xx_archive_relative_name_ok(const char *name)
{
    const char *start = name, *p;
    if (!name || !*name) return false;
    for (p = name;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!c || c == '/' || c == '\\') {
            if (xx_archive_name_dot_component(start, (size_t)(p - start))) return false;
            if (!c) return true;
            start = p + 1;
        } else if (c < 32 || c == 127 || c == ':') {
            return false;
        }
    }
}

static bool xx_archive_name_device(const char *s, size_t n)
{
    char b[9]; size_t i = 0;
    while (i < n && i < sizeof(b) - 1 && s[i] != '.') {
        unsigned char c = (unsigned char)s[i];
        b[i++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    while (i && (b[i - 1] == ' ' || b[i - 1] == '.')) --i;
    b[i] = 0;
    return !xx_rt_strcmp(b, "CON") || !xx_rt_strcmp(b, "PRN") || !xx_rt_strcmp(b, "AUX") || !xx_rt_strcmp(b, "NUL") ||
           !xx_rt_strcmp(b, "CLOCK$") || !xx_rt_strcmp(b, "CONIN$") || !xx_rt_strcmp(b, "CONOUT$") ||
           (i == 4 && (!xx_rt_memcmp(b, "COM", 3) || !xx_rt_memcmp(b, "LPT", 3)) && b[3] >= '1' && b[3] <= '9') ||
           (i == 5 && (!xx_rt_memcmp(b, "COM", 3) || !xx_rt_memcmp(b, "LPT", 3)) &&
               (unsigned char)b[3] == 0xc2 && ((unsigned char)b[4] == 0xb9 || (unsigned char)b[4] == 0xb2 || (unsigned char)b[4] == 0xb3));
}

static char *xx_archive_output_name(const char *raw, unsigned codepage, unsigned flags)
{
    const char *s = raw, *p, *start;
    char *out, *converted = NULL, drive = 0;
    size_t length, used, written, at = 0;
    bool converted_valid = true;
    const xx_codepage *utf8 = xx_codepage_by_number(65001), *cp = xx_codepage_by_number(codepage);
    static const char hex[] = "0123456789ABCDEF";
    if (!s || !*s) return NULL;
    /* UNC/device paths are never reinterpreted as archive-relative names. */
    if ((s[0] == '/' || s[0] == '\\') && (s[1] == '/' || s[1] == '\\')) return NULL;
    if (s[0] == '/' || s[0] == '\\') {
        if (!(flags & XX_ARCHIVE_NAME_ROOTED)) return NULL;
        ++s;
    }
    if (xx_archive_name_drive_letter(s[0]) && s[1] == ':') {
        if (!(flags & (XX_ARCHIVE_NAME_DRIVE | XX_ARCHIVE_NAME_DRIVE_FOLDER))) return NULL;
        if (flags & XX_ARCHIVE_NAME_DRIVE_FOLDER) drive = s[0];
        s += 2;
        if (*s == '/' || *s == '\\') ++s;
    }
    for (start = p = s;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c && c < 32) return NULL;
        if (c == 127) return NULL;
        if (!c || c == '/' || c == '\\') {
            if (p - start == 2 && start[0] == '.' && start[1] == '.') return NULL;
            if (!c) break;
            start = p + 1;
        }
    }
    length = xx_rt_strlen(s);
    if (length > (SIZE_MAX - 4) / 12) return NULL;
    converted = (char *)xx_mem_alloc(length * 4 + 1);
    if (!converted) return NULL;
    /* A valid UTF-8 string wins over a historical single-byte fallback. */
    if (utf8 && xx_codepage_to_utf8(utf8, (const unsigned char *)s, length,
            (unsigned char *)converted, length * 4, &used, &written) == XX_CODEPAGE_OK) {
        converted[written] = 0;
    } else if (cp && xx_codepage_to_utf8(cp, (const unsigned char *)s, length,
            (unsigned char *)converted, length * 4, &used, &written) == XX_CODEPAGE_OK) {
        converted[written] = 0;
    } else {
        /* Unknown/undefined historical bytes remain distinguishable. */
        xx_rt_memcpy(converted, s, length + 1);
        converted_valid = false;
    }
    s = converted;
    length = xx_rt_strlen(s);
    out = (char *)xx_mem_alloc(length * 3 + 4);
    if (!out) { xx_mem_free(converted); return NULL; }
    if (drive) { out[at++] = drive; out[at++] = '_'; }
    for (start = p = s;; ++p) {
        if (!*p || *p == '/' || *p == '\\') {
            size_t n = (size_t)(p - start), i;
            if (n && !(n == 1 && start[0] == '.')) {
                bool device = xx_archive_name_device(start, n);
                if (at) out[at++] = '/';
                for (i = 0; i < n; ++i) {
                    unsigned char c = (unsigned char)start[i];
                    bool escape = !(flags & XX_ARCHIVE_NAME_NATIVE_TAR) && (c == '%' || c == '~');
#ifdef _WIN32
                    escape = escape || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ||
                        ((c == '.' || c == ' ') && i + 1 == n) || (device && i == 0);
#else
                    (void)device;
#endif
                    /* If neither codec accepted the name, escape high bytes. */
                    if (c >= 128 && !converted_valid) escape = true;
                    if (escape) { out[at++] = '%'; out[at++] = hex[c >> 4]; out[at++] = hex[c & 15]; }
                    else out[at++] = (char)c;
                }
            }
            if (!*p) break;
            start = p + 1;
        }
    }
    xx_mem_free(converted);
    out[at] = 0;
    if (!at) { xx_mem_free(out); return NULL; }
    return out;
}

static inline bool xx_archive_output_equal_prefix(const char *name, const char *other)
{
#ifdef _WIN32
    wchar_t *a = xx_str_utf8_to_unicode(name), *b = xx_str_utf8_to_unicode(other);
    size_t n = a ? xx_str_wlen(a) : 0, m = b ? xx_str_wlen(b) : 0;
    bool same = a && b && n <= m && (!b[n] || b[n] == '/') &&
        CompareStringOrdinal(a, (int)n, b, (int)n, TRUE) == CSTR_EQUAL;
    xx_str_wfree(a); xx_str_wfree(b);
    return same;
#else
    size_t n = xx_rt_strlen(name), m = xx_rt_strlen(other);
    if (n > m || (other[n] && other[n] != '/')) return false;
    return !xx_rt_memcmp(name, other, n);
#endif
}

/* Stable suffixes cannot collide with literal archive names: '~' was escaped
 * above. The whole leaf is renamed when it would also be a directory. */
static inline bool xx_archive_output_suffix(char **name, size_t index)
{
    char suffix[40], *next;
    xx_rt_snprintf(suffix, sizeof(suffix), "~xx-%08llu", (unsigned long long)index);
    next = xx_str_concat(*name, suffix);
    if (!next) return false;
    xx_mem_free(*name); *name = next; return true;
}

#define XX_ARCHIVE_OUTPUT_RESOLVE(result, array, count, index, field, cp, flags) do { \
    size_t xx_oni; bool xx_oncollision = (count) > 2048; \
    for (xx_oni = 0; !xx_oncollision && (result) && xx_oni < (count); ++xx_oni) { \
        char *xx_onother; if (xx_oni == (index)) continue; \
        xx_onother = xx_archive_output_name((array)[xx_oni].field, cp, flags); \
        if (xx_onother && xx_archive_output_equal_prefix(result, xx_onother)) xx_oncollision = true; \
        xx_mem_free(xx_onother); \
    } \
    if (xx_oncollision && !xx_archive_output_suffix(&(result), index)) { xx_mem_free(result); (result) = NULL; } \
} while (0)

#endif

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

/* Serde_json-compatible diagnostics for the Cuckoo report parser (the
 * serde error texts of YARA-X 1.23.0's cuckoo module), included once from
 * xx_cuckoo_yx.c. Needs stdarg.h, stdio.h, stdlib.h, string.h and the
 * xx_json cursor from the including unit. */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Diagnostic-only JSON reader. The report reader keeps its existing storage
 * and validation; on failure this bounded tree retains token positions needed
 * for serde_json-compatible syntax and Cuckoo schema diagnostics. */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "xx_cuckoo_yx_debug_escape.inc"

typedef struct xx_cuckoo_yx_json_node {
    struct xx_cuckoo_yx_json_node *child, *next;
    char *key;
    size_t key_length; /* decoded key length; keys may embed NUL bytes */
    size_t key_end, start, end, count;
    unsigned char kind;
} xx_cuckoo_yx_json_node;

typedef struct {
    const uint8_t *data;
    size_t size, at, error_at;
    char message[768]; /* the message, or its prefix when full is set */
    char *full;        /* the whole message when it outgrew message[] */
} xx_cuckoo_yx_json_error;

/* Growable diagnostic text. serde prints the whole value, so nothing is
 * truncated; an allocation failure only marks the text failed. */
typedef struct {
    char *data;
    size_t length, capacity;
    bool failed;
} xx_cuckoo_yx_text_buf;

static void xx_cuckoo_yx_buf_put(xx_cuckoo_yx_text_buf *b, const char *bytes,
                                 size_t n)
{
    if (b->failed || !n) return;
    if (n >= b->capacity - b->length || !b->capacity) {
        size_t need = b->length + n + 1, next = b->capacity ? b->capacity : 128;
        char *grown;
        if (need <= b->length) { b->failed = true; return; }
        while (next < need) {
            if (next > SIZE_MAX / 2) { next = need; break; }
            next *= 2;
        }
        grown = (char *)realloc(b->data, next);
        if (!grown) { b->failed = true; return; }
        b->data = grown;
        b->capacity = next;
    }
    memcpy(b->data + b->length, bytes, n);
    b->length += n;
    b->data[b->length] = 0;
}

static void xx_cuckoo_yx_buf_str(xx_cuckoo_yx_text_buf *b, const char *text)
{
    xx_cuckoo_yx_buf_put(b, text, strlen(text));
}

/* Which typed context a value is being parsed in: serde reads the Cuckoo
 * structs field by field (numbers range-checked as they are read), the
 * network map through an intermediate serde_json::Value (every number in it
 * range-checked), and unknown fields of the derive structs not at all. */
typedef enum {
    XX_CJ_CTX_DOC,      /* the CuckooJson value: {network, behavior} */
    XX_CJ_CTX_NETWORK,  /* inside network's map: serde_json::Value context */
    XX_CJ_CTX_BEHAVIOR, /* inside behavior's value */
    XX_CJ_CTX_SUMMARY,  /* inside summary's value */
    XX_CJ_CTX_STICKY    /* checked regardless of keys */
} xx_cuckoo_yx_ctx;

static bool xx_cuckoo_yx_json_fail(xx_cuckoo_yx_json_error *p, size_t at,
                                const char *format, ...)
{
    if (!p->message[0]) {
        va_list args;
        p->error_at = at;
        va_start(args, format);
        vsnprintf(p->message, sizeof(p->message), format, args);
        va_end(args);
    }
    return false;
}

/* The same for a message of any length (it embeds the offending value). */
static bool xx_cuckoo_yx_json_fail_text(xx_cuckoo_yx_json_error *p, size_t at,
                                        const char *text, size_t length)
{
    if (!p->message[0] && length) {
        size_t prefix = length < sizeof(p->message) - 1 ? length : sizeof(p->message) - 1;
        p->error_at = at;
        memcpy(p->message, text, prefix);
        p->message[prefix] = 0;
        if (prefix < length) {
            /* Without memory the message keeps its prefix; the parse still
             * fails the same way. */
            p->full = (char *)malloc(length + 1);
            if (p->full) {
                memcpy(p->full, text, length);
                p->full[length] = 0;
            }
        }
    }
    return false;
}

static void xx_cuckoo_yx_json_free(xx_cuckoo_yx_json_node *v)
{
    while (v) {
        xx_cuckoo_yx_json_node *next = v->next;
        xx_cuckoo_yx_json_free(v->child);
        xx_str_free(v->key);
        free(v); v = next;
    }
}

static void xx_cuckoo_yx_json_space(xx_cuckoo_yx_json_error *p)
{
    while (p->at < p->size && strchr(" \t\r\n", p->data[p->at]) && p->data[p->at]) ++p->at;
}

/* Strict UTF-8 validation of a raw byte run, like bstr::decode_utf8. */
static bool xx_cuckoo_yx_json_utf8(const uint8_t *bytes, size_t size,
                                   size_t *bad)
{
    size_t i = 0;
    while (i < size) {
        uint8_t c = bytes[i];
        size_t width = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        size_t j;
        if (!width || width > size - i) { *bad = i; return false; }
        for (j = 1; j < width; ++j)
            if (bytes[i + j] < 0x80 || bytes[i + j] > 0xbf) { *bad = i + j; return false; }
        if ((c == 0xe0 && bytes[i + 1] < 0xa0) || (c == 0xed && bytes[i + 1] >= 0xa0) ||
            (c == 0xf0 && bytes[i + 1] < 0x90) || (c == 0xf4 && bytes[i + 1] >= 0x90)) {
            *bad = i + 1;
            return false;
        }
        i += width;
    }
    return true;
}

static bool xx_cuckoo_yx_json_hex4(xx_cuckoo_yx_json_error *p, uint32_t *value)
{
    uint32_t code = 0;
    unsigned i;
    for (i = 0; i < 4; ++i) {
        unsigned char digit;
        if (p->at == p->size)
            return xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing a string");
        digit = p->data[p->at++];
        if (digit >= '0' && digit <= '9') code = code * 16 + digit - '0';
        else if (digit >= 'a' && digit <= 'f') code = code * 16 + digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F') code = code * 16 + digit - 'A' + 10;
        else return xx_cuckoo_yx_json_fail(p, p->at, "invalid escape");
    }
    *value = code;
    return true;
}

static bool xx_cuckoo_yx_json_string(xx_cuckoo_yx_json_error *p)
{
    size_t segment = ++p->at;
    while (p->at < p->size) {
        unsigned char c = p->data[p->at++];
        if (c == '"') {
            size_t bad;
            if (!xx_cuckoo_yx_json_utf8(p->data + segment, p->at - 1 - segment,
                                        &bad))
                return xx_cuckoo_yx_json_fail(p, segment + bad + 1,
                                              "invalid unicode code point");
            return true;
        }
        if (c < 0x20)
            return xx_cuckoo_yx_json_fail(p, p->at, "control character (\\u0000-\\u001F) found while parsing a string");
        if (c == '\\') {
            size_t bad;
            if (!xx_cuckoo_yx_json_utf8(p->data + segment, p->at - 1 - segment,
                                        &bad))
                return xx_cuckoo_yx_json_fail(p, segment + bad + 1,
                                              "invalid unicode code point");
            if (p->at == p->size) break;
            c = p->data[p->at++];
            if (c == 'u') {
                unsigned value = 0;
                if (!xx_cuckoo_yx_json_hex4(p, &value)) return false;
                if (value >= 0xdc00 && value <= 0xdfff)
                    return xx_cuckoo_yx_json_fail(p, p->at, "lone leading surrogate in hex escape");
                if (value >= 0xd800 && value <= 0xdbff) {
                    unsigned low = 0;
                    if (p->size - p->at < 2 || p->data[p->at] != '\\' || p->data[p->at + 1] != 'u')
                        return xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), "unexpected end of hex escape");
                    p->at += 2;
                    if (!xx_cuckoo_yx_json_hex4(p, &low)) return false;
                    if (low < 0xdc00 || low > 0xdfff)
                        return xx_cuckoo_yx_json_fail(p, p->at, "lone leading surrogate in hex escape");
                }
            } else if (!strchr("\"\\/bfnrt", c))
                return xx_cuckoo_yx_json_fail(p, p->at, "invalid escape");
            segment = p->at;
        }
    }
    return xx_cuckoo_yx_json_fail(p, p->size, "EOF while parsing a string");
}

static bool xx_cuckoo_yx_json_number(xx_cuckoo_yx_json_error *p, bool checked)
{
    size_t start = p->at;
    if (p->data[p->at] == '-') ++p->at;
    if (p->at == p->size)
        return xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing a value");
    if (p->data[p->at] == '0') {
        ++p->at;
        if (p->at < p->size && p->data[p->at] >= '0' && p->data[p->at] <= '9')
            return xx_cuckoo_yx_json_fail(p, p->at + 1, "invalid number");
    } else if (p->data[p->at] >= '1' && p->data[p->at] <= '9') {
        do { ++p->at; } while (p->at < p->size && p->data[p->at] >= '0' && p->data[p->at] <= '9');
    } else return xx_cuckoo_yx_json_fail(p, p->at + 1, "invalid number");
    if (p->at < p->size && p->data[p->at] == '.') {
        ++p->at;
        if (p->at == p->size || p->data[p->at] < '0' || p->data[p->at] > '9')
            return xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), "invalid number");
        do { ++p->at; } while (p->at < p->size && p->data[p->at] >= '0' && p->data[p->at] <= '9');
    }
    if (p->at < p->size && (p->data[p->at] == 'e' || p->data[p->at] == 'E')) {
        ++p->at;
        if (p->at < p->size && (p->data[p->at] == '+' || p->data[p->at] == '-')) ++p->at;
        if (p->at == p->size || p->data[p->at] < '0' || p->data[p->at] > '9')
            return xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), "invalid number");
        do { ++p->at; } while (p->at < p->size && p->data[p->at] >= '0' && p->data[p->at] <= '9');
    }
    /* A number serde reads is parsed to i64/u64/f64; overflow is an error
     * even when the value is ignored afterwards. */
    if (checked && !xx_cuckoo_yx_number_in_range(p->data + start, p->at - start))
        return xx_cuckoo_yx_json_fail(p, p->at, "number out of range");
    return true;
}

/* Decode one scalar from a validated string token, advancing *at. */
static uint32_t xx_cuckoo_yx_json_unescape_scalar(xx_cuckoo_yx_json_error *p,
                                                  size_t *at)
{
    unsigned char c = p->data[(*at)++];
    if (c != '\\') {
        if (c < 0x80) return c;
        {
            size_t width = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
            uint32_t cp = c & (uint8_t)(width == 2 ? 0x1f : width == 3 ? 0x0f : 0x07);
            size_t j;
            for (j = 1; j < width; ++j)
                cp = (cp << 6) | (p->data[(*at)++] & 0x3f);
            return cp;
        }
    }
    c = p->data[(*at)++];
    if (c != 'u')
        return c == 'b' ? '\b' : c == 'f' ? '\f' : c == 'n' ? '\n' :
               c == 'r' ? '\r' : c == 't' ? '\t' : c;
    {
        uint32_t code = 0;
        unsigned k;
        for (k = 0; k < 4; ++k) {
            unsigned char d = p->data[(*at)++];
            code = (code << 4) | (d <= '9' ? (uint32_t)(d - '0') :
                  (uint32_t)(d <= 'F' ? d - 'A' + 10 : d - 'a' + 10));
        }
        if (code >= 0xd800 && code <= 0xdbff) {
            uint32_t low = 0;
            *at += 2; /* \u prefix of the low surrogate */
            for (k = 0; k < 4; ++k) {
                unsigned char d = p->data[(*at)++];
                low = (low << 4) | (d <= '9' ? (uint32_t)(d - '0') :
                      (uint32_t)(d <= 'F' ? d - 'A' + 10 : d - 'a' + 10));
            }
            code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
        }
        return code;
    }
}

static size_t xx_cuckoo_yx_utf8_encode(char *out, uint32_t cp)
{
    if (cp <= 0x7f) { out[0] = (char)cp; return 1; }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 63));
        out[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63));
    out[3] = (char)(0x80 | (cp & 63));
    return 4;
}

/* serde's Unexpected::Str: the whole decoded string with Rust's str Debug
 * escaping, appended to found. */
static void xx_cuckoo_yx_json_unexpected_string(xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, xx_cuckoo_yx_text_buf *found)
{
    size_t at = v->start + 1, stop = v->end - 1;
    xx_cuckoo_yx_buf_put(found, "\"", 1);
    while (at < stop && !found->failed) {
        uint32_t cp = xx_cuckoo_yx_json_unescape_scalar(p, &at);
        if (cp == '"') {
            xx_cuckoo_yx_buf_put(found, "\\\"", 2);
        } else if (cp == '\\') {
            xx_cuckoo_yx_buf_put(found, "\\\\", 2);
        } else if (cp == '\n') {
            xx_cuckoo_yx_buf_put(found, "\\n", 2);
        } else if (cp == '\r') {
            xx_cuckoo_yx_buf_put(found, "\\r", 2);
        } else if (cp == '\t') {
            xx_cuckoo_yx_buf_put(found, "\\t", 2);
        } else if (cp == 0) {
            xx_cuckoo_yx_buf_put(found, "\\0", 2);
        } else if (cp < 0x20 || cp == 0x7f || xx_cuckoo_yx_debug_is_escaped(cp)) {
            char escape[16];
            int n = snprintf(escape, sizeof(escape), "\\u{%x}", cp);
            if (n > 0 && (size_t)n < sizeof(escape))
                xx_cuckoo_yx_buf_put(found, escape, (size_t)n);
        } else if (cp < 0x80) {
            char ascii = (char)cp;
            xx_cuckoo_yx_buf_put(found, &ascii, 1);
        } else {
            char raw[4];
            xx_cuckoo_yx_buf_put(found, raw, xx_cuckoo_yx_utf8_encode(raw, cp));
        }
    }
    xx_cuckoo_yx_buf_put(found, "\"", 1);
}

/* A NUL-terminated copy of a number token: in stack when it fits, else on
 * the heap. NULL only when the heap copy cannot be allocated. */
static char *xx_cuckoo_yx_json_token(const xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, char *stack, size_t stack_size)
{
    size_t length = v->end - v->start;
    char *token = stack;
    if (length >= stack_size) {
        if (length == SIZE_MAX) return NULL;
        token = (char *)malloc(length + 1);
        if (!token) return NULL;
    }
    memcpy(token, p->data + v->start, length);
    token[length] = 0;
    return token;
}

/* serde's Unexpected for numbers: the decoded value, widening to f64 with
 * Rust's {:?} layout when serde_json can no longer keep the token integral.
 * Appends to found; false when the value overflows f64 (no rendering). */
static bool xx_cuckoo_yx_json_unexpected_number(xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, xx_cuckoo_yx_text_buf *found)
{
    char stack[384], *token, *end;
    bool floating = false, rendered = false;
    size_t i;
    for (i = v->start; i < v->end; ++i)
        if (p->data[i] == '.' || p->data[i] == 'e' || p->data[i] == 'E') floating = true;
    token = xx_cuckoo_yx_json_token(p, v, stack, sizeof(stack));
    if (!token) { found->failed = true; return true; }
    if (!floating && token[0] != '-') {
        errno = 0;
        (void)strtoull(token, &end, 10);
        if (!errno && !*end) {
            /* a full u64 consume is at most 20 digits */
            xx_cuckoo_yx_buf_str(found, "integer `");
            xx_cuckoo_yx_buf_str(found, token);
            xx_cuckoo_yx_buf_str(found, "`");
            rendered = true;
        }
    } else if (!floating) {
        long long parsed;
        errno = 0;
        parsed = strtoll(token, &end, 10);
        /* serde_json keeps -0 as an f64, not an integer */
        if (!errno && !*end && parsed != 0) {
            xx_cuckoo_yx_buf_str(found, "integer `");
            xx_cuckoo_yx_buf_str(found, token);
            xx_cuckoo_yx_buf_str(found, "`");
            rendered = true;
        }
    }
    if (!rendered) {
        double value = xx_rt_strtod(token, NULL);
        if (!xx_rt_isinf(value)) {
            /* Rust's {:?} f64 is at most "-1.7976931348623157e+308" */
            char number[32];
            xx_rt_dtoa_debug(value, number, sizeof(number));
            xx_cuckoo_yx_buf_str(found, "floating point `");
            xx_cuckoo_yx_buf_str(found, number);
            xx_cuckoo_yx_buf_str(found, "`");
            rendered = true;
        }
    }
    if (token != stack) free(token);
    return rendered;
}

static xx_cuckoo_yx_json_node *xx_cuckoo_yx_json_value(xx_cuckoo_yx_json_error *p, unsigned depth, bool checked, xx_cuckoo_yx_ctx ctx)
{
    xx_cuckoo_yx_json_node *v, **tail;
    xx_cuckoo_yx_json_space(p);
    if (p->at == p->size) { xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing a value"); return NULL; }
    if (depth >= 128) { xx_cuckoo_yx_json_fail(p, p->at + 1, "recursion limit exceeded"); return NULL; }
    v = (xx_cuckoo_yx_json_node *)calloc(1, sizeof(*v));
    if (!v) { xx_cuckoo_yx_json_fail(p, p->at, "out of memory"); return NULL; }
    v->start = p->at; v->kind = p->data[p->at]; tail = &v->child;
    if (v->kind == '{' || v->kind == '[') {
        bool object = v->kind == '{';
        char close = object ? '}' : ']';
        size_t index = 0;
        ++p->at; xx_cuckoo_yx_json_space(p);
        if (p->at < p->size && p->data[p->at] == close) { ++p->at; goto done; }
        for (;;) {
            xx_cuckoo_yx_json_node *child;
            char *key = NULL;
            size_t key_length = 0, key_end = 0, key_start = 0;
            bool child_checked = checked;
            xx_cuckoo_yx_ctx child_ctx = ctx;
            if (p->at == p->size) {
                xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing %s", object ? "an object" : "a list"); break;
            }
            if (object) {
                xx_json key_cursor;
                if (p->data[p->at] != '"') {
                    xx_cuckoo_yx_json_fail(p, p->at + 1, "key must be a string"); break;
                }
                key_start = p->at;
                xx_json_init(&key_cursor, p->data, p->size); key_cursor.position = p->at;
                if (!xx_cuckoo_yx_json_string(p)) break;
                key_end = p->at;
                if (!xx_json_string(&key_cursor, &key)) { xx_cuckoo_yx_json_fail(p, p->at, "invalid unicode code point"); break; }
                key_length = key_end - key_start >= 2 ? xx_cuckoo_yx_decoded_length(p->data + key_start, key_end - key_start) : 0;
                xx_cuckoo_yx_json_space(p);
                if (p->at == p->size || p->data[p->at] != ':') {
                    xx_str_free(key);
                    xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), p->at == p->size ? "EOF while parsing an object" : "expected `:`"); break;
                }
                ++p->at;
                /* Unknown fields of the derive structs are skipped unread,
                 * with everything below them (an unchecked sticky context:
                 * a nested "network" there is just data); known ones are
                 * read and range-checked, and everything in
                 * the network map is read through a serde_json::Value.
                 * Matching is length-aware: a key spelled "behavior\u0000"
                 * is a different, unknown field. */
                if (ctx == XX_CJ_CTX_DOC) {
                    if (key_length == 7 && memcmp(key, "network", 7) == 0) { child_ctx = XX_CJ_CTX_NETWORK; child_checked = true; }
                    else if (key_length == 8 && memcmp(key, "behavior", 8) == 0) { child_ctx = XX_CJ_CTX_BEHAVIOR; child_checked = true; }
                    else { child_checked = false; child_ctx = XX_CJ_CTX_STICKY; }
                } else if (ctx == XX_CJ_CTX_BEHAVIOR) {
                    if (key_length == 7 && memcmp(key, "summary", 7) == 0) { child_ctx = XX_CJ_CTX_SUMMARY; child_checked = true; }
                    else { child_checked = false; child_ctx = XX_CJ_CTX_STICKY; }
                } else if (ctx == XX_CJ_CTX_SUMMARY) {
                    child_checked = (key_length == 7 && memcmp(key, "mutexes", 7) == 0) ||
                                    (key_length == 5 && memcmp(key, "files", 5) == 0) ||
                                    (key_length == 4 && memcmp(key, "keys", 4) == 0);
                    child_ctx = XX_CJ_CTX_STICKY;
                } else {
                    child_checked = checked;
                    child_ctx = ctx == XX_CJ_CTX_NETWORK ? XX_CJ_CTX_NETWORK : XX_CJ_CTX_STICKY;
                }
            } else {
                /* Sequence elements map to struct fields in declaration
                 * order; trailing elements are never read. */
                if (ctx == XX_CJ_CTX_DOC) {
                    child_checked = index < 2;
                    child_ctx = index == 0 ? XX_CJ_CTX_NETWORK : index == 1 ? XX_CJ_CTX_BEHAVIOR : XX_CJ_CTX_STICKY;
                } else if (ctx == XX_CJ_CTX_BEHAVIOR) {
                    child_checked = index < 1;
                    child_ctx = index < 1 ? XX_CJ_CTX_SUMMARY : XX_CJ_CTX_STICKY;
                } else if (ctx == XX_CJ_CTX_SUMMARY) {
                    child_checked = index < 3;
                    child_ctx = XX_CJ_CTX_STICKY;
                } else {
                    child_checked = checked;
                    child_ctx = ctx == XX_CJ_CTX_NETWORK ? XX_CJ_CTX_NETWORK : XX_CJ_CTX_STICKY;
                }
            }
            child = xx_cuckoo_yx_json_value(p, depth + 1, child_checked, child_ctx);
            if (!child) { xx_str_free(key); break; }
            child->key = key; child->key_length = key_length; child->key_end = key_end;
            *tail = child; tail = &child->next; ++v->count; ++index;
            if (p->message[0]) break;
            xx_cuckoo_yx_json_space(p);
            if (p->at == p->size) { xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing %s", object ? "an object" : "a list"); break; }
            if (p->data[p->at] == close) { ++p->at; goto done; }
            if (p->data[p->at] != ',') {
                xx_cuckoo_yx_json_fail(p, p->at + 1, "expected `,` or `%c`", close); break;
            }
            ++p->at; xx_cuckoo_yx_json_space(p);
            if (object && p->at < p->size && p->data[p->at] == '}') {
                xx_cuckoo_yx_json_fail(p, p->at + 1, "trailing comma"); break;
            }
        }
        return v;
    } else if (v->kind == '"') {
        if (!xx_cuckoo_yx_json_string(p)) return v;
    } else if (v->kind == '-' || (v->kind >= '0' && v->kind <= '9')) {
        if (!xx_cuckoo_yx_json_number(p, checked)) return v;
    } else if (v->kind == 't' || v->kind == 'f' || v->kind == 'n') {
        const char *word = v->kind == 't' ? "true" : v->kind == 'f' ? "false" : "null";
        for (size_t i = 0; word[i]; ++i) {
            if (p->at == p->size || p->data[p->at] != (unsigned char)word[i]) {
                xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), p->at == p->size ? "EOF while parsing a value" : "expected ident"); return v;
            }
            ++p->at;
        }
    } else { xx_cuckoo_yx_json_fail(p, p->at + 1, "expected value"); return v; }
done:
    v->end = p->at;
    return v;
}

typedef enum {
    YX_CJ_ANY, YX_CJ_ROOT, YX_CJ_NETWORK, YX_CJ_BEHAVIOR, YX_CJ_SUMMARY,
    YX_CJ_HTTP, YX_CJ_TCP, YX_CJ_UDP, YX_CJ_DOMAIN, YX_CJ_OLD_DOMAIN,
    YX_CJ_STRING, YX_CJ_U64, YX_CJ_STRINGS, YX_CJ_HTTPS, YX_CJ_TCPS,
    YX_CJ_UDPS, YX_CJ_DOMAINS, YX_CJ_OLD_DOMAINS
} xx_cuckoo_yx_json_shape;

static bool xx_cuckoo_yx_json_key_is(const xx_cuckoo_yx_json_node *child,
                                     const char *expected)
{
    size_t length = strlen(expected);
    return child->key && child->key_length == length &&
           memcmp(child->key, expected, length) == 0;
}

/* Whether a later sibling carries the same key (allocation-free). */
static bool xx_cuckoo_yx_json_replaced_later(const xx_cuckoo_yx_json_node *child)
{
    const xx_cuckoo_yx_json_node *later;
    if (!child->key) return false;
    for (later = child->next; later; later = later->next)
        if (later->key && later->key_length == child->key_length &&
            !memcmp(later->key, child->key, child->key_length)) return true;
    return false;
}

static bool xx_cuckoo_yx_json_type_error(xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, const char *expected, size_t at, bool value_error)
{
    xx_cuckoo_yx_text_buf text = {0};
    if (!v->end) return true;
    xx_cuckoo_yx_buf_str(&text, value_error ? "invalid value: " : "invalid type: ");
    if (v->kind == 'n') xx_cuckoo_yx_buf_str(&text, "null");
    else if (v->kind == '{') xx_cuckoo_yx_buf_str(&text, "map");
    else if (v->kind == '[') xx_cuckoo_yx_buf_str(&text, "sequence");
    else if (v->kind == 't' || v->kind == 'f') xx_cuckoo_yx_buf_str(&text, v->kind == 't' ? "boolean `true`" : "boolean `false`");
    else if (v->kind == '"') {
        xx_cuckoo_yx_buf_str(&text, "string ");
        xx_cuckoo_yx_json_unexpected_string(p, v, &text);
    } else if (!xx_cuckoo_yx_json_unexpected_number(p, v, &text))
        xx_cuckoo_yx_buf_str(&text, "number");
    xx_cuckoo_yx_buf_str(&text, ", expected ");
    xx_cuckoo_yx_buf_str(&text, expected);
    if (text.failed)
        xx_cuckoo_yx_json_fail(p, at ? at : v->end, "out of memory");
    else
        xx_cuckoo_yx_json_fail_text(p, at ? at : v->end, text.data, text.length);
    free(text.data);
    return false;
}

static bool xx_cuckoo_yx_json_validate(xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, xx_cuckoo_yx_json_shape shape, bool optional, size_t at)
{
    static const char *names[] = {"", "CuckooJson", "", "BehaviorJson", "SummaryJson", "HttpJson", "TcpJson", "UdpJson", "DomainJson", "OldDomainJson"};
    const xx_cuckoo_yx_json_node *selected = NULL, *old_dns = NULL;
    unsigned seen = 0, fields;
    if (!v || shape == YX_CJ_ANY || (optional && v->kind == 'n')) return true;
    if (shape >= YX_CJ_STRINGS) {
        xx_cuckoo_yx_json_shape item_shape = shape == YX_CJ_STRINGS ? YX_CJ_STRING :
            shape == YX_CJ_HTTPS ? YX_CJ_HTTP : shape == YX_CJ_TCPS ? YX_CJ_TCP :
            shape == YX_CJ_UDPS ? YX_CJ_UDP : shape == YX_CJ_DOMAINS ? YX_CJ_DOMAIN : YX_CJ_OLD_DOMAIN;
        if (v->kind != '[') return xx_cuckoo_yx_json_type_error(p, v, "a sequence", at, false);
        for (const xx_cuckoo_yx_json_node *child = v->child; child; child = child->next)
            if (!xx_cuckoo_yx_json_validate(p, child, item_shape, false, at)) return false;
        return true;
    }
    if (shape == YX_CJ_STRING)
        return v->kind == '"' || xx_cuckoo_yx_json_type_error(p, v, "a string", at, false);
    if (shape == YX_CJ_U64) {
        char stack[96], *number, *end;
        bool integer = (v->kind >= '0' && v->kind <= '9') || v->kind == '-';
        if (!v->end) return true;
        for (size_t i = v->start; i < v->end; ++i)
            if (p->data[i] == '.' || p->data[i] == 'e' || p->data[i] == 'E') integer = false;
        if (integer) {
            bool negative = v->kind == '-';
            number = xx_cuckoo_yx_json_token(p, v, stack, sizeof(stack));
            if (!number) return xx_cuckoo_yx_json_fail(p, at ? at : v->end, "out of memory");
            errno = 0;
            if (!negative) {
                (void)strtoull(number, &end, 10);
                if (!errno && !*end) { if (number != stack) free(number); return true; }
            } else {
                long long signed_value = strtoll(number, &end, 10);
                /* A negative integer Value is an invalid value; -0 and
                 * i64 overflow are f64 Values, an invalid type. */
                if (!errno && !*end && signed_value != 0) {
                    if (number != stack) free(number);
                    return xx_cuckoo_yx_json_type_error(p, v, "u64", at, true);
                }
            }
            /* serde's Value holds an f64 once the token leaves i64/u64. */
            {
                char found[96];
                double value = xx_rt_strtod(number, NULL);
                if (!xx_rt_isinf(value)) {
                    char rendered[32];
                    xx_rt_dtoa_debug(value, rendered, sizeof(rendered));
                    snprintf(found, sizeof(found), "floating point `%.*s`", 31, rendered);
                } else snprintf(found, sizeof(found), "integer `%.*s`", 24, number);
                if (number != stack) free(number);
                return xx_cuckoo_yx_json_fail(p, at ? at : v->end, "invalid %s: %s, expected %s", "type", found, "u64");
            }
        }
        /* Float tokens and huge integers reach serde as f64 Values, so the
         * mismatch is a type error; only i64-range negative integers were
         * an invalid value. */
        return xx_cuckoo_yx_json_type_error(p, v, "u64", at, false);
    }
    fields = shape == YX_CJ_ROOT ? 2 : shape == YX_CJ_BEHAVIOR || shape == YX_CJ_DOMAIN || shape == YX_CJ_OLD_DOMAIN ? 1 : 3;
    if (v->kind == '[' && shape != YX_CJ_NETWORK) {
        const xx_cuckoo_yx_json_node *child = v->child;
        size_t i = 0;
        for (; child && i < fields; child = child->next, ++i) {
            xx_cuckoo_yx_json_shape child_shape = shape == YX_CJ_ROOT ? (i ? YX_CJ_BEHAVIOR : YX_CJ_NETWORK) :
                shape == YX_CJ_BEHAVIOR ? YX_CJ_SUMMARY : shape == YX_CJ_SUMMARY ? YX_CJ_STRINGS :
                (shape == YX_CJ_TCP || shape == YX_CJ_UDP) && i == 2 ? YX_CJ_U64 : YX_CJ_STRING;
            if (!xx_cuckoo_yx_json_validate(p, child, child_shape, true, at)) return false;
        }
        if (v->end && v->count < fields)
            return xx_cuckoo_yx_json_fail(p, at ? at : v->end, "invalid length %zu, expected struct %s with %u element%s", v->count, names[shape], fields, fields == 1 ? "" : "s");
        if (child) {
            /* Network fields are deserialized from serde_json::Value; the
             * other structs read directly from the JSON input stream. */
            if (at)
                return xx_cuckoo_yx_json_fail(p, at, "invalid length %zu, expected fewer elements in array", v->count);
            if (shape == YX_CJ_ROOT || shape == YX_CJ_BEHAVIOR || shape == YX_CJ_SUMMARY)
                return xx_cuckoo_yx_json_fail(p, child->start + 1, "trailing characters");
        }
        return true;
    }
    if (v->kind != '{') {
        char expected[80];
        snprintf(expected, sizeof(expected), shape == YX_CJ_NETWORK ? "string or object" : "struct %s", names[shape]);
        return xx_cuckoo_yx_json_type_error(p, v, expected, at, false);
    }
    {
        /* serde_json::Value maps keep only the final duplicate field: a
         * field is replaced when a later sibling carries the same key.
         * A hash pre-pass keeps the duplicate resolution linear (each slot
         * remembers the latest sibling with its key and marks the one it
         * displaces) while the validation pass still runs in document
         * order. Without memory for it, each field scans its later
         * siblings instead: slower, same answer. */
        unsigned char *replaced = NULL;
        size_t child_count = v->count, ci = 0;
        if (at && child_count) {
            size_t capacity = 16;
            while (capacity < child_count * 2) capacity *= 2;
            replaced = (unsigned char *)calloc(child_count, 1);
            if (replaced) {
                const xx_cuckoo_yx_json_node **slots =
                    (const xx_cuckoo_yx_json_node **)calloc(capacity, sizeof(*slots));
                size_t *indexes = (size_t *)calloc(capacity, sizeof(*indexes));
                if (slots && indexes) {
                    for (const xx_cuckoo_yx_json_node *child = v->child; child; child = child->next, ++ci) {
                        size_t probe, mask = capacity - 1;
                        uint32_t hash = 2166136261u;
                        size_t k;
                        if (!child->key) continue;
                        for (k = 0; k < child->key_length; ++k)
                            hash = (hash ^ (unsigned char)child->key[k]) * 16777619u;
                        probe = hash & mask;
                        while (slots[probe] &&
                               !(slots[probe]->key_length == child->key_length &&
                                 !memcmp(slots[probe]->key, child->key, child->key_length)))
                            probe = (probe + 1) & mask;
                        if (slots[probe]) replaced[indexes[probe]] = 1;
                        slots[probe] = child;
                        indexes[probe] = ci;
                    }
                } else {
                    free(replaced);
                    replaced = NULL;
                }
                free((void *)slots); free(indexes);
            }
        }
        ci = 0;
        for (const xx_cuckoo_yx_json_node *child = v->child; child; child = child->next, ++ci) {
            const char *key = child->key ? child->key : "";
            size_t key_length = child->key ? child->key_length : 0;
            xx_cuckoo_yx_json_shape child_shape = YX_CJ_ANY;
            unsigned bit = 0;
            size_t child_at = at;
            if (shape == YX_CJ_ROOT) {
                if (xx_cuckoo_yx_json_key_is(child, "network")) { child_shape = YX_CJ_NETWORK; bit = 1; }
                if (xx_cuckoo_yx_json_key_is(child, "behavior")) { child_shape = YX_CJ_BEHAVIOR; bit = 2; }
            } else if (shape == YX_CJ_BEHAVIOR) {
                if (xx_cuckoo_yx_json_key_is(child, "summary")) { child_shape = YX_CJ_SUMMARY; bit = 1; }
            } else if (shape == YX_CJ_SUMMARY) {
                if (xx_cuckoo_yx_json_key_is(child, "mutexes")) { child_shape = YX_CJ_STRINGS; bit = 1; }
                if (xx_cuckoo_yx_json_key_is(child, "files")) { child_shape = YX_CJ_STRINGS; bit = 2; }
                if (xx_cuckoo_yx_json_key_is(child, "keys")) { child_shape = YX_CJ_STRINGS; bit = 4; }
            } else if (shape == YX_CJ_NETWORK) {
                if (xx_cuckoo_yx_json_key_is(child, "domains")) { selected = child; continue; }
                if (xx_cuckoo_yx_json_key_is(child, "dns")) { old_dns = child; continue; }
                if (xx_cuckoo_yx_json_key_is(child, "http")) child_shape = YX_CJ_HTTPS;
                if (xx_cuckoo_yx_json_key_is(child, "tcp")) child_shape = YX_CJ_TCPS;
                if (xx_cuckoo_yx_json_key_is(child, "udp")) child_shape = YX_CJ_UDPS;
                if (xx_cuckoo_yx_json_key_is(child, "hosts")) child_shape = YX_CJ_STRINGS;
                child_at = child->end;
                if (child_at) {
                    while (child_at < p->size && p->data[child_at] &&
                           strchr(" \t\r\n", p->data[child_at])) ++child_at;
                    if (child_at < p->size && p->data[child_at] == '}') ++child_at;
                }
            } else if (shape == YX_CJ_HTTP) {
                if (xx_cuckoo_yx_json_key_is(child, "user-agent")) { child_shape = YX_CJ_STRING; bit = 1; }
                if (xx_cuckoo_yx_json_key_is(child, "method")) { child_shape = YX_CJ_STRING; bit = 2; }
                if (xx_cuckoo_yx_json_key_is(child, "uri")) { child_shape = YX_CJ_STRING; bit = 4; }
            } else if (shape == YX_CJ_TCP || shape == YX_CJ_UDP) {
                if (xx_cuckoo_yx_json_key_is(child, "dst")) { child_shape = YX_CJ_STRING; bit = 1; }
                if (xx_cuckoo_yx_json_key_is(child, "dst_domain")) { child_shape = YX_CJ_STRING; bit = 2; }
                if (xx_cuckoo_yx_json_key_is(child, "dport")) { child_shape = YX_CJ_U64; bit = 4; }
            } else if (shape == YX_CJ_DOMAIN || shape == YX_CJ_OLD_DOMAIN) {
                if (xx_cuckoo_yx_json_key_is(child, shape == YX_CJ_DOMAIN ? "domain" : "hostname")) { child_shape = YX_CJ_STRING; bit = 1; }
            }
            /* serde_json::Value maps keep only the final duplicate field. */
            if (at) {
                if (replaced ? replaced[ci] != 0 : xx_cuckoo_yx_json_replaced_later(child)) continue;
            } else if (bit && (seen & bit)) {
                char duplicate[320];
                size_t key_print = key_length < 300 ? key_length : 300;
                snprintf(duplicate, sizeof(duplicate), "duplicate field `%.*s`", (int)key_print, key);
                free(replaced);
                return xx_cuckoo_yx_json_fail(p, child->key_end, "%s", duplicate);
            }
            seen |= bit;
            if (!xx_cuckoo_yx_json_validate(p, child, child_shape, true, child_at)) { free(replaced); return false; }
        }
        free(replaced);
    }
    if (shape == YX_CJ_NETWORK && v->end) {
        if (selected) return xx_cuckoo_yx_json_validate(p, selected, YX_CJ_DOMAINS, true, v->end);
        if (old_dns) return xx_cuckoo_yx_json_validate(p, old_dns, YX_CJ_OLD_DOMAINS, true, v->end);
    }
    return true;
}

static void xx_cuckoo_yx_json_diagnostic(const uint8_t *data, size_t size, char *error, size_t capacity)
{
    xx_cuckoo_yx_json_error syntax = {0}, typed;
    xx_cuckoo_yx_json_node *root;
    size_t line = 1, column = 0;
    syntax.data = data; syntax.size = size;
    root = xx_cuckoo_yx_json_value(&syntax, 0, true, XX_CJ_CTX_DOC);
    if (!syntax.message[0]) {
        xx_cuckoo_yx_json_space(&syntax);
        if (syntax.at < size) xx_cuckoo_yx_json_fail(&syntax, syntax.at + 1, "trailing characters");
    }
    typed = syntax; typed.message[0] = 0; typed.full = NULL;
    if (xx_cuckoo_yx_json_validate(&typed, root, YX_CJ_ROOT, false, 0) ||
        (syntax.message[0] && typed.error_at > syntax.error_at)) {
        free(typed.full);
        typed = syntax;
        syntax.full = NULL; /* now owned by typed */
    }
    if (typed.message[0]) {
        const char *message = typed.full ? typed.full : typed.message;
        size_t length = strlen(message), copied;
        for (size_t i = 0; i < typed.error_at && i < size; ++i) {
            if (data[i] == '\n') { ++line; column = 0; }
            else ++column;
        }
        /* Copy the message itself, then the position; only the caller's
         * capacity truncates (no printf of an arbitrarily long %s). */
        copied = length < capacity - 1 ? length : capacity - 1;
        memcpy(error, message, copied);
        error[copied] = 0;
        if (copied < capacity - 1)
            snprintf(error + copied, capacity - copied, " at line %zu column %zu", line, column);
    } else error[0] = 0;
    free(typed.full);
    free(syntax.full);
    xx_cuckoo_yx_json_free(root);
}

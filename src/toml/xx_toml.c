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

#include <xxfclib/toml/xx_toml.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TOML 1.0 document parsing. Syntax is parsed independently of the CLI
 * configuration schema, so syntax errors take precedence over type errors. */
typedef struct xx_toml_parser {
    const char *source;
    size_t at, length;
    unsigned depth;
    bool failed;
    xx_toml_error *error;
    char *table_path;
} xx_toml_parser;

typedef struct xx_toml_path {
    char **keys;
    size_t *lengths;
    size_t count;
} xx_toml_path;

static bool xx_toml_fail(xx_toml_parser *p, size_t at, const char *format, ...)
{
    if (!p->failed) {
        va_list args, copy;
        int length;
        p->failed = true;
        p->error->offset = at;
        p->error->length = at < p->length ? 1 : 0;
        if (at < p->length && (unsigned char)p->source[at] >= 0xc0) {
            unsigned char first = (unsigned char)p->source[at];
            p->error->length = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        }
        va_start(args, format);
        va_copy(copy, args);
        length = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (length >= 0) {
            p->error->message = (char *)malloc((size_t)length + 1);
            if (p->error->message)
                vsnprintf(p->error->message, (size_t)length + 1, format, args);
        }
        va_end(args);
    }
    return false;
}

void xx_toml_error_clear(xx_toml_error *error)
{
    free(error->message);
    memset(error, 0, sizeof(*error));
}

static void xx_toml_error_replace(xx_toml_parser *p, const char *message)
{
    size_t length = strlen(message);
    char *copy = (char *)malloc(length + 1);
    if (!copy) return;
    memcpy(copy, message, length + 1);
    free(p->error->message);
    p->error->message = copy;
}

static void xx_toml_header_error(xx_toml_parser *p)
{
    static const char prefix[] = "invalid table header\n";
    size_t length = p->error->message ? strlen(p->error->message) : 0;
    char *message = (char *)malloc(sizeof(prefix) + length);
    if (!message) return;
    memcpy(message, prefix, sizeof(prefix) - 1);
    if (length) memcpy(message + sizeof(prefix) - 1, p->error->message, length);
    message[sizeof(prefix) - 1 + length] = 0;
    free(p->error->message);
    p->error->message = message;
}

static char xx_toml_peek(const xx_toml_parser *p)
{
    return p->at < p->length ? p->source[p->at] : 0;
}

static bool xx_toml_has(const xx_toml_parser *p, const char *s)
{
    size_t n = strlen(s);
    return n <= p->length - p->at && memcmp(p->source + p->at, s, n) == 0;
}

static char *xx_toml_copy(const char *s, size_t n)
{
    char *copy = (char *)malloc(n + 1);
    if (copy) { memcpy(copy, s, n); copy[n] = 0; }
    return copy;
}

static xx_toml_value *xx_toml_new(xx_toml_parser *p, xx_toml_kind kind)
{
    xx_toml_value *v = (xx_toml_value *)calloc(1, sizeof(*v));
    if (!v) xx_toml_fail(p, p->at, "out of memory");
    else { v->kind = kind; v->offset = p->at; }
    return v;
}

void xx_toml_free(xx_toml_value *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->count; ++i) xx_toml_free(v->children[i]);
    free(v->children); free(v->key); free(v->string); free(v);
}

static bool xx_toml_push(xx_toml_parser *p, xx_toml_value *parent,
                         xx_toml_value *v)
{
    xx_toml_value **items;
    if (parent->count >= SIZE_MAX / sizeof(*items) - 1)
        return xx_toml_fail(p, p->at, "out of memory");
    items = (xx_toml_value **)realloc(parent->children,
                                     (parent->count + 1) * sizeof(*items));
    if (!items) return xx_toml_fail(p, p->at, "out of memory");
    parent->children = items;
    parent->children[parent->count++] = v;
    return true;
}

static xx_toml_value *xx_toml_lookup(const xx_toml_value *v,
                                      const char *key, size_t length)
{
    if (!v || v->kind != XX_TOML_TABLE) return NULL;
    for (size_t i = 0; i < v->count; ++i)
        if (v->children[i]->key_length == length &&
            memcmp(v->children[i]->key, key, length) == 0)
            return v->children[i];
    return NULL;
}

const xx_toml_value *xx_toml_get(const xx_toml_value *v, const char *key)
{
    return xx_toml_lookup(v, key, strlen(key));
}

static void xx_toml_ws(xx_toml_parser *p)
{
    while (xx_toml_peek(p) == ' ' || xx_toml_peek(p) == '\t') ++p->at;
}

static bool xx_toml_newline(xx_toml_parser *p)
{
    if (xx_toml_has(p, "\r\n")) { p->at += 2; return true; }
    if (xx_toml_peek(p) == '\n') { ++p->at; return true; }
    return false;
}

static bool xx_toml_comment(xx_toml_parser *p)
{
    if (xx_toml_peek(p) != '#') return true;
    while (p->at < p->length) {
        unsigned char c = (unsigned char)p->source[p->at];
        if (c == '\n' || (c == '\r' && xx_toml_has(p, "\r\n"))) break;
        if ((c < 0x20 && c != '\t') || c == 0x7f)
            return xx_toml_fail(p, p->at, "%s", "");
        ++p->at;
    }
    return true;
}

static bool xx_toml_trivia(xx_toml_parser *p)
{
    for (;;) {
        xx_toml_ws(p);
        if (!xx_toml_comment(p)) return false;
        if (!xx_toml_newline(p)) return true;
    }
}

static bool xx_toml_line_end(xx_toml_parser *p)
{
    xx_toml_ws(p);
    if (!xx_toml_comment(p)) return false;
    if (p->at == p->length || xx_toml_newline(p)) return true;
    return xx_toml_fail(p, p->at, "expected newline, `#`");
}

static int xx_toml_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t xx_toml_utf8(char *out, uint32_t cp)
{
    if (cp <= 0x7f) { out[0] = (char)cp; return 1; }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 63)); return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 63));
        out[2] = (char)(0x80 | (cp & 63)); return 3;
    }
    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63));
    out[3] = (char)(0x80 | (cp & 63)); return 4;
}

static char *xx_toml_string(xx_toml_parser *p, bool key, size_t *length)
{
    char quote = xx_toml_peek(p);
    bool multi = !key && p->length - p->at >= 3 &&
        p->source[p->at + 1] == quote && p->source[p->at + 2] == quote;
    const char *label = quote == '\'' ?
        (multi ? "multiline literal string" : "literal string") :
        (multi ? "multiline basic string" : "basic string");
    char *out = (char *)malloc(p->length - p->at + 1);
    size_t used = 0;
    if (!out) { xx_toml_fail(p, p->at, "out of memory"); return NULL; }
    p->at += multi ? 3 : 1;
    if (multi) (void)xx_toml_newline(p);
    while (p->at < p->length) {
        unsigned char c = (unsigned char)p->source[p->at++];
        if (c == (unsigned char)quote) {
            size_t run = 1;
            if (multi) {
                while (p->at < p->length && p->source[p->at] == quote && run < 5) {
                    ++p->at; ++run;
                }
                if (run < 3) {
                    if (p->at == p->length) {
                        xx_toml_fail(p, p->at - run, "invalid %s", label);
                        goto failed;
                    }
                    while (run--) out[used++] = quote;
                    continue;
                }
                for (size_t i = 3; i < run; ++i) out[used++] = quote;
            }
            out[used] = 0; *length = used; return out;
        }
        if (c == '\\' && quote == '"') {
            if (multi) {
                size_t saved = p->at;
                xx_toml_ws(p);
                if (xx_toml_newline(p)) {
                    do { xx_toml_ws(p); } while (xx_toml_newline(p));
                    continue;
                }
                p->at = saved;
            }
            if (p->at == p->length) {
                xx_toml_fail(p, p->at - 1, "invalid %s", label); break;
            }
            c = (unsigned char)p->source[p->at++];
            switch (c) {
            case 'b': out[used++] = '\b'; break;
            case 'f': out[used++] = '\f'; break;
            case 'n': out[used++] = '\n'; break;
            case 'r': out[used++] = '\r'; break;
            case 't': out[used++] = '\t'; break;
            case '"': case '\\': out[used++] = (char)c; break;
            case 'u': case 'U': {
                unsigned digits = c == 'u' ? 4 : 8;
                uint32_t cp = 0;
                size_t begin = p->at;
                for (unsigned i = 0; i < digits; ++i) {
                    int digit = xx_toml_digit(xx_toml_peek(p));
                    if (p->at == p->length || digit < 0) {
                        xx_toml_fail(p, begin, "invalid unicode %u-digit hex code", digits);
                        goto failed;
                    }
                    cp = (cp << 4) | (unsigned)digit; ++p->at;
                }
                if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                    xx_toml_fail(p, begin, "invalid unicode %u-digit hex code\nvalue is out of range", digits);
                    goto failed;
                }
                used += xx_toml_utf8(out + used, cp);
                break;
            }
            default:
                xx_toml_fail(p, p->at, "invalid escape sequence\nexpected `b`, `f`, `n`, `r`, `t`, `u`, `U`, `\\`, `\"`");
                goto failed;
            }
        } else if (multi && c == '\r' && xx_toml_peek(p) == '\n') {
            ++p->at; out[used++] = '\n';
        } else if ((c < 0x20 && c != '\t' && !(multi && c == '\n')) || c == 0x7f) {
            xx_toml_fail(p, p->at - 1, "invalid %s", label); break;
        } else out[used++] = (char)c;
    }
    xx_toml_fail(p, p->at, "invalid %s", label);
failed:
    free(out); return NULL;
}

static void xx_toml_path_free(xx_toml_path *path)
{
    for (size_t i = 0; i < path->count; ++i) free(path->keys[i]);
    free(path->keys); free(path->lengths); memset(path, 0, sizeof(*path));
}

static bool xx_toml_key(xx_toml_parser *p, xx_toml_path *path)
{
    size_t begin = p->at, dot = SIZE_MAX;
    for (;;) {
        char *key, **keys;
        size_t length, *lengths, start;
        xx_toml_ws(p); start = p->at;
        if (xx_toml_peek(p) == '"' || xx_toml_peek(p) == '\'')
            key = xx_toml_string(p, true, &length);
        else {
            while (p->at < p->length) {
                char c = p->source[p->at];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-')) break;
                ++p->at;
            }
            length = p->at - start;
            if (!length) {
                if (dot != SIZE_MAX) { p->at = dot; return true; }
                return xx_toml_fail(p, start, "invalid key");
            }
            key = xx_toml_copy(p->source + start, length);
        }
        if (!key) return false;
        keys = (char **)realloc(path->keys, (path->count + 1) * sizeof(*keys));
        if (!keys) { free(key); return xx_toml_fail(p, start, "out of memory"); }
        path->keys = keys;
        lengths = (size_t *)realloc(path->lengths, (path->count + 1) * sizeof(*lengths));
        if (!lengths) { free(key); return xx_toml_fail(p, start, "out of memory"); }
        path->lengths = lengths;
        path->keys[path->count] = key; path->lengths[path->count++] = length;
        if (path->count >= 80)
            return xx_toml_fail(p, begin, "recursion limit exceeded");
        xx_toml_ws(p);
        if (xx_toml_peek(p) != '.') return true;
        dot = p->at++;
    }
}

static const char *xx_toml_type(const xx_toml_value *v)
{
    switch (v->kind) {
    case XX_TOML_TABLE: return "inline table";
    case XX_TOML_ARRAY: return "array";
    case XX_TOML_STRING: return "string";
    case XX_TOML_INTEGER: return "integer";
    case XX_TOML_FLOAT: return "float";
    case XX_TOML_BOOLEAN: return "boolean";
    case XX_TOML_DATETIME: return "datetime";
    }
    return "value";
}

static char *xx_toml_key_text(const char *key, size_t length)
{
    bool bare = length != 0;
    char *out;
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) bare = false;
    }
    if (bare) return xx_toml_copy(key, length);
    if (length > (SIZE_MAX - 3) / 6) return NULL;
    out = (char *)malloc(length * 6 + 3);
    if (!out) return NULL;
    out[used++] = '"';
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = (char)c; }
        else if (c == '\n') { out[used++] = '\\'; out[used++] = 'n'; }
        else if (c == '\r') { out[used++] = '\\'; out[used++] = 'r'; }
        else if (c == '\t') { out[used++] = '\\'; out[used++] = 't'; }
        else if (c < 0x20 || c == 0x7f) used += (size_t)sprintf(out + used, "\\u%04X", c);
        else out[used++] = (char)c;
    }
    out[used++] = '"'; out[used] = 0;
    return out;
}

static char *xx_toml_path_text(const xx_toml_path *path, size_t count)
{
    char *out = xx_toml_copy("", 0);
    size_t used = 0;
    for (size_t i = 0; out && i < count; ++i) {
        char *key = xx_toml_key_text(path->keys[i], path->lengths[i]);
        size_t length = key ? strlen(key) : 0;
        char *larger = key && length <= SIZE_MAX - used - 2 ?
            (char *)realloc(out, used + length + 2) : NULL;
        if (!larger) { free(key); free(out); return NULL; }
        out = larger;
        if (i) out[used++] = '.';
        memcpy(out + used, key, length + 1); used += length;
        free(key);
    }
    return out;
}

static xx_toml_value *xx_toml_descend(xx_toml_parser *p, xx_toml_value *table,
                                       xx_toml_path *path, size_t count,
                                       bool dotted, bool inline_table, size_t start)
{
    for (size_t i = 0; i < count; ++i) {
        xx_toml_value *child = xx_toml_lookup(table, path->keys[i], path->lengths[i]);
        if (!child) {
            child = xx_toml_new(p, XX_TOML_TABLE);
            if (!child) return NULL;
            child->implicit = true; child->dotted = dotted;
            child->key = xx_toml_copy(path->keys[i], path->lengths[i]);
            child->key_length = path->lengths[i];
            if (!child->key || !xx_toml_push(p, table, child)) {
                xx_toml_free(child); return NULL;
            }
        } else if (child->table_array && child->count) {
            child = child->children[child->count - 1];
        } else if (child->kind != XX_TOML_TABLE || (child->sealed && !inline_table)) {
            char *name = xx_toml_path_text(path, i + 1);
            xx_toml_fail(p, start, "dotted key `%s` attempted to extend non-table type (%s)", name ? name : "", xx_toml_type(child));
            free(name);
            return NULL;
        } else if (dotted && !child->implicit) {
            xx_toml_fail(p, start, "duplicate key `%s`", path->keys[i]);
            return NULL;
        }
        table = child;
    }
    return table;
}

static bool xx_toml_insert(xx_toml_parser *p, xx_toml_value *table,
                            xx_toml_path *path, xx_toml_value *v,
                            size_t start, bool inline_table)
{
    size_t last = path->count - 1;
    table = xx_toml_descend(p, table, path, last, true, inline_table, start);
    if (!table) return false;
    if (xx_toml_lookup(table, path->keys[last], path->lengths[last])) {
        if (inline_table) return xx_toml_fail(p, start, "duplicate key `%s`", path->keys[last]);
        return xx_toml_fail(p, start, "duplicate key `%s` in %s%s%s", path->keys[last],
                            p->table_path && p->table_path[0] ? "table `" : "document root",
                            p->table_path ? p->table_path : "", p->table_path && p->table_path[0] ? "`" : "");
    }
    v->key = xx_toml_copy(path->keys[last], path->lengths[last]);
    v->key_length = path->lengths[last];
    return v->key && xx_toml_push(p, table, v);
}

static xx_toml_value *xx_toml_value_parse(xx_toml_parser *p);

static xx_toml_value *xx_toml_container(xx_toml_parser *p, bool table)
{
    size_t start = p->at++;
    char close = table ? '}' : ']';
    xx_toml_value *v = xx_toml_new(p, table ? XX_TOML_TABLE : XX_TOML_ARRAY);
    if (!v) return NULL;
    v->offset = start; v->sealed = table;
    if (++p->depth >= 80) { xx_toml_fail(p, start, "recursion limit exceeded"); goto failed; }
    if (table) xx_toml_ws(p);
    else if (!xx_toml_trivia(p)) goto failed;
    if (xx_toml_peek(p) == close) { ++p->at; goto done; }
    for (;;) {
        xx_toml_value *child;
        xx_toml_path path = {0};
        char first = xx_toml_peek(p);
        bool key_start_valid = (first >= 'a' && first <= 'z') ||
            (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9') ||
            first == '_' || first == '-' || first == '\'' || first == '"';
        bool value_start_valid = (first >= '0' && first <= '9') || first == '+' ||
            first == '-' || first == '_' || first == '"' || first == '\'' ||
            first == '[' || first == '{' || first == 't' || first == 'f' || first == 'i' || first == 'n';
        if ((table && !key_start_valid) || (!table && !value_start_valid)) {
            xx_toml_fail(p, p->at, "invalid %s\nexpected `%c`", table ? "inline table" : "array", close);
            goto failed;
        }
        if (table) {
            if (!xx_toml_key(p, &path)) {
                if (p->error->message && !strcmp(p->error->message, "recursion limit exceeded"))
                    xx_toml_error_replace(p, "invalid inline table\nexpected `}`");
                xx_toml_path_free(&path); goto failed;
            }
            if (xx_toml_peek(p) != '=') {
                xx_toml_fail(p, p->at, "expected `.`, `=`");
                xx_toml_path_free(&path); goto failed;
            }
            ++p->at; xx_toml_ws(p);
        }
        child = xx_toml_value_parse(p);
        if (!child || !(table ? xx_toml_insert(p, v, &path, child, start + 1, true) :
                                xx_toml_push(p, v, child))) {
            xx_toml_path_free(&path); xx_toml_free(child); goto failed;
        }
        xx_toml_path_free(&path);
        if (table) xx_toml_ws(p);
        else if (!xx_toml_trivia(p)) goto failed;
        if (xx_toml_peek(p) == close) { ++p->at; goto done; }
        if (xx_toml_peek(p) != ',') {
            xx_toml_fail(p, p->at, "invalid %s\nexpected `%c`", table ? "inline table" : "array", close);
            goto failed;
        }
        {
            size_t comma = p->at++;
            if (table) {
                xx_toml_ws(p);
                if (xx_toml_peek(p) == ',' || xx_toml_peek(p) == '}' || xx_toml_peek(p) == '\n' ||
                    xx_toml_peek(p) == '\r' || xx_toml_peek(p) == '#' || p->at == p->length) {
                    xx_toml_fail(p, comma, "invalid inline table\nexpected `}`");
                    goto failed;
                }
            } else if (!xx_toml_trivia(p)) goto failed;
        }
        if (!table && xx_toml_peek(p) == close) { ++p->at; goto done; }
    }
done:
    --p->depth; v->length = p->at - start; return v;
failed:
    --p->depth; xx_toml_free(v); return NULL;
}

static bool xx_toml_digits(xx_toml_parser *p, unsigned radix, const char *label,
                            bool zero_prefix)
{
    int digit = xx_toml_digit(xx_toml_peek(p));
    bool zero = xx_toml_peek(p) == '0';
    if (p->at == p->length || digit < 0 || (unsigned)digit >= radix)
        return xx_toml_fail(p, p->at, "invalid %s%s", label, strcmp(label, "floating-point number") == 0 ? "\nexpected digit" : "");
    ++p->at;
    if (zero && !zero_prefix) return true;
    for (;;) {
        digit = xx_toml_digit(xx_toml_peek(p));
        if (p->at < p->length && digit >= 0 && (unsigned)digit < radix) ++p->at;
        else if (xx_toml_peek(p) == '_') {
            ++p->at; digit = xx_toml_digit(xx_toml_peek(p));
            if (p->at == p->length || digit < 0 || (unsigned)digit >= radix)
                return xx_toml_fail(p, p->at, "invalid %s\nexpected digit", label);
            ++p->at;
        } else return true;
    }
}

static bool xx_toml_decimal_part(xx_toml_parser *p, size_t digits, unsigned *n)
{
    *n = 0;
    for (size_t i = 0; i < digits; ++i) {
        char c = xx_toml_peek(p);
        if (c < '0' || c > '9') return false;
        *n = *n * 10 + (unsigned)(c - '0'); ++p->at;
    }
    return true;
}

/* Datetimes are a distinct TOML scalar. Figment subsequently treats them as
 * a map, but their syntax and range must be checked before schema validation. */
static bool xx_toml_datetime(xx_toml_parser *p, xx_toml_value *v)
{
    size_t start = p->at, field = start, date_end = start;
    unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    unsigned zone_hour = 0, zone_minute = 0;
    bool date = p->length - start >= 5 && p->source[start + 4] == '-';
    bool time = p->length - start >= 3 && p->source[start + 2] == ':';
    bool range = false, zone_z = false, zone_custom = false;
    char sign = '+', fraction[10] = {0}, output[48];
    size_t fractional_digits = 0, used = 0;
    const char *label = date ? "date-time" : "time";
    if (!date && !time) return false;
    if (date) {
        unsigned days;
        if (!xx_toml_decimal_part(p, 4, &year) || xx_toml_peek(p) != '-') goto fallback;
        field = ++p->at;
        if (!xx_toml_decimal_part(p, 2, &month)) goto bad;
        if (month < 1 || month > 12) { range = true; goto bad; }
        field = p->at;
        if (xx_toml_peek(p) != '-') goto bad;
        field = ++p->at;
        if (!xx_toml_decimal_part(p, 2, &day)) goto bad;
        days = month == 2 ? 28 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) :
               month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
        if (day < 1 || day > days) { range = true; goto bad; }
        date_end = p->at;
        if (xx_toml_peek(p) == 'T' || xx_toml_peek(p) == 't' || xx_toml_peek(p) == ' ') {
            ++p->at; time = true;
        }
    }
    if (time) {
        field = p->at;
        if (!xx_toml_decimal_part(p, 2, &hour) || hour > 23 || xx_toml_peek(p) != ':') {
            if (!date) goto fallback;
            time = false; p->at = date_end; goto decoded;
        }
        field = ++p->at;
        if (!xx_toml_decimal_part(p, 2, &minute)) goto bad;
        if (minute > 59) { range = true; goto bad; }
        field = p->at;
        if (xx_toml_peek(p) != ':') goto bad;
        field = ++p->at;
        if (!xx_toml_decimal_part(p, 2, &second)) goto bad;
        if (second > 60) { range = true; goto bad; }
        if (xx_toml_peek(p) == '.') {
            size_t dot = p->at++;
            while (xx_toml_peek(p) >= '0' && xx_toml_peek(p) <= '9') {
                if (fractional_digits < 9) fraction[fractional_digits++] = xx_toml_peek(p);
                ++p->at;
            }
            if (p->at == dot + 1) p->at = dot;
            while (fractional_digits && fraction[fractional_digits - 1] == '0') --fractional_digits;
            fraction[fractional_digits] = 0;
        }
        if (date && (xx_toml_peek(p) == 'Z' || xx_toml_peek(p) == 'z')) {
            ++p->at; zone_z = true;
        } else if (date && (xx_toml_peek(p) == '+' || xx_toml_peek(p) == '-')) {
            sign = xx_toml_peek(p); field = ++p->at; zone_custom = true; label = "time offset";
            if (!xx_toml_decimal_part(p, 2, &zone_hour)) goto bad;
            if (zone_hour > 23) { range = true; goto bad; }
            field = p->at;
            if (xx_toml_peek(p) != ':') goto bad;
            field = ++p->at;
            if (!xx_toml_decimal_part(p, 2, &zone_minute)) goto bad;
            if (zone_minute > 59) { range = true; goto bad; }
            if (!zone_hour && !zone_minute) sign = '+';
        }
    }
decoded:
    if (date) used += (size_t)snprintf(output, sizeof(output), "%04u-%02u-%02u", year, month, day);
    if (time) {
        used += (size_t)snprintf(output + used, sizeof(output) - used, "%s%02u:%02u:%02u", date ? "T" : "", hour, minute, second);
        if (fractional_digits) used += (size_t)snprintf(output + used, sizeof(output) - used, ".%s", fraction);
        if (zone_z) output[used++] = 'Z';
        else if (zone_custom) used += (size_t)snprintf(output + used, sizeof(output) - used, "%c%02u:%02u", sign, zone_hour, zone_minute);
    }
    v->kind = XX_TOML_DATETIME;
    v->string = xx_toml_copy(output, used); v->string_length = used;
    return v->string != NULL;
fallback:
    p->at = start; return false;
bad:
    xx_toml_fail(p, field, "invalid %s%s", label, range ? "\nvalue is out of range" : "");
    return false;
}

static xx_toml_value *xx_toml_value_parse(xx_toml_parser *p)
{
    size_t start = p->at, digits_start;
    char c = xx_toml_peek(p), *number = NULL, *end;
    unsigned radix = 10;
    bool negative = false, floating = false;
    xx_toml_value *v;
    if (c == '[' || c == '{') return xx_toml_container(p, c == '{');
    v = xx_toml_new(p, XX_TOML_STRING);
    if (!v) return NULL;
    if (c == '"' || c == '\'') {
        v->string = xx_toml_string(p, false, &v->string_length);
        if (!v->string) goto failed;
    } else if (xx_toml_has(p, "true") || xx_toml_has(p, "false")) {
        v->kind = XX_TOML_BOOLEAN; v->boolean = c == 't';
        p->at += v->boolean ? 4 : 5;
    } else if (xx_toml_datetime(p, v)) {
        /* Already decoded. */
    } else {
        const char *label = "integer";
        if (p->failed) goto failed;
        if (p->at == p->length) {
            xx_toml_fail(p, p->at, "%s", "");
            goto failed;
        }
        if (c == '+' || c == '-') { negative = c == '-'; ++p->at; }
        if (xx_toml_has(p, "inf") || xx_toml_has(p, "nan")) {
            v->kind = XX_TOML_FLOAT;
            v->floating = xx_toml_peek(p) == 'i' ? INFINITY : NAN;
            if (negative) v->floating = -v->floating;
            p->at += 3; goto done;
        }
        if (c != '+' && c != '-' && (c < '0' || c > '9')) {
            xx_toml_fail(p, start, c == '_' ? "invalid integer\nexpected leading digit" :
                         c == '.' ? "invalid floating-point number\nexpected leading digit" :
                         "invalid string\nexpected `\"`, `'`");
            goto failed;
        }
        if (p->at == start && (xx_toml_has(p, "0x") || xx_toml_has(p, "0o") || xx_toml_has(p, "0b"))) {
            c = p->source[p->at + 1];
            radix = c == 'x' ? 16 : c == 'o' ? 8 : 2;
            label = c == 'x' ? "hexadecimal integer" : c == 'o' ? "octal integer" : "binary integer";
            p->at += 2;
        }
        digits_start = p->at;
        if (!xx_toml_digits(p, radix, label, radix != 10)) goto failed;
        if (radix == 10 && xx_toml_peek(p) == '.') {
            floating = true; ++p->at;
            if (!xx_toml_digits(p, 10, "floating-point number", true)) {
                if (p->at && p->source[p->at - 1] == '_')
                    xx_toml_error_replace(p, "invalid floating-point number\nexpected digit, digit");
                goto failed;
            }
        }
        if (radix == 10 && (xx_toml_peek(p) == 'e' || xx_toml_peek(p) == 'E')) {
            floating = true; ++p->at;
            if (xx_toml_peek(p) == '+' || xx_toml_peek(p) == '-') ++p->at;
            if (!xx_toml_digits(p, 10, "floating-point number", true)) {
                if (!p->at || p->source[p->at - 1] != '_')
                    xx_toml_error_replace(p, "invalid floating-point number");
                goto failed;
            }
        }
        number = xx_toml_copy(p->source + (radix == 10 ? start : digits_start),
                               p->at - (radix == 10 ? start : digits_start));
        if (!number) goto failed;
        {
            char *write = number;
            for (char *read = number; *read; ++read) if (*read != '_') *write++ = *read;
            *write = 0;
        }
        errno = 0;
        if (floating) {
            v->kind = XX_TOML_FLOAT; v->floating = strtod(number, &end);
            if (*end || v->floating == INFINITY) {
                xx_toml_fail(p, start, "invalid floating-point number"); goto failed;
            }
        } else {
            v->kind = XX_TOML_INTEGER; v->integer = strtoll(number, &end, (int)radix);
            if (*end || errno == ERANGE) {
                xx_toml_fail(p, start, "%s", negative ? "number too small to fit in target type" : "number too large to fit in target type");
                goto failed;
            }
        }
    }
done:
    v->length = p->at - start; free(number); return v;
failed:
    free(number); xx_toml_free(v); return NULL;
}

static bool xx_toml_header(xx_toml_parser *p, xx_toml_value *root,
                            xx_toml_value **current)
{
    size_t start = p->at++;
    bool array = xx_toml_peek(p) == '[';
    xx_toml_path path = {0};
    xx_toml_value *parent, *child, *value;
    size_t last;
    if (array) ++p->at;
    if (!xx_toml_key(p, &path)) { xx_toml_header_error(p); goto failed; }
    if (xx_toml_peek(p) != ']') {
        xx_toml_fail(p, p->at, "invalid table header\nexpected `.`, `%s`", array ? "]]" : "]"); goto failed;
    }
    if (array && !xx_toml_has(p, "]]")) {
        xx_toml_fail(p, p->at, "invalid table header\nexpected `.`, `]]`"); goto failed;
    }
    p->at += array ? 2 : 1;
    if (!xx_toml_line_end(p)) { xx_toml_header_error(p); goto failed; }
    last = path.count - 1;
    parent = xx_toml_descend(p, root, &path, last, false, false, start);
    if (!parent) {
        xx_toml_header_error(p);
        goto failed;
    }
    child = xx_toml_lookup(parent, path.keys[last], path.lengths[last]);
    if (child && (array ? !child->table_array :
        child->kind != XX_TOML_TABLE || !child->implicit || child->dotted || child->sealed)) {
        char *name = xx_toml_path_text(&path, last);
        char *key = xx_toml_key_text(path.keys[last], path.lengths[last]);
        xx_toml_fail(p, start, "invalid table header\nduplicate key `%s` in %s%s%s",
                     key ? key : "", last ? "table `" : "document root", name ? name : "", last ? "`" : "");
        free(name); free(key);
        goto failed;
    }
    if (!child) {
        child = xx_toml_new(p, array ? XX_TOML_ARRAY : XX_TOML_TABLE);
        if (!child) goto failed;
        child->key = xx_toml_copy(path.keys[last], path.lengths[last]);
        child->key_length = path.lengths[last]; child->table_array = array;
        if (!child->key || !xx_toml_push(p, parent, child)) {
            xx_toml_free(child); goto failed;
        }
    }
    value = child;
    if (array) {
        value = xx_toml_new(p, XX_TOML_TABLE);
        if (!value || !xx_toml_push(p, child, value)) {
            xx_toml_free(value); goto failed;
        }
    }
    value->implicit = false; value->offset = start;
    free(p->table_path);
    p->table_path = xx_toml_path_text(&path, path.count);
    if (!p->table_path) { xx_toml_fail(p, start, "out of memory"); goto failed; }
    *current = value;
    xx_toml_path_free(&path); return true;
failed:
    xx_toml_path_free(&path); return false;
}

static bool xx_toml_valid_utf8(const unsigned char *s, size_t n, size_t *bad)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = s[i];
        size_t count = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!count || count > n - i) { *bad = i; return false; }
        for (size_t j = 1; j < count; ++j)
            if (s[i + j] < 0x80 || s[i + j] > 0xbf) { *bad = i; return false; }
        if ((c == 0xe0 && s[i + 1] < 0xa0) || (c == 0xed && s[i + 1] >= 0xa0) ||
            (c == 0xf0 && s[i + 1] < 0x90) || (c == 0xf4 && s[i + 1] >= 0x90)) {
            *bad = i; return false;
        }
        i += count;
    }
    return true;
}

xx_toml_value *xx_toml_parse(const char *source, size_t length, xx_toml_error *error)
{
    xx_toml_parser p = {0};
    xx_toml_value *root, *current;
    size_t bad;
    memset(error, 0, sizeof(*error));
    p.source = source; p.length = length; p.error = error;
    if (!xx_toml_valid_utf8((const unsigned char *)source, length, &bad)) {
        xx_toml_fail(&p, bad, "invalid UTF-8"); return NULL;
    }
    root = xx_toml_new(&p, XX_TOML_TABLE);
    if (!root) return NULL;
    current = root;
    if (xx_toml_has(&p, "\xef\xbb\xbf")) p.at += 3;
    while (p.at < length) {
        size_t start;
        xx_toml_path path = {0};
        xx_toml_value *v;
        if (!xx_toml_trivia(&p)) goto failed;
        if (p.at == length) break;
        if (xx_toml_peek(&p) == '[') {
            if (!xx_toml_header(&p, root, &current)) goto failed;
            continue;
        }
        start = p.at;
        if (!xx_toml_key(&p, &path)) { xx_toml_path_free(&path); goto failed; }
        if (xx_toml_peek(&p) != '=') {
            xx_toml_fail(&p, p.at, "expected `.`, `=`");
            xx_toml_path_free(&path); goto failed;
        }
        ++p.at; xx_toml_ws(&p);
        v = xx_toml_value_parse(&p);
        if (!v || !xx_toml_line_end(&p) || !xx_toml_insert(&p, current, &path, v, start, false)) {
            xx_toml_path_free(&path); xx_toml_free(v); goto failed;
        }
        xx_toml_path_free(&path);
    }
    free(p.table_path);
    return root;
failed:
    free(p.table_path);
    xx_toml_free(root); return NULL;
}

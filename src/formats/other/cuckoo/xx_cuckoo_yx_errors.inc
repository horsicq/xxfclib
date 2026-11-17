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

typedef struct xx_cuckoo_yx_json_node {
    struct xx_cuckoo_yx_json_node *child, *next;
    char *key;
    size_t key_end, start, end, count;
    unsigned char kind;
} xx_cuckoo_yx_json_node;

typedef struct {
    const uint8_t *data;
    size_t size, at, error_at;
    char message[768];
} xx_cuckoo_yx_json_error;

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

static bool xx_cuckoo_yx_json_string(xx_cuckoo_yx_json_error *p)
{
    ++p->at;
    while (p->at < p->size) {
        unsigned char c = p->data[p->at++];
        if (c == '"') return true;
        if (c < 0x20)
            return xx_cuckoo_yx_json_fail(p, p->at, "control character (\\u0000-\\u001F) found while parsing a string");
        if (c == '\\') {
            if (p->at == p->size) break;
            c = p->data[p->at++];
            if (c == 'u') {
                unsigned value = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    unsigned char digit;
                    if (p->at == p->size)
                        return xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing a string");
                    digit = p->data[p->at++];
                    if (digit >= '0' && digit <= '9') value = value * 16 + digit - '0';
                    else if (digit >= 'a' && digit <= 'f') value = value * 16 + digit - 'a' + 10;
                    else if (digit >= 'A' && digit <= 'F') value = value * 16 + digit - 'A' + 10;
                    else return xx_cuckoo_yx_json_fail(p, p->at, "invalid escape");
                }
                if (value >= 0xdc00 && value <= 0xdfff)
                    return xx_cuckoo_yx_json_fail(p, p->at, "lone leading surrogate in hex escape");
                if (value >= 0xd800 && value <= 0xdbff) {
                    unsigned low = 0;
                    if (p->size - p->at < 2 || p->data[p->at] != '\\' || p->data[p->at + 1] != 'u')
                        return xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), "unexpected end of hex escape");
                    p->at += 2;
                    for (unsigned i = 0; i < 4; ++i) {
                        unsigned char digit;
                        if (p->at == p->size)
                            return xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing a string");
                        digit = p->data[p->at++];
                        if (digit >= '0' && digit <= '9') low = low * 16 + digit - '0';
                        else if (digit >= 'a' && digit <= 'f') low = low * 16 + digit - 'a' + 10;
                        else if (digit >= 'A' && digit <= 'F') low = low * 16 + digit - 'A' + 10;
                        else return xx_cuckoo_yx_json_fail(p, p->at, "invalid escape");
                    }
                    if (low < 0xdc00 || low > 0xdfff)
                        return xx_cuckoo_yx_json_fail(p, p->at, "lone leading surrogate in hex escape");
                }
            } else if (!strchr("\"\\/bfnrt", c))
                return xx_cuckoo_yx_json_fail(p, p->at, "invalid escape");
        }
    }
    return xx_cuckoo_yx_json_fail(p, p->size, "EOF while parsing a string");
}

static bool xx_cuckoo_yx_json_number(xx_cuckoo_yx_json_error *p)
{
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
    return true;
}

static xx_cuckoo_yx_json_node *xx_cuckoo_yx_json_value(xx_cuckoo_yx_json_error *p, unsigned depth)
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
        ++p->at; xx_cuckoo_yx_json_space(p);
        if (p->at < p->size && p->data[p->at] == close) { ++p->at; goto done; }
        for (;;) {
            xx_cuckoo_yx_json_node *child;
            char *key = NULL;
            size_t key_end = 0;
            if (p->at == p->size) {
                xx_cuckoo_yx_json_fail(p, p->at, "EOF while parsing %s", object ? "an object" : "a list"); break;
            }
            if (object) {
                xx_json key_cursor;
                if (p->data[p->at] != '"') {
                    xx_cuckoo_yx_json_fail(p, p->at + 1, "key must be a string"); break;
                }
                xx_json_init(&key_cursor, p->data, p->size); key_cursor.position = p->at;
                if (!xx_cuckoo_yx_json_string(p)) break;
                key_end = p->at;
                if (!xx_json_string(&key_cursor, &key)) { xx_cuckoo_yx_json_fail(p, p->at, "invalid unicode code point"); break; }
                xx_cuckoo_yx_json_space(p);
                if (p->at == p->size || p->data[p->at] != ':') {
                    xx_str_free(key);
                    xx_cuckoo_yx_json_fail(p, p->at + (p->at < p->size), p->at == p->size ? "EOF while parsing an object" : "expected `:`"); break;
                }
                ++p->at;
            }
            child = xx_cuckoo_yx_json_value(p, depth + 1);
            if (!child) { xx_str_free(key); break; }
            child->key = key; child->key_end = key_end;
            *tail = child; tail = &child->next; ++v->count;
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
        if (!xx_cuckoo_yx_json_number(p)) return v;
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

static bool xx_cuckoo_yx_json_type_error(xx_cuckoo_yx_json_error *p,
    const xx_cuckoo_yx_json_node *v, const char *expected, size_t at, bool value_error)
{
    char found[384];
    if (!v->end) return true;
    if (v->kind == 'n') snprintf(found, sizeof(found), "null");
    else if (v->kind == '{') snprintf(found, sizeof(found), "map");
    else if (v->kind == '[') snprintf(found, sizeof(found), "sequence");
    else if (v->kind == 't' || v->kind == 'f') snprintf(found, sizeof(found), "boolean `%s`", v->kind == 't' ? "true" : "false");
    else if (v->kind == '"') snprintf(found, sizeof(found), "string %.*s", (int)(v->end - v->start), p->data + v->start);
    else {
        bool floating = false;
        for (size_t i = v->start; i < v->end; ++i)
            if (p->data[i] == '.' || p->data[i] == 'e' || p->data[i] == 'E') floating = true;
        snprintf(found, sizeof(found), "%s `%.*s`", floating ? "floating point" : "integer", (int)(v->end - v->start), p->data + v->start);
    }
    return xx_cuckoo_yx_json_fail(p, at ? at : v->end, "invalid %s: %s, expected %s", value_error ? "value" : "type", found, expected);
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
        char number[96], *end;
        size_t length = v->end - v->start;
        bool integer = v->kind >= '0' && v->kind <= '9';
        if (!v->end) return true;
        for (size_t i = v->start; i < v->end; ++i)
            if (p->data[i] == '.' || p->data[i] == 'e' || p->data[i] == 'E') integer = false;
        if (integer && length < sizeof(number)) {
            memcpy(number, p->data + v->start, length); number[length] = 0;
            errno = 0; (void)strtoull(number, &end, 10);
            if (!errno && !*end) return true;
        }
        return xx_cuckoo_yx_json_type_error(p, v, "u64", at, v->kind == '-');
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
    for (const xx_cuckoo_yx_json_node *child = v->child; child; child = child->next) {
        const char *key = child->key ? child->key : "";
        xx_cuckoo_yx_json_shape child_shape = YX_CJ_ANY;
        unsigned bit = 0;
        size_t child_at = at;
        if (shape == YX_CJ_ROOT) {
            if (!strcmp(key, "network")) { child_shape = YX_CJ_NETWORK; bit = 1; }
            if (!strcmp(key, "behavior")) { child_shape = YX_CJ_BEHAVIOR; bit = 2; }
        } else if (shape == YX_CJ_BEHAVIOR) {
            if (!strcmp(key, "summary")) { child_shape = YX_CJ_SUMMARY; bit = 1; }
        } else if (shape == YX_CJ_SUMMARY) {
            if (!strcmp(key, "mutexes")) { child_shape = YX_CJ_STRINGS; bit = 1; }
            if (!strcmp(key, "files")) { child_shape = YX_CJ_STRINGS; bit = 2; }
            if (!strcmp(key, "keys")) { child_shape = YX_CJ_STRINGS; bit = 4; }
        } else if (shape == YX_CJ_NETWORK) {
            if (!strcmp(key, "domains")) { selected = child; continue; }
            if (!strcmp(key, "dns")) { old_dns = child; continue; }
            if (!strcmp(key, "http")) child_shape = YX_CJ_HTTPS;
            if (!strcmp(key, "tcp")) child_shape = YX_CJ_TCPS;
            if (!strcmp(key, "udp")) child_shape = YX_CJ_UDPS;
            if (!strcmp(key, "hosts")) child_shape = YX_CJ_STRINGS;
            child_at = child->end;
            if (child_at) {
                while (child_at < p->size && p->data[child_at] &&
                       strchr(" \t\r\n", p->data[child_at])) ++child_at;
                if (child_at < p->size && p->data[child_at] == '}') ++child_at;
            }
        } else if (shape == YX_CJ_HTTP) {
            if (!strcmp(key, "user-agent")) { child_shape = YX_CJ_STRING; bit = 1; }
            if (!strcmp(key, "method")) { child_shape = YX_CJ_STRING; bit = 2; }
            if (!strcmp(key, "uri")) { child_shape = YX_CJ_STRING; bit = 4; }
        } else if (shape == YX_CJ_TCP || shape == YX_CJ_UDP) {
            if (!strcmp(key, "dst")) { child_shape = YX_CJ_STRING; bit = 1; }
            if (!strcmp(key, "dst_domain")) { child_shape = YX_CJ_STRING; bit = 2; }
            if (!strcmp(key, "dport")) { child_shape = YX_CJ_U64; bit = 4; }
        } else if (shape == YX_CJ_DOMAIN || shape == YX_CJ_OLD_DOMAIN) {
            if (!strcmp(key, shape == YX_CJ_DOMAIN ? "domain" : "hostname")) { child_shape = YX_CJ_STRING; bit = 1; }
        }
        /* serde_json::Value maps keep only the final duplicate field. */
        if (at) {
            bool replaced = false;
            for (const xx_cuckoo_yx_json_node *later = child->next; later; later = later->next)
                if (later->key && !strcmp(key, later->key)) { replaced = true; break; }
            if (replaced) continue;
        } else if (bit && (seen & bit))
            return xx_cuckoo_yx_json_fail(p, child->key_end, "duplicate field `%s`", key);
        seen |= bit;
        if (!xx_cuckoo_yx_json_validate(p, child, child_shape, true, child_at)) return false;
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
    root = xx_cuckoo_yx_json_value(&syntax, 0);
    if (!syntax.message[0]) {
        xx_cuckoo_yx_json_space(&syntax);
        if (syntax.at < size) xx_cuckoo_yx_json_fail(&syntax, syntax.at + 1, "trailing characters");
    }
    typed = syntax; typed.message[0] = 0;
    if (xx_cuckoo_yx_json_validate(&typed, root, YX_CJ_ROOT, false, 0) ||
        (syntax.message[0] && typed.error_at > syntax.error_at)) typed = syntax;
    if (typed.message[0]) {
        for (size_t i = 0; i < typed.error_at && i < size; ++i) {
            if (data[i] == '\n') { ++line; column = 0; }
            else ++column;
        }
        snprintf(error, capacity, "%s at line %zu column %zu", typed.message, line, column);
    } else error[0] = 0;
    xx_cuckoo_yx_json_free(root);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * See xx_json.h for the design. In short: a cursor over the text rather than
 * a document tree, because the callers read a few fields out of structures
 * that can be very large and then move on.
 *
 * The parser is strict where being lax would let malformed input through as a
 * valid archive: control characters inside strings must be escaped, numbers
 * must be JSON numbers rather than anything strtod would swallow, and an
 * unpaired surrogate is an error rather than a replacement character. A format
 * reader uses "did this parse" as evidence about what the file is, so a
 * forgiving parser would weaken every identification built on it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/json/xx_json.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

void xx_json_init(xx_json *json, const void *data, size_t size) {
    if (!json) return;
    json->data = (const uint8_t *)data;
    json->size = data ? size : 0U;
    json->position = 0U;
    json->depth = 0;
}

static void xx_json_skip_space(xx_json *json) {
    while (json->position < json->size) {
        uint8_t c = json->data[json->position];
        /* RFC 8259 whitespace, and nothing else: a stray byte is an error,
         * not something to step over. */
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        ++json->position;
    }
}

static bool xx_json_take(xx_json *json, char c) {
    xx_json_skip_space(json);
    if (json->position >= json->size ||
        json->data[json->position] != (uint8_t)c) {
        return false;
    }
    ++json->position;
    return true;
}

static bool xx_json_at(xx_json *json, char c) {
    xx_json_skip_space(json);
    return json->position < json->size &&
           json->data[json->position] == (uint8_t)c;
}

static int xx_json_hex(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool xx_json_literal(xx_json *json, const char *text) {
    size_t length = xx_str_len(text);

    xx_json_skip_space(json);
    if (json->position + length > json->size ||
        xx_rt_memcmp(json->data + json->position, text, length) != 0) {
        return false;
    }
    json->position += length;
    return true;
}

xx_json_type_t xx_json_peek(xx_json *json) {
    if (!json) return XX_JSON_TYPE_INVALID;
    xx_json_skip_space(json);
    if (json->position >= json->size) return XX_JSON_TYPE_INVALID;
    switch (json->data[json->position]) {
        case '{': return XX_JSON_TYPE_OBJECT;
        case '[': return XX_JSON_TYPE_ARRAY;
        case '"': return XX_JSON_TYPE_STRING;
        case 't':
        case 'f': return XX_JSON_TYPE_BOOL;
        case 'n': return XX_JSON_TYPE_NULL;
        case '-': return XX_JSON_TYPE_NUMBER;
        default:
            return (json->data[json->position] >= '0' &&
                    json->data[json->position] <= '9')
                       ? XX_JSON_TYPE_NUMBER
                       : XX_JSON_TYPE_INVALID;
    }
}

/*
 * Append one code point as UTF-8. @p raw marks a byte that came straight from
 * the source rather than from a \u escape: the source is already UTF-8, so
 * such a byte is passed through untouched instead of being re-encoded as if
 * it were a code point in its own right.
 */
static bool xx_json_emit(char **buffer, size_t *length, size_t *capacity,
                         uint32_t code, bool raw) {
    uint8_t encoded[4];
    size_t encoded_length;
    size_t i;

    if (raw || code < 0x80U) {
        encoded[0] = (uint8_t)code;
        encoded_length = 1U;
    } else if (code < 0x800U) {
        encoded[0] = (uint8_t)(0xC0U | (code >> 6));
        encoded[1] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 2U;
    } else if (code < 0x10000U) {
        encoded[0] = (uint8_t)(0xE0U | (code >> 12));
        encoded[1] = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 3U;
    } else {
        encoded[0] = (uint8_t)(0xF0U | (code >> 18));
        encoded[1] = (uint8_t)(0x80U | ((code >> 12) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
        encoded[3] = (uint8_t)(0x80U | (code & 0x3FU));
        encoded_length = 4U;
    }
    if (*length + encoded_length + 1U > *capacity) {
        char *grown;
        size_t wanted = *capacity;
        while (*length + encoded_length + 1U > wanted) wanted *= 2U;
        grown = (char *)xx_mem_realloc(*buffer, wanted);
        if (!grown) return false;
        *buffer = grown;
        *capacity = wanted;
    }
    for (i = 0U; i < encoded_length; ++i) {
        (*buffer)[(*length)++] = (char)encoded[i];
    }
    return true;
}

/* Read a string; @p out may be NULL to validate and discard. */
static bool xx_json_string_inner(xx_json *json, char **out) {
    size_t capacity = 32U;
    size_t length = 0U;
    char *buffer = NULL;

    if (out) *out = NULL;
    if (!xx_json_take(json, '"')) return false;
    if (out) {
        buffer = (char *)xx_mem_alloc(capacity);
        if (!buffer) return false;
    }

    for (;;) {
        uint8_t c;

        if (json->position >= json->size) goto fail;
        c = json->data[json->position++];
        if (c == '"') break;
        /* Unescaped control characters are not permitted. */
        if (c < 0x20U) goto fail;

        if (c != '\\') {
            if (out && !xx_json_emit(&buffer, &length, &capacity, c, true)) {
                goto fail;
            }
            continue;
        }
        if (json->position >= json->size) goto fail;
        c = json->data[json->position++];
        {
            uint32_t code;
            switch (c) {
                case '"': code = '"'; break;
                case '\\': code = '\\'; break;
                case '/': code = '/'; break;
                case 'b': code = 0x08U; break;
                case 'f': code = 0x0CU; break;
                case 'n': code = 0x0AU; break;
                case 'r': code = 0x0DU; break;
                case 't': code = 0x09U; break;
                case 'u': {
                    uint32_t value = 0U;
                    int i;
                    if (json->position + 4U > json->size) goto fail;
                    for (i = 0; i < 4; ++i) {
                        int digit = xx_json_hex(json->data[json->position++]);
                        if (digit < 0) goto fail;
                        value = (value << 4) | (uint32_t)digit;
                    }
                    if (value >= 0xD800U && value <= 0xDBFFU) {
                        /* A high surrogate must be followed by a low one. */
                        uint32_t low = 0U;
                        if (json->position + 6U > json->size ||
                            json->data[json->position] != '\\' ||
                            json->data[json->position + 1U] != 'u') {
                            goto fail;
                        }
                        json->position += 2U;
                        for (i = 0; i < 4; ++i) {
                            int digit =
                                xx_json_hex(json->data[json->position++]);
                            if (digit < 0) goto fail;
                            low = (low << 4) | (uint32_t)digit;
                        }
                        if (low < 0xDC00U || low > 0xDFFFU) goto fail;
                        value = 0x10000U + ((value - 0xD800U) << 10) +
                                (low - 0xDC00U);
                    } else if (value >= 0xDC00U && value <= 0xDFFFU) {
                        goto fail; /* unpaired low surrogate */
                    }
                    code = value;
                    break;
                }
                default: goto fail;
            }
            if (out &&
                !xx_json_emit(&buffer, &length, &capacity, code, false)) {
                goto fail;
            }
        }
    }

    if (out) {
        buffer[length] = '\0';
        *out = buffer;
    }
    return true;

fail:
    xx_mem_free(buffer);
    return false;
}

bool xx_json_string(xx_json *json, char **out) {
    return json && out && xx_json_string_inner(json, out);
}

bool xx_json_integer(xx_json *json, int64_t limit, int64_t *out) {
    int64_t value = 0;
    bool any = false;

    if (!json || limit < 0) return false;
    xx_json_skip_space(json);
    while (json->position < json->size) {
        uint8_t c = json->data[json->position];
        if (c < '0' || c > '9') break;
        if (value > (limit - (c - '0')) / 10) return false;
        value = value * 10 + (c - '0');
        any = true;
        ++json->position;
    }
    if (!any) return false;
    /* Anything that continues the number means it was not an integer. */
    if (json->position < json->size) {
        uint8_t c = json->data[json->position];
        if (c == '.' || c == 'e' || c == 'E') return false;
    }
    if (out) *out = value;
    return true;
}

bool xx_json_bool(xx_json *json, bool *out) {
    if (!json) return false;
    if (xx_json_literal(json, "true")) {
        if (out) *out = true;
        return true;
    }
    if (xx_json_literal(json, "false")) {
        if (out) *out = false;
        return true;
    }
    return false;
}

bool xx_json_null(xx_json *json) {
    return json && xx_json_literal(json, "null");
}

/* Consume a number in any JSON form, which xx_json_integer deliberately
 * refuses -- skipping has no reason to be that strict, only to be correct
 * about where the value ends. */
static bool xx_json_skip_number(xx_json *json) {
    bool any_digit = false;

    xx_json_skip_space(json);
    if (json->position < json->size && json->data[json->position] == '-') {
        ++json->position;
    }
    while (json->position < json->size) {
        uint8_t c = json->data[json->position];
        if (c >= '0' && c <= '9') {
            any_digit = true;
            ++json->position;
            continue;
        }
        if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            ++json->position;
            continue;
        }
        break;
    }
    return any_digit;
}

bool xx_json_object_begin(xx_json *json);
bool xx_json_object_empty(xx_json *json);
bool xx_json_object_key(xx_json *json, char **key);
bool xx_json_object_end(xx_json *json);
bool xx_json_array_begin(xx_json *json);
bool xx_json_array_empty(xx_json *json);
bool xx_json_array_end(xx_json *json);
bool xx_json_more(xx_json *json);

static bool xx_json_skip_container(xx_json *json, bool is_object) {
    bool ok;

    /* Skipping recurses, so an input nesting brackets a million deep would
     * exhaust the stack before any format reader saw it. Archive readers take
     * their input from untrusted files, so the bound lives here. */
    if (json->depth >= XX_JSON_MAX_DEPTH) return false;
    ++json->depth;

    if (is_object) {
        ok = xx_json_object_begin(json);
        if (ok && xx_json_object_empty(json)) {
            ok = xx_json_object_end(json);
        } else if (ok) {
            for (;;) {
                if (!xx_json_object_key(json, NULL) || !xx_json_skip(json)) {
                    ok = false;
                    break;
                }
                if (xx_json_more(json)) continue;
                ok = xx_json_object_end(json);
                break;
            }
        }
    } else {
        ok = xx_json_array_begin(json);
        if (ok && xx_json_array_empty(json)) {
            ok = xx_json_array_end(json);
        } else if (ok) {
            for (;;) {
                if (!xx_json_skip(json)) {
                    ok = false;
                    break;
                }
                if (xx_json_more(json)) continue;
                ok = xx_json_array_end(json);
                break;
            }
        }
    }

    --json->depth;
    return ok;
}

bool xx_json_skip(xx_json *json) {
    if (!json) return false;
    switch (xx_json_peek(json)) {
        case XX_JSON_TYPE_STRING: return xx_json_string_inner(json, NULL);
        case XX_JSON_TYPE_NUMBER: return xx_json_skip_number(json);
        case XX_JSON_TYPE_BOOL: return xx_json_bool(json, NULL);
        case XX_JSON_TYPE_NULL: return xx_json_null(json);
        case XX_JSON_TYPE_OBJECT: return xx_json_skip_container(json, true);
        case XX_JSON_TYPE_ARRAY: return xx_json_skip_container(json, false);
        default: return false;
    }
}

bool xx_json_object_begin(xx_json *json) {
    return json && xx_json_take(json, '{');
}

bool xx_json_object_empty(xx_json *json) {
    return json && xx_json_at(json, '}');
}

bool xx_json_object_key(xx_json *json, char **key) {
    if (!json) return false;
    if (key) {
        if (!xx_json_string_inner(json, key)) return false;
    } else if (!xx_json_string_inner(json, NULL)) {
        return false;
    }
    if (!xx_json_take(json, ':')) {
        if (key) {
            xx_str_free(*key);
            *key = NULL;
        }
        return false;
    }
    return true;
}

bool xx_json_object_end(xx_json *json) {
    return json && xx_json_take(json, '}');
}

bool xx_json_array_begin(xx_json *json) {
    return json && xx_json_take(json, '[');
}

bool xx_json_array_empty(xx_json *json) {
    return json && xx_json_at(json, ']');
}

bool xx_json_array_end(xx_json *json) {
    return json && xx_json_take(json, ']');
}

bool xx_json_more(xx_json *json) {
    return json && xx_json_take(json, ',');
}

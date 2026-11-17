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

#include <xxfclib/formats/other/cuckoo/xx_cuckoo_yx.h>

#include <xxfclib/json/xx_json.h>
#include <xxfclib/strings/xx_string.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
} xx_cuckoo_yx_text;

typedef struct {
    xx_cuckoo_yx_text *items;
    size_t count;
    size_t capacity;
} xx_cuckoo_yx_strings;

typedef struct {
    xx_cuckoo_yx_text method;
    xx_cuckoo_yx_text uri;
    xx_cuckoo_yx_text user_agent;
} xx_cuckoo_yx_http;

typedef struct {
    xx_cuckoo_yx_http *items;
    size_t count;
    size_t capacity;
} xx_cuckoo_yx_http_vector;

typedef struct {
    xx_cuckoo_yx_text dst;
    xx_cuckoo_yx_text dst_domain;
    uint64_t dport;
    bool has_dport;
} xx_cuckoo_yx_connection;

typedef struct {
    xx_cuckoo_yx_connection *items;
    size_t count;
    size_t capacity;
} xx_cuckoo_yx_connection_vector;

struct xx_cuckoo_yx_report {
    xx_cuckoo_yx_strings domains;
    xx_cuckoo_yx_strings hosts;
    xx_cuckoo_yx_strings mutexes;
    xx_cuckoo_yx_strings files;
    xx_cuckoo_yx_strings keys;
    xx_cuckoo_yx_http_vector http;
    xx_cuckoo_yx_connection_vector tcp;
    xx_cuckoo_yx_connection_vector udp;
};

typedef struct {
    const uint8_t *data;
    size_t size;
    bool present;
} xx_cuckoo_yx_raw;

static bool xx_cuckoo_yx_grow(void **items, size_t *capacity,
                            size_t count, size_t item_size)
{
    size_t next;
    void *grown;
    if (count < *capacity) return true;
    next = *capacity ? *capacity * 2u : 8u;
    if (next < *capacity || next > SIZE_MAX / item_size) return false;
    grown = realloc(*items, next * item_size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static bool xx_cuckoo_yx_push_string(xx_cuckoo_yx_strings *vector,
                                    xx_cuckoo_yx_text value)
{
    if (!xx_cuckoo_yx_grow((void **)&vector->items, &vector->capacity,
                         vector->count, sizeof(*vector->items)))
        return false;
    vector->items[vector->count++] = value;
    return true;
}

static bool xx_cuckoo_yx_push_http(xx_cuckoo_yx_http_vector *vector,
                                  xx_cuckoo_yx_http value)
{
    if (!xx_cuckoo_yx_grow((void **)&vector->items, &vector->capacity,
                         vector->count, sizeof(*vector->items)))
        return false;
    vector->items[vector->count++] = value;
    return true;
}

static bool xx_cuckoo_yx_push_connection(
    xx_cuckoo_yx_connection_vector *vector, xx_cuckoo_yx_connection value)
{
    if (!xx_cuckoo_yx_grow((void **)&vector->items, &vector->capacity,
                         vector->count, sizeof(*vector->items)))
        return false;
    vector->items[vector->count++] = value;
    return true;
}

static void xx_cuckoo_yx_strings_free(xx_cuckoo_yx_strings *vector)
{
    for (size_t i = 0; i < vector->count; ++i)
        xx_str_free(vector->items[i].data);
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static void xx_cuckoo_yx_http_free(xx_cuckoo_yx_http_vector *vector)
{
    for (size_t i = 0; i < vector->count; ++i) {
        xx_str_free(vector->items[i].method.data);
        xx_str_free(vector->items[i].uri.data);
        xx_str_free(vector->items[i].user_agent.data);
    }
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static void xx_cuckoo_yx_connections_free(xx_cuckoo_yx_connection_vector *vector)
{
    for (size_t i = 0; i < vector->count; ++i) {
        xx_str_free(vector->items[i].dst.data);
        xx_str_free(vector->items[i].dst_domain.data);
    }
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

XXFC_API void xx_cuckoo_yx_report_free(xx_cuckoo_yx_report *report)
{
    if (!report) return;
    xx_cuckoo_yx_strings_free(&report->domains);
    xx_cuckoo_yx_strings_free(&report->hosts);
    xx_cuckoo_yx_strings_free(&report->mutexes);
    xx_cuckoo_yx_strings_free(&report->files);
    xx_cuckoo_yx_strings_free(&report->keys);
    xx_cuckoo_yx_http_free(&report->http);
    xx_cuckoo_yx_connections_free(&report->tcp);
    xx_cuckoo_yx_connections_free(&report->udp);
    free(report);
}

static unsigned xx_cuckoo_yx_hex_digit(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10u;
    return byte - 'A' + 10u;
}

static size_t xx_cuckoo_yx_decoded_length(const uint8_t *raw, size_t size)
{
    size_t length = 0;
    for (size_t i = 1; i + 1 < size;) {
        uint8_t byte = raw[i++];
        if (byte != '\\') {
            ++length;
        } else if (raw[i++] != 'u') {
            ++length;
        } else {
            uint32_t code = 0;
            for (unsigned j = 0; j < 4; ++j)
                code = (code << 4) | xx_cuckoo_yx_hex_digit(raw[i++]);
            if (code >= 0xd800u && code <= 0xdbffu) {
                uint32_t low = 0;
                i += 2; /* Escape prefix of the low surrogate. */
                for (unsigned j = 0; j < 4; ++j)
                    low = (low << 4) | xx_cuckoo_yx_hex_digit(raw[i++]);
                code = UINT32_C(0x10000) + ((code - 0xd800u) << 10) +
                       (low - 0xdc00u);
            }
            length += code < 0x80u ? 1u : code < 0x800u ? 2u :
                      code < 0x10000u ? 3u : 4u;
        }
    }
    return length;
}

static bool xx_cuckoo_yx_object_key(xx_json *json, char **key,
                                   size_t *length)
{
    size_t start, end;
    xx_json_peek(json);
    start = json->position;
    if (!xx_json_object_key(json, key)) return false;
    end = start + 1;
    while (end < json->size) {
        if (json->data[end] == '\\') end += 2;
        else if (json->data[end++] == '"') break;
    }
    *length = xx_cuckoo_yx_decoded_length(json->data + start, end - start);
    return true;
}

static bool xx_cuckoo_yx_key_equal(const char *key, size_t length,
                                 const char *expected)
{
    size_t expected_length = strlen(expected);
    return length == expected_length &&
           memcmp(key, expected, length) == 0;
}

static bool xx_cuckoo_yx_mark_once(unsigned *seen, unsigned flag)
{
    if (*seen & flag) return false;
    *seen |= flag;
    return true;
}

static bool xx_cuckoo_yx_continuation(uint8_t byte)
{
    return byte >= 0x80u && byte <= 0xbfu;
}

static bool xx_cuckoo_yx_valid_utf8(const uint8_t *bytes, size_t size)
{
    for (size_t i = 0; i < size;) {
        uint8_t first = bytes[i++];
        if (first < 0x80u) continue;
        if (first >= 0xc2u && first <= 0xdfu) {
            if (size - i < 1u || !xx_cuckoo_yx_continuation(bytes[i]))
                return false;
            ++i;
        } else if (first >= 0xe0u && first <= 0xefu) {
            if (size - i < 2u || !xx_cuckoo_yx_continuation(bytes[i + 1u]))
                return false;
            if ((first == 0xe0u &&
                 (bytes[i] < 0xa0u || bytes[i] > 0xbfu)) ||
                (first == 0xedu &&
                 (bytes[i] < 0x80u || bytes[i] > 0x9fu)) ||
                (first != 0xe0u && first != 0xedu &&
                 !xx_cuckoo_yx_continuation(bytes[i])))
                return false;
            i += 2u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            if (size - i < 3u ||
                !xx_cuckoo_yx_continuation(bytes[i + 1u]) ||
                !xx_cuckoo_yx_continuation(bytes[i + 2u])) return false;
            if ((first == 0xf0u &&
                 (bytes[i] < 0x90u || bytes[i] > 0xbfu)) ||
                (first == 0xf4u &&
                 (bytes[i] < 0x80u || bytes[i] > 0x8fu)) ||
                (first != 0xf0u && first != 0xf4u &&
                 !xx_cuckoo_yx_continuation(bytes[i])))
                return false;
            i += 3u;
        } else return false;
    }
    return true;
}

static bool xx_cuckoo_yx_string(xx_json *json, xx_cuckoo_yx_text *out)
{
    size_t start;
    memset(out, 0, sizeof(*out));
    if (xx_json_peek(json) != XX_JSON_TYPE_STRING) return false;
    start = json->position;
    if (!xx_json_string(json, &out->data)) return false;
    out->length = xx_cuckoo_yx_decoded_length(json->data + start,
                                            json->position - start);
    return true;
}

static bool xx_cuckoo_yx_optional_string(xx_json *json, xx_cuckoo_yx_text *out)
{
    memset(out, 0, sizeof(*out));
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    return xx_cuckoo_yx_string(json, out);
}

static bool xx_cuckoo_yx_unsigned(xx_json *json, uint64_t *value)
{
    uint64_t parsed = 0;
    size_t at;
    if (xx_json_peek(json) != XX_JSON_TYPE_NUMBER) return false;
    at = json->position;
    if (at >= json->size || json->data[at] < '0' ||
        json->data[at] > '9') return false;
    if (json->data[at] == '0') {
        ++at;
        if (at < json->size && json->data[at] >= '0' &&
            json->data[at] <= '9') return false;
    } else {
        do {
            unsigned digit = (unsigned)(json->data[at] - '0');
            if (parsed > (UINT64_MAX - digit) / 10u) return false;
            parsed = parsed * 10u + digit;
            ++at;
        } while (at < json->size && json->data[at] >= '0' &&
                 json->data[at] <= '9');
    }
    if (at < json->size && json->data[at] != ',' &&
        json->data[at] != '}' && json->data[at] != ']' &&
        json->data[at] != ' ' && json->data[at] != '\t' &&
        json->data[at] != '\r' && json->data[at] != '\n')
        return false;
    json->position = at;
    *value = parsed;
    return true;
}

static bool xx_cuckoo_yx_skip_number(xx_json *json)
{
    size_t at = json->position;
    if (at < json->size && json->data[at] == '-') ++at;
    if (at >= json->size) return false;
    if (json->data[at] == '0') {
        ++at;
        if (at < json->size && json->data[at] >= '0' &&
            json->data[at] <= '9') return false;
    } else if (json->data[at] >= '1' && json->data[at] <= '9') {
        do { ++at; } while (at < json->size && json->data[at] >= '0' &&
                            json->data[at] <= '9');
    } else return false;
    if (at < json->size && json->data[at] == '.') {
        ++at;
        if (at >= json->size || json->data[at] < '0' ||
            json->data[at] > '9') return false;
        do { ++at; } while (at < json->size && json->data[at] >= '0' &&
                            json->data[at] <= '9');
    }
    if (at < json->size &&
        (json->data[at] == 'e' || json->data[at] == 'E')) {
        ++at;
        if (at < json->size &&
            (json->data[at] == '+' || json->data[at] == '-')) ++at;
        if (at >= json->size || json->data[at] < '0' ||
            json->data[at] > '9') return false;
        do { ++at; } while (at < json->size && json->data[at] >= '0' &&
                            json->data[at] <= '9');
    }
    if (at < json->size && json->data[at] != ',' &&
        json->data[at] != '}' && json->data[at] != ']' &&
        json->data[at] != ' ' && json->data[at] != '\t' &&
        json->data[at] != '\r' && json->data[at] != '\n') return false;
    json->position = at;
    return true;
}

static bool xx_cuckoo_yx_skip(xx_json *json, unsigned depth)
{
    xx_json_type_t type = xx_json_peek(json);
    if (depth >= XX_JSON_MAX_DEPTH) return false;
    if (type == XX_JSON_TYPE_STRING) return xx_json_skip(json);
    if (type == XX_JSON_TYPE_NUMBER) return xx_cuckoo_yx_skip_number(json);
    if (type == XX_JSON_TYPE_BOOL) return xx_json_bool(json, NULL);
    if (type == XX_JSON_TYPE_NULL) return xx_json_null(json);
    if (type == XX_JSON_TYPE_OBJECT) {
        if (!xx_json_object_begin(json)) return false;
        if (!xx_json_object_empty(json)) {
            do {
                if (!xx_json_object_key(json, NULL) ||
                    !xx_cuckoo_yx_skip(json, depth + 1)) return false;
            } while (xx_json_more(json));
        }
        return xx_json_object_end(json);
    }
    if (type == XX_JSON_TYPE_ARRAY) {
        if (!xx_json_array_begin(json)) return false;
        if (!xx_json_array_empty(json)) {
            do {
                if (!xx_cuckoo_yx_skip(json, depth + 1)) return false;
            } while (xx_json_more(json));
        }
        return xx_json_array_end(json);
    }
    return false;
}

static bool xx_cuckoo_yx_capture(xx_json *json, xx_cuckoo_yx_raw *raw)
{
    size_t start;
    xx_json_peek(json);
    start = json->position;
    if (!xx_cuckoo_yx_skip(json, 0)) return false;
    raw->data = json->data + start;
    raw->size = json->position - start;
    raw->present = true;
    return true;
}

static bool xx_cuckoo_yx_string_array(xx_json *json,
                                     xx_cuckoo_yx_strings *vector)
{
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_array_begin(json)) return false;
    if (!xx_json_array_empty(json)) {
        do {
            xx_cuckoo_yx_text value = {0};
            if (!xx_cuckoo_yx_string(json, &value)) return false;
            if (!xx_cuckoo_yx_push_string(vector, value)) {
                xx_str_free(value.data);
                return false;
            }
        } while (xx_json_more(json));
    }
    return xx_json_array_end(json);
}

static bool xx_cuckoo_yx_domain_array(xx_json *json,
                                     xx_cuckoo_yx_strings *vector,
                                     const char *field)
{
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_array_begin(json)) return false;
    if (!xx_json_array_empty(json)) {
        do {
            xx_cuckoo_yx_text domain = {0};
            xx_cuckoo_yx_raw selected = {0};
            if (!xx_json_object_begin(json)) return false;
            if (!xx_json_object_empty(json)) {
                do {
                    char *key = NULL;
                    size_t key_length = 0;
                    bool ok;
                    if (!xx_cuckoo_yx_object_key(json, &key, &key_length))
                        return false;
                    if (xx_cuckoo_yx_key_equal(key, key_length, field))
                        ok = xx_cuckoo_yx_capture(json, &selected);
                    else
                        ok = xx_cuckoo_yx_skip(json, 0);
                    xx_str_free(key);
                    if (!ok) return false;
                } while (xx_json_more(json));
            }
            if (!xx_json_object_end(json)) return false;
            /* The domains/dns arrays pass through serde_json::Value before
             * item decoding, so only the final duplicate key is typed. */
            if (selected.present) {
                xx_json value;
                xx_json_init(&value, selected.data, selected.size);
                if (!xx_cuckoo_yx_optional_string(&value, &domain)) return false;
            }
            if (domain.data && !xx_cuckoo_yx_push_string(vector, domain)) {
                xx_str_free(domain.data);
                return false;
            }
        } while (xx_json_more(json));
    }
    return xx_json_array_end(json);
}

static bool xx_cuckoo_yx_http_array(xx_json *json,
                                   xx_cuckoo_yx_http_vector *vector)
{
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_array_begin(json)) return false;
    if (!xx_json_array_empty(json)) {
        do {
            xx_cuckoo_yx_http item = {0};
            xx_cuckoo_yx_raw method = {0}, uri = {0}, user_agent = {0};
            if (!xx_json_object_begin(json)) return false;
            if (!xx_json_object_empty(json)) {
                do {
                    char *key = NULL;
                    size_t key_length = 0;
                    xx_cuckoo_yx_raw *slot = NULL;
                    bool ok;
                    if (!xx_cuckoo_yx_object_key(json, &key, &key_length))
                        goto item_error;
                    if (xx_cuckoo_yx_key_equal(key, key_length, "method")) {
                        slot = &method;
                    } else if (xx_cuckoo_yx_key_equal(key, key_length, "uri")) {
                        slot = &uri;
                    } else if (xx_cuckoo_yx_key_equal(key, key_length,
                                                    "user-agent")) {
                        slot = &user_agent;
                    }
                    if (slot) ok = xx_cuckoo_yx_capture(json, slot);
                    else ok = xx_cuckoo_yx_skip(json, 0);
                    xx_str_free(key);
                    if (!ok) goto item_error;
                } while (xx_json_more(json));
            }
            if (!xx_json_object_end(json)) goto item_error;
            /* Network array items are decoded from serde_json::Value maps,
             * where a duplicate key's final value replaces earlier ones. */
            if (method.present) {
                xx_json value;
                xx_json_init(&value, method.data, method.size);
                if (!xx_cuckoo_yx_optional_string(&value, &item.method))
                    goto item_error;
            }
            if (uri.present) {
                xx_json value;
                xx_json_init(&value, uri.data, uri.size);
                if (!xx_cuckoo_yx_optional_string(&value, &item.uri))
                    goto item_error;
            }
            if (user_agent.present) {
                xx_json value;
                xx_json_init(&value, user_agent.data, user_agent.size);
                if (!xx_cuckoo_yx_optional_string(&value, &item.user_agent))
                    goto item_error;
            }
            if (!xx_cuckoo_yx_push_http(vector, item)) goto item_error;
            continue;
item_error:
            xx_str_free(item.method.data);
            xx_str_free(item.uri.data);
            xx_str_free(item.user_agent.data);
            return false;
        } while (xx_json_more(json));
    }
    return xx_json_array_end(json);
}

static bool xx_cuckoo_yx_connection_array(
    xx_json *json, xx_cuckoo_yx_connection_vector *vector)
{
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_array_begin(json)) return false;
    if (!xx_json_array_empty(json)) {
        do {
            xx_cuckoo_yx_connection item = {0};
            xx_cuckoo_yx_raw dst = {0}, dst_domain = {0}, dport = {0};
            if (!xx_json_object_begin(json)) return false;
            if (!xx_json_object_empty(json)) {
                do {
                    char *key = NULL;
                    size_t key_length = 0;
                    xx_cuckoo_yx_raw *slot = NULL;
                    bool ok;
                    if (!xx_cuckoo_yx_object_key(json, &key, &key_length))
                        goto item_error;
                    if (xx_cuckoo_yx_key_equal(key, key_length, "dst")) {
                        slot = &dst;
                    } else if (xx_cuckoo_yx_key_equal(key, key_length,
                                                    "dst_domain")) {
                        slot = &dst_domain;
                    } else if (xx_cuckoo_yx_key_equal(key, key_length,
                                                    "dport")) {
                        slot = &dport;
                    }
                    if (slot) ok = xx_cuckoo_yx_capture(json, slot);
                    else ok = xx_cuckoo_yx_skip(json, 0);
                    xx_str_free(key);
                    if (!ok) goto item_error;
                } while (xx_json_more(json));
            }
            if (!xx_json_object_end(json)) goto item_error;
            if (dst.present) {
                xx_json value;
                xx_json_init(&value, dst.data, dst.size);
                if (!xx_cuckoo_yx_optional_string(&value, &item.dst))
                    goto item_error;
            }
            if (dst_domain.present) {
                xx_json value;
                xx_json_init(&value, dst_domain.data, dst_domain.size);
                if (!xx_cuckoo_yx_optional_string(&value, &item.dst_domain))
                    goto item_error;
            }
            if (dport.present) {
                xx_json value;
                xx_json_init(&value, dport.data, dport.size);
                if (xx_json_peek(&value) == XX_JSON_TYPE_NULL) {
                    if (!xx_json_null(&value)) goto item_error;
                } else {
                    if (!xx_cuckoo_yx_unsigned(&value, &item.dport))
                        goto item_error;
                    item.has_dport = true;
                }
            }
            if (!xx_cuckoo_yx_push_connection(vector, item)) goto item_error;
            continue;
item_error:
            xx_str_free(item.dst.data);
            xx_str_free(item.dst_domain.data);
            return false;
        } while (xx_json_more(json));
    }
    return xx_json_array_end(json);
}

static bool xx_cuckoo_yx_network(xx_json *json, xx_cuckoo_yx_report *report)
{
    xx_cuckoo_yx_raw domains = {0}, old_dns = {0};
    /* Null repeats of these four fields leave an earlier array intact. */
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_object_begin(json)) return false;
    if (!xx_json_object_empty(json)) {
        do {
            char *key = NULL;
            size_t key_length = 0;
            bool ok;
            if (!xx_cuckoo_yx_object_key(json, &key, &key_length)) return false;
            if (xx_cuckoo_yx_key_equal(key, key_length, "domains"))
                ok = xx_cuckoo_yx_capture(json, &domains);
            else if (xx_cuckoo_yx_key_equal(key, key_length, "dns"))
                ok = xx_cuckoo_yx_capture(json, &old_dns);
            else if (xx_cuckoo_yx_key_equal(key, key_length, "http")) {
                if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
                    ok = xx_json_null(json);
                else {
                    xx_cuckoo_yx_http_free(&report->http);
                    ok = xx_cuckoo_yx_http_array(json, &report->http);
                }
            } else if (xx_cuckoo_yx_key_equal(key, key_length, "tcp")) {
                if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
                    ok = xx_json_null(json);
                else {
                    xx_cuckoo_yx_connections_free(&report->tcp);
                    ok = xx_cuckoo_yx_connection_array(json, &report->tcp);
                }
            } else if (xx_cuckoo_yx_key_equal(key, key_length, "udp")) {
                if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
                    ok = xx_json_null(json);
                else {
                    xx_cuckoo_yx_connections_free(&report->udp);
                    ok = xx_cuckoo_yx_connection_array(json, &report->udp);
                }
            } else if (xx_cuckoo_yx_key_equal(key, key_length, "hosts")) {
                if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
                    ok = xx_json_null(json);
                else {
                    xx_cuckoo_yx_strings_free(&report->hosts);
                    ok = xx_cuckoo_yx_string_array(json, &report->hosts);
                }
            } else ok = xx_cuckoo_yx_skip(json, 0);
            xx_str_free(key);
            if (!ok) return false;
        } while (xx_json_more(json));
    }
    if (!xx_json_object_end(json)) return false;
    if (domains.present) {
        xx_json selected;
        xx_json_init(&selected, domains.data, domains.size);
        xx_cuckoo_yx_strings_free(&report->domains);
        return xx_cuckoo_yx_domain_array(&selected, &report->domains,
                                       "domain");
    }
    if (old_dns.present) {
        xx_json selected;
        xx_json_init(&selected, old_dns.data, old_dns.size);
        xx_cuckoo_yx_strings_free(&report->domains);
        return xx_cuckoo_yx_domain_array(&selected, &report->domains,
                                       "hostname");
    }
    return true;
}

static bool xx_cuckoo_yx_summary(xx_json *json, xx_cuckoo_yx_report *report)
{
    unsigned seen = 0;
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_object_begin(json)) return false;
    if (!xx_json_object_empty(json)) {
        do {
            char *key = NULL;
            size_t key_length = 0;
            bool ok;
            if (!xx_cuckoo_yx_object_key(json, &key, &key_length)) return false;
            if (xx_cuckoo_yx_key_equal(key, key_length, "mutexes")) {
                xx_cuckoo_yx_strings_free(&report->mutexes);
                ok = xx_cuckoo_yx_mark_once(&seen, 1u) &&
                     xx_cuckoo_yx_string_array(json, &report->mutexes);
            } else if (xx_cuckoo_yx_key_equal(key, key_length, "files")) {
                xx_cuckoo_yx_strings_free(&report->files);
                ok = xx_cuckoo_yx_mark_once(&seen, 2u) &&
                     xx_cuckoo_yx_string_array(json, &report->files);
            } else if (xx_cuckoo_yx_key_equal(key, key_length, "keys")) {
                xx_cuckoo_yx_strings_free(&report->keys);
                ok = xx_cuckoo_yx_mark_once(&seen, 4u) &&
                     xx_cuckoo_yx_string_array(json, &report->keys);
            } else ok = xx_cuckoo_yx_skip(json, 0);
            xx_str_free(key);
            if (!ok) return false;
        } while (xx_json_more(json));
    }
    return xx_json_object_end(json);
}

static bool xx_cuckoo_yx_behavior(xx_json *json,
                                 xx_cuckoo_yx_report *report)
{
    unsigned seen = 0;
    if (xx_json_peek(json) == XX_JSON_TYPE_NULL)
        return xx_json_null(json);
    if (!xx_json_object_begin(json)) return false;
    if (!xx_json_object_empty(json)) {
        do {
            char *key = NULL;
            size_t key_length = 0;
            bool ok;
            if (!xx_cuckoo_yx_object_key(json, &key, &key_length)) return false;
            if (xx_cuckoo_yx_key_equal(key, key_length, "summary"))
                ok = xx_cuckoo_yx_mark_once(&seen, 1u) &&
                     xx_cuckoo_yx_summary(json, report);
            else ok = xx_cuckoo_yx_skip(json, 0);
            xx_str_free(key);
            if (!ok) return false;
        } while (xx_json_more(json));
    }
    return xx_json_object_end(json);
}

#include "xx_cuckoo_yx_errors.inc"

static bool xx_cuckoo_yx_report_parse_data(const uint8_t *json, size_t size,
                             xx_cuckoo_yx_report **out)
{
    xx_cuckoo_yx_report *report;
    xx_json cursor;
    unsigned seen = 0;
    if (!out || (!json && size)) return false;
    *out = NULL;
    if (size && !xx_cuckoo_yx_valid_utf8(json, size)) return false;
    report = calloc(1, sizeof(*report));
    if (!report) return false;
    if (size == 0) {
        *out = report;
        return true;
    }
    xx_json_init(&cursor, json, size);
    /* Derived serde structs also accept a sequence in declaration order. */
    if (xx_json_peek(&cursor) == XX_JSON_TYPE_ARRAY) {
        if (!xx_json_array_begin(&cursor) || xx_json_array_empty(&cursor) ||
            !xx_cuckoo_yx_network(&cursor, report) || !xx_json_more(&cursor) ||
            !xx_cuckoo_yx_behavior(&cursor, report) || xx_json_more(&cursor) ||
            !xx_json_array_end(&cursor)) goto error;
        goto end_of_document;
    }
    if (!xx_json_object_begin(&cursor)) goto error;
    if (!xx_json_object_empty(&cursor)) {
        do {
            char *key = NULL;
            size_t key_length = 0;
            bool ok;
            if (!xx_cuckoo_yx_object_key(&cursor, &key, &key_length))
                goto error;
            if (xx_cuckoo_yx_key_equal(key, key_length, "network"))
                ok = xx_cuckoo_yx_mark_once(&seen, 1u) &&
                     xx_cuckoo_yx_network(&cursor, report);
            else if (xx_cuckoo_yx_key_equal(key, key_length, "behavior"))
                ok = xx_cuckoo_yx_mark_once(&seen, 2u) &&
                     xx_cuckoo_yx_behavior(&cursor, report);
            else ok = xx_cuckoo_yx_skip(&cursor, 0);
            xx_str_free(key);
            if (!ok) goto error;
        } while (xx_json_more(&cursor));
    }
    if (!xx_json_object_end(&cursor)) goto error;
end_of_document:
    while (cursor.position < cursor.size &&
           (cursor.data[cursor.position] == ' ' ||
            cursor.data[cursor.position] == '\t' ||
            cursor.data[cursor.position] == '\r' ||
            cursor.data[cursor.position] == '\n'))
        ++cursor.position;
    if (cursor.position != cursor.size) goto error;
    *out = report;
    return true;
error:
    xx_cuckoo_yx_report_free(report);
    return false;
}

XXFC_API bool xx_cuckoo_yx_report_parse_ex(const uint8_t *json, size_t size,
    xx_cuckoo_yx_report **out, char *error, size_t error_capacity)
{
    bool ok = xx_cuckoo_yx_report_parse_data(json, size, out);
    if (error && error_capacity) {
        error[0] = 0;
        if (!ok && json) xx_cuckoo_yx_json_diagnostic(json, size, error, error_capacity);
    }
    return ok;
}

XXFC_API bool xx_cuckoo_yx_report_parse(const uint8_t *json, size_t size,
    xx_cuckoo_yx_report **out)
{
    return xx_cuckoo_yx_report_parse_ex(json, size, out, NULL, 0);
}

static bool xx_cuckoo_yx_ascii_equal(const xx_cuckoo_yx_text *left,
                                   const char *right)
{
    size_t length = strlen(right);
    if (!left->data || left->length != length) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)left->data[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
        if (a != b) return false;
    }
    return true;
}

static uint64_t xx_cuckoo_yx_count_strings(
    const xx_cuckoo_yx_strings *vector, xx_cuckoo_yx_match_fn matches,
    void *opaque)
{
    uint64_t count = 0;
    for (size_t i = 0; i < vector->count; ++i) {
        const xx_cuckoo_yx_text *text = &vector->items[i];
        if (matches((const uint8_t *)text->data, text->length, opaque))
            ++count;
    }
    return count;
}

XXFC_API bool xx_cuckoo_yx_query(const xx_cuckoo_yx_report *report,
                      const char *function, xx_cuckoo_yx_match_fn matches,
                      void *opaque, bool has_port, int64_t port,
                      uint64_t *count)
{
    if (!report || !function || !matches || !count) return false;
    *count = 0;
    if (strcmp(function, "network.dns_lookup") == 0) {
        if (has_port) return false;
        *count = xx_cuckoo_yx_count_strings(&report->domains, matches, opaque);
    } else if (strcmp(function, "network.host") == 0) {
        if (has_port) return false;
        *count = xx_cuckoo_yx_count_strings(&report->hosts, matches, opaque);
    } else if (strcmp(function, "sync.mutex") == 0) {
        if (has_port) return false;
        *count = xx_cuckoo_yx_count_strings(&report->mutexes, matches, opaque);
    } else if (strcmp(function, "filesystem.file_access") == 0) {
        if (has_port) return false;
        *count = xx_cuckoo_yx_count_strings(&report->files, matches, opaque);
    } else if (strcmp(function, "registry.key_access") == 0) {
        if (has_port) return false;
        *count = xx_cuckoo_yx_count_strings(&report->keys, matches, opaque);
    } else if (strcmp(function, "network.http_request") == 0 ||
               strcmp(function, "network.http_get") == 0 ||
               strcmp(function, "network.http_post") == 0 ||
               strcmp(function, "network.http_user_agent") == 0) {
        bool agent = strcmp(function, "network.http_user_agent") == 0;
        bool get = strcmp(function, "network.http_get") == 0;
        bool post = strcmp(function, "network.http_post") == 0;
        if (has_port) return false;
        for (size_t i = 0; i < report->http.count; ++i) {
            const xx_cuckoo_yx_http *item = &report->http.items[i];
            const xx_cuckoo_yx_text *text = agent ? &item->user_agent :
                                                 &item->uri;
            if (!agent && (!item->method.data ||
                (get && !xx_cuckoo_yx_ascii_equal(&item->method, "get")) ||
                (post && !xx_cuckoo_yx_ascii_equal(&item->method, "post"))))
                continue;
            if (text->data && matches((const uint8_t *)text->data,
                                      text->length, opaque)) ++*count;
        }
    } else if (strcmp(function, "network.tcp") == 0 ||
               strcmp(function, "network.udp") == 0) {
        const xx_cuckoo_yx_connection_vector *vector =
            strcmp(function, "network.tcp") == 0 ?
                &report->tcp : &report->udp;
        if (!has_port) return false;
        for (size_t i = 0; i < vector->count; ++i) {
            const xx_cuckoo_yx_connection *item = &vector->items[i];
            if (!item->has_dport || item->dport != (uint64_t)port)
                continue;
            if ((item->dst.data &&
                 matches((const uint8_t *)item->dst.data,
                         item->dst.length, opaque)) ||
                (item->dst_domain.data &&
                 matches((const uint8_t *)item->dst_domain.data,
                         item->dst_domain.length, opaque)))
                ++*count;
        }
    } else return false;
    return true;
}

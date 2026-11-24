/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/json/xx_json_writer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#define JSON_FLUSH_SIZE (1u << 20) /* 1 MiB write flush threshold */

typedef enum xx_json_container_e {
    XX_JSON_CONTAINER_NONE = 0,
    XX_JSON_CONTAINER_OBJECT = 1,
    XX_JSON_CONTAINER_ARRAY = 2
} xx_json_container_t;

typedef struct xx_json_frame_s {
    uint8_t container; /* XX_JSON_CONTAINER_OBJECT or XX_JSON_CONTAINER_ARRAY */
    bool has_items;
} xx_json_frame_t;

struct xx_json_writer {
    xx_io_device *device;
    char *buf;
    size_t buf_size;
    size_t buf_capacity;
    int indent;                 /* 0 = compact, > 0 = indentation spaces */
    int depth;                  /* Current nesting depth (0 = top-level) */
    bool failed;                /* Sticky error flag */
    bool finished;              /* finish() has been called */
    bool crlf;                  /* true = CRLF, false = LF (default) */
    bool trailing_newline;      /* true = emit newline at finish (default) */
    bool top_level_done;        /* true once a top-level value was written */
    xx_json_frame_t stack[XX_JSON_WRITER_MAX_DEPTH];
};

/* ---- Internal buffer management ----------------------------------------- */

static bool buf_reserve(xx_json_writer *w, size_t extra)
{
    if (w->failed) {
        return false;
    }

    if (extra > (SIZE_MAX - w->buf_size - 1)) {
        w->failed = true;
        return false;
    }

    size_t need = w->buf_size + extra + 1;
    if (need > w->buf_capacity) {
        size_t cap = w->buf_capacity ? w->buf_capacity : 4096;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) {
                cap = need;
                break;
            }
            cap *= 2;
        }

        char *new_buf = (char *)xx_mem_realloc(w->buf, cap);
        if (!new_buf) {
            w->failed = true;
            return false;
        }

        w->buf = new_buf;
        w->buf_capacity = cap;
    }

    return true;
}

static void buf_append_n(xx_json_writer *w, const char *data, size_t len)
{
    if (buf_reserve(w, len)) {
        if (len && data) {
            memcpy(w->buf + w->buf_size, data, len);
        }
        w->buf_size += len;
        w->buf[w->buf_size] = '\0';
    }
}

static void buf_append_str(xx_json_writer *w, const char *str)
{
    if (str) {
        buf_append_n(w, str, strlen(str));
    }
}

static void buf_append_char(xx_json_writer *w, char c)
{
    buf_append_n(w, &c, 1);
}

static void buf_append_spaces(xx_json_writer *w, size_t count)
{
    if (buf_reserve(w, count)) {
        memset(w->buf + w->buf_size, ' ', count);
        w->buf_size += count;
        w->buf[w->buf_size] = '\0';
    }
}

static void json_flush(xx_json_writer *w)
{
    if (!w || !w->device || !w->buf_size) {
        return;
    }

    size_t done = 0;
    while ((done < w->buf_size) && (!w->failed)) {
        ssize_t written = xx_io_write(w->device, w->buf + done, w->buf_size - done);
        if (written <= 0) {
            w->failed = true;
            break;
        }
        done += (size_t)written;
    }

    if (done == w->buf_size) {
        w->buf_size = 0;
        if (w->buf) {
            w->buf[0] = '\0';
        }
    } else if (done > 0 && done < w->buf_size) {
        memmove(w->buf, w->buf + done, w->buf_size - done);
        w->buf_size -= done;
        w->buf[w->buf_size] = '\0';
    }
}

static void check_flush(xx_json_writer *w)
{
    if (w->buf_size >= JSON_FLUSH_SIZE) {
        json_flush(w);
    }
}

static void json_indent(xx_json_writer *w)
{
    if (w->indent > 0) {
        if (w->crlf) {
            buf_append_n(w, "\r\n", 2);
        } else {
            buf_append_char(w, '\n');
        }

        if (w->depth > 0) {
            buf_append_spaces(w, (size_t)w->depth * (size_t)w->indent);
        }
    }
}

/* ---- Escaping ----------------------------------------------------------- */

size_t xx_json_escape(char *out, size_t capacity, const char *utf8, size_t length)
{
    size_t needed = 0;
    size_t written = 0;
    size_t i = 0;

    static const char hex[] = "0123456789abcdef";

#define ESCAPE_EMIT_BYTE(b) do { \
    if (out && capacity > 0 && (written + 1 < capacity)) { \
        out[written] = (char)(b); \
    } \
    written++; \
    needed++; \
} while (0)

    if (!utf8) {
        length = 0;
    }

    while (i < length) {
        unsigned char value = (unsigned char)utf8[i];

        if (value < 0x80) {
            if (value == '"') {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('"');
            } else if (value == '\\') {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('\\');
            } else if (value == '\n') {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('n');
            } else if (value == '\r') {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('r');
            } else if (value == '\t') {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('t');
            } else if (value < 0x20 || value == 0x7f) {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('u');
                ESCAPE_EMIT_BYTE('0');
                ESCAPE_EMIT_BYTE('0');
                ESCAPE_EMIT_BYTE(hex[(value >> 4) & 0x0F]);
                ESCAPE_EMIT_BYTE(hex[value & 0x0F]);
            } else {
                ESCAPE_EMIT_BYTE(value);
            }
            i++;
        } else {
            size_t seq_len = 0;
            uint32_t code = 0;
            bool ok = true;

            if ((value & 0xE0) == 0xC0) {
                seq_len = 2;
                code = value & 0x1F;
            } else if ((value & 0xF0) == 0xE0) {
                seq_len = 3;
                code = value & 0x0F;
            } else if ((value & 0xF8) == 0xF0) {
                seq_len = 4;
                code = value & 0x07;
            } else {
                ok = false;
            }

            if (ok && (seq_len > (length - i))) {
                ok = false;
            }

            if (ok) {
                for (size_t k = 1; k < seq_len; k++) {
                    unsigned char c = (unsigned char)utf8[i + k];
                    if ((c & 0xC0) != 0x80) {
                        ok = false;
                        break;
                    }
                    code = (code << 6) | (c & 0x3F);
                }
            }

            if (ok) {
                if (((seq_len == 2) && (code < 0x80)) ||
                    ((seq_len == 3) && (code < 0x800)) ||
                    ((seq_len == 4) && ((code < 0x10000) || (code > 0x10FFFF))) ||
                    ((code >= 0xD800) && (code <= 0xDFFF))) {
                    ok = false;
                }
            }

            if (ok) {
                for (size_t k = 0; k < seq_len; k++) {
                    ESCAPE_EMIT_BYTE(utf8[i + k]);
                }
                i += seq_len;
            } else {
                ESCAPE_EMIT_BYTE('\\');
                ESCAPE_EMIT_BYTE('u');
                ESCAPE_EMIT_BYTE('f');
                ESCAPE_EMIT_BYTE('f');
                ESCAPE_EMIT_BYTE('f');
                ESCAPE_EMIT_BYTE('d');
                i++;
            }
        }
    }

#undef ESCAPE_EMIT_BYTE

    if (out && capacity > 0) {
        if (written < capacity) {
            out[written] = '\0';
        } else {
            out[capacity - 1] = '\0';
        }
    }

    return needed;
}

static void json_append_string_escaped(xx_json_writer *w, const char *utf8, size_t length)
{
    buf_append_char(w, '"');
    if (!utf8 || length == 0) {
        buf_append_char(w, '"');
        return;
    }

    static const char hex[] = "0123456789abcdef";
    size_t i = 0;

    while (i < length && !w->failed) {
        unsigned char value = (unsigned char)utf8[i];

        if (value < 0x80) {
            if (value == '"') {
                buf_append_n(w, "\\\"", 2);
            } else if (value == '\\') {
                buf_append_n(w, "\\\\", 2);
            } else if (value == '\n') {
                buf_append_n(w, "\\n", 2);
            } else if (value == '\r') {
                buf_append_n(w, "\\r", 2);
            } else if (value == '\t') {
                buf_append_n(w, "\\t", 2);
            } else if (value < 0x20 || value == 0x7f) {
                char esc[6];
                esc[0] = '\\';
                esc[1] = 'u';
                esc[2] = '0';
                esc[3] = '0';
                esc[4] = hex[(value >> 4) & 0x0F];
                esc[5] = hex[value & 0x0F];
                buf_append_n(w, esc, 6);
            } else {
                /* Safe ASCII: batch contiguous bytes */
                size_t start = i;
                while (i < length) {
                    unsigned char v = (unsigned char)utf8[i];
                    if (v < 0x20 || v >= 0x7f || v == '"' || v == '\\') {
                        break;
                    }
                    i++;
                }
                buf_append_n(w, utf8 + start, i - start);
                continue;
            }
            i++;
        } else {
            size_t seq_len = 0;
            uint32_t code = 0;
            bool ok = true;

            if ((value & 0xE0) == 0xC0) {
                seq_len = 2;
                code = value & 0x1F;
            } else if ((value & 0xF0) == 0xE0) {
                seq_len = 3;
                code = value & 0x0F;
            } else if ((value & 0xF8) == 0xF0) {
                seq_len = 4;
                code = value & 0x07;
            } else {
                ok = false;
            }

            if (ok && (seq_len > (length - i))) {
                ok = false;
            }

            if (ok) {
                for (size_t k = 1; k < seq_len; k++) {
                    unsigned char c = (unsigned char)utf8[i + k];
                    if ((c & 0xC0) != 0x80) {
                        ok = false;
                        break;
                    }
                    code = (code << 6) | (c & 0x3F);
                }
            }

            if (ok) {
                if (((seq_len == 2) && (code < 0x80)) ||
                    ((seq_len == 3) && (code < 0x800)) ||
                    ((seq_len == 4) && ((code < 0x10000) || (code > 0x10FFFF))) ||
                    ((code >= 0xD800) && (code <= 0xDFFF))) {
                    ok = false;
                }
            }

            if (ok) {
                buf_append_n(w, utf8 + i, seq_len);
                i += seq_len;
            } else {
                buf_append_n(w, "\\ufffd", 6);
                i++;
            }
        }
    }

    buf_append_char(w, '"');
}

/* ---- Value start & container management --------------------------------- */

static void json_value_start(xx_json_writer *w, const char *key)
{
    if (w->failed || w->finished) {
        return;
    }

    if (w->depth == 0) {
        if (w->top_level_done) {
            /* Misuse: multiple top-level values */
            w->failed = true;
            return;
        }
        if (key != NULL) {
            /* Misuse: key at top-level */
            w->failed = true;
            return;
        }
    } else {
        xx_json_frame_t *frame = &w->stack[w->depth];
        if (frame->container == XX_JSON_CONTAINER_ARRAY) {
            if (key != NULL) {
                /* Misuse: key inside an array */
                w->failed = true;
                return;
            }
        } else if (frame->container == XX_JSON_CONTAINER_OBJECT) {
            if (key == NULL) {
                /* Misuse: missing key inside an object */
                w->failed = true;
                return;
            }
        }

        if (frame->has_items) {
            buf_append_char(w, ',');
        }
        frame->has_items = true;
        json_indent(w);
    }

    if (key) {
        json_append_string_escaped(w, key, strlen(key));
        if (w->indent > 0) {
            buf_append_n(w, ": ", 2);
        } else {
            buf_append_char(w, ':');
        }
    }
}

static void json_open(xx_json_writer *w, const char *key, char bracket, uint8_t container)
{
    if (!w || w->failed || w->finished) {
        return;
    }

    json_value_start(w, key);
    if (w->failed) {
        return;
    }

    buf_append_char(w, bracket);

    if (w->depth + 1 < XX_JSON_WRITER_MAX_DEPTH) {
        w->depth++;
        w->stack[w->depth].container = container;
        w->stack[w->depth].has_items = false;
    } else {
        /* Exceeded nesting depth limit */
        w->failed = true;
    }
}

static void json_close(xx_json_writer *w, char bracket, uint8_t expected_container)
{
    if (!w || w->failed || w->finished) {
        return;
    }

    if (w->depth <= 0) {
        /* Misuse: end without begin */
        w->failed = true;
        return;
    }

    if (w->stack[w->depth].container != expected_container) {
        /* Misuse: container mismatch (e.g. end_object on array) */
        w->failed = true;
        return;
    }

    bool had_items = w->stack[w->depth].has_items;
    w->depth--;

    if (had_items) {
        json_indent(w);
    }

    buf_append_char(w, bracket);

    if (w->depth == 0) {
        w->top_level_done = true;
    }

    check_flush(w);
}

/* ---- Public API --------------------------------------------------------- */

xx_json_writer *xx_json_writer_create(xx_io_device *device, int indent)
{
    if (!device) {
        return NULL;
    }

    xx_json_writer *w = (xx_json_writer *)xx_mem_alloc(sizeof(xx_json_writer));
    if (!w) {
        return NULL;
    }

    memset(w, 0, sizeof(*w));
    w->device = device;
    w->indent = (indent < 0) ? 0 : (indent > 16 ? 16 : indent);
    w->depth = 0;
    w->failed = false;
    w->finished = false;
    w->crlf = false;
    w->trailing_newline = true;
    w->top_level_done = false;

    return w;
}

bool xx_json_writer_finish(xx_json_writer *w)
{
    if (!w) {
        return false;
    }
    if (w->finished) {
        return !w->failed;
    }

    if (w->depth != 0 || !w->top_level_done) {
        /* Misuse: unclosed containers or empty document */
        w->failed = true;
    }

    if (!w->failed && w->trailing_newline) {
        if (w->crlf) {
            buf_append_n(w, "\r\n", 2);
        } else {
            buf_append_char(w, '\n');
        }
    }

    json_flush(w);
    w->finished = true;

    if (w->buf) {
        xx_mem_free(w->buf);
        w->buf = NULL;
        w->buf_size = 0;
        w->buf_capacity = 0;
    }

    return !w->failed;
}

void xx_json_writer_destroy(xx_json_writer *w)
{
    if (!w) {
        return;
    }

    if (w->buf) {
        xx_mem_free(w->buf);
        w->buf = NULL;
    }

    xx_mem_free(w);
}

void xx_json_writer_begin_object(xx_json_writer *w, const char *key)
{
    json_open(w, key, '{', XX_JSON_CONTAINER_OBJECT);
}

void xx_json_writer_end_object(xx_json_writer *w)
{
    json_close(w, '}', XX_JSON_CONTAINER_OBJECT);
}

void xx_json_writer_begin_array(xx_json_writer *w, const char *key)
{
    json_open(w, key, '[', XX_JSON_CONTAINER_ARRAY);
}

void xx_json_writer_end_array(xx_json_writer *w)
{
    json_close(w, ']', XX_JSON_CONTAINER_ARRAY);
}

void xx_json_writer_string_n(xx_json_writer *w, const char *key, const char *utf8, size_t length)
{
    if (!w) return;
    json_value_start(w, key);
    if (w->failed) return;

    json_append_string_escaped(w, utf8, length);
    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_string(xx_json_writer *w, const char *key, const char *utf8)
{
    xx_json_writer_string_n(w, key, utf8, utf8 ? strlen(utf8) : 0);
}

void xx_json_writer_int(xx_json_writer *w, const char *key, int64_t v)
{
    if (!w) return;
    json_value_start(w, key);
    if (w->failed) return;

    if (v < 0) {
        buf_append_char(w, '-');
        uint64_t uv = (uint64_t)0 - (uint64_t)v;
        char digits[32];
        size_t pos = sizeof(digits);
        do {
            digits[--pos] = (char)('0' + (uv % 10));
            uv /= 10;
        } while (uv);
        buf_append_n(w, digits + pos, sizeof(digits) - pos);
    } else {
        uint64_t uv = (uint64_t)v;
        char digits[32];
        size_t pos = sizeof(digits);
        do {
            digits[--pos] = (char)('0' + (uv % 10));
            uv /= 10;
        } while (uv);
        buf_append_n(w, digits + pos, sizeof(digits) - pos);
    }

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_uint(xx_json_writer *w, const char *key, uint64_t v)
{
    if (!w) return;
    json_value_start(w, key);
    if (w->failed) return;

    char digits[32];
    size_t pos = sizeof(digits);
    do {
        digits[--pos] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    buf_append_n(w, digits + pos, sizeof(digits) - pos);

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_double(xx_json_writer *w, const char *key, double v)
{
    if (!w) return;
    if (!isfinite(v)) {
        xx_json_writer_null(w, key);
        return;
    }

    json_value_start(w, key);
    if (w->failed) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", v);
    for (char *p = buf; *p; p++) {
        if (*p == ',') {
            *p = '.';
        }
    }
    buf_append_str(w, buf);

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_bool(xx_json_writer *w, const char *key, bool v)
{
    if (!w) return;
    json_value_start(w, key);
    if (w->failed) return;

    if (v) {
        buf_append_n(w, "true", 4);
    } else {
        buf_append_n(w, "false", 5);
    }

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_null(xx_json_writer *w, const char *key)
{
    if (!w) return;
    json_value_start(w, key);
    if (w->failed) return;

    buf_append_n(w, "null", 4);

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_raw_n(xx_json_writer *w, const char *key, const char *json_text, size_t length)
{
    if (!w) return;
    if (!json_text) {
        xx_json_writer_null(w, key);
        return;
    }

    json_value_start(w, key);
    if (w->failed) return;

    buf_append_n(w, json_text, length);

    if (w->depth == 0) {
        w->top_level_done = true;
    }
    check_flush(w);
}

void xx_json_writer_raw(xx_json_writer *w, const char *key, const char *json_text)
{
    if (!json_text) {
        xx_json_writer_null(w, key);
    } else {
        xx_json_writer_raw_n(w, key, json_text, strlen(json_text));
    }
}

bool xx_json_writer_failed(const xx_json_writer *w)
{
    return w ? w->failed : true;
}

void xx_json_writer_set_crlf(xx_json_writer *w, bool crlf)
{
    if (w) {
        w->crlf = crlf;
    }
}

void xx_json_writer_set_trailing_newline(xx_json_writer *w, bool enable)
{
    if (w) {
        w->trailing_newline = enable;
    }
}

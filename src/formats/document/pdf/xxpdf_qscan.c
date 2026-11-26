/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PDF structure scan with the rules of _mylibs/XPDF/xpdf.cpp. Each function
 * names the XPDF member it follows. The reference is line oriented and does
 * not validate the document, and DIE reports depend on that behaviour:
 *  - a "startxref" is used only when it points at "xref" or at an object
 *    header in front of it and is followed by "%%EOF";
 *  - without a usable "startxref" the objects are read one after another
 *    from the start of the file until the first line that is neither an
 *    object nor a comment;
 *  - an object is a list of tokens ("<<", "/Name", "12 0 R", ...), cut at a
 *    token limit, and names are found by comparing whole tokens;
 *  - everything after the last footer and the last object is the overlay.
 * Quirks of the reference tokenizer are kept on purpose (only generation 0
 * references are joined into one token, comments are not skipped, a stream
 * keyword followed by a space is not a stream).
 */
#include "xxpdf_qscan.h"
#include "xxpdf_decode.h"
#include "xxpdf_date.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/sort/xx_sort.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define QS_CACHE_SIZE ((size_t)64U * 1024U)
#define QS_DECODE_LIMIT ((int64_t)100 * 1024 * 1024) /* PDF_DEFAULT_DECODE_OUTPUT_LIMIT */
#define QS_STRING_LIMIT ((int64_t)32 * 1024 * 1024)
#define QS_TOKEN_KEEP 512U /* longest name the reference can produce */
#define QS_TRIGGERS 12U

typedef struct {
    int64_t xref_offset, footer_offset, footer_size;
    bool is_object, is_xref;
} qs_href;
typedef struct {
    int64_t offset, size;
    uint64_t id, order;
} qs_object;
typedef struct {
    qs_object *item;
    size_t count, capacity;
} qs_objects;
/* QMap<quint64, qint64> / QSet<qint64>: insert replaces the value. */
typedef struct {
    uint64_t *key;
    int64_t *value;
    uint8_t *used;
    size_t count, capacity;
} qs_map;
typedef struct {
    size_t at, size;     /* text kept in the pool */
    int64_t offset, full; /* extent in the file, for strings and hex strings */
    bool null;            /* a null QString: no text was read at all */
} qs_token;
typedef struct {
    uint16_t *unit; /* QString contents */
    size_t count, capacity;
} qs_units;
typedef struct {
    qs_token *token;
    size_t count, capacity;
    char *pool;
    size_t used, pool_capacity;
} qs_parts;
typedef struct {
    qs_parts parts;
    size_t stream_count;
    int64_t stream_offset, stream_size; /* the first stream */
} qs_xpart;
struct xx_pdf_qscan {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t base, size, header_offset;
    uint8_t *cache;
    int64_t cache_start;
    size_t cache_size;
    bool failed; /* I/O error or out of memory */
    qs_href *href;
    size_t href_count, href_capacity;
    qs_objects objects;
    qs_map id_offset;
};

static const char *const qs_triggers[QS_TRIGGERS] = {"/OpenAction", "/AA",         "/JavaScript", "/JS",        "/Launch",   "/EmbeddedFile",
                                                     "/URI",        "/SubmitForm", "/GoToR",      "/RichMedia", "/AcroForm", "/XFA"};

/* ---- device access ------------------------------------------------------ */

static bool qs_stopped(xx_pdf_qscan *q)
{
    return q->failed || (q->pd && xx_pd_is_stopped(q->pd));
}
static bool qs_fill(xx_pdf_qscan *q, int64_t at)
{
    size_t want, done = 0;
    q->cache_start = at - at % (int64_t)QS_CACHE_SIZE;
    want = q->size - q->cache_start < (int64_t)QS_CACHE_SIZE ? (size_t)(q->size - q->cache_start) : QS_CACHE_SIZE;
    q->cache_size = 0;
    if (xx_io_seek64(q->device, q->base + q->cache_start, SEEK_SET)) {
        q->failed = true;
        return false;
    }
    while (done < want) {
        ssize_t got = xx_io_read(q->device, q->cache + done, want - done);
        if (got <= 0 || (size_t)got > want - done) {
            q->failed = true;
            return false;
        }
        done += (size_t)got;
    }
    q->cache_size = want;
    return true;
}
/* read_uint8: -1 outside the file. */
static int qs_byte(xx_pdf_qscan *q, int64_t at)
{
    if (at < 0 || at >= q->size || q->failed) return -1;
    if (at < q->cache_start || at - q->cache_start >= (int64_t)q->cache_size)
        if (!qs_fill(q, at)) return -1;
    return q->cache[(size_t)(at - q->cache_start)];
}
/* read_array: the result is cut at the end of the file. */
static size_t qs_read(xx_pdf_qscan *q, int64_t at, uint8_t *out, size_t size)
{
    size_t done = 0;
    if (at < 0 || at >= q->size) return 0;
    if ((int64_t)size > q->size - at) size = (size_t)(q->size - at);
    while (done < size) {
        size_t in_cache, part;
        if (qs_byte(q, at) < 0) return done;
        in_cache = (size_t)(at - q->cache_start);
        part = q->cache_size - in_cache;
        if (part > size - done) part = size - done;
        xx_mem_copy(out + done, q->cache + in_cache, part);
        done += part;
        at += (int64_t)part;
    }
    return done;
}
/* find_ansiString / find_signature for a literal: size -1 means to the end. */
static int64_t qs_find(xx_pdf_qscan *q, int64_t at, int64_t size, const char *needle)
{
    size_t length = xx_rt_strlen(needle), i;
    int64_t end;
    if (at < 0 || at >= q->size || !length) return -1;
    if (size == -1 || size > q->size - at) size = q->size - at;
    end = at + size - (int64_t)length;
    while (at <= end) {
        const uint8_t *hit;
        size_t in_cache, span;
        if (qs_stopped(q) || qs_byte(q, at) < 0) return -1;
        in_cache = (size_t)(at - q->cache_start);
        span = q->cache_size - in_cache;
        if ((int64_t)span > end - at + 1) span = (size_t)(end - at + 1);
        hit = (const uint8_t *)xx_rt_memchr(q->cache + in_cache, (uint8_t)needle[0], span);
        if (!hit) {
            at += (int64_t)span;
            continue;
        }
        at += (int64_t)(hit - (q->cache + in_cache));
        for (i = 1; i < length; ++i)
            if (qs_byte(q, at + (int64_t)i) != (uint8_t)needle[i]) break;
        if (i == length) return at;
        ++at;
    }
    return -1;
}

/* ---- QString helpers ---------------------------------------------------- */

static bool qs_qspace(uint8_t c) /* QChar::isSpace for Latin-1 */
{
    return c == ' ' || (c >= 9U && c <= 13U) || c == 0x85U || c == 0xA0U;
}
/* QString::toLongLong: surrounding spaces, an optional sign and digits. */
static int64_t qs_to_ll(const char *text, size_t size, bool *ok)
{
    uint64_t value = 0, limit;
    bool negative = false, good;
    size_t i = 0;
    if (ok) *ok = false;
    while (i < size && qs_qspace((uint8_t)text[i])) ++i;
    while (size > i && qs_qspace((uint8_t)text[size - 1U])) --size;
    if (i < size && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    limit = negative ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
    good = i < size;
    for (; i < size; ++i) {
        uint64_t digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (uint64_t)(text[i] - '0');
        if (value > (limit - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    if (!good) return 0;
    if (ok) *ok = true;
    return negative ? (int64_t)(0U - value) : (int64_t)value;
}
static uint64_t qs_to_ull(const char *text, size_t size)
{
    uint64_t value = 0;
    size_t i = 0;
    while (i < size && qs_qspace((uint8_t)text[i])) ++i;
    while (size > i && qs_qspace((uint8_t)text[size - 1U])) --size;
    if (i < size && text[i] == '+') ++i;
    if (i == size) return 0;
    for (; i < size; ++i) {
        uint64_t digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (uint64_t)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    return value;
}
static int32_t qs_to_int(const char *text, size_t size) /* QString::toInt */
{
    bool ok;
    int64_t value = qs_to_ll(text, size, &ok);
    return ok && value >= INT32_MIN && value <= INT32_MAX ? (int32_t)value : 0;
}
/* QString::section(" ", index, index): empty fields count. */
static const char *qs_section(const char *text, size_t size, unsigned index, size_t *out_size)
{
    size_t start = 0, i;
    for (i = 0; i <= size; ++i) {
        if (i == size || text[i] == ' ') {
            if (!index) {
                *out_size = i - start;
                return text + start;
            }
            --index;
            start = i + 1U;
        }
    }
    *out_size = 0;
    return text + size;
}
static bool qs_is(const char *text, size_t size, const char *literal)
{
    size_t length = xx_rt_strlen(literal);
    return size == length && !xx_rt_memcmp(text, literal, length);
}
/* XPDF::_isObject: "N G obj" at the start of the line. */
static bool qs_is_object(const char *s, size_t n)
{
    size_t i = 0, digits = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; ++i) ++digits;
    if (!digits || i >= n || !(s[i] == ' ' || s[i] == '\t')) return false;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    for (digits = 0; i < n && s[i] >= '0' && s[i] <= '9'; ++i) ++digits;
    if (!digits || i >= n || !(s[i] == ' ' || s[i] == '\t')) return false;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i + 3U > n || s[i] != 'o' || s[i + 1U] != 'b' || s[i + 2U] != 'j') return false;
    i += 3U;
    if (i < n) {
        char c = s[i];
        if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '<' || c == '[' || c == '(' || c == '/' || c == '%')) return false;
    }
    return true;
}
static bool qs_is_end_object(const char *s, size_t n) /* trimmed() == "endobj" */
{
    while (n && qs_qspace((uint8_t)s[0])) {
        ++s;
        --n;
    }
    while (n && qs_qspace((uint8_t)s[n - 1U])) --n;
    return qs_is(s, n, "endobj");
}
static bool qs_is_comment(const char *s, size_t n)
{
    return n && s[0] == '%';
}
static bool qs_is_xref(const char *s, size_t n)
{
    return n >= 4U && !xx_rt_memcmp(s, "xref", 4U) && (n == 4U || s[4] == ' ');
}
/* XPDF::getObjectID: the leading digits as qint32, kept as quint64. */
static uint64_t qs_object_id(const char *s, size_t n)
{
    uint64_t value = 0;
    bool negative = false;
    size_t i = 0;
    if (i < n && s[i] == '-') {
        negative = true;
        ++i;
    }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; ++i) value = value * 10U + (uint64_t)(s[i] - '0');
    if (negative) value = 0U - value;
    return (uint64_t)(int64_t)(int32_t)(uint32_t)value;
}
/* XPDF::_isIndirectRef: "N G R" in one token. */
static bool qs_is_indirect(const char *s, size_t n)
{
    const char *field;
    size_t size;
    bool id, generation;
    if (!n || s[0] < '0' || s[0] > '9') return false;
    field = qs_section(s, n, 2, &size);
    if (!qs_is(field, size, "R")) return false;
    field = qs_section(s, n, 0, &size);
    (void)qs_to_ll(field, size, &id);
    field = qs_section(s, n, 1, &size);
    (void)qs_to_ll(field, size, &generation);
    return id && generation;
}

/* ---- line and token readers --------------------------------------------- */

/* XPDF::skipPDFEnding */
static int64_t qs_skip_ending(xx_pdf_qscan *q, int64_t *offset)
{
    int64_t at = *offset, start = at;
    for (;;) {
        int c = qs_byte(q, at);
        if (c == 10) ++at;
        else if (c == 13) {
            ++at;
            if (qs_byte(q, at) == 10) ++at;
        } else break;
    }
    *offset = at;
    return at - start;
}
/* XPDF::skipPDFSpace */
static int64_t qs_skip_space(xx_pdf_qscan *q, int64_t *offset)
{
    int64_t at = *offset, start = at;
    int c;
    while ((c = qs_byte(q, at)) == ' ' || c == 9 || c == 12) ++at;
    *offset = at;
    return at - start;
}
/* XPDF::_readPDFString (title = false) and _readPDFStringPart_title: one line
 * of at most `window` bytes. out holds window + 1 bytes. Returns the length
 * of the text; *consumed also counts the line ending after it. */
static size_t qs_line(xx_pdf_qscan *q, int64_t offset, size_t window, bool title, char *out, int64_t *consumed)
{
    size_t got, at;
    int64_t end;
    out[0] = 0;
    *consumed = 0;
    if (offset < 0 || offset >= q->size) return 0;
    got = qs_read(q, offset, (uint8_t *)out, window);
    for (at = 0; at < got; ++at) {
        char c = out[at];
        if (c == 0 || c == 10 || c == 13 || (title && c == '<')) break;
    }
    out[at] = 0;
    end = offset + (int64_t)at;
    *consumed = (int64_t)at + qs_skip_ending(q, &end);
    return at;
}
static bool qs_name_end(int c) /* isPdfNameTerminator */
{
    return c == 0 || c == 10 || c == 13 || c == '[' || c == ']' || c == '<' || c == '>' || c == ' ' || c == 9 || c == 12 || c == '(';
}
static bool qs_value_end(int c) /* isPdfValueTerminator */
{
    return c == 0 || c == 10 || c == 13 || c == '[' || c == ']' || c == '<' || c == '>' || c == '/';
}
static bool qs_units_add(xx_pdf_qscan *q, qs_units *u, unsigned unit)
{
    if (!u) return true;
    if (u->count == u->capacity) {
        size_t n = u->capacity ? u->capacity * 2U : 64U;
        uint16_t *next = n > SIZE_MAX / sizeof(*next) ? NULL : (uint16_t *)xx_mem_realloc(u->unit, n * sizeof(*next));
        if (!next) {
            q->failed = true;
            return false;
        }
        u->unit = next;
        u->capacity = n;
    }
    u->unit[u->count++] = (uint16_t)unit;
    return true;
}
/* XPDF::_readPDFStringPart_str: the size of a "(...)" literal and, when
 * units is given, its QString. The reference keeps the parentheses, drops
 * every backslash and takes the character after it as it is (no escape is
 * decoded); after a FE FF mark the bytes are read as 16-bit characters. */
static int64_t qs_string(xx_pdf_qscan *q, int64_t offset, qs_units *units)
{
    int64_t at = offset, size = 0;
    bool started = false, done = false, wide = false, escape = false;
    int depth = 0;
    while (!qs_stopped(q)) {
        int c = qs_byte(q, at);
        if (c < 0 || size > QS_STRING_LIMIT) break;
        if (!wide) {
            if (!started && (c == 0 || c == 10 || c == 13)) break;
            if (!started) {
                if (c == '(') {
                    started = true;
                    if (at + 2 < q->size && qs_byte(q, at + 1) == 0xFE && qs_byte(q, at + 2) == 0xFF) {
                        wide = true;
                        size += 2;
                        at += 2;
                    }
                } else escape = false;
                if (!qs_units_add(q, units, (unsigned)c)) break;
            } else if (c == '(' && !escape) {
                ++depth;
                if (!qs_units_add(q, units, '(')) break;
            } else if (c == ')' && !escape) {
                if (!qs_units_add(q, units, ')')) break;
                if (depth > 0) --depth;
                else done = true;
            } else if (c == '\\') escape = true;
            else {
                escape = false;
                if (!qs_units_add(q, units, (unsigned)c)) break;
            }
            ++size;
            ++at;
        } else {
            int low = qs_byte(q, at + 1);
            unsigned word;
            if (low < 0) break;
            word = ((unsigned)c << 8) | (unsigned)low;
            if (c == '(' && !escape) {
                ++depth;
                if (!qs_units_add(q, units, '(')) break;
                ++size;
            } else if (c == ')' && !escape) {
                if (!qs_units_add(q, units, ')')) break;
                ++size;
                if (depth > 0) --depth;
                else done = true;
            } else if (word == '\\') {
                escape = true;
                at += 2;
                size += 2;
                continue;
            } else if (escape && word == 0x6E29U) {
                escape = false;
                if (!qs_units_add(q, units, ')')) break;
                ++size;
                done = true;
            } else {
                escape = false;
                if (!qs_units_add(q, units, word)) break;
                at += 2;
                size += 2;
                continue;
            }
            ++at;
        }
        if (started && done) break;
    }
    return size;
}
/* QString::toUtf8 of units[first .. first + count): an unpaired surrogate
 * becomes '?'. The text ends at a NUL character. */
static char *qs_units_utf8(const uint16_t *unit, size_t count)
{
    char *out = count > (SIZE_MAX - 1U) / 3U ? NULL : xx_str_create_len(count * 3U);
    size_t i, n = 0;
    if (!out) return NULL;
    for (i = 0; i < count && unit[i]; ++i) {
        uint32_t c = unit[i];
        if (c >= 0xD800U && c <= 0xDBFFU && i + 1U < count && unit[i + 1U] >= 0xDC00U && unit[i + 1U] <= 0xDFFFU) {
            c = 0x10000U + ((c - 0xD800U) << 10) + (unit[++i] - 0xDC00U);
            out[n++] = (char)(0xF0U | (c >> 18));
            out[n++] = (char)(0x80U | ((c >> 12) & 0x3FU));
            out[n++] = (char)(0x80U | ((c >> 6) & 0x3FU));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        } else if (c >= 0xD800U && c <= 0xDFFFU) out[n++] = '?';
        else if (c < 0x80U) out[n++] = (char)c;
        else if (c < 0x800U) {
            out[n++] = (char)(0xC0U | (c >> 6));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        } else {
            out[n++] = (char)(0xE0U | (c >> 12));
            out[n++] = (char)(0x80U | ((c >> 6) & 0x3FU));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[n] = 0;
    return out;
}
/* QString::fromLatin1(text, size).toUtf8() */
static char *qs_latin1_utf8(const char *text, size_t size)
{
    char *out = size > (SIZE_MAX - 1U) / 2U ? NULL : xx_str_create_len(size * 2U);
    size_t i, n = 0;
    if (!out) return NULL;
    for (i = 0; i < size && text[i]; ++i) {
        uint8_t c = (uint8_t)text[i];
        if (c < 0x80U) out[n++] = (char)c;
        else {
            out[n++] = (char)(0xC0U | (c >> 6));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[n] = 0;
    return out;
}

static void qs_parts_free(qs_parts *p)
{
    xx_mem_free(p->token);
    xx_mem_free(p->pool);
    xx_mem_zero(p, sizeof(*p));
}
static bool qs_parts_add(xx_pdf_qscan *q, qs_parts *p, const char *text, size_t size, int64_t offset, int64_t full, bool null)
{
    if (p->count == p->capacity) {
        size_t n = p->capacity ? p->capacity * 2U : 64U;
        qs_token *next = (qs_token *)xx_mem_realloc(p->token, n * sizeof(*next));
        if (!next) goto fail;
        p->token = next;
        p->capacity = n;
    }
    if (size + 1U > p->pool_capacity - p->used) {
        size_t n = p->pool_capacity ? p->pool_capacity * 2U : 1024U;
        char *next;
        while (n - p->used < size + 1U) n *= 2U;
        next = (char *)xx_mem_realloc(p->pool, n);
        if (!next) goto fail;
        p->pool = next;
        p->pool_capacity = n;
    }
    if (size) xx_mem_copy(p->pool + p->used, text, size);
    p->pool[p->used + size] = 0;
    p->token[p->count].at = p->used;
    p->token[p->count].size = size;
    p->token[p->count].offset = offset;
    p->token[p->count].full = full;
    p->token[p->count].null = null;
    ++p->count;
    p->used += size + 1U;
    return true;
fail:
    q->failed = true;
    return false;
}
static const char *qs_part(const qs_parts *p, size_t index, size_t *size)
{
    *size = p->token[index].size;
    return p->pool + p->token[index].at;
}
static bool qs_part_is(const qs_parts *p, size_t index, const char *literal)
{
    size_t size;
    const char *text = qs_part(p, index, &size);
    return qs_is(text, size, literal);
}
/* QList::indexOf: SIZE_MAX when absent. */
static size_t qs_parts_find(const qs_parts *p, const char *literal)
{
    size_t i;
    for (i = 0; i < p->count; ++i)
        if (qs_part_is(p, i, literal)) return i;
    return SIZE_MAX;
}
/* XPDF::_readPDFStringPart: appends one token and returns the bytes consumed,
 * trailing white space included. *empty tells the caller the token had no
 * text. The reference reads a 512 byte window; a token or the white space
 * after it never crosses the window. */
static bool qs_read_part(xx_pdf_qscan *q, int64_t offset, qs_parts *parts, int64_t *consumed, bool *empty)
{
    uint8_t data[516];
    size_t got, pos = 0, token_end = 0, token_size = 0;
    const char *token = (const char *)data;
    bool null = false;
    *consumed = 0;
    *empty = true;
    got = offset >= 0 && offset < q->size ? qs_read(q, offset, data, 512U) : 0;
    if (!got) return !q->failed && qs_parts_add(q, parts, "", 0, offset, 0, true);
    if (data[0] == '/') {
        for (pos = 0; pos < got; ++pos)
            if (qs_name_end(data[pos]) || (pos && data[pos] == '/')) break;
        token_size = token_end = pos;
    } else if (data[0] == '(' || (data[0] == '<' && !(got > 1U && data[1] == '<'))) {
        int64_t size, end;
        if (data[0] == '(') {
            size = qs_string(q, offset, NULL);
            token_size = 1;
        } else {
            /* _readPDFStringPart_hex: through the first '>' or to the end. */
            int64_t close = qs_find(q, offset, -1, ">");
            size = close < 0 ? q->size - offset : close - offset + 1;
            token_size = size < (int64_t)QS_TOKEN_KEEP ? (size_t)size : QS_TOKEN_KEEP;
            if (token_size > got) token_size = got;
        }
        end = offset + size;
        *consumed = size + qs_skip_space(q, &end);
        *consumed += qs_skip_ending(q, &end);
        *empty = false;
        return !q->failed && qs_parts_add(q, parts, token, token_size, offset, size, false);
    } else if (data[0] == '<') {
        token_size = token_end = 2;
    } else if (data[0] == '>') {
        if (got > 1U && data[1] == '>') token_size = token_end = 2;
        else null = true;
    } else if (data[0] == '[' || data[0] == ']') {
        token_size = token_end = 1;
    } else {
        bool space = false;
        for (pos = 0; pos < got; ++pos) {
            if (qs_value_end(data[pos])) break;
            if (data[pos] == ' ' || data[pos] == 9 || data[pos] == 12) {
                ++pos;
                space = true;
                break;
            }
        }
        token_size = space ? pos - 1U : pos;
        token_end = pos;
        if (space) {
            /* Only "N 0 R" is joined into one token. */
            if (pos + 2U < got) {
                if (data[pos] == '0' && data[pos + 1U] == ' ' && data[pos + 2U] == 'R') token_size = token_end = pos + 3U;
            } else if (q->size - (offset + (int64_t)pos) >= 3) {
                uint8_t suffix[3];
                if (qs_read(q, offset + (int64_t)pos, suffix, 3U) == 3U && suffix[0] == '0' && suffix[1] == ' ' && suffix[2] == 'R') {
                    /* QString::append(" 0 R") after the text without its space. */
                    xx_mem_copy(data + token_size, " 0 R", 4U);
                    token_size += 4U;
                    token_end = pos + 3U;
                }
            }
        }
    }
    if (q->failed) return false;
    if (!qs_parts_add(q, parts, token, token_size, offset, (int64_t)token_size, null)) return false;
    pos = token_end;
    while (pos < got && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\f' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == 0)) ++pos;
    *consumed = (int64_t)pos;
    *empty = !token_size;
    return true;
}
/* XPDF::_readPDFStringPart_val: the text of a direct value. */
static size_t qs_read_value(xx_pdf_qscan *q, int64_t offset, char *out /* 257 bytes */)
{
    size_t got, pos;
    got = offset >= 0 && offset < q->size ? qs_read(q, offset, (uint8_t *)out, 256U) : 0;
    for (pos = 0; pos < got; ++pos) {
        uint8_t c = (uint8_t)out[pos];
        if (qs_value_end(c) || c == ' ' || c == 9 || c == 12) break;
    }
    /* A following "0 R" makes the text a reference, never a number. */
    if (pos < got && (out[pos] == ' ' || out[pos] == 9 || out[pos] == 12) && qs_byte(q, offset + (int64_t)pos + 1) == '0' &&
        qs_byte(q, offset + (int64_t)pos + 2) == ' ' && qs_byte(q, offset + (int64_t)pos + 3) == 'R')
        pos = 0;
    out[pos] = 0;
    return pos;
}

/* ---- containers --------------------------------------------------------- */

static bool qs_objects_add(xx_pdf_qscan *q, qs_objects *list, int64_t offset, int64_t size, uint64_t id)
{
    if (list->count == list->capacity) {
        size_t n = list->capacity ? list->capacity * 2U : 64U;
        qs_object *next = n > SIZE_MAX / sizeof(*next) ? NULL : (qs_object *)xx_mem_realloc(list->item, n * sizeof(*next));
        if (!next) {
            q->failed = true;
            return false;
        }
        list->item = next;
        list->capacity = n;
    }
    list->item[list->count].offset = offset;
    list->item[list->count].size = size;
    list->item[list->count].id = id;
    list->item[list->count].order = list->count;
    ++list->count;
    return true;
}
static int qs_object_compare(const void *a, const void *b, void *context)
{
    const qs_object *x = (const qs_object *)a, *y = (const qs_object *)b;
    (void)context;
    if (x->offset != y->offset) return x->offset < y->offset ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}
/* The reference collects into a QMap keyed by offset: the list comes out in
 * offset order and a later record replaces an earlier one at that offset. */
static void qs_objects_by_offset(qs_objects *list)
{
    size_t i, out = 0;
    xx_sort_context(list->item, list->count, sizeof(*list->item), qs_object_compare, NULL);
    for (i = 0; i < list->count; ++i) {
        if (i + 1U < list->count && list->item[i + 1U].offset == list->item[i].offset) continue;
        list->item[out++] = list->item[i];
    }
    list->count = out;
    for (i = 0; i + 1U < list->count; ++i) list->item[i].size = list->item[i + 1U].offset - list->item[i].offset;
}
static size_t qs_map_slot(const qs_map *m, uint64_t key)
{
    size_t at = (size_t)((key * UINT64_C(0x9E3779B97F4A7C15)) >> 20) & (m->capacity - 1U);
    while (m->used[at] && m->key[at] != key) at = (at + 1U) & (m->capacity - 1U);
    return at;
}
static void qs_map_free(qs_map *m)
{
    xx_mem_free(m->key);
    xx_mem_free(m->value);
    xx_mem_free(m->used);
    xx_mem_zero(m, sizeof(*m));
}
static bool qs_map_get(const qs_map *m, uint64_t key, int64_t *value)
{
    size_t at;
    if (!m->capacity) return false;
    at = qs_map_slot(m, key);
    if (!m->used[at]) return false;
    if (value) *value = m->value[at];
    return true;
}
static bool qs_map_put(xx_pdf_qscan *q, qs_map *m, uint64_t key, int64_t value)
{
    size_t at;
    if (m->count * 2U >= m->capacity) {
        qs_map next;
        size_t i;
        xx_mem_zero(&next, sizeof(next));
        next.capacity = m->capacity ? m->capacity * 2U : 256U;
        if (next.capacity > SIZE_MAX / sizeof(*next.key)) goto fail;
        next.key = (uint64_t *)xx_mem_alloc(next.capacity * sizeof(*next.key));
        next.value = (int64_t *)xx_mem_alloc(next.capacity * sizeof(*next.value));
        next.used = (uint8_t *)xx_mem_alloc(next.capacity);
        if (!next.key || !next.value || !next.used) {
            qs_map_free(&next);
            goto fail;
        }
        xx_mem_zero(next.used, next.capacity);
        for (i = 0; i < m->capacity; ++i) {
            if (!m->used[i]) continue;
            at = qs_map_slot(&next, m->key[i]);
            next.used[at] = 1;
            next.key[at] = m->key[i];
            next.value[at] = m->value[i];
        }
        next.count = m->count;
        qs_map_free(m);
        *m = next;
    }
    at = qs_map_slot(m, key);
    if (!m->used[at]) {
        m->used[at] = 1;
        m->key[at] = key;
        ++m->count;
    }
    m->value[at] = value;
    return true;
fail:
    q->failed = true;
    return false;
}

/* ---- object parsing ----------------------------------------------------- */

/* XPDF::findObjectHeaderInRange */
static int64_t qs_find_header(xx_pdf_qscan *q, uint64_t id, int64_t start, int64_t size)
{
    char needle[40];
    int64_t end = start + size, from = start;
    if (size <= 0) return -1;
    (void)xx_rt_snprintf(needle, sizeof(needle), "%llu 0 obj", (unsigned long long)id);
    while (!qs_stopped(q) && end - from > 0) {
        int64_t hit = qs_find(q, from, end - from, needle);
        int previous;
        if (hit == -1) break;
        previous = hit ? qs_byte(q, hit - 1) : -1;
        if (!hit || !(previous >= '0' && previous <= '9')) return hit;
        from = hit + 1;
    }
    return -1;
}
/* XPDF::resolveObjectOffset */
static int64_t qs_resolve(xx_pdf_qscan *q, uint64_t id, int64_t hint)
{
    int64_t found, forward, back;
    if (qs_map_get(&q->id_offset, id, &found)) return found;
    if (hint < 0) hint = 0;
    if (hint > q->size) hint = q->size;
    forward = q->size - hint < 1048576 ? q->size - hint : 1048576;
    found = qs_find_header(q, id, hint, forward);
    if (found == -1) {
        back = hint - 1048576 > 0 ? hint - 1048576 : 0;
        found = qs_find_header(q, id, back, hint - back);
    }
    return found;
}
/* XPDF::getFirstStringValueByKey: the value after the last occurrence of the
 * key. NULL when the key is absent. */
static const char *qs_value(const qs_parts *p, const char *key, size_t *size)
{
    const char *result = NULL;
    size_t i;
    for (i = 0; i + 1U < p->count; ++i)
        if (qs_part_is(p, i, key)) result = qs_part(p, i + 1U, size);
    return result;
}
static int64_t qs_value_ll(const qs_parts *p, const char *key, bool *present)
{
    size_t size = 0;
    const char *text = qs_value(p, key, &size);
    if (present) *present = text != NULL;
    return text ? qs_to_ll(text, size, NULL) : 0;
}
/* QString::toDouble and QString::number(value, 'g', shortest) for a token
 * that is not an integer. NULL when the text is not a finite number. */
static char *qs_double_text(const char *text, size_t size)
{
    char buffer[64], digits[24], out[400];
    size_t i = 0, n = 0, count = 0, at = 0;
    int exponent = 0, point, cutoff = 6, precision;
    double value;
    bool negative, any = false;
    const char *end;
    while (size && qs_qspace((uint8_t)text[0])) {
        ++text;
        --size;
    }
    while (size && qs_qspace((uint8_t)text[size - 1U])) --size;
    if (!size || size >= sizeof(buffer)) return NULL;
    if (text[i] == '+' || text[i] == '-') ++i;
    for (; i < size && text[i] >= '0' && text[i] <= '9'; ++i) any = true;
    if (i < size && text[i] == '.')
        for (++i; i < size && text[i] >= '0' && text[i] <= '9'; ++i) any = true;
    if (!any) return NULL;
    if (i < size && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < size && (text[i] == '+' || text[i] == '-')) ++i;
        if (i >= size) return NULL;
        for (; i < size && text[i] >= '0' && text[i] <= '9'; ++i) {
        }
    }
    if (i != size) return NULL;
    xx_mem_copy(buffer, text, size);
    buffer[size] = 0;
    value = xx_rt_strtod(buffer, &end);
    if (value - value != 0.0) return NULL; /* overflow */
    negative = buffer[0] == '-';
    if (negative) value = -value;
    /* The shortest digits that read back as the same value. */
    for (precision = 0; precision < 17; ++precision) {
        (void)xx_rt_snprintf(buffer, sizeof(buffer), "%.*e", precision, value);
        if (xx_rt_strtod(buffer, &end) == value) break;
    }
    for (i = 0; buffer[i] && buffer[i] != 'e'; ++i)
        if (buffer[i] >= '0' && buffer[i] <= '9' && count + 1U < sizeof(digits)) digits[count++] = buffer[i];
    if (buffer[i] == 'e') {
        bool minus = buffer[i + 1U] == '-';
        for (i += 2U; buffer[i] >= '0' && buffer[i] <= '9'; ++i) exponent = exponent * 10 + (buffer[i] - '0');
        if (minus) exponent = -exponent;
    }
    while (count > 1U && digits[count - 1U] == '0') --count;
    point = exponent + 1;
    if (point > 0) {
        cutoff = (int)count + 4 + (point <= 10 ? 1 : point > 100 ? 2 : 1);
        if ((int)count > point) ++cutoff;
    }
    if (negative && value != 0.0) out[at++] = '-';
    if (point != (int)count && (point <= -4 || point > cutoff)) {
        out[at++] = digits[0];
        if (count > 1U) out[at++] = '.';
        for (n = 1; n < count; ++n) out[at++] = digits[n];
        (void)xx_rt_snprintf(out + at, sizeof(out) - at, "e%c%02d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
    } else {
        if (point <= 0) {
            out[at++] = '0';
            out[at++] = '.';
            for (n = 0; n < (size_t)(-point); ++n) out[at++] = '0';
        }
        for (n = 0; n < count; ++n) {
            if (point > 0 && n == (size_t)point) out[at++] = '.';
            out[at++] = digits[n];
        }
        for (n = count; (int)n < point; ++n) out[at++] = '0';
        out[at] = 0;
    }
    return xx_str_create(out);
}
/* XPDF::_parseValue followed by QVariant::toString for the token at index.
 * *out stays NULL for a null variant (an invalid date, a null string). */
static bool qs_value_text(xx_pdf_qscan *q, const qs_parts *p, size_t index, char **out, xx_pdf_qscan_value_type *type)
{
    const qs_token *token = &p->token[index];
    const char *text = p->pool + token->at;
    size_t size = token->size;
    bool number;
    int64_t value;
    *out = NULL;
    *type = XX_PDF_QSCAN_VALUE_OTHER;
    if (token->null) return true;
    if (size && text[0] == '(') {
        qs_units units;
        xx_mem_zero(&units, sizeof(units));
        (void)qs_string(q, token->offset, &units);
        if (q->failed) {
            xx_mem_free(units.unit);
            return false;
        }
        if (units.count >= 18U && units.unit[1] == 'D' && units.unit[2] == ':' && units.unit[units.count - 1U] == ')') {
            /* _isDateTime: "(D:...)" of 18 characters or more. */
            char *inner = qs_units_utf8(units.unit + 1U, units.count - 1U), *date = inner ? xx_pdf_date_text(inner) : NULL;
            xx_str_free(inner);
            xx_mem_free(units.unit);
            if (!date) return false;
            if (*date) *out = date;
            else xx_str_free(date);
            *type = XX_PDF_QSCAN_VALUE_DATE;
            return true;
        }
        if (units.count >= 2U && units.unit[units.count - 1U] == ')') {
            *out = qs_units_utf8(units.unit + 1U, units.count - 2U);
            *type = XX_PDF_QSCAN_VALUE_STRING;
        } else *out = qs_units_utf8(units.unit, units.count);
        xx_mem_free(units.unit);
        return *out != NULL;
    }
    if (size && text[0] == '<' && !qs_is(text, size, "<<")) {
        /* A hex string: the pool keeps only its start. */
        char *raw;
        size_t full = (uint64_t)token->full > SIZE_MAX - 1U ? 0 : (size_t)token->full;
        raw = (char *)xx_mem_alloc(full + 1U);
        if (!raw) return false;
        full = qs_read(q, token->offset, (uint8_t *)raw, full);
        if (full >= 2U && raw[full - 1U] == '>' && raw[0] != raw[1]) {
            *out = qs_latin1_utf8(raw + 1U, full - 2U);
            *type = XX_PDF_QSCAN_VALUE_HEX;
        } else *out = qs_latin1_utf8(raw, full);
        xx_mem_free(raw);
        return *out != NULL;
    }
    value = qs_to_ll(text, size, &number);
    if (number) {
        char digits[32];
        (void)xx_rt_snprintf(digits, sizeof(digits), "%lld", (long long)value);
        *out = xx_str_create(digits);
    } else if (!qs_is(text, size, "true") && !qs_is(text, size, "false") && (*out = qs_double_text(text, size)) != NULL) return true;
    else *out = qs_latin1_utf8(text, size);
    return *out != NULL;
}
/* XPDF::_resolveFilterList for "/Filter": the index of the first key, or
 * SIZE_MAX. The names follow at *first .. *first + *count - 1 when the value
 * is an array (only tokens that begin with '/' are names). */
static size_t qs_filter_key(const qs_parts *p)
{
    size_t i;
    for (i = 0; i + 1U < p->count; ++i)
        if (qs_part_is(p, i, "/Filter")) return i;
    return SIZE_MAX;
}
/* Calls visit for every filter name of the object; returns the last one. */
static const char *qs_filters(const qs_parts *p, size_t *last_size, bool (*visit)(void *, const char *, size_t), void *context, bool *ok)
{
    size_t key = qs_filter_key(p), size, i;
    const char *text, *last = NULL;
    *last_size = 0;
    if (ok) *ok = true;
    if (key == SIZE_MAX) return NULL;
    text = qs_part(p, key + 1U, &size);
    if (qs_is(text, size, "[")) {
        for (i = key + 2U; i < p->count; ++i) {
            text = qs_part(p, i, &size);
            if (qs_is(text, size, "]")) break;
            if (!size || text[0] != '/') continue;
            last = text;
            *last_size = size;
            if (visit && !visit(context, text, size)) {
                *ok = false;
                return NULL;
            }
        }
    } else if (size && text[0] == '/') {
        last = text;
        *last_size = size;
        if (visit && !visit(context, text, size)) {
            *ok = false;
            return NULL;
        }
    }
    return last;
}
/* pdfFilterToHandleMethod: XX_PDF_FILTER_UNSUPPORTED stands for "stored". */
static xx_pdf_filter qs_filter_method(const char *name, size_t size)
{
    if (!name) return XX_PDF_FILTER_UNSUPPORTED;
    if (qs_is(name, size, "/FlateDecode")) return XX_PDF_FILTER_FLATE;
    if (qs_is(name, size, "/LZWDecode")) return XX_PDF_FILTER_LZW;
    if (qs_is(name, size, "/ASCII85Decode")) return XX_PDF_FILTER_ASCII85;
    if (qs_is(name, size, "/ASCIIHexDecode")) return XX_PDF_FILTER_ASCIIHEX;
    if (qs_is(name, size, "/RunLengthDecode")) return XX_PDF_FILTER_RUNLENGTH;
    return XX_PDF_FILTER_UNSUPPORTED;
}

/* XPDF::handleXpart. part_limit -1 means no limit. With resolve_streams the
 * first stream extent is reported. Returns false only when the scan failed. */
static bool qs_handle_xpart(xx_pdf_qscan *q, int64_t offset, int part_limit, bool resolve_streams, qs_xpart *x)
{
    char line[21];
    const char *length = "";
    size_t length_size = 0, length_part = SIZE_MAX;
    char joined[80];
    bool want_length = false;
    xx_mem_zero(x, sizeof(*x));
    while (!qs_stopped(q)) {
        int64_t consumed;
        size_t n = qs_line(q, offset, 20U, true, line, &consumed);
        bool stop = false;
        offset += consumed;
        if (qs_is_object(line, n)) {
            int dicts = 0, arrays = 0, count = 0;
            while (!qs_stopped(q)) {
                const char *token;
                size_t size;
                bool empty;
                if (!(count < part_limit || part_limit == -1)) {
                    stop = true;
                    break;
                }
                if (!qs_read_part(q, offset, &x->parts, &consumed, &empty)) return false;
                ++count;
                offset += consumed;
                if (empty) break;
                token = qs_part(&x->parts, x->parts.count - 1U, &size);
                if (qs_is(token, size, "<<")) ++dicts;
                else if (qs_is(token, size, ">>")) --dicts;
                else if (qs_is(token, size, "[")) ++arrays;
                else if (qs_is(token, size, "]")) --arrays;
                if (want_length) {
                    want_length = false;
                    length_part = x->parts.count - 1U;
                } else if (qs_is(token, size, "/Length")) want_length = true;
                if (!dicts && !arrays) break;
            }
            if (stop) break;
        } else if (qs_is(line, n, "stream")) {
            int64_t stream_offset = offset, stream_size = 0;
            bool has_size = false, indirect = false;
            if (!resolve_streams) break;
            if (length_part != SIZE_MAX) length = qs_part(&x->parts, length_part, &length_size);
            if (qs_is_indirect(length, length_size)) indirect = true;
            else {
                bool number;
                size_t field_size, p;
                const char *field = qs_section(length, length_size, 0, &field_size);
                int64_t value = qs_to_ll(field, field_size, &number);
                if (number) {
                    char text[32];
                    (void)xx_rt_snprintf(text, sizeof(text), "%lld", (long long)value);
                    /* "/Length N G R" with a generation other than 0. */
                    for (p = 1; p + 2U < x->parts.count; ++p) {
                        if (qs_part_is(&x->parts, p, text) && qs_part_is(&x->parts, p + 2U, "R") && qs_part_is(&x->parts, p - 1U, "/Length")) {
                            size_t a, b;
                            const char *first = qs_part(&x->parts, p, &a), *second = qs_part(&x->parts, p + 1U, &b);
                            if (a + b + 4U <= sizeof(joined)) {
                                xx_mem_copy(joined, first, a);
                                joined[a] = ' ';
                                xx_mem_copy(joined + a + 1U, second, b);
                                xx_mem_copy(joined + a + 1U + b, " R", 3U);
                                length = joined;
                                length_size = a + b + 3U;
                                length_part = SIZE_MAX;
                            }
                            indirect = true;
                            break;
                        }
                    }
                }
            }
            if (!indirect) {
                bool ok;
                int64_t value = qs_to_ll(length, length_size, &ok);
                if (ok && value >= 0) {
                    stream_size = value;
                    has_size = true;
                }
            } else {
                size_t field_size;
                const char *field = qs_section(length, length_size, 2, &field_size);
                if (qs_is(field, field_size, "R")) {
                    int64_t target;
                    field = qs_section(length, length_size, 0, &field_size);
                    target = qs_resolve(q, (uint64_t)qs_to_ll(field, field_size, NULL), offset);
                    if (target != -1) {
                        char header[65], value_text[257];
                        int64_t header_size, value;
                        size_t value_size;
                        bool ok;
                        (void)qs_line(q, target, 64U, false, header, &header_size);
                        value_size = qs_read_value(q, target + header_size, value_text);
                        value = qs_to_ll(value_text, value_size, &ok);
                        if (ok && value >= 0) {
                            stream_size = value;
                            has_size = true;
                        }
                    }
                }
            }
            /* The declared length must end just before "endstream". */
            if (has_size) {
                if (stream_size > q->size - stream_offset) has_size = false;
                else {
                    int64_t probe = stream_offset + stream_size;
                    int64_t window = q->size - probe < 40 ? q->size - probe : 40;
                    if (window <= 0 || qs_find(q, probe, window, "endstream") == -1) has_size = false;
                }
            }
            if (!has_size) {
                int64_t end_stream = qs_find(q, stream_offset, q->size - stream_offset, "endstream"), body_end;
                if (end_stream == -1) break;
                body_end = end_stream;
                if (body_end > stream_offset && qs_byte(q, body_end - 1) == 10) --body_end;
                if (body_end > stream_offset && qs_byte(q, body_end - 1) == 13) --body_end;
                stream_size = body_end - stream_offset;
                offset = end_stream + 9;
            } else offset += stream_size;
            (void)qs_skip_ending(q, &offset);
            if (!x->stream_count) {
                x->stream_offset = stream_offset;
                x->stream_size = stream_size;
            }
            ++x->stream_count;
        } else if (qs_is(line, n, "endstream")) {
            /* next line */
        } else if (qs_is_end_object(line, n) || !n) break;
    }
    return !q->failed;
}
typedef struct {
    uint8_t *data;
    size_t size, capacity;
} qs_bytes;
/* A decoder that writes more than the output limit has failed. */
static bool qs_bytes_put(qs_bytes *b, const uint8_t *data, size_t size)
{
    if (size > (size_t)QS_DECODE_LIMIT - b->size) return false;
    if (b->size + size > b->capacity) {
        size_t n = b->capacity ? b->capacity * 2U : 4096U;
        uint8_t *next;
        while (n < b->size + size) n *= 2U;
        next = (uint8_t *)xx_mem_realloc(b->data, n);
        if (!next) return false;
        b->data = next;
        b->capacity = n;
    }
    xx_mem_copy(b->data + b->size, data, size);
    b->size += size;
    return true;
}
/* XASCIIHexDecoder::decompress_pdf: '>' ends the data, every byte that is
 * not a hex digit is skipped, a last single digit is the high half. */
static bool qs_decode_hex(const uint8_t *in, size_t size, qs_bytes *out)
{
    size_t i;
    int high = -1;
    for (i = 0; i < size && in[i] != '>'; ++i) {
        uint8_t c = in[i], byte;
        int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (value < 0) continue;
        if (high < 0) {
            high = value;
            continue;
        }
        byte = (uint8_t)((high << 4) | value);
        high = -1;
        if (!qs_bytes_put(out, &byte, 1U)) return false;
    }
    if (high >= 0) {
        uint8_t byte = (uint8_t)(high << 4);
        if (!qs_bytes_put(out, &byte, 1U)) return false;
    }
    return true;
}
/* XASCII85Decoder::decompress_pdf: an optional "<~", bytes up to the space
 * are skipped, "~>" ends the data and is not required. A byte outside
 * '!'..'u', a 'z' inside a group, a group above 2^32 - 1 and a last group of
 * one digit are errors. */
static bool qs_decode_a85(const uint8_t *in, size_t size, qs_bytes *out)
{
    uint64_t group = 0;
    unsigned count = 0;
    size_t i = size >= 2U && in[0] == '<' && in[1] == '~' ? 2U : 0U;
    uint8_t bytes[4];
    for (; i < size; ++i) {
        uint8_t c = in[i];
        if (c <= ' ') continue;
        if (c == '~') {
            if (i + 1U >= size || in[i + 1U] != '>') return false;
            break;
        }
        if (c == 'z') {
            if (count) return false;
            bytes[0] = bytes[1] = bytes[2] = bytes[3] = 0;
            if (!qs_bytes_put(out, bytes, 4U)) return false;
            continue;
        }
        if (c < '!' || c > 'u') return false;
        group = group * 85U + (uint64_t)(c - '!');
        if (++count == 5U) {
            if (group > 0xFFFFFFFFU) return false;
            bytes[0] = (uint8_t)(group >> 24);
            bytes[1] = (uint8_t)(group >> 16);
            bytes[2] = (uint8_t)(group >> 8);
            bytes[3] = (uint8_t)group;
            if (!qs_bytes_put(out, bytes, 4U)) return false;
            group = 0;
            count = 0;
        }
    }
    if (count == 1U) return false;
    if (count > 1U) {
        unsigned k;
        for (k = count; k < 5U; ++k) group = group * 85U + 84U;
        if (group > 0xFFFFFFFFU) return false;
        bytes[0] = (uint8_t)(group >> 24);
        bytes[1] = (uint8_t)(group >> 16);
        bytes[2] = (uint8_t)(group >> 8);
        bytes[3] = (uint8_t)group;
        if (!qs_bytes_put(out, bytes, count - 1U)) return false;
    }
    return true;
}
/* XRunLengthDecoder::decompress_pdf: the end mark 128 is required. */
static bool qs_decode_run_length(const uint8_t *in, size_t size, qs_bytes *out)
{
    size_t i = 0;
    while (i < size) {
        unsigned length = in[i++];
        if (length == 128U) return true;
        if (length < 128U) {
            size_t count = (size_t)length + 1U;
            if (count > size - i) count = size - i;
            if (!qs_bytes_put(out, in + i, count)) return false;
            i += count;
        } else {
            uint8_t run[128];
            if (i >= size) break;
            xx_mem_set(run, in[i++], 257U - length);
            if (!qs_bytes_put(out, run, 257U - length)) return false;
        }
    }
    return false;
}
/* decompressPdfBuffer with one filter: a stream whose decoder reports an
 * error gives no data at all. Flate is strict (the whole input is one zlib
 * stream with a correct checksum). NULL/0 when there is no data. */
static uint8_t *qs_stream_data(xx_pdf_qscan *q, int64_t offset, int64_t size, xx_pdf_filter method, size_t *out_size)
{
    uint8_t *raw, *decoded = NULL;
    qs_bytes bytes;
    bool ok;
    *out_size = 0;
    if ((uint64_t)size > SIZE_MAX - 1U) return NULL;
    raw = (uint8_t *)xx_mem_alloc((size_t)size + 1U);
    if (!raw) {
        q->failed = true;
        return NULL;
    }
    if (qs_read(q, offset, raw, (size_t)size) != (size_t)size) {
        xx_mem_free(raw);
        return NULL;
    }
    if (method == XX_PDF_FILTER_UNSUPPORTED) {
        *out_size = (size_t)size;
        return raw;
    }
    if (method == XX_PDF_FILTER_FLATE || method == XX_PDF_FILTER_LZW) {
        xx_pdf_filter_spec spec;
        xx_mem_zero(&spec, sizeof(spec));
        spec.filter = method;
        spec.params.predictor = 1;
        spec.params.colors = 1;
        spec.params.bits_per_component = 8;
        spec.params.columns = 1;
        spec.params.early_change = 1;
        if (!xx_pdf_decode_stream(raw, (size_t)size, &spec, 1U, (size_t)QS_DECODE_LIMIT, SIZE_MAX, q->pd, &decoded, out_size)) {
            decoded = NULL;
            *out_size = 0;
        }
        xx_mem_free(raw);
        return decoded;
    }
    xx_mem_zero(&bytes, sizeof(bytes));
    ok = method == XX_PDF_FILTER_ASCIIHEX   ? qs_decode_hex(raw, (size_t)size, &bytes)
         : method == XX_PDF_FILTER_ASCII85 ? qs_decode_a85(raw, (size_t)size, &bytes)
                                           : qs_decode_run_length(raw, (size_t)size, &bytes);
    xx_mem_free(raw);
    if (!ok || !bytes.size) {
        xx_mem_free(bytes.data);
        return NULL;
    }
    *out_size = bytes.size;
    return bytes.data;
}
/* XPDF::_applyPredictor */
static void qs_apply_predictor(xx_pdf_qscan *q, uint8_t **data, size_t *data_size, int32_t predictor, int32_t columns, int32_t colors, int32_t bits)
{
    int64_t bpp_wide, row_wide;
    size_t bpp, row, rows, r, c, size = *data_size;
    uint8_t *in = *data, *out;
    if (predictor < 2 || columns <= 0 || colors <= 0 || bits <= 0 || !in) return;
    bpp_wide = ((int64_t)colors * bits + 7) / 8;
    row_wide = ((int64_t)columns * colors * bits + 7) / 8;
    if (row_wide <= 0 || row_wide > 64 * 1024 * 1024) return;
    bpp = bpp_wide > 1 ? (size_t)(int32_t)bpp_wide : 1U;
    row = (size_t)row_wide;
    if (predictor == 2) {
        if (bits != 8) return;
        rows = size / row;
        for (r = 0; r < rows; ++r)
            for (c = bpp; c < row; ++c) in[r * row + c] = (uint8_t)(in[r * row + c] + in[r * row + c - bpp]);
        return;
    }
    /* PNG predictors: every row starts with its filter type. */
    rows = size / (row + 1U);
    out = (uint8_t *)xx_mem_alloc(rows * row + 1U);
    if (!out) {
        q->failed = true;
        return;
    }
    for (r = 0; r < rows; ++r) {
        const uint8_t *source = in + r * (row + 1U);
        const uint8_t *previous = r ? out + (r - 1U) * row : NULL;
        uint8_t *target = out + r * row;
        unsigned filter = source[0];
        for (c = 0; c < row; ++c) {
            int a = c >= bpp ? target[c - bpp] : 0;
            int b = previous ? previous[c] : 0;
            int d = previous && c >= bpp ? previous[c - bpp] : 0;
            int value = source[1U + c];
            if (filter == 1) value += a;
            else if (filter == 2) value += b;
            else if (filter == 3) value += (a + b) >> 1;
            else if (filter == 4) {
                int p = a + b - d;
                int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > d ? p - d : d - p;
                value += pa <= pb && pa <= pc ? a : pb <= pc ? b : d;
            }
            target[c] = (uint8_t)(value & 0xFF);
        }
    }
    xx_mem_free(in);
    *data = out;
    *data_size = rows * row;
}
/* The numbers of "/Key [ a b c ]" (the first key, as QList::indexOf). */
static size_t qs_array_values(const qs_parts *p, const char *key, int64_t *out, size_t limit, bool as_int)
{
    size_t at = qs_parts_find(p, key), count = 0, i, size;
    if (at == SIZE_MAX || at + 1U >= p->count || !qs_part_is(p, at + 1U, "[")) return 0;
    for (i = at + 2U; i < p->count; ++i) {
        const char *text = qs_part(p, i, &size);
        if (qs_is(text, size, "]")) break;
        if (count < limit) out[count] = as_int ? qs_to_int(text, size) : qs_to_ll(text, size, NULL);
        ++count;
    }
    return count;
}
/* XPDF::getObjectsFromXrefStream. *is_stream is set when objects were found. */
static bool qs_objects_from_xref_stream(xx_pdf_qscan *q, int64_t xref_offset, qs_objects *list, bool *is_stream)
{
    qs_map visited, seen;
    int64_t current = xref_offset;
    bool ok = false;
    *is_stream = false;
    xx_mem_zero(&visited, sizeof(visited));
    xx_mem_zero(&seen, sizeof(seen));
    while (current >= 0 && current < q->size && !qs_map_get(&visited, (uint64_t)current, NULL) && !qs_stopped(q)) {
        char head[41];
        int64_t consumed, widths[3] = {0, 0, 0}, *index = NULL, size_field, row = 0;
        int64_t default_index[2];
        size_t n, index_count, pairs, s, last_size, data_size = 0;
        qs_xpart x;
        const char *text, *last;
        int32_t predictor = 1, columns = 1, colors = 1, bits = 8;
        int64_t row_size;
        uint8_t *data = NULL;
        bool present;
        if (!qs_map_put(q, &visited, (uint64_t)current, 0)) goto done;
        n = qs_line(q, current, 40U, false, head, &consumed);
        if (!qs_is_object(head, n)) break;
        if (!qs_handle_xpart(q, current, -1, true, &x)) {
            qs_parts_free(&x.parts);
            goto done;
        }
        text = qs_value(&x.parts, "/Type", &n);
        if (!text || !qs_is(text, n, "/XRef") || !x.stream_count) {
            qs_parts_free(&x.parts);
            break;
        }
        if (qs_array_values(&x.parts, "/W", widths, 3U, true) < 3U) widths[0] = widths[1] = widths[2] = 0;
        row_size = widths[0] + widths[1] + widths[2];
        if (row_size <= 0 || widths[0] < 0 || widths[1] < 0 || widths[2] < 0 || widths[0] > 8 || widths[1] > 8 || widths[2] > 8) {
            qs_parts_free(&x.parts);
            break;
        }
        size_field = qs_value_ll(&x.parts, "/Size", NULL);
        index_count = qs_array_values(&x.parts, "/Index", NULL, 0U, false);
        if (index_count >= 2U) {
            index = index_count > SIZE_MAX / sizeof(*index) ? NULL : (int64_t *)xx_mem_alloc(index_count * sizeof(*index));
            if (!index) {
                q->failed = true;
                qs_parts_free(&x.parts);
                goto done;
            }
            (void)qs_array_values(&x.parts, "/Index", index, index_count, false);
        } else {
            default_index[0] = 0;
            default_index[1] = size_field > 0 ? size_field : 0;
            index_count = 2;
        }
        last = qs_filters(&x.parts, &last_size, NULL, NULL, NULL);
        {
            int64_t value = qs_value_ll(&x.parts, "/Predictor", &present);
            if (present) predictor = (int32_t)value;
            value = qs_value_ll(&x.parts, "/Columns", &present);
            if (present) columns = (int32_t)value;
            value = qs_value_ll(&x.parts, "/Colors", &present);
            if (present) colors = (int32_t)value;
            value = qs_value_ll(&x.parts, "/BitsPerComponent", &present);
            if (present) bits = (int32_t)value;
        }
        if (x.stream_offset >= 0 && x.stream_offset <= q->size && x.stream_size > 0 && x.stream_size <= q->size - x.stream_offset && x.stream_size <= QS_DECODE_LIMIT)
            data = qs_stream_data(q, x.stream_offset, x.stream_size, qs_filter_method(last, last_size), &data_size);
        if (predictor >= 2) qs_apply_predictor(q, &data, &data_size, predictor, columns, colors, bits);
        pairs = index_count / 2U;
        for (s = 0; s < pairs && !qs_stopped(q); ++s) {
            int64_t first = (index ? index : default_index)[s * 2U], entries = (index ? index : default_index)[s * 2U + 1U], k;
            for (k = 0; k < entries; ++k) {
                uint64_t type = 1, field = 0, id = (uint64_t)first + (uint64_t)k;
                int64_t base = row * row_size, i;
                if ((uint64_t)(row + 1) * (uint64_t)row_size > (uint64_t)data_size) break;
                if (widths[0]) {
                    type = 0;
                    for (i = 0; i < widths[0]; ++i) type = (type << 8) | data[base + i];
                }
                for (i = 0; i < widths[1]; ++i) field = (field << 8) | data[base + widths[0] + i];
                if (type == 1 && (int64_t)field > 0 && (int64_t)field < q->size && !qs_map_get(&seen, id, NULL)) {
                    if (!qs_objects_add(q, list, (int64_t)field, 0, id) || !qs_map_put(q, &seen, id, 0)) {
                        xx_mem_free(data);
                        xx_mem_free(index);
                        qs_parts_free(&x.parts);
                        goto done;
                    }
                }
                ++row;
            }
        }
        current = qs_value_ll(&x.parts, "/Prev", &present);
        if (!present) current = -1;
        xx_mem_free(data);
        xx_mem_free(index);
        qs_parts_free(&x.parts);
    }
    if (qs_stopped(q)) goto done;
    *is_stream = list->count != 0;
    /* Sizes are the distances between offsets; the last one stays 0. */
    qs_objects_by_offset(list);
    ok = true;
done:
    qs_map_free(&visited);
    qs_map_free(&seen);
    return ok;
}
/* XPDF::getObjectsFromStartxref: the objects of a classic "xref" table. */
static bool qs_objects_from_xref_table(xx_pdf_qscan *q, const qs_href *href, qs_objects *list)
{
    char line[21];
    int64_t current = href->xref_offset, consumed, end;
    size_t n = qs_line(q, current, 20U, false, line, &consumed);
    if (!qs_is_xref(line, n)) return !q->failed;
    current += consumed;
    while (!qs_stopped(q)) {
        const char *field;
        size_t size;
        uint64_t id, count, i;
        n = qs_line(q, current, 20U, false, line, &consumed);
        if (!n) break;
        field = qs_section(line, n, 0, &size);
        id = qs_to_ull(field, size);
        field = qs_section(line, n, 1, &size);
        count = qs_to_ull(field, size);
        current += consumed;
        if (!count) break;
        for (i = 0; i < count && !qs_stopped(q); ++i) {
            n = qs_line(q, current, 20U, false, line, &consumed);
            /* No progress means the end of the file. */
            if (consumed <= 0) break;
            field = qs_section(line, n, 2, &size);
            if (qs_is(field, size, "n")) {
                int64_t offset;
                field = qs_section(line, n, 0, &size);
                offset = (int64_t)qs_to_ull(field, size);
                if (offset > 0 && offset < q->size && !qs_objects_add(q, list, offset, 0, id + i)) return false;
            }
            current += consumed;
        }
    }
    if (qs_stopped(q)) return false;
    qs_objects_by_offset(list);
    if (list->count) {
        /* The last object ends at the table, or at the end of the file when
         * the table lies in front of it. */
        qs_object *last = &list->item[list->count - 1U];
        end = href->xref_offset;
        if (end <= last->offset) end = q->size;
        last->size = end - last->offset > 0 ? end - last->offset : 0;
    }
    return true;
}
/* XPDF::findObjects */
static bool qs_find_objects(xx_pdf_qscan *q, int64_t offset, int64_t size, bool deep, qs_objects *list)
{
    int64_t current = offset, bound;
    if (size == -1) size = q->size - offset;
    bound = offset + size;
    while (!qs_stopped(q) && current < bound) {
        char line[65];
        int64_t entry = current, consumed;
        size_t n = qs_line(q, current, 64U, false, line, &consumed);
        if (qs_is_object(line, n)) {
            char tail[33];
            int64_t search = current + consumed, end_object, tail_size;
            end_object = bound - search > 0 ? qs_find(q, search, bound - search, "endobj") : -1;
            if (end_object == -1) break;
            n = qs_line(q, end_object, 32U, false, tail, &tail_size);
            if (!qs_is_end_object(tail, n)) break;
            if (!qs_objects_add(q, list, current, end_object + tail_size - current, qs_object_id(line, xx_rt_strlen(line)))) return false;
            current = end_object + tail_size;
        } else if (qs_is_comment(line, n)) {
            current += consumed;
        } else {
            int64_t keyword;
            if (!deep) break;
            keyword = qs_find(q, current, bound - current, " obj");
            if (keyword == -1) break;
            current = keyword;
            while (current > 0 && !qs_stopped(q)) {
                int previous = qs_byte(q, current - 1);
                if (!((previous >= '0' && previous <= '9') || previous == ' ')) break;
                --current;
            }
            /* Always move forward. */
            if (current <= entry) current = keyword + 4;
            if (current >= bound) break;
        }
    }
    return !qs_stopped(q);
}
/* XPDF::findStartxrefs(0) */
static bool qs_find_startxrefs(xx_pdf_qscan *q)
{
    int64_t offset = 0;
    while (!qs_stopped(q)) {
        char line[21], number[21], target_line[21];
        int64_t start = qs_find(q, offset, -1, "startxref"), current, consumed, number_size, target;
        size_t n;
        bool is_xref, is_object;
        if (start == -1) break;
        current = start;
        (void)qs_line(q, current, 20U, false, line, &consumed);
        current += consumed;
        n = qs_line(q, current, 20U, false, number, &number_size);
        target = qs_to_ll(number, n, NULL);
        n = qs_line(q, target, 20U, false, target_line, &consumed);
        is_xref = qs_is_xref(target_line, n);
        is_object = qs_is_object(target_line, n);
        if ((is_xref || is_object) && target < current) {
            current += number_size;
            n = qs_line(q, current, 20U, false, line, &consumed);
            if (n >= 5U && !xx_rt_memcmp(line, "%%EOF", 5U)) {
                qs_href *record;
                current += 5;
                if (qs_byte(q, current) == 13) ++current;
                if (qs_byte(q, current) == 10) ++current;
                if (q->href_count == q->href_capacity) {
                    size_t capacity = q->href_capacity ? q->href_capacity * 2U : 8U;
                    qs_href *next = (qs_href *)xx_mem_realloc(q->href, capacity * sizeof(*next));
                    if (!next) {
                        q->failed = true;
                        return false;
                    }
                    q->href = next;
                    q->href_capacity = capacity;
                }
                record = &q->href[q->href_count++];
                record->xref_offset = target;
                record->footer_offset = start;
                record->footer_size = current - start;
                record->is_object = is_object;
                record->is_xref = is_xref;
                /* Text after "%%EOF" on its line, or anything but an object,
                 * a comment or a table after it, ends the search. */
                if (n != 5U) break;
                n = qs_line(q, current, 20U, false, line, &consumed);
                if (!qs_is_object(line, n) && !qs_is_comment(line, n) && !qs_is_xref(line, n)) break;
            }
        }
        offset = start + 10;
    }
    return !qs_stopped(q);
}
/* XPDF::scanStructure */
static bool qs_scan(xx_pdf_qscan *q)
{
    qs_map seen_offsets;
    qs_objects part;
    size_t i, j;
    bool ok = false;
    xx_mem_zero(&seen_offsets, sizeof(seen_offsets));
    xx_mem_zero(&part, sizeof(part));
    if (!qs_find_startxrefs(q)) return false;
    if (!q->href_count) {
        if (!qs_find_objects(q, 0, -1, false, &q->objects)) return false;
        for (j = 0; j < q->objects.count; ++j)
            if (!qs_map_put(q, &q->id_offset, q->objects.item[j].id, q->objects.item[j].offset)) return false;
        return true;
    }
    for (i = 0; i < q->href_count && !qs_stopped(q); ++i) {
        const qs_href *href = &q->href[i];
        part.count = 0;
        if (href->is_xref) {
            if (!qs_objects_from_xref_table(q, href, &part)) goto done;
        } else if (href->is_object) {
            bool is_stream = false;
            if (!qs_objects_from_xref_stream(q, href->xref_offset, &part, &is_stream)) goto done;
            if (!is_stream) {
                /* Not a usable cross-reference stream: search the objects. */
                part.count = 0;
                if (!qs_find_objects(q, 0, href->footer_offset, true, &part)) goto done;
            }
        }
        for (j = 0; j < part.count; ++j) {
            const qs_object *o = &part.item[j];
            if (!qs_map_get(&seen_offsets, (uint64_t)o->offset, NULL)) {
                if (!qs_map_put(q, &seen_offsets, (uint64_t)o->offset, 0) || !qs_objects_add(q, &q->objects, o->offset, o->size, o->id)) goto done;
            }
            /* A later section replaces the offset of an object number. */
            if (!qs_map_put(q, &q->id_offset, o->id, o->offset)) goto done;
        }
    }
    ok = !qs_stopped(q);
done:
    qs_map_free(&seen_offsets);
    xx_mem_free(part.item);
    return ok;
}

/* ---- public ------------------------------------------------------------- */

void xx_pdf_qscan_free(xx_pdf_qscan *q)
{
    if (!q) return;
    xx_mem_free(q->cache);
    xx_mem_free(q->href);
    xx_mem_free(q->objects.item);
    qs_map_free(&q->id_offset);
    xx_mem_free(q);
}
xx_pdf_qscan *xx_pdf_qscan_create(xx_io_device *device, int64_t base_address, xx_pd_struct *pd)
{
    xx_pdf_qscan *q;
    int64_t total;
    uint8_t magic[4];
    if (!device || base_address < 0 || (pd && xx_pd_is_stopped(pd))) return NULL;
    total = xx_io_size(device);
    if (total < base_address) return NULL;
    q = (xx_pdf_qscan *)xx_mem_alloc(sizeof(*q));
    if (!q) return NULL;
    xx_mem_zero(q, sizeof(*q));
    q->device = device;
    q->pd = pd;
    q->base = base_address;
    q->size = total - base_address;
    q->cache_start = -1;
    q->cache = (uint8_t *)xx_mem_alloc(QS_CACHE_SIZE);
    if (!q->cache) goto fail;
    /* XPDF::getHeaderOffset: "%PDF" at 0, or "%PDF-" in the first 1024 bytes. */
    q->header_offset = -1;
    if (q->size > 4) {
        if (qs_read(q, 0, magic, 4U) == 4U && !xx_rt_memcmp(magic, "%PDF", 4U)) q->header_offset = 0;
        else {
            q->header_offset = qs_find(q, 0, q->size < 1029 ? q->size : 1029, "%PDF-");
            if (q->header_offset > 1024) q->header_offset = -1;
        }
    }
    if (q->header_offset < 0 || !qs_scan(q)) goto fail;
    return q;
fail:
    xx_pdf_qscan_free(q);
    return NULL;
}
/* XPDF::getVersion: the three characters after "%PDF-", cut at a line end. */
void xx_pdf_qscan_version(const xx_pdf_qscan *scan, char version[8])
{
    xx_pdf_qscan *q = (xx_pdf_qscan *)scan; /* only the read cache changes */
    char text[4];
    int64_t consumed;
    size_t size, i, n = 0;
    version[0] = 0;
    if (!q) return;
    size = qs_line(q, q->header_offset + 5, 3U, false, text, &consumed);
    for (i = 0; i < size; ++i) {
        uint8_t c = (uint8_t)text[i];
        if (c < 0x80U) version[n++] = (char)c;
        else {
            version[n++] = (char)(0xC0U | (c >> 6));
            version[n++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    version[n] = 0;
}
/* XPDF::getHeaderCommentAsHex: when the line after the header starts with
 * '%', up to 40 bytes of it in lower-case hex. */
char *xx_pdf_qscan_header_comment_hex(xx_pdf_qscan *q)
{
    static const char hex[] = "0123456789abcdef";
    char line[101], text[81];
    uint8_t data[40];
    int64_t consumed, at;
    size_t got, i;
    if (!q || q->failed) return NULL;
    (void)qs_line(q, q->header_offset, 100U, false, line, &consumed);
    at = q->header_offset + consumed;
    text[0] = 0;
    if (qs_byte(q, at) == '%') {
        got = qs_read(q, at + 1, data, sizeof(data));
        for (i = 0; i < got && data[i] != 0 && data[i] != 10 && data[i] != 13; ++i) {
            text[i * 2U] = hex[data[i] >> 4];
            text[i * 2U + 1U] = hex[data[i] & 15U];
        }
        text[i * 2U] = 0;
    }
    return q->failed ? NULL : xx_str_create(text);
}
/* XPDF::getFilePartsWithDecodeLimit, FILEPART_OVERLAY */
void xx_pdf_qscan_overlay(const xx_pdf_qscan *scan, int64_t *offset, int64_t *size)
{
    xx_pdf_qscan *q = (xx_pdf_qscan *)scan; /* only the read cache changes */
    int64_t end;
    size_t i;
    *offset = -1;
    *size = 0;
    if (!q) return;
    if (q->href_count) {
        end = q->href[q->href_count - 1U].footer_offset + q->href[q->href_count - 1U].footer_size;
    } else {
        char line[21];
        int64_t consumed;
        (void)qs_line(q, q->header_offset, 20U, false, line, &consumed);
        end = q->header_offset + consumed;
    }
    for (i = 0; i < q->objects.count; ++i)
        if (q->objects.item[i].offset + q->objects.item[i].size > end) end = q->objects.item[i].offset + q->objects.item[i].size;
    if (q->failed || end >= q->size) return;
    *offset = end;
    *size = q->size - end;
}

typedef struct {
    char **name;
    size_t count, capacity;
    bool failed;
} qs_names;
static bool qs_names_add(void *context, const char *text, size_t size)
{
    qs_names *names = (qs_names *)context;
    size_t i;
    char *copy;
    for (i = 0; i < names->count; ++i)
        if (qs_is(text, size, names->name[i])) return true;
    if (names->count == names->capacity) {
        size_t n = names->capacity ? names->capacity * 2U : 8U;
        char **next = (char **)xx_mem_realloc(names->name, n * sizeof(*next));
        if (!next) goto fail;
        names->name = next;
        names->capacity = n;
    }
    copy = xx_str_create_len(size);
    if (!copy) goto fail;
    xx_mem_copy(copy, text, size);
    copy[size] = 0;
    names->name[names->count++] = copy;
    return true;
fail:
    names->failed = true;
    return false;
}
static int qs_name_compare(const void *a, const void *b, void *context)
{
    const uint8_t *x = *(const uint8_t *const *)a, *y = *(const uint8_t *const *)b;
    (void)context;
    for (; *x && *x == *y; ++x, ++y) {
    }
    return (int)*x - (int)*y;
}
static bool qs_append(char **out, const char *text, size_t size, const char *separator)
{
    size_t a = *out ? xx_rt_strlen(*out) : 0, s = a ? xx_rt_strlen(separator) : 0;
    char *next = xx_str_create_len(a + s + size);
    if (!next) return false;
    if (a) xx_mem_copy(next, *out, a);
    if (s) xx_mem_copy(next + a, separator, s);
    if (size) xx_mem_copy(next + a + s, text, size);
    next[a + s + size] = 0;
    xx_str_free(*out);
    *out = next;
    return true;
}
static bool qs_contains(const uint8_t *data, size_t size, const char *needle)
{
    size_t length = xx_rt_strlen(needle);
    const uint8_t *at = data, *end = data + size;
    if (!data || length > size) return false;
    end -= length - 1U;
    while (at < end && (at = (const uint8_t *)xx_rt_memchr(at, (uint8_t)needle[0], (size_t)(end - at))) != NULL) {
        if (!xx_rt_memcmp(at, needle, length)) return true;
        ++at;
    }
    return false;
}
void xx_pdf_qscan_info_free(xx_pdf_qscan_info *info)
{
    size_t i;
    if (!info) return;
    xx_str_free(info->filters);
    xx_str_free(info->linearized);
    xx_str_free(info->suspicious);
    for (i = 0; i < XX_PDF_QSCAN_META_COUNT; ++i) xx_str_free(info->meta[i]);
    xx_mem_zero(info, sizeof(*info));
}
const char *const xx_pdf_qscan_meta_keys[XX_PDF_QSCAN_META_COUNT] = {"/Title", "/Author", "/Subject", "/Keywords", "/Creator", "/Producer", "/CreationDate", "/ModDate"};

/* The lines of XPDF::getInfo that are made from the object token lists: the
 * filter names, getMetaInfoString, getLinearizedInfoString and
 * getSuspiciousInfoString. */
bool xx_pdf_qscan_get_info(xx_pdf_qscan *q, int part_limit, bool locked, xx_pdf_qscan_info *info, xx_pd_struct *pd)
{
    qs_names names;
    size_t *streams = NULL, stream_count = 0, stream_capacity = 0, i, t;
    uint32_t found = 0;
    const uint32_t all = (1U << QS_TRIGGERS) - 1U;
    bool ok = false;
    if (!info) return false;
    xx_mem_zero(info, sizeof(*info));
    xx_mem_zero(&names, sizeof(names));
    if (!q || q->failed) return false;
    q->pd = pd;
    /* XPDF::getParts: one token list per object, in scan order. */
    for (i = 0; i < q->objects.count; ++i) {
        qs_xpart x;
        size_t last_size;
        bool good;
        if (!qs_handle_xpart(q, q->objects.item[i].offset, part_limit, false, &x)) {
            qs_parts_free(&x.parts);
            goto done;
        }
        (void)qs_filters(&x.parts, &last_size, qs_names_add, &names, &good);
        if (!good) {
            qs_parts_free(&x.parts);
            goto done;
        }
        if (!info->linearized && qs_parts_find(&x.parts, "/Linearized") != SIZE_MAX) {
            char text[128];
            bool present;
            int64_t declared = qs_value_ll(&x.parts, "/L", &present);
            if (present && declared > 0 && declared != q->size)
                (void)xx_rt_snprintf(text, sizeof(text), "Linearized (declared L=%lld, actual=%lld)", (long long)declared, (long long)q->size);
            else (void)xx_rt_snprintf(text, sizeof(text), "Linearized");
            info->linearized = xx_str_create(text);
            if (!info->linearized) {
                qs_parts_free(&x.parts);
                goto done;
            }
        }
        /* getMetaInfoString: per key, the first value that is not empty. */
        for (t = 0; t + 1U < x.parts.count; ++t) {
            size_t k;
            if (x.parts.pool[x.parts.token[t].at] != '/') continue;
            for (k = 0; k < XX_PDF_QSCAN_META_COUNT; ++k) {
                xx_pdf_qscan_value_type type;
                char *text;
                if (info->meta[k] || !qs_part_is(&x.parts, t, xx_pdf_qscan_meta_keys[k])) continue;
                if (!qs_value_text(q, &x.parts, t + 1U, &text, &type)) {
                    qs_parts_free(&x.parts);
                    goto done;
                }
                if (text && *text) info->meta[k] = text;
                else xx_str_free(text);
            }
        }
        for (t = 0; t < QS_TRIGGERS; ++t)
            if (!(found & (1U << t)) && qs_parts_find(&x.parts, qs_triggers[t]) != SIZE_MAX) found |= 1U << t;
        if (qs_parts_find(&x.parts, "/ObjStm") != SIZE_MAX) {
            if (stream_count == stream_capacity) {
                size_t n = stream_capacity ? stream_capacity * 2U : 16U;
                size_t *next = (size_t *)xx_mem_realloc(streams, n * sizeof(*next));
                if (!next) {
                    qs_parts_free(&x.parts);
                    goto done;
                }
                streams = next;
                stream_capacity = n;
            }
            streams[stream_count++] = i;
        }
        qs_parts_free(&x.parts);
    }
    /* Object streams hide dictionaries: search their decoded bytes. */
    for (i = 0; i < stream_count && found != all && !locked; ++i) {
        qs_xpart x;
        const char *last;
        size_t last_size, data_size = 0;
        uint8_t *data;
        if (!qs_handle_xpart(q, q->objects.item[streams[i]].offset, -1, true, &x)) {
            qs_parts_free(&x.parts);
            goto done;
        }
        last = qs_filters(&x.parts, &last_size, NULL, NULL, NULL);
        if (!x.stream_count || x.stream_offset < 0 || x.stream_offset > q->size || x.stream_size <= 0 || x.stream_size > QS_DECODE_LIMIT ||
            x.stream_size > q->size - x.stream_offset) {
            qs_parts_free(&x.parts);
            continue;
        }
        data = qs_stream_data(q, x.stream_offset, x.stream_size, qs_filter_method(last, last_size), &data_size);
        qs_parts_free(&x.parts);
        for (t = 0; t < QS_TRIGGERS; ++t)
            if (!(found & (1U << t)) && qs_contains(data, data_size, qs_triggers[t])) found |= 1U << t;
        xx_mem_free(data);
        if (qs_stopped(q)) goto done;
    }
    if (qs_stopped(q)) goto done;
    xx_sort_context(names.name, names.count, sizeof(*names.name), qs_name_compare, NULL);
    for (i = 0; i < names.count; ++i) {
        char *name = qs_latin1_utf8(names.name[i], xx_rt_strlen(names.name[i]));
        bool good = name && qs_append(&info->filters, name, xx_rt_strlen(name), ", ");
        xx_str_free(name);
        if (!good) goto done;
    }
    for (t = 0; t < QS_TRIGGERS; ++t)
        if ((found & (1U << t)) && !qs_append(&info->suspicious, qs_triggers[t], xx_rt_strlen(qs_triggers[t]), ", ")) goto done;
    if (!info->filters) info->filters = xx_str_create("");
    if (!info->linearized) info->linearized = xx_str_create("");
    if (!info->suspicious) info->suspicious = xx_str_create("");
    ok = info->filters && info->linearized && info->suspicious;
    for (i = 0; i < XX_PDF_QSCAN_META_COUNT; ++i) {
        if (!info->meta[i]) info->meta[i] = xx_str_create("");
        if (!info->meta[i]) ok = false;
    }
done:
    for (i = 0; i < names.count; ++i) xx_str_free(names.name[i]);
    xx_mem_free(names.name);
    xx_mem_free(streams);
    if (!ok) xx_pdf_qscan_info_free(info);
    return ok;
}
void xx_pdf_qscan_values_free(xx_pdf_qscan_value *values, size_t count)
{
    size_t i;
    for (i = 0; values && i < count; ++i) xx_str_free(values[i].text);
    xx_mem_free(values);
}
/* XPDF::getValuesByKey over XPDF::getParts(part_limit). */
bool xx_pdf_qscan_get_values(xx_pdf_qscan *q, const char *key, int part_limit, xx_pdf_qscan_value **values, size_t *count, xx_pd_struct *pd)
{
    xx_pdf_qscan_value *list = NULL;
    size_t used = 0, capacity = 0, i, j, k;
    if (values) *values = NULL;
    if (count) *count = 0;
    if (!q || q->failed || !key || !values || !count) return false;
    q->pd = pd;
    for (i = 0; i < q->objects.count; ++i) {
        qs_xpart x;
        if (!qs_handle_xpart(q, q->objects.item[i].offset, part_limit, false, &x)) {
            qs_parts_free(&x.parts);
            goto fail;
        }
        for (j = 0; j + 1U < x.parts.count; ++j) {
            xx_pdf_qscan_value_type type;
            char *text;
            if (!qs_part_is(&x.parts, j, key)) continue;
            if (!qs_value_text(q, &x.parts, j + 1U, &text, &type)) {
                qs_parts_free(&x.parts);
                goto fail;
            }
            if (!text) continue;
            for (k = 0; k < used; ++k)
                if (!xx_rt_strcmp(list[k].text, text)) break;
            if (k < used) {
                xx_str_free(text);
                continue;
            }
            if (used == capacity) {
                size_t n = capacity ? capacity * 2U : 8U;
                xx_pdf_qscan_value *next = (xx_pdf_qscan_value *)xx_mem_realloc(list, n * sizeof(*next));
                if (!next) {
                    xx_str_free(text);
                    qs_parts_free(&x.parts);
                    goto fail;
                }
                list = next;
                capacity = n;
            }
            list[used].text = text;
            list[used].type = type;
            ++used;
        }
        qs_parts_free(&x.parts);
    }
    if (qs_stopped(q)) goto fail;
    *values = list;
    *count = used;
    return true;
fail:
    xx_pdf_qscan_values_free(list, used);
    return false;
}

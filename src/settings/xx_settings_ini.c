/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_settings_internal.h"
#include "platforms/xx_settings_platform.h"

/* Two dialects share one line model. A file whose first line (after any blank or
 * comment lines) is the v1 header was written by xxfclib and keeps its original rules:
 * two-digit \x escapes, unknown escapes keep their backslash, and a line that cannot be
 * parsed rejects the file. Every other file is read like QSettings::IniFormat
 * (qsettings.cpp readIniLine, iniUnescapedKey and iniUnescapedStringList; see parse_qt
 * for what stores that are not Qt-compatible read differently) and written in the Qt 5 dialect, which
 * Qt 6 reads identically. Saving an existing file replaces only the lines of the
 * changed keys; every other byte, including comments and Qt-only values, is kept. */

#define INI_HEADER "; xxfclib settings v1"
#define INI_NONE ((size_t)-1)

enum { INI_OTHER, INI_HEADER_LINE, INI_SECTION, INI_ENTRY };
enum { INI_KEEP, INI_DELETE, INI_REPLACE, INI_REENCODE };

typedef struct ini_record_s {
    size_t start, end; /* Bytes of the record, including its line terminator. */
    size_t key_end;    /* ENTRY: the raw key is start..key_end without trailing blanks. */
    size_t section;    /* ENTRY: owning SECTION record, INI_NONE before the first one. */
    char *name;        /* SECTION: key prefix ("" or "Group/"), NULL if unusable. ENTRY: full key, NULL if not loaded. */
    char *text;        /* ENTRY: decoded value text; may contain NUL bytes. */
    size_t text_size;
    const xx_settings_entry *change;
    unsigned char kind, action;
    bool terminated;
    bool open_quote; /* Qt dialect: a quote left open runs to the end of the file. */
} ini_record;

typedef struct ini_document_s {
    char *data;
    size_t size;
    bool v1;
    ini_record *records;
    size_t count, capacity;
} ini_document;

typedef struct ini_insert_s {
    const xx_settings_entry *entry;
    size_t prefix; /* Bytes of entry->key naming its group, '/' included; 0 for a root key. */
    size_t anchor; /* Record the line follows, INI_NONE for a new section. */
} ini_insert;

typedef struct unit_writer_s {
    xx_settings_buffer *buffer;
    unsigned high; /* Pending UTF-16 high surrogate, 0 if none. */
} unit_writer;

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}
static bool blank_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool ascii_equal(const char *text, size_t size, const char *lower)
{
    size_t length = xx_rt_strlen(lower);
    if (size != length) return false;
    for (size_t i = 0; i < size; ++i)
        if ((text[i] >= 'A' && text[i] <= 'Z' ? text[i] - 'A' + 'a' : text[i]) != lower[i]) return false;
    return true;
}
static char *concat(const char *left, const char *right, size_t right_size)
{
    xx_settings_buffer buffer = {0};
    if (!xx_settings_buffer_text(&buffer, left) || !xx_settings_buffer_append(&buffer, right, right_size)) {
        xx_rt_free(buffer.data);
        return NULL;
    }
    return buffer.data;
}

/* ------------------------------------------------------------ UTF-8 / UTF-16 --- */

static bool put_code_point(xx_settings_buffer *buffer, uint32_t code)
{
    char bytes[4];
    size_t size;
    if (code < 0x80) {
        bytes[0] = (char)code;
        size = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xc0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3f));
        size = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xe0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (code & 0x3f));
        size = 3;
    } else {
        bytes[0] = (char)(0xf0 | (code >> 18));
        bytes[1] = (char)(0x80 | ((code >> 12) & 0x3f));
        bytes[2] = (char)(0x80 | ((code >> 6) & 0x3f));
        bytes[3] = (char)(0x80 | (code & 0x3f));
        size = 4;
    }
    return xx_settings_buffer_append(buffer, bytes, size);
}

/* An unpaired surrogate cannot be UTF-8; it becomes U+FFFD. */
static bool flush_high(unit_writer *writer)
{
    if (!writer->high) return true;
    writer->high = 0;
    return put_code_point(writer->buffer, 0xfffd);
}

static bool put_unit(unit_writer *writer, unsigned unit)
{
    unit &= 0xffff;
    if (unit >= 0xdc00 && unit <= 0xdfff && writer->high) {
        uint32_t code = 0x10000 + ((uint32_t)(writer->high - 0xd800) << 10) + (unit - 0xdc00);
        writer->high = 0;
        return put_code_point(writer->buffer, code);
    }
    if (!flush_high(writer)) return false;
    if (unit >= 0xd800 && unit <= 0xdbff) {
        writer->high = unit;
        return true;
    }
    return put_code_point(writer->buffer, unit >= 0xdc00 && unit <= 0xdfff ? 0xfffd : unit);
}

static bool put_raw(unit_writer *writer, const char *data, size_t size)
{
    return flush_high(writer) && xx_settings_buffer_append(writer->buffer, data, size);
}

/* Returns the length of one valid UTF-8 sequence, or 0. */
static size_t utf8_decode(const unsigned char *text, size_t size, uint32_t *code)
{
    uint32_t value = text[0];
    size_t length;
    if (value < 0x80) {
        *code = value;
        return 1;
    }
    if (value >= 0xc2 && value <= 0xdf) {
        length = 2;
        value &= 0x1f;
    } else if (value >= 0xe0 && value <= 0xef) {
        length = 3;
        value &= 0x0f;
    } else if (value >= 0xf0 && value <= 0xf4) {
        length = 4;
        value &= 0x07;
    } else return 0;
    if (size < length) return 0;
    for (size_t i = 1; i < length; ++i) {
        if ((text[i] & 0xc0) != 0x80) return 0;
        value = (value << 6) | (text[i] & 0x3f);
    }
    if ((length == 3 && (value < 0x800 || (value >= 0xd800 && value <= 0xdfff))) || (length == 4 && (value < 0x10000 || value > 0x10ffff))) return 0;
    *code = value;
    return length;
}

/* ------------------------------------------------------------ v1 dialect --- */

static xxfc_status_t decode_key(const char *text, size_t size, char **result)
{
    char *key = xx_settings_duplicate(text, size);
    size_t out = 0;
    *result = NULL;
    if (!key) return XXFC_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < size; ++i) {
        char c = text[i];
        if (c == '%' && i + 2 < size && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            c = (char)((hex(text[i + 1]) << 4) | hex(text[i + 2]));
            i += 2;
            if (!c) {
                xx_rt_free(key);
                return XXFC_ERR_INVALID_ARG;
            }
        }
        key[out++] = c == '\\' ? '/' : c;
    }
    key[out] = 0;
    *result = key;
    return XXFC_OK;
}

static xxfc_status_t decode_text(const char *text, size_t size, xx_settings_buffer *buffer)
{
    bool quoted = false;
    for (size_t i = 0; i < size; ++i) {
        char c = text[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (c == '\\') {
            if (++i == size) return XXFC_ERR_INVALID_ARG;
            c = text[i];
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '0': c = 0; break;
                case 'a': c = '\a'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'v': c = '\v'; break;
                case '\\':
                case '"': break;
                case 'x': {
                    int digit;
                    if (i + 1 >= size || (digit = hex(text[i + 1])) < 0) return XXFC_ERR_INVALID_ARG;
                    c = (char)digit;
                    ++i;
                    if (i + 1 < size && (digit = hex(text[i + 1])) >= 0) {
                        c = (char)(((unsigned char)c << 4) | digit);
                        ++i;
                    }
                    break;
                }
                default:
                    if (!xx_settings_buffer_text(buffer, "\\")) return XXFC_ERR_OUT_OF_MEMORY;
                    break;
            }
        }
        if (!xx_settings_buffer_append(buffer, &c, 1)) return XXFC_ERR_OUT_OF_MEMORY;
    }
    if (quoted) return XXFC_ERR_INVALID_ARG;
    return xx_settings_buffer_append(buffer, "", 0) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
}

static bool encode_key(xx_settings_buffer *buffer, const char *key, size_t size, bool nested)
{
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (nested && c == '/') {
            if (!xx_settings_buffer_text(buffer, "\\")) return false;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
            if (!xx_settings_buffer_append(buffer, &c, 1)) return false;
        } else {
            char escaped[3] = {'%', digits[c >> 4], digits[c & 15]};
            if (!xx_settings_buffer_append(buffer, escaped, 3)) return false;
        }
    }
    return true;
}

static bool encode_text(xx_settings_buffer *buffer, const char *text, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    if (!xx_settings_buffer_text(buffer, "\"")) return false;
    for (const unsigned char *p = (const unsigned char *)text, *end = p + size; p < end; ++p) {
        const char *escape = NULL;
        switch (*p) {
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            case '"': escape = "\\\""; break;
            case '\\': escape = "\\\\"; break;
            default: break;
        }
        if (escape) {
            if (!xx_settings_buffer_text(buffer, escape)) return false;
        } else if (*p < 32) {
            char escaped[4] = {'\\', 'x', digits[*p >> 4], digits[*p & 15]};
            if (!xx_settings_buffer_append(buffer, escaped, 4)) return false;
        } else if (!xx_settings_buffer_append(buffer, p, 1)) return false;
    }
    return xx_settings_buffer_text(buffer, "\"");
}

/* ------------------------------------------------------------ Qt dialect --- */

/* QSettingsPrivate::iniUnescapedKey: '\' separates groups, %XX is a Latin-1 code
 * point and %UXXXX a UTF-16 unit; other bytes are kept (UTF-8, as Qt 6 reads them). */
static xxfc_status_t qt_decode_key(const char *text, size_t size, char **result)
{
    xx_settings_buffer buffer = {0};
    unit_writer writer = {&buffer, 0};
    bool ok = xx_settings_buffer_append(&buffer, "", 0);
    *result = NULL;
    for (size_t i = 0; ok && i < size;) {
        if (text[i] == '\\') {
            ok = put_raw(&writer, "/", 1);
            ++i;
            continue;
        }
        if (text[i] == '%' && i + 1 < size) {
            size_t first = i + 1, digits = 2, k = 0;
            unsigned unit = 0;
            if (text[i + 1] == 'U') {
                ++first;
                digits = 4;
            }
            if (first + digits <= size)
                for (; k < digits && hex(text[first + k]) >= 0; ++k) unit = unit * 16 + (unsigned)hex(text[first + k]);
            if (first + digits <= size && k == digits) {
                ok = put_unit(&writer, unit);
                i = first + digits;
                continue;
            }
        }
        ok = put_raw(&writer, text + i, 1);
        ++i;
    }
    if (!ok || !flush_high(&writer)) {
        xx_rt_free(buffer.data);
        return XXFC_ERR_OUT_OF_MEMORY;
    }
    if (xx_rt_memchr(buffer.data, 0, buffer.size)) {
        xx_rt_free(buffer.data);
        return XXFC_ERR_INVALID_ARG;
    }
    *result = buffer.data;
    return XXFC_OK;
}

static void chop_blanks(xx_settings_buffer *buffer, size_t limit)
{
    while (buffer->size > limit && (buffer->data[buffer->size - 1] == ' ' || buffer->data[buffer->size - 1] == '\t')) --buffer->size;
    buffer->data[buffer->size] = 0;
}

/* QSettingsPrivate::iniUnescapedStringList. \x and octal escapes take every following
 * digit as one UTF-16 unit. With @p marks a list (unquoted commas) is flattened to "a, b"
 * and @p marks receives the size_t offset of each ", " between items; without it unquoted
 * commas are ordinary text ("Ctrl+," stays "Ctrl+,"). */
static bool qt_decode_text(const char *data, size_t i, size_t to, xx_settings_buffer *out, xx_settings_buffer *marks)
{
    static const char codes[] = "aabbffnnrrttvv\"\"\?\?''\\\\"; /* Escaped '?' avoids a trigraph. */
    static const char values[] = "\a\a\b\b\f\f\n\n\r\r\t\t\v\v\"\"\?\?''\\\\";
    unit_writer writer = {out, 0};
    bool in_quotes = false, item_quoted = false;
    size_t chop_limit;
    if (!xx_settings_buffer_append(out, "", 0)) return false;
skip_blanks:
    while (i < to && (data[i] == ' ' || data[i] == '\t')) ++i;
normal:
    chop_limit = out->size;
    while (i < to) {
        char c = data[i];
        if (c == '\\') {
            size_t k;
            if (++i >= to) return flush_high(&writer);
            c = data[i++];
            for (k = 0; codes[k]; k += 2)
                if (c == codes[k]) {
                    if (!put_unit(&writer, (unsigned char)values[k])) return false;
                    goto normal;
                }
            if (c == 'x') {
                if (i >= to) return flush_high(&writer);
                if (hex(data[i]) >= 0) {
                    unsigned unit = 0;
                    while (i < to && hex(data[i]) >= 0) unit = ((unit << 4) | (unsigned)hex(data[i++])) & 0xffff;
                    if (!put_unit(&writer, unit)) return false;
                    goto normal;
                }
            } else if (c >= '0' && c <= '7') {
                unsigned unit = (unsigned)(c - '0');
                while (i < to && data[i] >= '0' && data[i] <= '7') unit = ((unit << 3) | (unsigned)(data[i++] - '0')) & 0xffff;
                if (!put_unit(&writer, unit)) return false;
                goto normal;
            } else if (c == '\n' || c == '\r') {
                if (i < to && (data[i] == '\n' || data[i] == '\r') && data[i] != c) ++i;
            }
            chop_limit = out->size; /* Any other escaped character is dropped. */
            continue;
        }
        if (c == '"') {
            ++i;
            item_quoted = true;
            in_quotes = !in_quotes;
            if (!in_quotes) goto skip_blanks;
            continue;
        }
        if (c == ',' && !in_quotes && marks) {
            if (!flush_high(&writer)) return false;
            if (!item_quoted) chop_blanks(out, chop_limit);
            if (!xx_settings_buffer_append(marks, &out->size, sizeof(out->size)) || !xx_settings_buffer_text(out, ", ")) return false;
            item_quoted = false;
            ++i;
            goto skip_blanks;
        }
        {
            size_t j = i + 1;
            while (j < to && data[j] != '\\' && data[j] != '"' && data[j] != ',') ++j;
            if (!put_raw(&writer, data + i, j - i)) return false;
            i = j;
        }
    }
    if (!flush_high(&writer)) return false;
    if (!item_quoted) chop_blanks(out, chop_limit);
    return true;
}

static bool hex_letter_or_digit(unsigned unit)
{
    return (unit >= '0' && unit <= '9') || (unit >= 'a' && unit <= 'f') || (unit >= 'A' && unit <= 'F');
}

static bool append_unit_escape(xx_settings_buffer *buffer, unsigned unit)
{
    static const char digits[] = "0123456789abcdef";
    char text[8];
    size_t size = 0;
    char reversed[4];
    size_t count = 0;
    do {
        reversed[count++] = digits[unit & 15];
        unit >>= 4;
    } while (unit && count < 4);
    text[size++] = '\\';
    text[size++] = 'x';
    while (count) text[size++] = reversed[--count];
    return xx_settings_buffer_append(buffer, text, size);
}

/* QSettingsPrivate::iniEscapedString of Qt 5 without an INI codec: every unit outside
 * 0x20..0x7e is a \x escape, and a hex digit after an escape is escaped as well. */
static bool qt_escape_value(xx_settings_buffer *out, const char *text, size_t size)
{
    xx_settings_buffer escaped = {0};
    bool quotes = false, escape_digit = false, ok = xx_settings_buffer_append(&escaped, "", 0);
    for (size_t i = 0; ok && i < size;) {
        uint32_t code;
        unsigned units[2] = {0, 0};
        size_t length = utf8_decode((const unsigned char *)text + i, size - i, &code), count = 1;
        if (!length) {
            /* Not UTF-8: keep the byte, which the reader copies back unchanged. */
            ok = xx_settings_buffer_append(&escaped, text + i, 1);
            escape_digit = false;
            ++i;
            continue;
        }
        i += length;
        units[0] = code;
        if (code >= 0x10000) {
            units[0] = 0xd800 + ((code - 0x10000) >> 10);
            units[1] = 0xdc00 + ((code - 0x10000) & 0x3ff);
            count = 2;
        }
        for (size_t k = 0; ok && k < count; ++k) {
            unsigned unit = units[k];
            const char *named = NULL;
            if (unit == ';' || unit == ',' || unit == '=') quotes = true;
            if (escape_digit && hex_letter_or_digit(unit)) {
                ok = append_unit_escape(&escaped, unit);
                continue;
            }
            escape_digit = false;
            switch (unit) {
                case 0:
                    named = "\\0";
                    escape_digit = true;
                    break;
                case '\a': named = "\\a"; break;
                case '\b': named = "\\b"; break;
                case '\f': named = "\\f"; break;
                case '\n': named = "\\n"; break;
                case '\r': named = "\\r"; break;
                case '\t': named = "\\t"; break;
                case '\v': named = "\\v"; break;
                case '"': named = "\\\""; break;
                case '\\': named = "\\\\"; break;
                default: break;
            }
            if (named) ok = xx_settings_buffer_text(&escaped, named);
            else if (unit <= 0x1f || unit >= 0x7f) {
                ok = append_unit_escape(&escaped, unit);
                escape_digit = true;
            } else {
                char c = (char)unit;
                ok = xx_settings_buffer_append(&escaped, &c, 1);
            }
        }
    }
    if (ok && (quotes || (escaped.size && (escaped.data[0] == ' ' || escaped.data[escaped.size - 1] == ' '))))
        ok = xx_settings_buffer_text(out, "\"") && xx_settings_buffer_append(out, escaped.data, escaped.size) && xx_settings_buffer_text(out, "\"");
    else if (ok) ok = xx_settings_buffer_append(out, escaped.data, escaped.size);
    xx_rt_free(escaped.data);
    return ok;
}

/* QSettingsPrivate::iniEscapedKey. */
static bool qt_escape_key(xx_settings_buffer *buffer, const char *key, size_t size)
{
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < size;) {
        uint32_t code;
        size_t length = utf8_decode((const unsigned char *)key + i, size - i, &code);
        bool ok;
        if (!length) {
            ok = xx_settings_buffer_append(buffer, key + i, 1);
            ++i;
        } else if (code == '/') {
            ok = xx_settings_buffer_text(buffer, "\\");
        } else if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == '_' || code == '-' || code == '.') {
            char c = (char)code;
            ok = xx_settings_buffer_append(buffer, &c, 1);
        } else if (code <= 0xff) {
            char escaped[3] = {'%', digits[code >> 4], digits[code & 15]};
            ok = xx_settings_buffer_append(buffer, escaped, 3);
        } else {
            unsigned units[2] = {code, 0};
            size_t count = 1;
            if (code >= 0x10000) {
                units[0] = 0xd800 + ((code - 0x10000) >> 10);
                units[1] = 0xdc00 + ((code - 0x10000) & 0x3ff);
                count = 2;
            }
            ok = true;
            for (size_t k = 0; ok && k < count; ++k) {
                char escaped[6] = {'%', 'U', digits[(units[k] >> 12) & 15], digits[(units[k] >> 8) & 15], digits[(units[k] >> 4) & 15], digits[units[k] & 15]};
                ok = xx_settings_buffer_append(buffer, escaped, 6);
            }
        }
        if (!ok) return false;
        if (length) i += length;
    }
    return true;
}

/* ------------------------------------------------------------ document --- */

static void free_document(ini_document *document)
{
    for (size_t i = 0; i < document->count; ++i) {
        xx_rt_free(document->records[i].name);
        xx_rt_free(document->records[i].text);
    }
    xx_rt_free(document->records);
    xx_rt_free(document->data);
    xx_rt_memset(document, 0, sizeof(*document));
}

static ini_record *add_record(ini_document *document, size_t start, size_t end, bool terminated)
{
    ini_record *record;
    if (document->count == document->capacity) {
        size_t capacity = document->capacity ? document->capacity * 2 : 32;
        ini_record *records;
        if (capacity > XX_SETTINGS_MAX_FILE_SIZE) return NULL;
        records = (ini_record *)xx_rt_realloc(document->records, capacity * sizeof(*records));
        if (!records) return NULL;
        document->records = records;
        document->capacity = capacity;
    }
    record = &document->records[document->count++];
    xx_rt_memset(record, 0, sizeof(*record));
    record->start = start;
    record->end = end;
    record->terminated = terminated;
    record->section = INI_NONE;
    return record;
}

/* End of the physical line holding @p position, past "\r\n", "\n" or "\r". */
static size_t line_end(const ini_document *document, size_t position, bool *terminated)
{
    while (position < document->size && document->data[position] != '\n' && document->data[position] != '\r') ++position;
    *terminated = position < document->size;
    if (*terminated) position += document->data[position] == '\r' && position + 1 < document->size && document->data[position + 1] == '\n' ? 2 : 1;
    return position;
}

static xxfc_status_t load_entry(xx_settings *store, ini_record *record, bool lenient)
{
    xx_settings_value value;
    xxfc_status_t status = xx_settings_decode_value(record->text, record->text_size, &value);
    if (status == XXFC_ERR_INVALID_ARG && lenient) {
        /* QSettings has no typed values: an unknown "@XX..." prefix is plain text. */
        xx_settings_value_release(&value);
        value.type = XX_SETTINGS_VALUE_STRING;
        value.data.buffer.data = record->text; /* Borrowed; xx_settings_set copies it. */
        value.data.buffer.size = record->text_size;
        status = xx_settings_set(store, record->name, &value);
    } else {
        if (status == XXFC_OK) status = xx_settings_set(store, record->name, &value);
        xx_settings_value_release(&value);
    }
    if (status == XXFC_ERR_INVALID_ARG && lenient) {
        /* A key the store cannot hold is left in the file untouched. */
        xx_rt_free(record->name);
        record->name = NULL;
        status = XXFC_OK;
    }
    return status;
}

/* A QSettings list ("a, b": unquoted commas) is a STRING_LIST, as the registry and macOS
 * preferences hold one; @p marks are the offsets of the ", " between its items in the
 * flattened text. A list with a NUL in it stays that text. */
static xxfc_status_t load_list(xx_settings *store, ini_record *record, const size_t *marks, size_t separators)
{
    char **items;
    xx_settings_value value;
    xxfc_status_t status = XXFC_OK;
    size_t start = 0, count = separators + 1;
    if (xx_rt_memchr(record->text, 0, record->text_size)) return load_entry(store, record, true);
    items = (char **)xx_rt_calloc(count, sizeof(*items));
    if (!items) return XXFC_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; i < count && status == XXFC_OK; ++i) {
        size_t end = i < separators ? marks[i] : record->text_size;
        const char *item = record->text + start;
        size_t size = end - start;
        if (size >= 2 && item[0] == '@' && item[1] == '@') { /* QSettings doubles an item's leading '@'. */
            ++item;
            --size;
        }
        items[i] = xx_settings_duplicate(item, size);
        if (!items[i]) status = XXFC_ERR_OUT_OF_MEMORY;
        start = end + 2;
    }
    if (status == XXFC_OK) {
        xx_rt_memset(&value, 0, sizeof(value));
        value.type = XX_SETTINGS_VALUE_STRING_LIST;
        value.data.list.items = (const char *const *)items;
        value.data.list.count = count;
        status = xx_settings_set(store, record->name, &value);
        if (status == XXFC_ERR_INVALID_ARG) {
            /* A key the store cannot hold is left in the file untouched. */
            xx_rt_free(record->name);
            record->name = NULL;
            status = XXFC_OK;
        }
    }
    for (size_t i = 0; i < count; ++i) xx_rt_free(items[i]);
    xx_rt_free(items);
    return status;
}

static xxfc_status_t parse_v1(ini_document *document, xx_settings *store, size_t position)
{
    const char *data = document->data, *prefix = "";
    size_t section = INI_NONE;
    bool preamble = true; /* Blank and comment lines before the first entry may hold the header. */
    xxfc_status_t status = XXFC_OK;
    while (position < document->size && status == XXFC_OK) {
        const char *newline = (const char *)xx_rt_memchr(data + position, '\n', document->size - position);
        size_t content = newline ? (size_t)(newline - data) : document->size, first = position, last = content, index = document->count;
        ini_record *record = add_record(document, position, newline ? content + 1 : document->size, newline != NULL);
        if (!record) return XXFC_ERR_OUT_OF_MEMORY;
        position = record->end;
        while (first < last && space(data[first])) ++first;
        while (last > first && space(data[last - 1])) --last;
        if (preamble && last - first == sizeof(INI_HEADER) - 1 && !xx_rt_memcmp(data + first, INI_HEADER, last - first)) {
            record->kind = INI_HEADER_LINE;
            preamble = false;
            continue;
        }
        if (first == last || data[first] == ';' || data[first] == '#') continue;
        preamble = false;
        if (data[first] == '[') {
            char *decoded;
            if (last - first < 2 || data[last - 1] != ']') return XXFC_ERR_INVALID_ARG;
            ++first;
            --last;
            record->kind = INI_SECTION;
            if (last - first == 7 && !xx_rt_memcmp(data + first, "General", 7)) {
                record->name = xx_settings_duplicate("", 0);
                if (!record->name) return XXFC_ERR_OUT_OF_MEMORY;
            } else {
                if (last - first == 8 && !xx_rt_memcmp(data + first, "%General", 8)) ++first;
                status = decode_key(data + first, last - first, &decoded);
                if (status != XXFC_OK) return status;
                record->name = decoded[0] ? concat(decoded, "/", 1) : decoded;
                if (decoded[0]) xx_rt_free(decoded);
                if (!record->name) return XXFC_ERR_OUT_OF_MEMORY;
            }
            section = index;
            prefix = record->name;
        } else {
            const char *equal = (const char *)xx_rt_memchr(data + first, '=', last - first);
            xx_settings_buffer decoded = {0};
            char *name;
            size_t key_end, value_start;
            if (!equal) return XXFC_ERR_INVALID_ARG;
            key_end = (size_t)(equal - data);
            value_start = key_end + 1;
            while (key_end > first && space(data[key_end - 1])) --key_end;
            while (value_start < last && space(data[value_start])) ++value_start;
            record->kind = INI_ENTRY;
            record->key_end = key_end;
            record->section = section;
            status = decode_key(data + first, key_end - first, &name);
            if (status != XXFC_OK) return status;
            record->name = concat(prefix, name, xx_rt_strlen(name));
            xx_rt_free(name);
            if (!record->name) return XXFC_ERR_OUT_OF_MEMORY;
            status = decode_text(data + value_start, last - value_start, &decoded);
            record->text = decoded.data;
            record->text_size = decoded.size;
            if (status == XXFC_OK) status = load_entry(store, record, false);
        }
    }
    return status;
}

/* QConfFileSettingsPrivate::readIniLine from a non-blank character: quotes and '\'
 * escapes span line breaks; an unquoted line break or ';' ends the line. *quoted_end
 * is true when a quote is still open at the end of the file. */
static void scan_qt_line(const ini_document *document, size_t i, size_t *content_end, size_t *equals, bool *quoted_end)
{
    const char *data = document->data;
    bool quoted = false;
    *equals = INI_NONE;
    while (i < document->size) {
        char c = data[i++];
        if (c == '=') {
            if (!quoted && *equals == INI_NONE) *equals = i - 1;
        } else if (c == '\n' || c == '\r' || c == ';') {
            if (!quoted) {
                --i;
                break;
            }
        } else if (c == '\\') {
            if (i < document->size) {
                char next = data[i++];
                if (i < document->size && ((next == '\n' && data[i] == '\r') || (next == '\r' && data[i] == '\n'))) ++i;
            }
        } else if (c == '"') {
            quoted = !quoted;
        }
    }
    *content_end = i;
    *quoted_end = quoted;
}

/* A Qt-compatible store reads exactly what QSettings reads, which tolerates a section
 * line without ']' and turns unquoted commas into a list (a STRING_LIST here). Other
 * stores (xxfclib's own files, such as hand-written shortcut lists) reject that section
 * line and keep commas as text. */
static xxfc_status_t parse_qt(ini_document *document, xx_settings *store, size_t position)
{
    const char *data = document->data, *prefix = "";
    size_t section = INI_NONE;
    xxfc_status_t status = XXFC_OK;
    while (position < document->size && status == XXFC_OK) {
        size_t first = position, content, equals, index = document->count, end;
        bool terminated, open_quote;
        ini_record *record;
        char c;
        while (first < document->size && (data[first] == ' ' || data[first] == '\t')) ++first;
        c = first < document->size ? data[first] : '\n';
        if (c == '\n' || c == '\r' || c == ';') {
            /* Blank line or comment. A '#' line is a key to QSettings, so it is parsed below. */
            end = line_end(document, first, &terminated);
            if (!add_record(document, position, end, terminated)) return XXFC_ERR_OUT_OF_MEMORY;
            position = end;
            continue;
        }
        scan_qt_line(document, first, &content, &equals, &open_quote);
        end = line_end(document, content, &terminated);
        record = add_record(document, position, end, terminated);
        if (!record) return XXFC_ERR_OUT_OF_MEMORY;
        record->open_quote = open_quote;
        position = end;
        if (c == '[') {
            size_t name_start = first + 1, name_end = content;
            const char *close = (const char *)xx_rt_memchr(data + name_start, ']', content - name_start);
            if (close) name_end = (size_t)(close - data);
            else if (!store->qt_compatible) return XXFC_ERR_INVALID_ARG;
            while (name_start < name_end && blank_char(data[name_start])) ++name_start;
            while (name_end > name_start && blank_char(data[name_end - 1])) --name_end;
            record->kind = INI_SECTION;
            if (ascii_equal(data + name_start, name_end - name_start, "general")) {
                record->name = xx_settings_duplicate("", 0);
                if (!record->name) status = XXFC_ERR_OUT_OF_MEMORY;
            } else if (ascii_equal(data + name_start, name_end - name_start, "%general")) {
                record->name = xx_settings_duplicate(data + name_start + 1, name_end - name_start - 1);
                if (record->name) {
                    char *named = concat(record->name, "/", 1);
                    xx_rt_free(record->name);
                    record->name = named;
                }
                if (!record->name) status = XXFC_ERR_OUT_OF_MEMORY;
            } else {
                char *decoded;
                status = qt_decode_key(data + name_start, name_end - name_start, &decoded);
                if (status == XXFC_OK) {
                    record->name = concat(decoded, "/", 1);
                    xx_rt_free(decoded);
                    if (!record->name) status = XXFC_ERR_OUT_OF_MEMORY;
                } else if (status == XXFC_ERR_INVALID_ARG) status = XXFC_OK; /* Its keys are skipped. */
            }
            section = index;
            prefix = record->name;
        } else if (equals != INI_NONE) {
            size_t key_end = equals;
            xx_settings_buffer decoded = {0}, marks = {0};
            char *relative = NULL;
            while (key_end > first && (data[key_end - 1] == ' ' || data[key_end - 1] == '\t')) --key_end;
            record->kind = INI_ENTRY;
            record->key_end = key_end;
            record->section = section;
            if (!qt_decode_text(data, equals + 1, content, &decoded, store->qt_compatible ? &marks : NULL)) {
                xx_rt_free(decoded.data);
                xx_rt_free(marks.data);
                return XXFC_ERR_OUT_OF_MEMORY;
            }
            record->text = decoded.data;
            record->text_size = decoded.size;
            status = qt_decode_key(data + first, key_end - first, &relative);
            if (status == XXFC_OK && prefix) {
                record->name = concat(prefix, relative, xx_rt_strlen(relative));
                if (!record->name) status = XXFC_ERR_OUT_OF_MEMORY;
            }
            xx_rt_free(relative);
            if (status == XXFC_ERR_INVALID_ARG) status = XXFC_OK;
            if (status == XXFC_OK && record->name)
                status = marks.size ? load_list(store, record, (const size_t *)(const void *)marks.data, marks.size / sizeof(size_t)) : load_entry(store, record, true);
            xx_rt_free(marks.data);
        }
        /* Any other line is one QSettings ignores; it is kept as it is. */
    }
    return status;
}

/* Reads and parses the file at @p path, loading its keys into @p store. */
static xxfc_status_t load_document(const char *path, ini_document *document, xx_settings *store)
{
    size_t position = 0, end;
    xxfc_status_t status = xx_settings_platform_read_file(path, &document->data, &document->size);
    if (status != XXFC_OK || !document->data) return status; /* Missing file. */
    if (xx_rt_memchr(document->data, 0, document->size)) return XXFC_ERR_INVALID_ARG;
    if (document->size >= 3 && (unsigned char)document->data[0] == 0xef && (unsigned char)document->data[1] == 0xbb && (unsigned char)document->data[2] == 0xbf) {
        if (!add_record(document, 0, 3, false)) return XXFC_ERR_OUT_OF_MEMORY;
        position = 3;
    }
    /* The v1 header is the first line, or follows blank and comment lines added above it. */
    for (end = position; end < document->size && !document->v1;) {
        size_t first = end, last;
        while (end < document->size && document->data[end] != '\n') ++end;
        last = end;
        if (end < document->size) ++end;
        while (first < last && space(document->data[first])) ++first;
        while (last > first && space(document->data[last - 1])) --last;
        document->v1 = last - first == sizeof(INI_HEADER) - 1 && !xx_rt_memcmp(document->data + first, INI_HEADER, last - first);
        if (first < last && document->data[first] != ';' && document->data[first] != '#') break;
    }
    return document->v1 ? parse_v1(document, store, position) : parse_qt(document, store, position);
}

xxfc_status_t xx_settings_read_ini(xx_settings *settings)
{
    ini_document document = {0};
    xxfc_status_t status = load_document(settings->location, &document, settings);
    free_document(&document);
    return status;
}

/* ------------------------------------------------------------ writing --- */

xxfc_status_t xx_settings_write_ini(const xx_settings *settings)
{
    xx_settings_buffer buffer = {0};
    bool success = xx_settings_buffer_text(&buffer, INI_HEADER "\n");
    for (xx_settings_entry *entry = settings->entries; entry && success; entry = entry->next) {
        const char *slash;
        char *value;
        if (entry->value.type == XX_SETTINGS_VALUE_NONE) continue;
        slash = xx_rt_strchr(entry->key, '/');
        success = xx_settings_buffer_text(&buffer, "[");
        if (success && slash) {
            if (slash - entry->key == 7 && xx_rt_strncmp(entry->key, "General", 7) == 0) success = xx_settings_buffer_text(&buffer, "%General");
            else success = encode_key(&buffer, entry->key, (size_t)(slash - entry->key), false);
        } else if (success) success = xx_settings_buffer_text(&buffer, "General");
        if (success)
            success = xx_settings_buffer_text(&buffer, "]\n") &&
                      encode_key(&buffer, slash ? slash + 1 : entry->key, xx_rt_strlen(slash ? slash + 1 : entry->key), true) && xx_settings_buffer_text(&buffer, "=");
        value = xx_settings_encode_value(&entry->value);
        if (!value) success = false;
        if (success) success = encode_text(&buffer, value, xx_rt_strlen(value)) && xx_settings_buffer_text(&buffer, "\n");
        xx_rt_free(value);
    }
    xxfc_status_t status = success ? xx_settings_platform_write_file(settings->location, buffer.data ? buffer.data : "", buffer.size) : XXFC_ERR_OUT_OF_MEMORY;
    xx_rt_free(buffer.data);
    return status;
}

static bool values_equal(const xx_settings_value *left, const xx_settings_value *right)
{
    if (left->type != right->type) return false;
    switch (left->type) {
        case XX_SETTINGS_VALUE_NONE: return true;
        case XX_SETTINGS_VALUE_BOOL: return left->data.boolean == right->data.boolean;
        case XX_SETTINGS_VALUE_INT64: return left->data.integer == right->data.integer;
        case XX_SETTINGS_VALUE_UINT64: return left->data.unsigned_integer == right->data.unsigned_integer;
        case XX_SETTINGS_VALUE_DOUBLE: return !xx_rt_memcmp(&left->data.real, &right->data.real, sizeof(left->data.real));
        case XX_SETTINGS_VALUE_STRING_LIST:
            if (left->data.list.count != right->data.list.count) return false;
            for (size_t i = 0; i < left->data.list.count; ++i)
                if (xx_rt_strcmp(left->data.list.items[i], right->data.list.items[i])) return false;
            return true;
        default:
            return left->data.buffer.size == right->data.buffer.size &&
                   (!left->data.buffer.size || !xx_rt_memcmp(left->data.buffer.data, right->data.buffer.data, left->data.buffer.size));
    }
}

static bool ensure_line_start(xx_settings_buffer *buffer, const char *eol)
{
    if (!buffer->size || buffer->data[buffer->size - 1] == '\n' || buffer->data[buffer->size - 1] == '\r') return true;
    return xx_settings_buffer_text(buffer, eol);
}

static bool ends_with_blank_line(const xx_settings_buffer *buffer)
{
    size_t i = buffer->size, breaks = 0;
    while (i && (buffer->data[i - 1] == '\n' || buffer->data[i - 1] == '\r')) {
        if (buffer->data[i - 1] == '\n' || i == buffer->size || buffer->data[i] != '\n') ++breaks;
        --i;
    }
    return !i || breaks >= 2;
}

/* A section header for the group @p key[0..prefix), prefix including its '/'. */
static bool append_section(xx_settings_buffer *buffer, bool qt, const char *key, size_t prefix)
{
    bool ok = xx_settings_buffer_text(buffer, "[");
    if (!prefix) return ok && xx_settings_buffer_text(buffer, "General]");
    --prefix;
    if (qt && ascii_equal(key, prefix, "general")) ok = ok && xx_settings_buffer_text(buffer, "%") && xx_settings_buffer_append(buffer, key, prefix);
    else if (!qt && prefix == 7 && !xx_rt_memcmp(key, "General", 7)) ok = ok && xx_settings_buffer_text(buffer, "%General");
    else ok = ok && (qt ? qt_escape_key(buffer, key, prefix) : encode_key(buffer, key, prefix, false));
    return ok && xx_settings_buffer_text(buffer, "]");
}

static bool append_value(xx_settings_buffer *buffer, bool qt, const xx_settings_value *value, const char *text, size_t size)
{
    char *encoded = NULL;
    bool ok;
    if (value) {
        encoded = xx_settings_encode_value(value);
        if (!encoded) return false;
        text = encoded;
        size = xx_rt_strlen(encoded);
    }
    ok = xx_settings_buffer_text(buffer, "=") && (qt ? qt_escape_value(buffer, text, size) : encode_text(buffer, text, size));
    xx_rt_free(encoded);
    return ok;
}

static bool append_key(xx_settings_buffer *buffer, bool qt, const char *relative)
{
    return qt ? qt_escape_key(buffer, relative, xx_rt_strlen(relative)) : encode_key(buffer, relative, xx_rt_strlen(relative), true);
}

static int compare_inserts(const ini_insert *left, const ini_insert *right)
{
    size_t size = left->prefix < right->prefix ? left->prefix : right->prefix;
    int result;
    if (left->anchor != right->anchor) return left->anchor < right->anchor ? -1 : 1;
    if ((left->prefix != 0) != (right->prefix != 0)) return left->prefix ? 1 : -1;
    result = xx_rt_memcmp(left->entry->key, right->entry->key, size);
    if (result) return result;
    if (left->prefix != right->prefix) return left->prefix < right->prefix ? -1 : 1;
    return xx_rt_strcmp(left->entry->key + left->prefix, right->entry->key + right->prefix);
}

static bool record_is_blank(const ini_document *document, const ini_record *record)
{
    for (size_t i = record->start; i < record->end; ++i)
        if (!blank_char(document->data[i])) return false;
    return true;
}

static const char *default_eol(bool qt)
{
#if defined(_WIN32)
    return qt ? "\r\n" : "\n"; /* QSettings writes CRLF on Windows. */
#else
    (void)qt;
    return "\n";
#endif
}

xxfc_status_t xx_settings_save_ini(const xx_settings *settings, xx_settings *merged)
{
    ini_document document = {0};
    ini_insert *inserts = NULL;
    size_t insert_count = 0, insert_capacity = 0, next = 0, body = 0;
    xx_settings_buffer output = {0};
    const char *eol;
    bool blank, qt, convert, unclosed = false, ok = true;
    xxfc_status_t status = load_document(settings->location, &document, merged);
    if (status != XXFC_OK) goto done;
    if (document.size >= 3 && (unsigned char)document.data[0] == 0xef && (unsigned char)document.data[1] == 0xbb && (unsigned char)document.data[2] == 0xbf) body = 3;
    while (body < document.size && blank_char(document.data[body])) ++body;
    blank = body == document.size;
    if (blank && !settings->qt_compatible) {
        /* A new file from a store that is not Qt-compatible keeps the v1 layout. */
        for (const xx_settings_entry *entry = settings->entries; entry && status == XXFC_OK; entry = entry->next)
            if (entry->dirty) status = xx_settings_set(merged, entry->key, &entry->value);
        if (status == XXFC_OK) status = xx_settings_write_ini(merged);
        goto done;
    }
    qt = !document.v1 || settings->qt_compatible;
    convert = document.v1 && settings->qt_compatible; /* Qt cannot share v1 escapes: rewrite every entry. */
    eol = default_eol(qt);
    if (blank) document.count = 0; /* Only whitespace: start a fresh Qt file. */
    else
        for (size_t i = 0; i < document.size; ++i)
            if (document.data[i] == '\n' || document.data[i] == '\r') {
                eol = document.data[i] == '\n' ? "\n" : (i + 1 < document.size && document.data[i + 1] == '\n' ? "\r\n" : "\r");
                break;
            }

    for (const xx_settings_entry *entry = settings->entries; entry && status == XXFC_OK; entry = entry->next) {
        size_t last = INI_NONE;
        if (!entry->dirty) continue;
        for (size_t i = 0; i < document.count; ++i) {
            ini_record *record = &document.records[i];
            if (record->kind != INI_ENTRY || !record->name || xx_rt_strcmp(record->name, entry->key)) continue;
            if (entry->value.type == XX_SETTINGS_VALUE_NONE) record->action = INI_DELETE;
            last = i;
        }
        if (entry->value.type != XX_SETTINGS_VALUE_NONE && last != INI_NONE) {
            /* An unchanged value keeps its bytes, e.g. a Qt list read back as text. */
            const xx_settings_value *current = xx_settings_get(merged, entry->key);
            if (!current || !values_equal(current, &entry->value)) {
                document.records[last].action = INI_REPLACE;
                document.records[last].change = entry;
            }
        } else if (entry->value.type != XX_SETTINGS_VALUE_NONE) {
            const char *slash = xx_rt_strchr(entry->key, '/');
            if (insert_count == insert_capacity) {
                size_t capacity = insert_capacity ? insert_capacity * 2 : 16;
                ini_insert *grown = (ini_insert *)xx_rt_realloc(inserts, capacity * sizeof(*grown));
                if (!grown) {
                    status = XXFC_ERR_OUT_OF_MEMORY;
                    break;
                }
                inserts = grown;
                insert_capacity = capacity;
            }
            inserts[insert_count].entry = entry;
            inserts[insert_count].prefix = slash ? (size_t)(slash - entry->key) + 1 : 0;
            inserts[insert_count].anchor = INI_NONE;
            ++insert_count;
        }
        status = xx_settings_set(merged, entry->key, &entry->value);
    }
    if (status != XXFC_OK) goto done;

    /* A new key goes after the last entry of the last matching section, like QSettings
     * groups it: "Scan/Die/X" joins [Scan] as Die\X, a root key joins [General]. */
    for (size_t n = 0; n < insert_count; ++n) {
        ini_insert *insert = &inserts[n];
        size_t target = INI_NONE, i;
        bool found = false;
        for (i = document.count; i-- > 0;) {
            const ini_record *record = &document.records[i];
            if (record->kind == INI_SECTION && record->name && xx_rt_strlen(record->name) == insert->prefix &&
                !xx_rt_memcmp(record->name, insert->entry->key, insert->prefix)) {
                target = i;
                found = true;
                break;
            }
        }
        if (!found && !insert->prefix)
            for (i = 0; i < document.count && document.records[i].kind != INI_SECTION; ++i)
                if (document.records[i].kind == INI_ENTRY) found = true; /* Root keys before any section. */
        if (!found) continue;
        if (target != INI_NONE) insert->anchor = target;
        for (i = target == INI_NONE ? 0 : target + 1; i < document.count && document.records[i].kind != INI_SECTION; ++i)
            if (document.records[i].kind == INI_ENTRY) insert->anchor = i;
    }
    for (size_t n = 1; n < insert_count; ++n) {
        ini_insert key = inserts[n];
        size_t k = n;
        while (k && compare_inserts(&inserts[k - 1], &key) > 0) {
            inserts[k] = inserts[k - 1];
            --k;
        }
        inserts[k] = key;
    }

    if (convert)
        for (size_t i = 0; i < document.count; ++i) {
            ini_record *record = &document.records[i];
            if (record->kind == INI_HEADER_LINE) record->action = INI_DELETE;
            else if ((record->kind == INI_SECTION || (record->kind == INI_ENTRY && record->name)) && record->action == INI_KEEP) record->action = INI_REENCODE;
        }

    /* A section whose every entry was removed goes too, with its blank lines. */
    for (size_t s = 0; s < document.count; ++s) {
        size_t end = s + 1;
        bool had = false, live = false;
        if (document.records[s].kind != INI_SECTION) continue;
        for (; end < document.count && document.records[end].kind != INI_SECTION; ++end)
            if (document.records[end].kind == INI_ENTRY) {
                had = true;
                if (document.records[end].action != INI_DELETE) live = true;
            }
        for (size_t n = 0; n < insert_count; ++n)
            if (inserts[n].anchor >= s && inserts[n].anchor < end) live = true;
        if (!had || live) continue;
        document.records[s].action = INI_DELETE;
        for (size_t i = s + 1; i < end; ++i)
            if (document.records[i].kind == INI_OTHER && record_is_blank(&document, &document.records[i])) document.records[i].action = INI_DELETE;
    }

    ok = xx_settings_buffer_append(&output, "", 0);
    for (size_t i = 0; ok && i < document.count; ++i) {
        const ini_record *record = &document.records[i];
        if (record->action == INI_KEEP) ok = xx_settings_buffer_append(&output, document.data + record->start, record->end - record->start);
        else if (record->action == INI_REENCODE && record->kind == INI_SECTION) {
            ok = append_section(&output, qt, record->name, xx_rt_strlen(record->name)) && (!record->terminated || xx_settings_buffer_text(&output, eol));
        } else if (record->action != INI_DELETE) {
            if (convert || record->action == INI_REENCODE) {
                const char *prefix = record->section == INI_NONE ? "" : document.records[record->section].name;
                ok = append_key(&output, qt, record->name + xx_rt_strlen(prefix));
            } else ok = xx_settings_buffer_append(&output, document.data + record->start, record->key_end - record->start);
            ok = ok && append_value(&output, qt, record->change ? &record->change->value : NULL, record->text, record->text_size) &&
                 (!record->terminated || xx_settings_buffer_text(&output, eol));
        }
        /* A kept line whose quote is never closed reads to the end of the file, in QSettings
         * too; closing it there keeps its value and stops it from swallowing what follows. */
        unclosed = record->action == INI_KEEP && record->open_quote;
        for (; ok && next < insert_count && inserts[next].anchor == i; ++next) {
            if (unclosed) ok = xx_settings_buffer_text(&output, "\"");
            unclosed = false;
            ok = ok && ensure_line_start(&output, eol) && append_key(&output, qt, inserts[next].entry->key + inserts[next].prefix) &&
                 append_value(&output, qt, &inserts[next].entry->value, NULL, 0) && xx_settings_buffer_text(&output, eol);
        }
    }
    for (size_t first = next; ok && next < insert_count; ++next) {
        const ini_insert *insert = &inserts[next];
        if (unclosed) ok = xx_settings_buffer_text(&output, "\"");
        unclosed = false;
        if (ok && (next == first || insert->prefix != inserts[next - 1].prefix || xx_rt_memcmp(insert->entry->key, inserts[next - 1].entry->key, insert->prefix))) {
            ok = ensure_line_start(&output, eol);
            if (ok && qt && output.size && !ends_with_blank_line(&output)) ok = xx_settings_buffer_text(&output, eol);
            ok = ok && append_section(&output, qt, insert->entry->key, insert->prefix) && xx_settings_buffer_text(&output, eol);
        }
        ok = ok && append_key(&output, qt, insert->entry->key + insert->prefix) && append_value(&output, qt, &insert->entry->value, NULL, 0) &&
             xx_settings_buffer_text(&output, eol);
    }
    if (!ok) status = XXFC_ERR_OUT_OF_MEMORY;
    else if (output.size != document.size || xx_rt_memcmp(output.data, document.data ? document.data : "", output.size))
        status = xx_settings_platform_write_file(settings->location, output.data, output.size);
done:
    xx_rt_free(output.data);
    xx_rt_free(inserts);
    free_document(&document);
    return status;
}

xxfc_status_t xx_settings_save_native_file(const xx_settings *settings)
{
    xx_settings *merged = xx_settings_create_ini(settings->location);
    xxfc_status_t status;
    if (!merged) return XXFC_ERR_OUT_OF_MEMORY;
    merged->qt_compatible = settings->qt_compatible;
    status = xx_settings_save_ini(settings, merged);
    xx_settings_destroy(merged);
    return status;
}

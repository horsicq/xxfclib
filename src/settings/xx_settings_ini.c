/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_settings_internal.h"
#include "platforms/xx_settings_platform.h"

static bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static char *trim(char *text) {
    size_t length;
    while (space(*text)) ++text;
    length = xx_rt_strlen(text);
    while (length && space(text[length - 1])) text[--length] = 0;
    return text;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *decode_key(const char *text) {
    char *key = xx_settings_duplicate(text, xx_rt_strlen(text));
    size_t out = 0;
    if (!key) return NULL;
    for (size_t i = 0; text[i]; ++i) {
        char c = text[i];
        if (c == '%' && text[i + 1] && text[i + 2] && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            c = (char)((hex(text[i + 1]) << 4) | hex(text[i + 2]));
            i += 2;
            if (!c) { xx_rt_free(key); return NULL; }
        }
        key[out++] = c == '\\' ? '/' : c;
    }
    key[out] = 0;
    return key;
}

static xxfc_status_t decode_text(const char *text, xx_settings_buffer *buffer) {
    size_t size = xx_rt_strlen(text);
    bool quoted = false;
    for (size_t i = 0; i < size; ++i) {
        char c = text[i];
        if (c == '"') { quoted = !quoted; continue; }
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
                case '\\': case '"': break;
                case 'x': {
                    int digit;
                    if (i + 1 >= size || (digit = hex(text[i + 1])) < 0) return XXFC_ERR_INVALID_ARG;
                    c = (char)digit;
                    ++i;
                    if (i + 1 < size && (digit = hex(text[i + 1])) >= 0) { c = (char)(((unsigned char)c << 4) | digit); ++i; }
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

xxfc_status_t xx_settings_read_ini(xx_settings *settings) {
    char *file = NULL, *section = NULL, *line;
    size_t size = 0;
    xxfc_status_t status = xx_settings_platform_read_file(settings->location, &file, &size);
    if (status != XXFC_OK) return status;
    if (!file) return XXFC_OK; /* Missing file. */
    if (xx_rt_memchr(file, 0, size)) { xx_rt_free(file); return XXFC_ERR_INVALID_ARG; }
    line = file;
    if (size >= 3 && (unsigned char)file[0] == 0xef && (unsigned char)file[1] == 0xbb && (unsigned char)file[2] == 0xbf) line += 3;
    while (*line && status == XXFC_OK) {
        char *next = xx_rt_strchr(line, '\n');
        char *text;
        if (next) *next++ = 0;
        text = trim(line);
        if (text[0] && text[0] != ';' && text[0] != '#') {
            if (text[0] == '[') {
                size_t length = xx_rt_strlen(text);
                if (length < 2 || text[length - 1] != ']') { status = XXFC_ERR_INVALID_ARG; break; }
                text[length - 1] = 0;
                xx_rt_free(section);
                if (xx_rt_strcmp(text + 1, "General") == 0) section = xx_settings_duplicate("", 0);
                else section = decode_key(text + 1 + (xx_rt_strcmp(text + 1, "%General") == 0 ? 1 : 0));
                if (!section) status = XXFC_ERR_INVALID_ARG;
            } else {
                char *equal = xx_rt_strchr(text, '=');
                if (!equal) { status = XXFC_ERR_INVALID_ARG; break; }
                *equal++ = 0;
                char *name = decode_key(trim(text));
                xx_settings_buffer key = {0}, decoded = {0};
                xx_settings_value value = {0};
                if (!name) status = XXFC_ERR_INVALID_ARG;
                else if ((section && section[0] && (!xx_settings_buffer_text(&key, section) || !xx_settings_buffer_text(&key, "/"))) ||
                         !xx_settings_buffer_text(&key, name)) status = XXFC_ERR_OUT_OF_MEMORY;
                if (status == XXFC_OK) status = decode_text(trim(equal), &decoded);
                if (status == XXFC_OK) status = xx_settings_decode_value(decoded.data, decoded.size, &value);
                if (status == XXFC_OK) status = xx_settings_set(settings, key.data, &value);
                xx_settings_value_release(&value);
                xx_rt_free(name);
                xx_rt_free(key.data);
                xx_rt_free(decoded.data);
            }
        }
        if (!next) break;
        line = next;
    }
    xx_rt_free(section);
    xx_rt_free(file);
    return status;
}

static bool encode_key(xx_settings_buffer *buffer, const char *key, size_t size, bool nested) {
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

static bool encode_text(xx_settings_buffer *buffer, const char *text) {
    static const char digits[] = "0123456789abcdef";
    if (!xx_settings_buffer_text(buffer, "\"")) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *escape = NULL;
        switch (*p) {
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            case '"': escape = "\\\""; break;
            case '\\': escape = "\\\\"; break;
            default: break;
        }
        if (escape) { if (!xx_settings_buffer_text(buffer, escape)) return false; }
        else if (*p < 32) {
            char escaped[4] = {'\\', 'x', digits[*p >> 4], digits[*p & 15]};
            if (!xx_settings_buffer_append(buffer, escaped, 4)) return false;
        } else if (!xx_settings_buffer_append(buffer, p, 1)) return false;
    }
    return xx_settings_buffer_text(buffer, "\"\n");
}

xxfc_status_t xx_settings_write_ini(const xx_settings *settings) {
    xx_settings_buffer buffer = {0};
    bool success = xx_settings_buffer_text(&buffer, "; xxfclib settings v1\n");
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
        if (success) success = xx_settings_buffer_text(&buffer, "]\n") && encode_key(&buffer, slash ? slash + 1 : entry->key, xx_rt_strlen(slash ? slash + 1 : entry->key), true) && xx_settings_buffer_text(&buffer, "=");
        value = xx_settings_encode_value(&entry->value);
        if (!value) success = false;
        if (success) success = encode_text(&buffer, value);
        xx_rt_free(value);
    }
    xxfc_status_t status = success ? xx_settings_platform_write_file(settings->location, buffer.data ? buffer.data : "", buffer.size) : XXFC_ERR_OUT_OF_MEMORY;
    xx_rt_free(buffer.data);
    return status;
}

xxfc_status_t xx_settings_save_native_file(const xx_settings *settings) {
    xx_settings *merged = xx_settings_create_ini(settings->location);
    xxfc_status_t status;
    if (!merged) return XXFC_ERR_OUT_OF_MEMORY;
    status = xx_settings_load(merged);
    for (xx_settings_entry *entry = settings->entries; entry && status == XXFC_OK; entry = entry->next) {
        if (entry->dirty) status = xx_settings_set(merged, entry->key, &entry->value);
    }
    if (status == XXFC_OK) status = xx_settings_write_ini(merged);
    xx_settings_destroy(merged);
    return status;
}

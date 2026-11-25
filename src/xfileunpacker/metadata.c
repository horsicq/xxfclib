/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "metadata.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xxfclib/strings/xx_string.h>

static char *copy_string(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

void xfu_free_properties(xfu_property *properties, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        free((char *)properties[i].name);
        free((char *)properties[i].value);
    }
    free(properties);
}

static bool add_property(xfu_property **properties, size_t *count, const char *name, const char *value)
{
    xfu_property *grown;
    size_t i, occurrences = 0;
    char duplicate[128];
    char *key, *text;
    for (i = 0; i < *count; ++i)
        if (!strcmp((*properties)[i].name, name) || (!strncmp((*properties)[i].name, name, strlen(name)) && !strncmp((*properties)[i].name + strlen(name), " #", 2)))
            ++occurrences;
    if (occurrences) {
        snprintf(duplicate, sizeof(duplicate), "%s #%zu", name, occurrences + 1);
        name = duplicate;
    }
    key = copy_string(name);
    text = copy_string(value);
    if (!key || !text || *count >= SIZE_MAX / sizeof(*grown)) {
        free(key);
        free(text);
        return false;
    }
    grown = (xfu_property *)realloc(*properties, (*count + 1) * sizeof(*grown));
    if (!grown) {
        free(key);
        free(text);
        return false;
    }
    *properties = grown;
    grown[*count].name = key;
    grown[*count].value = text;
    ++*count;
    return true;
}

static const char *meta_name(uint32_t id)
{
    switch (id) {
        case XX_META_ID_ORIGINAL_NAME: return "Original name";
        case XX_META_ID_UNCOMPRESSED_SIZE: return "Uncompressed size";
        case XX_META_ID_COMPRESSED_SIZE: return "Compressed size";
        case XX_META_ID_CRC32: return "CRC32";
        case XX_META_ID_COMPRESSION_METHOD: return "Method ID";
        case XX_META_ID_ATTRIBUTES: return "Raw attributes";
        case XX_META_ID_TIMESTAMP: return "Timestamp";
        case XX_META_ID_LAST_MOD_TIME: return "DOS time";
        case XX_META_ID_LAST_MOD_DATE: return "DOS date";
        case XX_META_ID_IS_FOLDER: return "Is folder";
        case XX_META_ID_IS_ENCRYPTED: return "Encrypted";
        case XX_META_ID_COMMENT: return "Comment";
        case XX_META_ID_EXTRA_FIELD: return "Extra field (hex)";
        case XX_META_ID_VERSION_NEEDED: return "Version needed";
        case XX_META_ID_VERSION_MADE_BY: return "Version made by";
        case XX_META_ID_FLAGS: return "Flags";
        case XX_META_ID_INTERNAL_ATTRS: return "Internal attributes";
        case XX_META_ID_EXTERNAL_ATTRS: return "External attributes";
        case XX_META_ID_DISK_NUMBER_START: return "Start disk";
        case XX_META_ID_RELATIVE_OFFSET_LOCAL_HEADER: return "Local header offset";
        case XX_META_ID_OPT_UNPACK_PATH: return "Unpack path";
        case XX_META_ID_OPT_PASSWORD: return "Password";
        case XX_META_ID_OPT_OVERWRITE: return "Overwrite";
        case XX_META_ID_COMPRESSION_LEVEL: return "Compression level";
        case XX_META_ID_ENCRYPTION_METHOD: return "Encryption method";
        case XX_META_ID_OPT_MAX_MEMBER_SIZE: return "Maximum member size";
        case XX_META_ID_OPT_MEMORY_LIMIT: return "Memory limit";
        case XX_META_ID_LINK_TARGET: return "Link target";
        default: return NULL;
    }
}

/* Preserve full strings, including embedded NULs, and every byte of blobs. */
static char *text_bytes(const unsigned char *data, size_t size, bool hex, bool escape_slashes)
{
    static const char digits[] = "0123456789ABCDEF";
    char *text;
    size_t i, used = 0;
    if (size > (SIZE_MAX - 1) / 4 || (!data && size)) return NULL;
    text = (char *)malloc(size * 4 + 1);
    if (!text) return NULL;
    for (i = 0; i < size; ++i) {
        unsigned char c = data[i];
        if (hex) {
            if (i) text[used++] = ' ';
            text[used++] = digits[c >> 4];
            text[used++] = digits[c & 15];
        } else if (escape_slashes && c == '\\') {
            text[used++] = '\\';
            text[used++] = '\\';
        } else if (c < 32 || c == 127) {
            text[used++] = '\\';
            text[used++] = 'x';
            text[used++] = digits[c >> 4];
            text[used++] = digits[c & 15];
        } else text[used++] = (char)c;
    }
    text[used] = 0;
    return text;
}

static char *variant_text(const xx_var *var, uint32_t id)
{
    char text[128];
    uint64_t value;
    switch (var->type) {
        case XX_VAR_TYPE_NONE: return copy_string("<none>");
        case XX_VAR_TYPE_STRING:
        case XX_VAR_TYPE_STRING_VIEW:
            /* Password escapes must be unambiguous: a literal "\\x0A" differs
             * from a newline, including when Info groups values by their text. */
            return text_bytes((const unsigned char *)var->val.str.ptr, var->val.str.len, false, id == XX_META_ID_PASSWORD);
        case XX_VAR_TYPE_WSTRING:
        case XX_VAR_TYPE_WSTRING_VIEW: {
            size_t i, used = 0;
            char *joined = copy_string("");
            /* Convert each NUL-delimited segment so even embedded NULs survive. */
            if (!var->val.wstr.ptr && var->val.wstr.len) {
                free(joined);
                return NULL;
            }
            for (i = 0; joined && i < var->val.wstr.len;) {
                size_t start = i, length;
                wchar_t *segment;
                char *utf8, *escaped, *grown;
                while (i < var->val.wstr.len && var->val.wstr.ptr[i]) ++i;
                length = i - start;
                if (length > SIZE_MAX / sizeof(*segment) - 1) {
                    free(joined);
                    return NULL;
                }
                segment = (wchar_t *)malloc((length + 1) * sizeof(*segment));
                if (!segment) {
                    free(joined);
                    return NULL;
                }
                memcpy(segment, var->val.wstr.ptr + start, length * sizeof(*segment));
                segment[length] = 0;
                utf8 = xx_str_unicode_to_utf8(segment);
                free(segment);
                escaped = utf8 ? text_bytes((const unsigned char *)utf8, strlen(utf8), false, id == XX_META_ID_PASSWORD) : NULL;
                xx_str_free(utf8);
                if (!escaped) {
                    free(joined);
                    return NULL;
                }
                length = strlen(escaped);
                if (used > SIZE_MAX - length - 5) {
                    free(escaped);
                    free(joined);
                    return NULL;
                }
                grown = (char *)realloc(joined, used + length + 5);
                if (!grown) {
                    free(escaped);
                    free(joined);
                    return NULL;
                }
                joined = grown;
                memcpy(joined + used, escaped, length);
                used += length;
                free(escaped);
                if (i < var->val.wstr.len) {
                    memcpy(joined + used, "\\x00", 4);
                    used += 4;
                    ++i;
                }
                joined[used] = 0;
            }
            return joined;
        }
        case XX_VAR_TYPE_BYTES:
        case XX_VAR_TYPE_BYTES_VIEW: return text_bytes(var->val.bytes.data, var->val.bytes.size, true, false);
        case XX_VAR_TYPE_BOOL: return copy_string(var->val.b ? "Yes" : "No");
        case XX_VAR_TYPE_FLOAT: snprintf(text, sizeof(text), "%.9g", (double)var->val.f); break;
        case XX_VAR_TYPE_DOUBLE: snprintf(text, sizeof(text), "%.17g", var->val.d); break;
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: snprintf(text, sizeof(text), "%" PRId64, xx_var_get_i64(var)); break;
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64:
            value = xx_var_get_u64(var);
            if (id == XX_META_ID_CRC32) snprintf(text, sizeof(text), "%08" PRIX64, value);
            else if (id == XX_META_ID_FLAGS || id == XX_META_ID_ATTRIBUTES || id == XX_META_ID_INTERNAL_ATTRS || id == XX_META_ID_EXTERNAL_ATTRS)
                snprintf(text, sizeof(text), "0x%" PRIX64, value);
            else snprintf(text, sizeof(text), "%" PRIu64, value);
            break;
        case XX_VAR_TYPE_PTR: snprintf(text, sizeof(text), "%p", var->val.ptr); break;
        default: snprintf(text, sizeof(text), "<variant type %" PRIu32 ">", var->type); break;
    }
    return copy_string(text);
}

static const char *zip_method(uint64_t method)
{
    switch (method) {
        case 0: return "Store";
        case 1: return "Shrink";
        case 2:
        case 3:
        case 4:
        case 5: return "Reduce";
        case 6: return "Implode";
        case 8: return "Deflate";
        case 9: return "Deflate64";
        case 12: return "BZip2";
        case 14: return "LZMA";
        case 20:
        case 93: return "Zstandard";
        case 95: return "XZ";
        case 98: return "PPMd";
        case 99: return "AES";
        default: return "Unknown";
    }
}

bool xfu_record_properties(const xx_archive_record *record, bool zip, xfu_property **properties, size_t *count)
{
    size_t i, n = xx_list_count(&record->list_meta);
    char text[128];
    const int64_t fields[] = {record->header_offset, record->header_size, record->data_offset, record->compressed_size};
    const char *names[] = {"Header offset", "Header size", "Data offset", "Packed data size"};
    *properties = NULL;
    *count = 0;
    if (zip) {
        const xx_var *method = xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_METHOD);
        const xx_var *host = xx_archive_record_find_meta(record, XX_META_ID_VERSION_MADE_BY);
        if (method) {
            uint64_t id = xx_var_get_u64(method);
            const char *level = "";
            if (id == 8 || id == 9) {
                static const char *levels[] = {":Normal", ":Maximum", ":Fast", ":Fastest"};
                level = levels[(xx_archive_record_get_meta_u64(record, XX_META_ID_FLAGS, 0) >> 1) & 3];
            }
            snprintf(text, sizeof(text), "%s%s", zip_method(id), level);
            if (!add_property(properties, count, "Method", text)) goto failed;
        }
        if (host) {
            static const char *hosts[] = {"FAT",     "Amiga", "VMS",  "Unix",  "VM/CMS", "Atari ST", "HPFS", "Macintosh", "Z-System", "CP/M",
                                          "TOPS-20", "NTFS",  "QDOS", "Acorn", "VFAT",   "MVS",      "BeOS", "Tandem",    "OS/400",   "OS X"};
            uint64_t id = xx_var_get_u64(host) >> 8;
            if (id < sizeof(hosts) / sizeof(hosts[0])) snprintf(text, sizeof(text), "%s", hosts[id]);
            else snprintf(text, sizeof(text), "Unknown (%" PRIu64 ")", id);
            if (!add_property(properties, count, "Host OS", text)) goto failed;
        }
    }
    for (i = 0; i < 4; ++i) {
        snprintf(text, sizeof(text), "%" PRId64, fields[i]);
        if (!add_property(properties, count, names[i], text)) goto failed;
    }
    for (i = 0; i < n; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at(&record->list_meta, i);
        const char *name;
        char *value;
        if (!meta) continue;
        name = meta_name(meta->meta_id);
        if (!name) {
            snprintf(text, sizeof(text), "Metadata %" PRIu32, meta->meta_id);
            name = text;
        }
        value = variant_text(&meta->var, meta->meta_id);
        if (!value) goto failed;
        if (!add_property(properties, count, name, value)) {
            free(value);
            goto failed;
        }
        free(value);
    }
    return true;
failed:
    xfu_free_properties(*properties, *count);
    *properties = NULL;
    *count = 0;
    return false;
}

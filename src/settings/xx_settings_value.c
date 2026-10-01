/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_settings_internal.h"

static bool append_hex(xx_settings_buffer *buffer, const char *data, size_t size) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        unsigned char byte = (unsigned char)data[i];
        char pair[2] = {digits[byte >> 4], digits[byte & 15]};
        if (!xx_settings_buffer_append(buffer, pair, 2)) return false;
    }
    return true;
}

static bool append_u32(xx_settings_buffer *buffer, uint32_t value) {
    char bytes[4];
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (char)(value >> (i * 8));
    return xx_settings_buffer_append(buffer, bytes, 4);
}

char *xx_settings_encode_value(const xx_settings_value *value) {
    xx_settings_buffer buffer = {0};
    char number[80];
    const char *prefix = NULL;
    bool success = false;
    switch (value->type) {
        case XX_SETTINGS_VALUE_BOOL:
            success = xx_settings_buffer_text(&buffer, value->data.boolean ? "@XXBool(true)" : "@XXBool(false)");
            break;
        case XX_SETTINGS_VALUE_INT64:
            xx_rt_snprintf(number, sizeof(number), "@XXInt64(%lld)", (long long)value->data.integer);
            success = xx_settings_buffer_text(&buffer, number);
            break;
        case XX_SETTINGS_VALUE_UINT64:
            xx_rt_snprintf(number, sizeof(number), "@XXUInt64(%llu)", (unsigned long long)value->data.unsigned_integer);
            success = xx_settings_buffer_text(&buffer, number);
            break;
        case XX_SETTINGS_VALUE_DOUBLE: {
            uint64_t bits;
            /* Preserve every IEEE-754 bit, including negative zero, NaNs and infinities. */
            xx_rt_memcpy(&bits, &value->data.real, sizeof(bits));
            xx_rt_snprintf(number, sizeof(number), "@XXDoubleBits(%016llx)", (unsigned long long)bits);
            success = xx_settings_buffer_text(&buffer, number);
            break;
        }
        case XX_SETTINGS_VALUE_STRING:
            if (value->data.buffer.size && xx_rt_memchr(value->data.buffer.data, 0, value->data.buffer.size)) {
                prefix = "@XXString(";
            } else {
                success = !value->data.buffer.size || value->data.buffer.data[0] != '@' || xx_settings_buffer_text(&buffer, "@");
                if (success) success = xx_settings_buffer_append(&buffer, value->data.buffer.data, value->data.buffer.size);
            }
            break;
        case XX_SETTINGS_VALUE_BYTES: prefix = "@XXBytes("; break;
        case XX_SETTINGS_VALUE_OPAQUE: prefix = "@XXOpaque("; break;
        case XX_SETTINGS_VALUE_STRING_LIST: {
            xx_settings_buffer binary = {0};
            success = value->data.list.count <= UINT32_MAX && append_u32(&binary, (uint32_t)value->data.list.count);
            for (size_t i = 0; success && i < value->data.list.count; ++i) {
                size_t length = xx_rt_strlen(value->data.list.items[i]);
                success = length <= UINT32_MAX && append_u32(&binary, (uint32_t)length) &&
                          xx_settings_buffer_append(&binary, value->data.list.items[i], length);
            }
            if (success) success = xx_settings_buffer_text(&buffer, "@XXList(") && append_hex(&buffer, binary.data, binary.size) && xx_settings_buffer_text(&buffer, ")");
            xx_rt_free(binary.data);
            break;
        }
        default: break;
    }
    if (prefix) success = xx_settings_buffer_text(&buffer, prefix) && append_hex(&buffer, value->data.buffer.data, value->data.buffer.size) && xx_settings_buffer_text(&buffer, ")");
    if (!success) { xx_rt_free(buffer.data); return NULL; }
    return buffer.data;
}

static int hex_digit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static bool unsigned_number(const char *text, size_t size, uint64_t limit, uint64_t *value, unsigned base) {
    uint64_t result = 0;
    if (!size) return false;
    for (size_t i = 0; i < size; ++i) {
        int digit = hex_digit(text[i]);
        if (digit < 0 || (unsigned)digit >= base || result > (limit - (unsigned)digit) / base) return false;
        result = result * base + (unsigned)digit;
    }
    *value = result;
    return true;
}

static uint32_t read_u32(const char *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)(unsigned char)bytes[i] << (8 * i);
    return value;
}

xxfc_status_t xx_settings_decode_value(const char *text, size_t size, xx_settings_value *value) {
    size_t prefix_size = 0;
    xx_settings_value_type_t type = XX_SETTINGS_VALUE_STRING;
    xx_rt_memset(value, 0, sizeof(*value));
    if (size >= 2 && text[0] == '@' && text[1] == '@') { ++text; --size; }
    else if (size >= 4 && xx_rt_strncmp(text, "@XX", 3) == 0) {
        static const struct { const char *prefix; xx_settings_value_type_t type; } types[] = {
            {"@XXBool(", XX_SETTINGS_VALUE_BOOL}, {"@XXInt64(", XX_SETTINGS_VALUE_INT64},
            {"@XXUInt64(", XX_SETTINGS_VALUE_UINT64}, {"@XXDoubleBits(", XX_SETTINGS_VALUE_DOUBLE},
            {"@XXString(", XX_SETTINGS_VALUE_STRING}, {"@XXBytes(", XX_SETTINGS_VALUE_BYTES},
            {"@XXOpaque(", XX_SETTINGS_VALUE_OPAQUE}, {"@XXList(", XX_SETTINGS_VALUE_STRING_LIST}
        };
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            size_t length = xx_rt_strlen(types[i].prefix);
            if (size >= length && xx_rt_strncmp(text, types[i].prefix, length) == 0) { prefix_size = length; type = types[i].type; break; }
        }
        if (!prefix_size || size <= prefix_size || text[size - 1] != ')') return XXFC_ERR_INVALID_ARG;
    }
    value->type = type;
    if (!prefix_size) {
        value->data.buffer.data = xx_settings_duplicate(text, size);
        value->data.buffer.size = size;
        return value->data.buffer.data ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
    }
    text += prefix_size;
    size -= prefix_size + 1;
    if (type == XX_SETTINGS_VALUE_BOOL) {
        if (size == 4 && xx_rt_strncmp(text, "true", 4) == 0) value->data.boolean = true;
        else if (!(size == 5 && xx_rt_strncmp(text, "false", 5) == 0)) return XXFC_ERR_INVALID_ARG;
        return XXFC_OK;
    }
    if (type == XX_SETTINGS_VALUE_INT64 || type == XX_SETTINGS_VALUE_UINT64 || type == XX_SETTINGS_VALUE_DOUBLE) {
        uint64_t number;
        bool negative = type == XX_SETTINGS_VALUE_INT64 && size && text[0] == '-';
        uint64_t limit = type == XX_SETTINGS_VALUE_INT64 ? (uint64_t)INT64_MAX + (negative ? 1U : 0U) : UINT64_MAX;
        if (negative) { ++text; --size; }
        if ((type == XX_SETTINGS_VALUE_DOUBLE && size != 16) || !unsigned_number(text, size, limit, &number, type == XX_SETTINGS_VALUE_DOUBLE ? 16 : 10)) return XXFC_ERR_INVALID_ARG;
        if (type == XX_SETTINGS_VALUE_DOUBLE) xx_rt_memcpy(&value->data.real, &number, sizeof(number));
        else if (type == XX_SETTINGS_VALUE_UINT64) value->data.unsigned_integer = number;
        else value->data.integer = negative ? (number == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)number) : (int64_t)number;
        return XXFC_OK;
    }
    {
        char *binary;
        size_t length = size / 2;
        if (size % 2) return XXFC_ERR_INVALID_ARG;
        binary = xx_rt_malloc(length + 1);
        if (!binary) return XXFC_ERR_OUT_OF_MEMORY;
        for (size_t i = 0; i < length; ++i) {
            int high = hex_digit(text[i * 2]), low = hex_digit(text[i * 2 + 1]);
            if (high < 0 || low < 0) { xx_rt_free(binary); return XXFC_ERR_INVALID_ARG; }
            binary[i] = (char)((high << 4) | low);
        }
        binary[length] = 0;
        if (type != XX_SETTINGS_VALUE_STRING_LIST) {
            value->data.buffer.data = binary;
            value->data.buffer.size = length;
            return XXFC_OK;
        }
        {
            uint32_t count;
            size_t offset = 4;
            char **items;
            if (length < 4 || (count = read_u32(binary)) > (length - 4) / 4) { xx_rt_free(binary); return XXFC_ERR_INVALID_ARG; }
            items = xx_rt_calloc(count ? count : 1, sizeof(*items));
            if (!items) { xx_rt_free(binary); return XXFC_ERR_OUT_OF_MEMORY; }
            value->data.list.items = (const char *const *)items;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t item_size;
                if (length - offset < 4) goto invalid_list;
                item_size = read_u32(binary + offset);
                offset += 4;
                if (item_size > length - offset || xx_rt_memchr(binary + offset, 0, item_size)) goto invalid_list;
                items[i] = xx_settings_duplicate(binary + offset, item_size);
                if (!items[i]) { xx_rt_free(binary); xx_settings_value_release(value); return XXFC_ERR_OUT_OF_MEMORY; }
                ++value->data.list.count;
                offset += item_size;
            }
            if (offset != length) goto invalid_list;
            xx_rt_free(binary);
            return XXFC_OK;
invalid_list:
            xx_rt_free(binary);
            xx_settings_value_release(value);
            return XXFC_ERR_INVALID_ARG;
        }
    }
}

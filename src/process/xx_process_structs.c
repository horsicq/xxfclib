/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xx_process_structs_internal.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "xx_process_structs_runtime.h"

const char *xx_process_structs_status_string(xx_process_structs_status status)
{
    static const char *const names[] = {
        "success", "invalid argument", "out of memory", "process unavailable", "process read failed", "process identity changed", "unsupported platform or layout"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown error";
}

void xx_process_structs_free(xx_process_structs_snapshot *snapshot)
{
    if (!snapshot) return;
    for (size_t i = 0; i < snapshot->structure_count; ++i) free(snapshot->structures[i].fields);
    free(snapshot->structures);
    free(snapshot);
}

void xx_ps_warn(xx_ps_context *context, const char *format, ...)
{
    char *warning = context->snapshot->warning;
    size_t length = strlen(warning), capacity = sizeof(context->snapshot->warning);
    va_list args;
    if (length >= capacity - 3) return;
    if (length) {
        warning[length++] = ' ';
        warning[length] = 0;
    }
    va_start(args, format);
    (void)vsnprintf(warning + length, capacity - length, format, args);
    va_end(args);
}

xx_process_structure *xx_ps_add(xx_ps_context *context, const char *name, const char *layout, uint64_t address, bool has_address, uint64_t size, uint64_t thread_id)
{
    xx_process_structs_snapshot *snapshot = context->snapshot;
    xx_process_structure *items, *item;
    if (context->status != XX_PROCESS_STRUCTS_OK) return NULL;
    if (snapshot->structure_count >= context->limits.max_structures) {
        if (!snapshot->truncated) xx_ps_warn(context, "Structure limit reached; result is truncated.");
        snapshot->truncated = true;
        return NULL;
    }
    if (snapshot->structure_count == context->structure_capacity) {
        size_t maximum = SIZE_MAX / sizeof(*items);
        size_t capacity = context->structure_capacity;
        if (capacity == maximum) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return NULL;
        }
        capacity = capacity ? (capacity > maximum / 2 ? maximum : capacity * 2) : 32;
        if (capacity > context->limits.max_structures) capacity = context->limits.max_structures;
        items = (xx_process_structure *)realloc(snapshot->structures, capacity * sizeof(*items));
        if (!items) {
            context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
            return NULL;
        }
        snapshot->structures = items;
        context->structure_capacity = capacity;
    }
    item = &snapshot->structures[snapshot->structure_count++];
    memset(item, 0, sizeof(*item));
    (void)snprintf(item->name, sizeof(item->name), "%s", name ? name : "");
    (void)snprintf(item->layout, sizeof(item->layout), "%s", layout ? layout : "");
    item->address = address;
    item->has_address = has_address;
    item->size = size;
    item->thread_id = thread_id;
    return item;
}

static xx_process_struct_field *xx_ps_field(xx_ps_context *context, xx_process_structure *item, const wchar_t *name, const wchar_t *type, uint64_t offset, size_t size,
                                            xx_data_struct_record_property_t properties, bool available)
{
    xx_process_struct_field *fields, *field;
    if (!item || context->status != XX_PROCESS_STRUCTS_OK) return NULL;
    if (!name || !type || offset > INT64_MAX || size > INT64_MAX) {
        context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
        return NULL;
    }
    if (item->field_count >= SIZE_MAX / sizeof(*fields)) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return NULL;
    }
    fields = (xx_process_struct_field *)realloc(item->fields, (item->field_count + 1) * sizeof(*fields));
    if (!fields) {
        context->status = XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
        return NULL;
    }
    item->fields = fields;
    field = &fields[item->field_count++];
    memset(field, 0, sizeof(*field));
    field->desc.name = name;
    field->desc.type = type;
    field->desc.rel_offset = (int64_t)offset;
    field->desc.size = (int64_t)size;
    field->desc.property = properties;
    field->available = available;
    if (item->has_address && offset <= UINT64_MAX - item->address) {
        field->address = item->address + offset;
        field->has_address = true;
    }
    if (!available) {
        (void)snprintf(field->raw, sizeof(field->raw), "<unavailable>");
        (void)snprintf(field->value, sizeof(field->value), "<unavailable>");
        (void)snprintf(field->comment, sizeof(field->comment), "Field bytes could not be read; value is unknown.");
    }
    return field;
}

static bool xx_ps_signed_type(const wchar_t *type, size_t size)
{
    if (!type) return false;
    switch (size) {
        case 1: return !wcscmp(type, L"int8") || !wcscmp(type, L"int8_t");
        case 2: return !wcscmp(type, L"int16") || !wcscmp(type, L"int16_t");
        case 4: return !wcscmp(type, L"int32") || !wcscmp(type, L"int32_t");
        case 8: return !wcscmp(type, L"int64") || !wcscmp(type, L"int64_t");
        default: return false;
    }
}

bool xx_ps_uint(xx_ps_context *context, xx_process_structure *item, const wchar_t *name, const wchar_t *type, uint64_t offset, size_t size,
                xx_data_struct_record_property_t properties, uint64_t value, bool available)
{
    xx_process_struct_field *field;
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        context->status = XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
        return false;
    }
    field = xx_ps_field(context, item, name, type, offset, size, properties, available);
    if (!field) return false;
    if (!available) return true;
    if (size < 8) value &= (UINT64_C(1) << (size * 8)) - 1;
    (void)snprintf(field->raw, sizeof(field->raw), "0x%0*" PRIX64, (int)size * 2, value);
    if (properties & (XX_DATA_STRUCT_RECORD_PROPERTY_POINTER | XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS | XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS |
                      XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE))
        (void)snprintf(field->value, sizeof(field->value), "0x%" PRIX64, value);
    else if (xx_ps_signed_type(type, size) && (value & (UINT64_C(1) << (size * 8 - 1)))) {
        /* Magnitude arithmetic avoids implementation-defined conversion of
         * values above INT64_MAX and handles the target's INT64_MIN too. */
        uint64_t mask = size == 8 ? UINT64_MAX : (UINT64_C(1) << (size * 8)) - 1;
        uint64_t magnitude = ((~value) & mask) + 1;
        (void)snprintf(field->value, sizeof(field->value), "-%" PRIu64, magnitude);
    } else (void)snprintf(field->value, sizeof(field->value), "%" PRIu64, value);
    if (properties & XX_DATA_STRUCT_RECORD_PROPERTY_SIZE) {
        if (field->value[0] == '-') (void)snprintf(field->comment, sizeof(field->comment), "%s bytes", field->value);
        else (void)snprintf(field->comment, sizeof(field->comment), "%" PRIu64 " bytes", value);
    }
    return true;
}

bool xx_ps_bytes(xx_ps_context *context, xx_process_structure *item, const wchar_t *name, const wchar_t *type, uint64_t offset, size_t size,
                 xx_data_struct_record_property_t properties, const void *data, bool available)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *bytes = (const unsigned char *)data;
    xx_process_struct_field *field = xx_ps_field(context, item, name, type, offset, size, properties, available && (bytes || !size));
    size_t shown = size < 64 ? size : 64, out = 0;
    if (!field) return false;
    if (!field->available) return true;
    field->raw[0] = '0';
    field->raw[1] = 'x';
    for (size_t i = 0; i < shown; ++i) {
        field->raw[2 + i * 2] = hex[bytes[i] >> 4];
        field->raw[3 + i * 2] = hex[bytes[i] & 15];
    }
    field->raw[2 + shown * 2] = 0;
    if (properties & XX_DATA_STRUCT_RECORD_PROPERTY_STRING) {
        for (size_t i = 0; i < shown && bytes[i] && out + 4 < sizeof(field->value); ++i) {
            unsigned char b = bytes[i];
            if (b >= 32 && b < 127 && b != '\\') field->value[out++] = (char)b;
            else {
                field->value[out++] = '\\';
                field->value[out++] = 'x';
                field->value[out++] = hex[b >> 4];
                field->value[out++] = hex[b & 15];
            }
        }
        field->value[out] = 0;
    } else (void)snprintf(field->value, sizeof(field->value), "%zu bytes", size);
    if (shown != size) (void)snprintf(field->comment, sizeof(field->comment), "Raw/value preview contains the first %zu of %zu bytes.", shown, size);
    return true;
}

bool xx_ps_decode(xx_ps_context *context, xx_process_structure *item, const xx_data_struct_field_desc *fields, size_t count, const void *data, size_t length,
                  bool big_endian)
{
    const unsigned char *bytes = (const unsigned char *)data;
    if (!item) return context->status == XX_PROCESS_STRUCTS_OK;
    for (size_t i = 0; i < count; ++i) {
        const xx_data_struct_field_desc *field = &fields[i];
        bool available =
            bytes && field->rel_offset >= 0 && field->size >= 0 && (uint64_t)field->rel_offset <= length && (uint64_t)field->size <= length - (size_t)field->rel_offset;
        size_t size = field->size >= 0 && (uint64_t)field->size <= SIZE_MAX ? (size_t)field->size : 0;
        bool array = field->type && wcschr(field->type, L'[');
        if ((size == 1 || size == 2 || size == 4 || size == 8) && !array) {
            uint64_t value = 0;
            if (available)
                for (size_t j = 0; j < size; ++j) value |= (uint64_t)bytes[(size_t)field->rel_offset + j] << ((big_endian ? size - j - 1 : j) * 8);
            if (!xx_ps_uint(context, item, field->name, field->type, (uint64_t)field->rel_offset, size, field->property, value, available)) return false;
        } else if (!xx_ps_bytes(context, item, field->name, field->type, (uint64_t)field->rel_offset, size, field->property,
                                available ? bytes + (size_t)field->rel_offset : NULL, available))
            return false;
    }
    return true;
}

bool xx_ps_read(xx_io_device *device, uint64_t address, void *bytes, size_t size)
{
    return device && size <= 65536 && (!size || (bytes && (uint64_t)(size - 1) <= UINT64_MAX - address)) && xx_io_process_read_exact_at(device, address, bytes, size);
}

xx_process_structs_status xx_process_structs_read(uint64_t pid, uint64_t identity, const xx_process_structs_options *options, xx_process_structs_snapshot **out)
{
    xx_ps_context context = {0};
    xx_process_structs_status status;
    if (!out) return XX_PROCESS_STRUCTS_INVALID_ARGUMENT;
    *out = NULL;
    if (options) context.limits = *options;
    if (!context.limits.max_structures) context.limits.max_structures = SIZE_MAX;
    if (!context.limits.max_threads) context.limits.max_threads = SIZE_MAX;
    if (!context.limits.max_modules) context.limits.max_modules = SIZE_MAX;
    context.snapshot = (xx_process_structs_snapshot *)calloc(1, sizeof(*context.snapshot));
    if (!context.snapshot) return XX_PROCESS_STRUCTS_OUT_OF_MEMORY;
    status = xx_ps_collect(&context, pid, identity);
    if (context.status != XX_PROCESS_STRUCTS_OK) status = context.status;
    if (status != XX_PROCESS_STRUCTS_OK) {
        xx_process_structs_free(context.snapshot);
        return status;
    }
    *out = context.snapshot;
    return XX_PROCESS_STRUCTS_OK;
}

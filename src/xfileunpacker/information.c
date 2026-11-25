/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "information.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct information_buffer {
    char *text;
    size_t used, capacity;
} information_buffer;

typedef struct password_member {
    const char *value;
    size_t member;
} password_member;

static bool append(information_buffer *buffer, const char *format, ...)
{
    va_list args, measured;
    int length;
    size_t needed, capacity;
    char *grown;
    va_start(args, format);
    va_copy(measured, args);
    length = vsnprintf(NULL, 0, format, measured);
    va_end(measured);
    if (length < 0 || buffer->used > SIZE_MAX - (size_t)length - 1) {
        va_end(args);
        return false;
    }
    needed = buffer->used + (size_t)length + 1;
    if (needed > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        grown = (char *)realloc(buffer->text, capacity);
        if (!grown) {
            va_end(args);
            return false;
        }
        buffer->text = grown;
        buffer->capacity = capacity;
    }
    length = vsnprintf(buffer->text + buffer->used, buffer->capacity - buffer->used, format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= buffer->capacity - buffer->used) return false;
    buffer->used += (size_t)length;
    return true;
}

/* Repeated metadata IDs receive " #N" display labels. They still represent
 * passwords, while properties such as "Password source" do not. */
static bool password_property(const char *name)
{
    const char *number;
    if (!name) return false;
    if (!strcmp(name, "Password")) return true;
    if (strncmp(name, "Password #", 10)) return false;
    number = name + 10;
    if (*number < '1' || *number > '9') return false;
    while (*number >= '0' && *number <= '9') ++number;
    return !*number;
}

static int compare_passwords(const void *left, const void *right)
{
    const password_member *a = (const password_member *)left;
    const password_member *b = (const password_member *)right;
    int order = strcmp(a->value, b->value);
    if (order) return order;
    return a->member < b->member ? -1 : a->member > b->member ? 1 : 0;
}

static bool password_groups(information_buffer *buffer, const xfu_entry *entries, size_t count)
{
    password_member *passwords;
    size_t i, j, found = 0, used = 0, groups = 0;
    for (i = 0; i < count; ++i) {
        if (entries[i].property_count && !entries[i].properties) return false;
        for (j = 0; j < entries[i].property_count; ++j)
            if (password_property(entries[i].properties[j].name)) {
                if (!entries[i].properties[j].value || found == SIZE_MAX) return false;
                ++found;
            }
    }
    if (!found) return append(buffer, "\n\nAvailable passwords: none");
    if (found > SIZE_MAX / sizeof(*passwords)) return false;
    passwords = (password_member *)malloc(found * sizeof(*passwords));
    if (!passwords) return false;
    for (i = 0; i < count; ++i)
        for (j = 0; j < entries[i].property_count; ++j)
            if (password_property(entries[i].properties[j].name)) {
                passwords[used].value = entries[i].properties[j].value;
                passwords[used++].member = i;
            }
    qsort(passwords, found, sizeof(*passwords), compare_passwords);
    for (i = 0; i < found; ++i)
        if (!i || strcmp(passwords[i].value, passwords[i - 1].value)) ++groups;
    if (!append(buffer, "\n\nAvailable passwords: %zu", groups)) goto failed;
    for (i = 0; i < found; i = j) {
        size_t members = 1;
        for (j = i + 1; j < found && !strcmp(passwords[i].value, passwords[j].value); ++j)
            if (passwords[j].member != passwords[j - 1].member) ++members;
        if (!append(buffer, "\nPassword: %s\nMembers with this password: %zu", passwords[i].value, members)) goto failed;
    }
    free(passwords);
    return true;
failed:
    free(passwords);
    return false;
}

char *xfu_information_text(const char *archive, const xfu_entry *entries, size_t count, const xfu_entry *selected, bool advanced)
{
    information_buffer buffer = {0};
    char size[40] = "unknown", packed[40] = "unknown";
    size_t i;
    if (!archive || (count && !entries) || (selected && selected->property_count && !selected->properties)) return NULL;
    if (!append(&buffer, "Archive: %s\nMembers: %zu", archive, count)) goto failed;
    if (!selected) {
        if (!append(&buffer, "\n\nSelected: none") || !password_groups(&buffer, entries, count)) goto failed;
        return buffer.text;
    }
    if (selected->unpacked_size >= 0) snprintf(size, sizeof(size), "%" PRId64, selected->unpacked_size);
    if (selected->packed_size >= 0) snprintf(packed, sizeof(packed), "%" PRId64, selected->packed_size);
    if (!append(&buffer, "\n\nSelected: %s\nType: %s\nSize: %s bytes\nPacked size: %s bytes\nModified: %s\nAttributes: %s", selected->name ? selected->name : "",
                selected->is_directory ? "Folder" : "File", size, packed, selected->modified, selected->attributes))
        goto failed;
    for (i = 0; i < selected->property_count; ++i) {
        const xfu_property *property = &selected->properties[i];
        if (!property->name || !property->value) goto failed;
        if ((advanced || password_property(property->name)) && !append(&buffer, "\n%s: %s", property->name, property->value)) goto failed;
    }
    return buffer.text;
failed:
    free(buffer.text);
    return NULL;
}

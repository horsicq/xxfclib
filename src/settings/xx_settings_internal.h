/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SETTINGS_INTERNAL_H
#define XX_SETTINGS_INTERNAL_H

#include "xxfclib/settings/xx_settings.h"
#include "xxfclib/rt/xx_rt.h"

#define XX_SETTINGS_MAX_FILE_SIZE (16U * 1024U * 1024U)

typedef struct xx_settings_entry_s {
    char *key;
    xx_settings_value value;
    bool dirty;
    struct xx_settings_entry_s *next;
} xx_settings_entry;

struct xx_settings_s {
    xx_settings_format_t format;
    char *location;
    char *organization;
    char *application;
    xx_settings_entry *entries;
};

typedef struct xx_settings_buffer_s {
    char *data;
    size_t size;
    size_t capacity;
} xx_settings_buffer;

char *xx_settings_duplicate(const char *text, size_t size);
bool xx_settings_buffer_append(xx_settings_buffer *buffer, const void *data, size_t size);
bool xx_settings_buffer_text(xx_settings_buffer *buffer, const char *text);
void xx_settings_value_release(xx_settings_value *value);
char *xx_settings_encode_value(const xx_settings_value *value);
xxfc_status_t xx_settings_decode_value(const char *text, size_t size, xx_settings_value *value);
xxfc_status_t xx_settings_read_ini(xx_settings *settings);
xxfc_status_t xx_settings_write_ini(const xx_settings *settings);
xxfc_status_t xx_settings_save_native_file(const xx_settings *settings);

#endif

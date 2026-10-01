/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_settings_internal.h"
#include "xxfclib/settings/xx_shortcuts.h"

struct xx_shortcuts_s {
    xx_shortcut *items;
    size_t count;
};

void xx_shortcuts_destroy(xx_shortcuts *shortcuts) {
    if (!shortcuts) return;
    for (size_t i = 0; i < shortcuts->count; ++i) {
        xx_rt_free((void *)shortcuts->items[i].action);
        xx_rt_free((void *)shortcuts->items[i].sequence);
    }
    xx_rt_free(shortcuts->items);
    xx_rt_free(shortcuts);
}

size_t xx_shortcuts_count(const xx_shortcuts *shortcuts) { return shortcuts ? shortcuts->count : 0; }
const xx_shortcut *xx_shortcuts_at(const xx_shortcuts *shortcuts, size_t index) {
    return shortcuts && index < shortcuts->count ? &shortcuts->items[index] : NULL;
}

static xxfc_status_t put(xx_shortcuts *shortcuts, const char *action, const char *sequence) {
    size_t index;
    char *copy;
    if (!action || !action[0] || !sequence || xx_rt_strchr(action, '/')) return XXFC_ERR_INVALID_ARG;
    for (index = 0; index < shortcuts->count; ++index)
        if (!xx_rt_strcmp(shortcuts->items[index].action, action)) break;
    copy = xx_settings_duplicate(sequence, xx_rt_strlen(sequence));
    if (!copy) return XXFC_ERR_OUT_OF_MEMORY;
    if (index == shortcuts->count) {
        char *name = xx_settings_duplicate(action, xx_rt_strlen(action));
        if (!name) { xx_rt_free(copy); return XXFC_ERR_OUT_OF_MEMORY; }
        shortcuts->items[index].action = name;
        shortcuts->items[index].sequence = NULL;
        ++shortcuts->count;
    }
    xx_rt_free((void *)shortcuts->items[index].sequence);
    shortcuts->items[index].sequence = copy;
    return XXFC_OK;
}

xxfc_status_t xx_shortcuts_load(const char *path, const xx_shortcut *defaults,
    size_t count, xx_shortcuts **out) {
    xx_shortcuts *result = NULL;
    xx_settings *settings;
    xxfc_status_t status;
    size_t capacity;
    if (!out) return XXFC_ERR_NULL_PARAM;
    *out = NULL;
    if ((!defaults && count) || (path && !path[0])) return XXFC_ERR_INVALID_ARG;
    settings = xx_settings_create_ini(path ? path : "shortcuts.ini");
    if (!settings) return XXFC_ERR_OUT_OF_MEMORY;
    status = xx_settings_load(settings);
    capacity = xx_settings_count(settings);
    if (capacity > SIZE_MAX - count || capacity + count > SIZE_MAX / sizeof(xx_shortcut))
        status = XXFC_ERR_OUT_OF_MEMORY;
    if (status == XXFC_OK) {
        result = (xx_shortcuts *)xx_rt_calloc(1, sizeof(*result));
        if (!result) status = XXFC_ERR_OUT_OF_MEMORY;
    }
    if (status == XXFC_OK && capacity + count) {
        result->items = (xx_shortcut *)xx_rt_calloc(capacity + count, sizeof(*result->items));
        if (!result->items) status = XXFC_ERR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; status == XXFC_OK && i < count; ++i) {
        for (size_t j = 0; j < i; ++j)
            if (defaults[i].action && defaults[j].action && !xx_rt_strcmp(defaults[i].action, defaults[j].action))
                status = XXFC_ERR_INVALID_ARG;
        if (status == XXFC_OK) status = put(result, defaults[i].action, defaults[i].sequence);
    }
    for (size_t i = 0; status == XXFC_OK && i < xx_settings_count(settings); ++i) {
        const char *key = xx_settings_key_at(settings, i);
        const xx_settings_value *value;
        if (xx_rt_strncmp(key, "shortcuts/", 10)) continue;
        value = xx_settings_get(settings, key);
        if (!value || value->type != XX_SETTINGS_VALUE_STRING ||
            xx_rt_strlen(value->data.buffer.data) != value->data.buffer.size) {
            status = XXFC_ERR_INVALID_ARG; break;
        }
        status = put(result, key + 10, value->data.buffer.data);
    }
    xx_settings_destroy(settings);
    if (status != XXFC_OK) xx_shortcuts_destroy(result);
    else *out = result;
    return status;
}

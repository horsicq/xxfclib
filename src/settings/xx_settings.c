/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_settings_internal.h"
#include "platforms/xx_settings_platform.h"
#include "xxfclib/global/xx_settings_global.h"

char *xx_settings_duplicate(const char *text, size_t size) {
    char *copy;
    if ((!text && size) || size == (size_t)-1) return NULL;
    copy = xx_rt_malloc(size + 1);
    if (copy) {
        if (size) xx_rt_memcpy(copy, text, size);
        copy[size] = 0;
    }
    return copy;
}

bool xx_settings_buffer_append(xx_settings_buffer *buffer, const void *data, size_t size) {
    size_t needed, capacity;
    char *allocation;
    if (size > XX_SETTINGS_MAX_FILE_SIZE || buffer->size > XX_SETTINGS_MAX_FILE_SIZE - size) return false;
    needed = buffer->size + size + 1;
    if (needed > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 128;
        while (capacity < needed) capacity *= 2;
        allocation = xx_rt_realloc(buffer->data, capacity);
        if (!allocation) return false;
        buffer->data = allocation;
        buffer->capacity = capacity;
    }
    if (size) xx_rt_memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = 0;
    return true;
}

bool xx_settings_buffer_text(xx_settings_buffer *buffer, const char *text) {
    return xx_settings_buffer_append(buffer, text, xx_rt_strlen(text));
}

void xx_settings_value_release(xx_settings_value *value) {
    if (value->type == XX_SETTINGS_VALUE_STRING || value->type == XX_SETTINGS_VALUE_BYTES || value->type == XX_SETTINGS_VALUE_OPAQUE) {
        xx_rt_free((void *)value->data.buffer.data);
    } else if (value->type == XX_SETTINGS_VALUE_STRING_LIST) {
        for (size_t i = 0; i < value->data.list.count; ++i) xx_rt_free((void *)value->data.list.items[i]);
        xx_rt_free((void *)value->data.list.items);
    }
    xx_rt_memset(value, 0, sizeof(*value));
}

static xxfc_status_t copy_value(xx_settings_value *destination, const xx_settings_value *source) {
    *destination = *source;
    if (source->type == XX_SETTINGS_VALUE_STRING || source->type == XX_SETTINGS_VALUE_BYTES || source->type == XX_SETTINGS_VALUE_OPAQUE) {
        if ((!source->data.buffer.data && source->data.buffer.size) || source->data.buffer.size > XX_SETTINGS_MAX_FILE_SIZE) return XXFC_ERR_INVALID_ARG;
        destination->data.buffer.data = xx_settings_duplicate(source->data.buffer.data, source->data.buffer.size);
        return destination->data.buffer.data ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
    }
    if (source->type == XX_SETTINGS_VALUE_STRING_LIST) {
        char **items;
        size_t count = source->data.list.count;
        destination->data.list.items = NULL;
        destination->data.list.count = 0;
        if ((!source->data.list.items && count) || count > XX_SETTINGS_MAX_FILE_SIZE / sizeof(char *)) return XXFC_ERR_INVALID_ARG;
        items = xx_rt_calloc(count ? count : 1, sizeof(*items));
        if (!items) return XXFC_ERR_OUT_OF_MEMORY;
        destination->data.list.items = (const char *const *)items;
        size_t total_size = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!source->data.list.items[i]) {
                xx_settings_value_release(destination);
                return XXFC_ERR_INVALID_ARG;
            }
            size_t item_size = xx_rt_strlen(source->data.list.items[i]);
            if (item_size > XX_SETTINGS_MAX_FILE_SIZE - total_size) { xx_settings_value_release(destination); return XXFC_ERR_INVALID_ARG; }
            total_size += item_size;
            items[i] = xx_settings_duplicate(source->data.list.items[i], item_size);
            if (!items[i]) {
                xx_settings_value_release(destination);
                return XXFC_ERR_OUT_OF_MEMORY;
            }
            ++destination->data.list.count;
        }
    } else if (source->type < XX_SETTINGS_VALUE_NONE || source->type > XX_SETTINGS_VALUE_OPAQUE) {
        return XXFC_ERR_INVALID_ARG;
    }
    return XXFC_OK;
}

static xx_settings_entry *find_entry(const xx_settings *settings, const char *key) {
    if (!settings || !key) return NULL;
    for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) {
        if (xx_rt_strcmp(entry->key, key) == 0) return entry;
    }
    return NULL;
}

static bool valid_key(const char *key) {
    size_t length;
    if (!key || !key[0] || key[0] == '/') return false;
    length = xx_rt_strlen(key);
    if (length > 4096 || key[length - 1] == '/') return false;
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char)key[i] < 32 || key[i] == '\\' || (key[i] == '/' && key[i + 1] == '/')) return false;
    }
    return true;
}

static void free_entries(xx_settings_entry *entry) {
    while (entry) {
        xx_settings_entry *next = entry->next;
        xx_settings_value_release(&entry->value);
        xx_rt_free(entry->key);
        xx_rt_free(entry);
        entry = next;
    }
}

static void mark_clean(xx_settings *settings) {
    xx_settings_entry **link = &settings->entries;
    while (*link) {
        xx_settings_entry *entry = *link;
        if (entry->value.type == XX_SETTINGS_VALUE_NONE) {
            *link = entry->next;
            entry->next = NULL;
            free_entries(entry);
        } else {
            entry->dirty = false;
            link = &entry->next;
        }
    }
}

xx_settings *xx_settings_create_memory(void) {
    return xx_rt_calloc(1, sizeof(xx_settings));
}

xx_settings *xx_settings_create_ini(const char *path) {
    xx_settings *settings;
    if (!path || !path[0]) return NULL;
    settings = xx_settings_create_memory();
    if (!settings) return NULL;
    settings->format = XX_SETTINGS_FORMAT_INI;
    settings->location = xx_settings_duplicate(path, xx_rt_strlen(path));
    if (!settings->location) { xx_settings_destroy(settings); return NULL; }
    return settings;
}

static bool valid_component(const char *name) {
    if (!name || !name[0] || xx_rt_strcmp(name, ".") == 0 || xx_rt_strcmp(name, "..") == 0) return false;
    for (const char *p = name; *p; ++p) if ((unsigned char)*p < 32 || *p == '/' || *p == '\\' || *p == ':') return false;
    return xx_rt_strlen(name) <= 255;
}

xx_settings *xx_settings_create_native(const char *organization, const char *application) {
    xx_settings *settings;
    if (!valid_component(organization) || !valid_component(application)) return NULL;
    settings = xx_settings_create_memory();
    if (!settings) return NULL;
    settings->format = XX_SETTINGS_FORMAT_NATIVE;
    settings->organization = xx_settings_duplicate(organization, xx_rt_strlen(organization));
    settings->application = xx_settings_duplicate(application, xx_rt_strlen(application));
    settings->location = xx_settings_platform_native_location(organization, application);
    if (!settings->organization || !settings->application || !settings->location) {
        xx_settings_destroy(settings);
        return NULL;
    }
    return settings;
}

void xx_settings_destroy(xx_settings *settings) {
    if (!settings) return;
    if (xx_get_settings() == settings) xx_set_settings(NULL);
    free_entries(settings->entries);
    xx_rt_free(settings->location);
    xx_rt_free(settings->organization);
    xx_rt_free(settings->application);
    xx_rt_free(settings);
}

xx_settings_format_t xx_settings_get_format(const xx_settings *settings) { return settings ? settings->format : XX_SETTINGS_FORMAT_MEMORY; }
const char *xx_settings_get_location(const xx_settings *settings) { return settings ? settings->location : NULL; }
const char *xx_settings_get_organization(const xx_settings *settings) { return settings ? settings->organization : NULL; }
const char *xx_settings_get_application(const xx_settings *settings) { return settings ? settings->application : NULL; }

const xx_settings_value *xx_settings_get(const xx_settings *settings, const char *key) {
    xx_settings_entry *entry = find_entry(settings, key);
    return entry && entry->value.type != XX_SETTINGS_VALUE_NONE ? &entry->value : NULL;
}

xxfc_status_t xx_settings_set(xx_settings *settings, const char *key, const xx_settings_value *value) {
    xx_settings_entry *entry;
    xx_settings_value copy;
    xxfc_status_t status;
    if (!settings || !key || !value) return XXFC_ERR_NULL_PARAM;
    if (!valid_key(key)) return XXFC_ERR_INVALID_ARG;
    status = copy_value(&copy, value);
    if (status != XXFC_OK) return status;
    entry = find_entry(settings, key);
    if (!entry) {
        entry = xx_rt_calloc(1, sizeof(*entry));
        if (entry) entry->key = xx_settings_duplicate(key, xx_rt_strlen(key));
        if (!entry || !entry->key) {
            xx_rt_free(entry);
            xx_settings_value_release(&copy);
            return XXFC_ERR_OUT_OF_MEMORY;
        }
        entry->next = settings->entries;
        settings->entries = entry;
    }
    xx_settings_value_release(&entry->value);
    entry->value = copy;
    entry->dirty = true;
    return XXFC_OK;
}

xxfc_status_t xx_settings_remove(xx_settings *settings, const char *key) {
    xx_settings_value value = {0};
    return xx_settings_set(settings, key, &value);
}

void xx_settings_clear(xx_settings *settings) {
    if (!settings) return;
    for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) {
        xx_settings_value_release(&entry->value);
        entry->dirty = true;
    }
}

size_t xx_settings_count(const xx_settings *settings) {
    size_t count = 0;
    if (settings) for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) if (entry->value.type != XX_SETTINGS_VALUE_NONE) ++count;
    return count;
}

const char *xx_settings_key_at(const xx_settings *settings, size_t index) {
    if (settings) for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) {
        if (entry->value.type != XX_SETTINGS_VALUE_NONE && index-- == 0) return entry->key;
    }
    return NULL;
}

xxfc_status_t xx_settings_load(xx_settings *settings) {
    xx_settings temporary;
    xxfc_status_t status;
    if (!settings || settings->format == XX_SETTINGS_FORMAT_MEMORY) return XXFC_OK;
    temporary = *settings;
    temporary.entries = NULL;
    status = settings->format == XX_SETTINGS_FORMAT_INI ? xx_settings_read_ini(&temporary) : xx_settings_platform_load_native(&temporary);
    if (status == XXFC_OK) {
        free_entries(settings->entries);
        settings->entries = temporary.entries;
        mark_clean(settings);
    } else {
        free_entries(temporary.entries);
    }
    return status;
}

xxfc_status_t xx_settings_save(xx_settings *settings) {
    xxfc_status_t status;
    bool changed = false;
    if (!settings) return XXFC_OK;
    for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) if (entry->dirty) changed = true;
    if (!changed) return XXFC_OK;
    if (settings->format == XX_SETTINGS_FORMAT_MEMORY) { mark_clean(settings); return XXFC_OK; }
    if (settings->format == XX_SETTINGS_FORMAT_NATIVE) {
        status = xx_settings_platform_save_native(settings);
        if (status == XXFC_OK) mark_clean(settings);
        return status;
    }
    {
        xx_settings temporary = *settings;
        temporary.entries = NULL;
        status = xx_settings_read_ini(&temporary);
        if (status == XXFC_OK) {
            for (xx_settings_entry *entry = settings->entries; entry && status == XXFC_OK; entry = entry->next) {
                if (entry->dirty) status = xx_settings_set(&temporary, entry->key, &entry->value);
            }
        }
        if (status == XXFC_OK) status = xx_settings_write_ini(&temporary);
        if (status == XXFC_OK) {
            free_entries(settings->entries);
            settings->entries = temporary.entries;
            mark_clean(settings);
        } else free_entries(temporary.entries);
    }
    return status;
}

bool xx_settings_is_writable(const xx_settings *settings) {
    if (!settings) return false;
    if (settings->format == XX_SETTINGS_FORMAT_MEMORY) return true;
    return settings->format == XX_SETTINGS_FORMAT_INI ? xx_settings_platform_file_writable(settings->location) : xx_settings_platform_native_writable(settings);
}

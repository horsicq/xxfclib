/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(__APPLE__)
#include "xx_settings_platform.h"
#include <CoreFoundation/CoreFoundation.h>

char *xx_settings_platform_native_location(const char *organization, const char *application)
{
    xx_settings_buffer domain = {0};
    if (!xx_settings_buffer_text(&domain, organization) || !xx_settings_buffer_text(&domain, ".") || !xx_settings_buffer_text(&domain, application)) {
        xx_rt_free(domain.data);
        return NULL;
    }
    return domain.data;
}

static char *string_utf8(CFStringRef string)
{
    CFIndex length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    if (length < 0 || length > XX_SETTINGS_MAX_FILE_SIZE) return NULL;
    char *text = (char *)xx_rt_malloc((size_t)length + 1);
    if (text && !CFStringGetCString(string, text, length + 1, kCFStringEncodingUTF8)) {
        xx_rt_free(text);
        return NULL;
    }
    return text;
}

/* Native preference groups use dots. Rotate literal dots and middle dots so
 * slash-separated C keys round-trip and existing QSettings keys remain readable. */
static char *preference_key(const char *key, bool native)
{
    xx_settings_buffer result = {0};
    bool ok = true;
    for (const unsigned char *p = (const unsigned char *)key; ok && *p; ++p) {
        if (*p == '/') ok = xx_settings_buffer_text(&result, native ? "." : "\xc2\xb7");
        else if (*p == '.') ok = xx_settings_buffer_text(&result, native ? "\xc2\xb7" : "/");
        else if (*p == 0xc2 && p[1] == 0xb7) {
            ok = xx_settings_buffer_text(&result, native ? "/" : ".");
            ++p;
        } else ok = xx_settings_buffer_append(&result, p, 1);
    }
    if (!ok || !xx_settings_buffer_append(&result, "", 0)) {
        xx_rt_free(result.data);
        return NULL;
    }
    return result.data;
}

static xxfc_status_t read_value(xx_settings *settings, const char *key, CFTypeRef object)
{
    xx_settings_value value = {0};
    CFTypeID type = CFGetTypeID(object);
    xxfc_status_t status = XXFC_OK;
    if (type == CFStringGetTypeID()) {
        char *text = string_utf8((CFStringRef)object);
        if (!text) return XXFC_ERR_INVALID_ARG;
        status = xx_settings_decode_value(text, xx_rt_strlen(text), &value);
        xx_rt_free(text);
        if (status == XXFC_OK) status = xx_settings_set(settings, key, &value);
        xx_settings_value_release(&value);
        return status;
    }
    if (type == CFBooleanGetTypeID()) {
        value.type = XX_SETTINGS_VALUE_BOOL;
        value.data.boolean = CFBooleanGetValue((CFBooleanRef)object);
    } else if (type == CFNumberGetTypeID()) {
        bool floating = CFNumberIsFloatType((CFNumberRef)object);
        value.type = floating ? XX_SETTINGS_VALUE_DOUBLE : XX_SETTINGS_VALUE_INT64;
        if (!CFNumberGetValue((CFNumberRef)object, floating ? kCFNumberDoubleType : kCFNumberSInt64Type,
                              floating ? (void *)&value.data.real : (void *)&value.data.integer))
            return XXFC_ERR_INVALID_ARG;
    } else if (type == CFDataGetTypeID()) {
        CFIndex size = CFDataGetLength((CFDataRef)object);
        if (size > XX_SETTINGS_MAX_FILE_SIZE) return XXFC_ERR_INVALID_ARG;
        value.type = XX_SETTINGS_VALUE_BYTES;
        value.data.buffer.data = (const char *)CFDataGetBytePtr((CFDataRef)object);
        value.data.buffer.size = (size_t)size;
    } else if (type == CFArrayGetTypeID()) {
        CFIndex count = CFArrayGetCount((CFArrayRef)object);
        if (count < 0 || count > XX_SETTINGS_MAX_FILE_SIZE / sizeof(char *)) return XXFC_ERR_INVALID_ARG;
        char **items = (char **)xx_rt_calloc(count ? (size_t)count : 1, sizeof(char *));
        if (!items) return XXFC_ERR_OUT_OF_MEMORY;
        bool supported = true;
        for (CFIndex i = 0; i < count; ++i) {
            CFTypeRef item = CFArrayGetValueAtIndex((CFArrayRef)object, i);
            if (CFGetTypeID(item) != CFStringGetTypeID()) {
                supported = false;
                break;
            }
            if (!(items[i] = string_utf8((CFStringRef)item))) {
                status = XXFC_ERR_INVALID_ARG;
                break;
            }
        }
        value.type = XX_SETTINGS_VALUE_STRING_LIST;
        value.data.list.items = (const char *const *)items;
        value.data.list.count = (size_t)count;
        if (status == XXFC_OK && supported) status = xx_settings_set(settings, key, &value);
        xx_settings_value_release(&value);
        return status;
    } else return XXFC_OK;
    return xx_settings_set(settings, key, &value);
}

xxfc_status_t xx_settings_platform_load_native(xx_settings *settings)
{
    CFStringRef domain = CFStringCreateWithCString(NULL, settings->location, kCFStringEncodingUTF8);
    if (!domain) return XXFC_ERR_INVALID_ARG;
    if (!CFPreferencesSynchronize(domain, kCFPreferencesCurrentUser, kCFPreferencesAnyHost)) {
        CFRelease(domain);
        return XXFC_ERR_IO;
    }
    CFDictionaryRef values = CFPreferencesCopyMultiple(NULL, domain, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    xxfc_status_t status = XXFC_OK;
    if (values) {
        CFIndex count = CFDictionaryGetCount(values);
        const void **keys = (const void **)xx_rt_calloc(count ? (size_t)count : 1, sizeof(void *));
        const void **objects = (const void **)xx_rt_calloc(count ? (size_t)count : 1, sizeof(void *));
        if (!keys || !objects) status = XXFC_ERR_OUT_OF_MEMORY;
        else {
            CFDictionaryGetKeysAndValues(values, keys, objects);
            for (CFIndex i = 0; i < count && status == XXFC_OK; ++i) {
                char *raw_key = CFGetTypeID(keys[i]) == CFStringGetTypeID() ? string_utf8((CFStringRef)keys[i]) : NULL;
                char *key = raw_key ? preference_key(raw_key, false) : NULL;
                if (!key) status = XXFC_ERR_INVALID_ARG;
                else status = read_value(settings, key, objects[i]);
                xx_rt_free(raw_key);
                xx_rt_free(key);
            }
        }
        xx_rt_free(keys);
        xx_rt_free(objects);
        CFRelease(values);
    }
    CFRelease(domain);
    return status;
}

xxfc_status_t xx_settings_platform_save_native(const xx_settings *settings)
{
    CFStringRef domain = CFStringCreateWithCString(NULL, settings->location, kCFStringEncodingUTF8);
    CFMutableDictionaryRef values = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFMutableArrayRef removed = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    xxfc_status_t status = domain && values && removed ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
    for (xx_settings_entry *entry = settings->entries; entry && status == XXFC_OK; entry = entry->next) {
        if (!entry->dirty) continue;
        char *native_key = preference_key(entry->key, true);
        CFStringRef key = native_key ? CFStringCreateWithCString(NULL, native_key, kCFStringEncodingUTF8) : NULL;
        xx_rt_free(native_key);
        if (!key) {
            status = XXFC_ERR_INVALID_ARG;
            break;
        }
        if (entry->value.type == XX_SETTINGS_VALUE_NONE) CFArrayAppendValue(removed, key);
        else {
            char *text = xx_settings_encode_value(&entry->value);
            CFStringRef value = text ? CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8) : NULL;
            if (value) {
                CFDictionarySetValue(values, key, value);
                CFRelease(value);
            } else status = XXFC_ERR_INVALID_ARG;
            xx_rt_free(text);
        }
        CFRelease(key);
    }
    if (status == XXFC_OK) {
        CFPreferencesSetMultiple(values, removed, domain, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        if (!CFPreferencesSynchronize(domain, kCFPreferencesCurrentUser, kCFPreferencesAnyHost)) status = XXFC_ERR_IO;
    }
    if (removed) CFRelease(removed);
    if (values) CFRelease(values);
    if (domain) CFRelease(domain);
    return status;
}

bool xx_settings_platform_native_writable(const xx_settings *settings)
{
    const char *home = xx_rt_getenv("HOME");
    xx_settings_buffer path = {0};
    if (!home || !xx_settings_buffer_text(&path, home) || !xx_settings_buffer_text(&path, "/Library/Preferences/") ||
        !xx_settings_buffer_text(&path, settings->location) || !xx_settings_buffer_text(&path, ".plist")) {
        xx_rt_free(path.data);
        return false;
    }
    bool result = xx_settings_platform_file_writable(path.data);
    xx_rt_free(path.data);
    return result;
}
#endif

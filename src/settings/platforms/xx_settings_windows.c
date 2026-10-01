/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if defined(_WIN32)
#include "xx_settings_platform.h"
#include <windows.h>

/* Resolve registry functions from System32 to retain the library's kernel32-only
 * static imports and its CRT-free Windows link. The module stays loaded. */
typedef struct registry_api_s {
    LSTATUS (WINAPI *RegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
    LSTATUS (WINAPI *RegCreateKeyExW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
    LSTATUS (WINAPI *RegCloseKey)(HKEY);
    LSTATUS (WINAPI *RegQueryInfoKeyW)(HKEY, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, PFILETIME);
    LSTATUS (WINAPI *RegEnumValueW)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
    LSTATUS (WINAPI *RegEnumKeyExW)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME);
    LSTATUS (WINAPI *RegDeleteValueW)(HKEY, LPCWSTR);
    LSTATUS (WINAPI *RegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *, DWORD);
} registry_api;

static bool load_registry(registry_api *api) {
    HMODULE module = GetModuleHandleW(L"advapi32.dll");
    if (!module) module = LoadLibraryExW(L"advapi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
#define LOAD_REGISTRY_FUNCTION(name) do { FARPROC function = GetProcAddress(module, #name); if (!function) return false; xx_rt_memcpy(&api->name, &function, sizeof(function)); } while (0)
    LOAD_REGISTRY_FUNCTION(RegOpenKeyExW);
    LOAD_REGISTRY_FUNCTION(RegCreateKeyExW);
    LOAD_REGISTRY_FUNCTION(RegCloseKey);
    LOAD_REGISTRY_FUNCTION(RegQueryInfoKeyW);
    LOAD_REGISTRY_FUNCTION(RegEnumValueW);
    LOAD_REGISTRY_FUNCTION(RegEnumKeyExW);
    LOAD_REGISTRY_FUNCTION(RegDeleteValueW);
    LOAD_REGISTRY_FUNCTION(RegSetValueExW);
#undef LOAD_REGISTRY_FUNCTION
    return true;
}
#define RegOpenKeyExW api.RegOpenKeyExW
#define RegCreateKeyExW api.RegCreateKeyExW
#define RegCloseKey api.RegCloseKey
#define RegQueryInfoKeyW api.RegQueryInfoKeyW
#define RegEnumValueW api.RegEnumValueW
#define RegEnumKeyExW api.RegEnumKeyExW
#define RegDeleteValueW api.RegDeleteValueW
#define RegSetValueExW api.RegSetValueExW

char *xx_settings_platform_native_location(const char *organization, const char *application) {
    xx_settings_buffer buffer = {0};
    if (!xx_settings_buffer_text(&buffer, "Software\\") || !xx_settings_buffer_text(&buffer, organization) ||
        !xx_settings_buffer_text(&buffer, "\\") || !xx_settings_buffer_text(&buffer, application)) { xx_rt_free(buffer.data); return NULL; }
    return buffer.data;
}

static xxfc_status_t registry_value(xx_settings *settings, const char *key, DWORD type, BYTE *data, DWORD size) {
    xx_settings_value value = {0};
    xxfc_status_t status = XXFC_OK;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        if (size % sizeof(WCHAR)) return XXFC_ERR_INVALID_ARG;
        ((WCHAR *)data)[size / sizeof(WCHAR)] = 0;
        char *text = xx_rt_utf16_to_utf8(data);
        if (!text) return XXFC_ERR_INVALID_ARG;
        status = xx_settings_decode_value(text, xx_rt_strlen(text), &value);
        xx_rt_free(text);
        if (status == XXFC_OK) status = xx_settings_set(settings, key, &value);
        xx_settings_value_release(&value);
        return status;
    }
    if (type == REG_DWORD && size == 4) { value.type = XX_SETTINGS_VALUE_UINT64; DWORD number; xx_rt_memcpy(&number, data, 4); value.data.unsigned_integer = number; }
    else if (type == REG_QWORD && size == 8) { value.type = XX_SETTINGS_VALUE_UINT64; xx_rt_memcpy(&value.data.unsigned_integer, data, 8); }
    else if (type == REG_BINARY) { value.type = XX_SETTINGS_VALUE_BYTES; value.data.buffer.data = (const char *)data; value.data.buffer.size = size; }
    else if (type == REG_MULTI_SZ) {
        if (size % sizeof(WCHAR)) return XXFC_ERR_INVALID_ARG;
        WCHAR *strings = (WCHAR *)data;
        size_t units = size / sizeof(WCHAR), count = 0;
        strings[units] = strings[units + 1] = 0;
        for (size_t i = 0; i < units && strings[i]; ++count) { while (i < units && strings[i]) ++i; ++i; }
        char **items = (char **)xx_rt_calloc(count ? count : 1, sizeof(char *));
        if (!items) return XXFC_ERR_OUT_OF_MEMORY;
        size_t offset = 0;
        for (size_t i = 0; i < count; ++i) {
            items[i] = xx_rt_utf16_to_utf8(strings + offset);
            if (!items[i]) { status = XXFC_ERR_INVALID_ARG; break; }
            while (strings[offset]) ++offset;
            ++offset;
        }
        value.type = XX_SETTINGS_VALUE_STRING_LIST; value.data.list.items = (const char *const *)items; value.data.list.count = count;
        if (status == XXFC_OK) status = xx_settings_set(settings, key, &value);
        xx_settings_value_release(&value);
        return status;
    } else return XXFC_OK; /* Preserve unrecognized registry types when saving other keys. */
    return xx_settings_set(settings, key, &value);
}

static xxfc_status_t read_key(xx_settings *settings, HKEY key, const char *prefix, unsigned int depth, registry_api api) {
    DWORD max_name = 0, max_data = 0, max_subkey = 0;
    if (depth > 64 || RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, &max_subkey, NULL, NULL, &max_name, &max_data, NULL, NULL) != ERROR_SUCCESS ||
        max_data > XX_SETTINGS_MAX_FILE_SIZE || max_name > 16383 || max_subkey > 255) return XXFC_ERR_IO;
    size_t name_capacity = (max_name > max_subkey ? max_name : max_subkey) + 2;
    WCHAR *name = (WCHAR *)xx_rt_calloc(name_capacity, sizeof(WCHAR));
    BYTE *data = (BYTE *)xx_rt_calloc((size_t)max_data + 2 * sizeof(WCHAR), 1);
    xxfc_status_t status = name && data ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
    for (DWORD index = 0; status == XXFC_OK; ++index) {
        DWORD name_size = (DWORD)name_capacity, data_size = max_data, type = 0;
        LSTATUS error = RegEnumValueW(key, index, name, &name_size, NULL, &type, data, &data_size);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) { status = XXFC_ERR_IO; break; }
        if (!name_size) continue;
        name[name_size] = 0;
        char *utf8 = xx_rt_utf16_to_utf8(name);
        xx_settings_buffer full = {0};
        if (!utf8 || !xx_settings_buffer_text(&full, prefix) || !xx_settings_buffer_text(&full, utf8)) status = XXFC_ERR_OUT_OF_MEMORY;
        else status = registry_value(settings, full.data, type, data, data_size);
        xx_rt_free(utf8); xx_rt_free(full.data);
    }
    for (DWORD index = 0; status == XXFC_OK; ++index) {
        DWORD name_size = (DWORD)name_capacity;
        LSTATUS error = RegEnumKeyExW(key, index, name, &name_size, NULL, NULL, NULL, NULL);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) { status = XXFC_ERR_IO; break; }
        name[name_size] = 0;
        HKEY child;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &child) != ERROR_SUCCESS) { status = XXFC_ERR_IO; break; }
        char *utf8 = xx_rt_utf16_to_utf8(name);
        xx_settings_buffer full = {0};
        if (!utf8 || !xx_settings_buffer_text(&full, prefix) || !xx_settings_buffer_text(&full, utf8) || !xx_settings_buffer_text(&full, "/")) status = XXFC_ERR_OUT_OF_MEMORY;
        else status = read_key(settings, child, full.data, depth + 1, api);
        RegCloseKey(child); xx_rt_free(utf8); xx_rt_free(full.data);
    }
    xx_rt_free(name); xx_rt_free(data);
    return status;
}

xxfc_status_t xx_settings_platform_load_native(xx_settings *settings) {
    registry_api api;
    if (!load_registry(&api)) return XXFC_ERR_IO;
    WCHAR *path = (WCHAR *)xx_rt_utf8_to_utf16(settings->location);
    HKEY root;
    if (!path) return XXFC_ERR_INVALID_ARG;
    LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &root);
    xx_rt_free(path);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return XXFC_OK;
    if (error != ERROR_SUCCESS) return XXFC_ERR_IO;
    xxfc_status_t status = read_key(settings, root, "", 0, api);
    RegCloseKey(root);
    return status;
}

xxfc_status_t xx_settings_platform_save_native(const xx_settings *settings) {
    registry_api api;
    if (!load_registry(&api)) return XXFC_ERR_IO;
    /* Validate value encoding before touching the registry. I/O errors can still cause a partial save. */
    for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) if (entry->dirty && entry->value.type != XX_SETTINGS_VALUE_NONE) {
        char *text = xx_settings_encode_value(&entry->value);
        if (!text) return XXFC_ERR_OUT_OF_MEMORY;
        xx_rt_free(text);
    }
    for (xx_settings_entry *entry = settings->entries; entry; entry = entry->next) {
        if (!entry->dirty) continue;
        char *key = xx_settings_duplicate(entry->key, xx_rt_strlen(entry->key));
        if (!key) return XXFC_ERR_OUT_OF_MEMORY;
        char *last = NULL;
        for (char *p = key; *p; ++p) if (*p == '/') { *p = '\\'; last = p; }
        const char *name = last ? last + 1 : key;
        if (last) *last = 0;
        xx_settings_buffer full = {0};
        bool ok = xx_settings_buffer_text(&full, settings->location);
        if (last) ok = ok && xx_settings_buffer_text(&full, "\\") && xx_settings_buffer_text(&full, key);
        WCHAR *path = ok ? (WCHAR *)xx_rt_utf8_to_utf16(full.data) : NULL;
        WCHAR *wide_name = (WCHAR *)xx_rt_utf8_to_utf16(name);
        HKEY subkey;
        LSTATUS error = ERROR_NOT_ENOUGH_MEMORY;
        if (path && wide_name) {
            if (entry->value.type == XX_SETTINGS_VALUE_NONE) {
                error = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &subkey);
                if (error == ERROR_SUCCESS) { error = RegDeleteValueW(subkey, wide_name); RegCloseKey(subkey); }
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) error = ERROR_SUCCESS;
            } else {
                char *text = xx_settings_encode_value(&entry->value);
                WCHAR *wide_text = text ? (WCHAR *)xx_rt_utf8_to_utf16(text) : NULL;
                if (wide_text) {
                    error = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &subkey, NULL);
                    if (error == ERROR_SUCCESS) {
                        size_t units = 0; while (wide_text[units]) ++units;
                        error = RegSetValueExW(subkey, wide_name, 0, REG_SZ, (const BYTE *)wide_text, (DWORD)((units + 1) * sizeof(WCHAR)));
                        RegCloseKey(subkey);
                    }
                }
                xx_rt_free(text); xx_rt_free(wide_text);
            }
        }
        xx_rt_free(path); xx_rt_free(wide_name); xx_rt_free(full.data); xx_rt_free(key);
        if (error != ERROR_SUCCESS) return XXFC_ERR_IO;
    }
    return XXFC_OK;
}

bool xx_settings_platform_native_writable(const xx_settings *settings) {
    registry_api api;
    if (!load_registry(&api)) return false;
    WCHAR *path = (WCHAR *)xx_rt_utf8_to_utf16(settings->location);
    if (!path) return false;
    bool result = false;
    for (;;) {
        HKEY key;
        LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &key);
        if (error == ERROR_SUCCESS) { RegCloseKey(key); result = true; break; }
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) break;
        size_t size = 0; while (path[size]) ++size;
        while (size && path[size - 1] != L'\\') --size;
        if (!size) break;
        path[size - 1] = 0;
    }
    xx_rt_free(path);
    return result;
}
#endif

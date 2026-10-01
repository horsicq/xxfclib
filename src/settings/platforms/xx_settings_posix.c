/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if !defined(_WIN32) && !defined(__APPLE__)
#include "xx_settings_platform.h"

char *xx_settings_platform_native_location(const char *organization, const char *application) {
    xx_settings_buffer path = {0};
    const char *config = xx_rt_getenv("XDG_CONFIG_HOME");
    bool ok;
    if (config && config[0] == '/') ok = xx_settings_buffer_text(&path, config);
    else {
        const char *home = xx_rt_getenv("HOME");
        if (!home || home[0] != '/') return NULL;
        ok = xx_settings_buffer_text(&path, home) && xx_settings_buffer_text(&path, "/.config");
    }
    ok = ok && xx_settings_buffer_text(&path, "/") && xx_settings_buffer_text(&path, organization) &&
        xx_settings_buffer_text(&path, "/") && xx_settings_buffer_text(&path, application) && xx_settings_buffer_text(&path, ".conf");
    if (!ok) { xx_rt_free(path.data); return NULL; }
    return path.data;
}

xxfc_status_t xx_settings_platform_load_native(xx_settings *settings) { return xx_settings_read_ini(settings); }
xxfc_status_t xx_settings_platform_save_native(const xx_settings *settings) { return xx_settings_save_native_file(settings); }
bool xx_settings_platform_native_writable(const xx_settings *settings) { return xx_settings_platform_file_writable(settings->location); }
#endif

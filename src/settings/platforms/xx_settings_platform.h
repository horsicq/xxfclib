/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SETTINGS_PLATFORM_H
#define XX_SETTINGS_PLATFORM_H

#include "../xx_settings_internal.h"

char *xx_settings_platform_native_location(const char *organization, const char *application);
xxfc_status_t xx_settings_platform_load_native(xx_settings *settings);
xxfc_status_t xx_settings_platform_save_native(const xx_settings *settings);
bool xx_settings_platform_native_writable(const xx_settings *settings);
xxfc_status_t xx_settings_platform_read_file(const char *path, char **text, size_t *size);
xxfc_status_t xx_settings_platform_write_file(const char *path, const char *text, size_t size);
bool xx_settings_platform_file_writable(const char *path);

#endif

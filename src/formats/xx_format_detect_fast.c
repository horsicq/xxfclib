/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"
#include <string.h>

static const struct {
    const char *extension;
    xx_file_type_t type;
} fast_extensions[] = {
#include "xx_format_fast_extensions.inc"
};

static const size_t fast_extension_count =
    sizeof(fast_extensions) / sizeof(fast_extensions[0]);

xx_file_type_t xx_format_get_file_type_extension(const char *source_path) {
    const char *name, *cursor;
    size_t name_length;
    if (!source_path) return XX_FILE_TYPE_UNKNOWN;
    name = source_path;
    for (cursor = source_path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    name_length = (size_t)(cursor - name);
    for (cursor = name; *cursor; ++cursor) {
        char suffix[32];
        size_t length, i, low = 0, high = fast_extension_count;
        if (*cursor != '.') continue;
        length = name_length - (size_t)(cursor - name) - 1;
        if (!length || length >= sizeof(suffix)) continue;
        for (i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)cursor[i + 1];
            suffix[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        suffix[length] = '\0';
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            int order = strcmp(suffix, fast_extensions[middle].extension);
            if (!order) return fast_extensions[middle].type;
            if (order < 0) high = middle;
            else low = middle + 1;
        }
    }
    return XX_FILE_TYPE_UNKNOWN;
}

xx_file_type_t xx_format_get_file_type_device_fast(xx_io_device *dev,
                                                  const char *source_path) {
    xx_file_type_t type;
    if (!dev) return XX_FILE_TYPE_UNKNOWN;
    if (!source_path) source_path = xx_io_source_path(dev);
    type = xx_format_get_file_type_extension(source_path);
    if (type != XX_FILE_TYPE_UNKNOWN) return type;
    return xx_format_get_file_type_device(dev);
}

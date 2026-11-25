/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/translation/xx_translation.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

xx_translation_status xx_translation_load_po_file(const char *path, xx_translation **out, size_t *error_line)
{
    xx_io_device *file;
    int64_t length;
    void *data;
    xx_translation_status status;
    if (error_line) *error_line = 0;
    if (!out) return XX_TRANSLATION_INVALID_ARGUMENT;
    *out = NULL;
    if (!path) return XX_TRANSLATION_INVALID_ARGUMENT;
    file = xx_io_file_open(path, "rb");
    if (!file) return XX_TRANSLATION_IO_ERROR;
    length = xx_io_size(file);
    if (length < 0 || (uint64_t)length > PTRDIFF_MAX) {
        xx_io_close(file);
        return XX_TRANSLATION_IO_ERROR;
    }
    data = xx_mem_alloc(length ? (size_t)length : 1);
    if (!data) {
        xx_io_close(file);
        return XX_TRANSLATION_OUT_OF_MEMORY;
    }
    if (length && !xx_io_read_at(file, 0, data, (size_t)length)) {
        xx_mem_free(data);
        xx_io_close(file);
        return XX_TRANSLATION_IO_ERROR;
    }
    xx_io_close(file);
    status = xx_translation_load_po(data, (size_t)length, out, error_line);
    xx_mem_free(data);
    return status;
}

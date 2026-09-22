/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_io_platform.h
 * @brief Internal platform abstraction interface for file I/O operations.
 */

#ifndef XX_IO_PLATFORM_H
#define XX_IO_PLATFORM_H

#include "xxfclib/io/xx_io.h"

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open file handle on host platform.
 */
void* xx_io_platform_file_open(const char *path, const char *mode);

/**
 * @brief Read bytes from platform file handle.
 */
ssize_t xx_io_platform_file_read(void *handle, void *buf, size_t n);

/**
 * @brief Write bytes to platform file handle.
 */
ssize_t xx_io_platform_file_write(void *handle, const void *buf, size_t n);

/**
 * @brief Reposition file pointer on platform file handle.
 */
int xx_io_platform_file_seek(void *handle, long off, int whence);
int xx_io_platform_file_seek64(void *handle, int64_t off, int whence);
int64_t xx_io_platform_file_tell(void *handle);

/**
 * @brief Close platform file handle.
 */
int xx_io_platform_file_close(void *handle);

/**
 * @brief Get total file size in bytes from platform file handle.
 */
int64_t xx_io_platform_file_size(void *handle);

bool xx_io_platform_file_exists_a(const char *path);
bool xx_io_platform_file_exists_w(const wchar_t *path);
bool xx_io_platform_file_remove_a(const char *path);
bool xx_io_platform_file_remove_w(const wchar_t *path);
bool xx_io_platform_file_replace_a(const char *source,
                                   const char *destination, bool overwrite);
bool xx_io_platform_file_replace_w(const wchar_t *source,
                                   const wchar_t *destination, bool overwrite);

/**
 * @brief Recursively create parent directories on host platform (wide string).
 */
bool xx_io_platform_create_dirs_w(const wchar_t *path, bool is_dir);

/**
 * @brief Recursively create parent directories on host platform (UTF-8 string).
 */
bool xx_io_platform_create_dirs_a(const char *path, bool is_dir);

/**
 * @brief Apply DOS date, time, and external attributes on host platform (wide string).
 */
bool xx_io_platform_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);

/**
 * @brief Apply DOS date, time, and external attributes on host platform (UTF-8 string).
 */
bool xx_io_platform_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);


/**
 * @brief Fill @p output with @p size cryptographically secure random bytes.
 *
 * Windows goes through BCryptGenRandom, resolved at run time so the CRT-free
 * target keeps its single kernel32 import; POSIX reads /dev/urandom.
 *
 * @return false if no entropy source was available or the read fell short.
 */
bool xx_io_platform_secure_random(uint8_t *output, size_t size);

/**
 * @brief The directory separator this platform writes, as a wide character.
 *
 * L'\' on Windows and L'/' elsewhere. The archive readers build extraction
 * paths in UTF-16 and each carried its own #if to pick the separator and to
 * rewrite the foreign one; they call this instead.
 */
wchar_t xx_io_platform_wseparator(void);

/**
 * @brief True when a path segment names a reserved Windows device.
 *
 * CON, PRN, AUX, NUL, COM1-9, LPT1-9 and the CONIN$/CONOUT$/CLOCK$ family,
 * with or without an extension. Creating such a name opens the device rather
 * than a file, so archive readers must refuse it before extracting.
 *
 * Always false on platforms where the names carry no special meaning, which
 * lets callers ask unconditionally.
 *
 * @param segment One path component, not NUL-terminated.
 * @param length  Its length in wide characters.
 */
bool xx_io_platform_wsegment_is_reserved(const wchar_t *segment, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* XX_IO_PLATFORM_H */

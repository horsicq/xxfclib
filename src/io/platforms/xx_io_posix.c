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
 * @file xx_io_posix.c
 * @brief POSIX / standard C platform file I/O implementation.
 */

#if !defined(_WIN32)

/* Request large-file stdio before any system headers are included. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "xx_io_platform.h"
#include "../xx_io_policy.h"

#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include <stdlib.h>

void* xx_io_platform_file_open(const char *path, const char *mode) {
    if (!xx_io_policy_file_open_allowed(mode)) return NULL;
    if (!path || !mode) {
        return NULL;
    }
    return (void*)fopen(path, mode);
}

void* xx_io_platform_temp_open(void) {
    if (!xx_io_policy_mutation_allowed()) return NULL;
    return (void*)tmpfile();
}

ssize_t xx_io_platform_file_read(void *handle, void *buf, size_t n) {
    if (!handle || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }

    FILE *fp = (FILE*)handle;
    size_t bytes_read = fread(buf, 1, n, fp);
    if (bytes_read == 0) {
        if (ferror(fp)) {
            return -1;
        }
        return 0; /* EOF */
    }
    return (ssize_t)bytes_read;
}

ssize_t xx_io_platform_file_write(void *handle, const void *buf, size_t n) {
    if (!xx_io_policy_mutation_allowed()) return -1;
    if (!handle || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }

    FILE *fp = (FILE*)handle;
    size_t bytes_written = fwrite(buf, 1, n, fp);
    if (bytes_written == 0 && ferror(fp)) {
        return -1;
    }
    return (ssize_t)bytes_written;
}

int xx_io_platform_file_seek(void *handle, long off, int whence) {
    return xx_io_platform_file_seek64(handle, (int64_t)off, whence);
}

int xx_io_platform_file_seek64(void *handle, int64_t off, int whence) {
    if (!handle) {
        return -1;
    }
    if ((int64_t)(off_t)off != off) {
        return -1;
    }
    FILE *fp = (FILE*)handle;
    return fseeko(fp, (off_t)off, whence);
}

int64_t xx_io_platform_file_tell(void *handle) {
    if (!handle) {
        return -1;
    }
    off_t pos = ftello((FILE*)handle);
    if (pos < 0 || (uintmax_t)pos > INT64_MAX) {
        return -1;
    }
    return (int64_t)pos;
}

int xx_io_platform_file_close(void *handle) {
    if (!handle) {
        return -1;
    }
    FILE *fp = (FILE*)handle;
    return fclose(fp);
}

int64_t xx_io_platform_file_size(void *handle) {
    if (!handle) {
        return -1;
    }
    FILE *fp = (FILE*)handle;
    off_t cur = ftello(fp);
    if (cur < 0) {
        return -1;
    }
    if (fseeko(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    int64_t sz = xx_io_platform_file_tell(handle);
    if (fseeko(fp, cur, SEEK_SET) != 0) {
        return -1;
    }
    return sz;
}

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <time.h>
#include <string.h>

static bool xx_posix_wide_path(const wchar_t *path, char output[4096]) {
    size_t length;
    if (!path || !output) return false;
    length = wcstombs(output, path, 4095U);
    if (length == (size_t)-1 || length >= 4095U) return false;
    output[length] = '\0';
    return true;
}

bool xx_io_platform_file_exists_a(const char *path) {
    struct stat info;
    return path && path[0] && lstat(path, &info) == 0;
}

bool xx_io_platform_file_exists_w(const wchar_t *path) {
    char converted[4096];
    return xx_posix_wide_path(path, converted) &&
           xx_io_platform_file_exists_a(converted);
}

bool xx_io_platform_file_remove_a(const char *path) {
    if (!xx_io_policy_mutation_allowed()) return false;
    return path && path[0] && unlink(path) == 0;
}

bool xx_io_platform_file_remove_w(const wchar_t *path) {
    char converted[4096];
    return xx_posix_wide_path(path, converted) &&
           xx_io_platform_file_remove_a(converted);
}

bool xx_io_platform_file_replace_a(const char *source,
                                   const char *destination,
                                   bool overwrite) {
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!source || !source[0] || !destination || !destination[0]) return false;
    if (overwrite) return rename(source, destination) == 0;
    if (link(source, destination) != 0) return false;
    (void)unlink(source);
    return true;
}

bool xx_io_platform_file_replace_w(const wchar_t *source,
                                   const wchar_t *destination,
                                   bool overwrite) {
    char source_path[4096], destination_path[4096];
    return xx_posix_wide_path(source, source_path) &&
           xx_posix_wide_path(destination, destination_path) &&
           xx_io_platform_file_replace_a(source_path, destination_path,
                                         overwrite);
}

bool xx_io_platform_create_dirs_a(const char *path, bool is_dir) {
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!path || !path[0]) {
        return false;
    }
    char temp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        temp[i] = path[i];
    }
    temp[len] = '\0';

    for (size_t i = 0; i < len; ++i) {
        if (temp[i] == '/' || temp[i] == '\\') {
            struct stat info;
            temp[i] = '\0';
            if (i > 0) {
                if (mkdir(temp, 0755) != 0) {
                    if (errno != EEXIST || lstat(temp, &info) != 0 ||
                        !S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))
                        return false;
                }
            }
            temp[i] = '/';
        }
    }
    if (is_dir) {
        struct stat info;
        if (mkdir(temp, 0755) != 0 &&
            (errno != EEXIST || lstat(temp, &info) != 0 ||
             !S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))) return false;
    }
    return true;
}

/* wchar_t is UCS-4 here, so each unit encodes to at most four UTF-8 bytes.
 * The earlier version truncated with a (char) cast into a 1024-byte buffer,
 * which mangled every non-ASCII name and silently cut long paths; the archive
 * readers worked around it by converting before the call. */
static char *xx_io_posix_wide_to_utf8(const wchar_t *path) {
    size_t index;
    size_t size = 1U;
    char *out;
    char *write;

    for (index = 0U; path[index] != L'\0'; ++index) {
        unsigned long code = (unsigned long)path[index];
        size += (code < 0x80UL)     ? 1U
                : (code < 0x800UL)  ? 2U
                : (code < 0x10000UL) ? 3U
                                     : 4U;
    }
    out = (char *)xx_rt_malloc(size);
    if (!out) {
        return NULL;
    }
    write = out;
    for (index = 0U; path[index] != L'\0'; ++index) {
        unsigned long code = (unsigned long)path[index];
        if (code < 0x80UL) {
            *write++ = (char)code;
        } else if (code < 0x800UL) {
            *write++ = (char)(0xC0UL | (code >> 6));
            *write++ = (char)(0x80UL | (code & 0x3FUL));
        } else if (code < 0x10000UL) {
            *write++ = (char)(0xE0UL | (code >> 12));
            *write++ = (char)(0x80UL | ((code >> 6) & 0x3FUL));
            *write++ = (char)(0x80UL | (code & 0x3FUL));
        } else {
            *write++ = (char)(0xF0UL | (code >> 18));
            *write++ = (char)(0x80UL | ((code >> 12) & 0x3FUL));
            *write++ = (char)(0x80UL | ((code >> 6) & 0x3FUL));
            *write++ = (char)(0x80UL | (code & 0x3FUL));
        }
    }
    *write = '\0';
    return out;
}

bool xx_io_platform_create_dirs_w(const wchar_t *path, bool is_dir) {
    char *narrow;
    bool result;

    if (!path || !path[0]) {
        return false;
    }
    narrow = xx_io_posix_wide_to_utf8(path);
    if (!narrow) {
        return false;
    }
    result = xx_io_platform_create_dirs_a(narrow, is_dir);
    xx_rt_free(narrow);
    return result;
}

bool xx_io_platform_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!path || !path[0]) {
        return false;
    }

    if (dos_date != 0 || dos_time != 0) {
        struct tm tm_val;
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_mday = (dos_date & 0x1F);
        tm_val.tm_mon  = ((dos_date >> 5) & 0x0F) - 1;
        tm_val.tm_year = ((dos_date >> 9) & 0x7F) + 80;
        tm_val.tm_sec  = (dos_time & 0x1F) * 2;
        tm_val.tm_min  = (dos_time >> 5) & 0x3F;
        tm_val.tm_hour = (dos_time >> 11) & 0x1F;
        tm_val.tm_isdst = -1;

        time_t t = mktime(&tm_val);
        if (t != (time_t)-1) {
            struct utimbuf ub;
            ub.actime = t;
            ub.modtime = t;
            utime(path, &ub);
        }
    }

    if (attrs != 0) {
        mode_t mode = 0;
        if (attrs & 0xFFFF0000) {
            mode = (mode_t)((attrs >> 16) & 0777);
        } else if (attrs & 0x01) {
            mode = 0444;
        }
        if (mode != 0) {
            chmod(path, mode);
        }
    }

    return true;
}

bool xx_io_platform_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs) {
    if (!path || !path[0]) {
        return false;
    }
    char temp[1024];
    size_t len = 0;
    while (path[len] != L'\0' && len < sizeof(temp) - 1) {
        temp[len] = (char)path[len];
        len++;
    }
    temp[len] = '\0';
    return xx_io_platform_apply_dos_time_and_attrs_a(temp, dos_date, dos_time, attrs);
}


/* --- Path safety and entropy, shared by the archive readers --------------- */

/* No name is reserved here, so the readers can ask unconditionally. */
bool xx_io_platform_wsegment_is_reserved(const wchar_t *segment, size_t length) {
    (void)segment;
    (void)length;
    return false;
}

bool xx_io_platform_secure_random(uint8_t *output, size_t size) {
    if (!output) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
{
        void *random_file = xx_rt_fopen("/dev/urandom", "rb");
        size_t offset = 0U;
        if (!random_file) {
            return false;
        }
        while (offset < size) {
            size_t amount = xx_rt_fread(output + offset, 1U, size - offset,
                                  random_file);
            if (amount == 0U) {
                xx_rt_fclose(random_file);
                return false;
            }
            offset += amount;
        }
        return xx_rt_fclose(random_file) == 0;
    }
}

wchar_t xx_io_platform_wseparator(void) {
    return L'/';
}

/* ---------------------------------------------------------- process memory */

#include <fcntl.h>

/* fd is stored as (void*)(intptr_t)(fd + 1) so a valid fd of 0 is not NULL. */
void* xx_io_platform_process_open(uint64_t pid) {
    char path[64];
    int fd;

    if (pid == 0) {
        pid = (uint64_t)getpid();
    }
    xx_rt_snprintf(path, sizeof(path), "/proc/%llu/mem",
                   (unsigned long long)pid);
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    }
    if (fd < 0) {
        return NULL;
    }
    return (void *)(intptr_t)(fd + 1);
}

ssize_t xx_io_platform_process_read(void *handle, uint64_t addr, void *buf,
                                    size_t n) {
    int fd = (int)((intptr_t)handle - 1);
    ssize_t total = 0;
    if (!handle || !buf) {
        return -1;
    }
    while ((size_t)total < n) {
        ssize_t got = pread(fd, (unsigned char *)buf + total, n - (size_t)total,
                            (off_t)(addr + (uint64_t)total));
        if (got < 0) {
            return total > 0 ? total : -1;
        }
        if (got == 0) {
            break;
        }
        total += got;
    }
    return total;
}

ssize_t xx_io_platform_process_write(void *handle, uint64_t addr,
                                     const void *buf, size_t n) {
    int fd = (int)((intptr_t)handle - 1);
    ssize_t total = 0;
    if (!handle || !buf) {
        return -1;
    }
    while ((size_t)total < n) {
        ssize_t put = pwrite(fd, (const unsigned char *)buf + total,
                            n - (size_t)total, (off_t)(addr + (uint64_t)total));
        if (put < 0) {
            return total > 0 ? total : -1;
        }
        if (put == 0) {
            break;
        }
        total += put;
    }
    return total;
}

int xx_io_platform_process_close(void *handle) {
    if (!handle) {
        return -1;
    }
    return close((int)((intptr_t)handle - 1));
}

void* xx_io_platform_process_adopt(void *native) {
    int fd = (int)(intptr_t)native;
    if (fd < 0) {
        return NULL;
    }
    /* Same fd+1 encoding the open path uses, so a valid fd 0 is not NULL. */
    return (void *)(intptr_t)(fd + 1);
}

#endif /* !_WIN32 */

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
#include "../../strings/platforms/xx_string_platform.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A file opened for writing with truncation inside an extraction scope: an
 * entry already at path that is not a plain file of its own (a link, a file
 * with other hard links, a FIFO, ...) is replaced by a new file, never
 * written through. So is a plain file that this account may not write (one
 * made read-only), as the rename of a staged file replaces it. O_NOFOLLOW,
 * where there is one, also refuses a link put there after the look. */
static FILE *xx_io_posix_open_replacing(const char *path, const char *mode)
{
    struct stat info;
    int update = strchr(mode, '+') != NULL;
    int flags = O_CREAT | O_TRUNC | (update ? O_RDWR : O_WRONLY);
    int descriptor;
    FILE *file;
    if (lstat(path, &info) == 0 && !S_ISDIR(info.st_mode) && (!S_ISREG(info.st_mode) || info.st_nlink > 1)) {
        if (unlink(path) != 0) {
            xx_io_extract_fail(errno);
            return NULL;
        }
        flags |= O_EXCL;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    descriptor = open(path, flags, 0666);
    if (descriptor < 0 && (errno == EACCES || errno == EPERM) && !(flags & O_EXCL) && lstat(path, &info) == 0 && S_ISREG(info.st_mode) && unlink(path) == 0) {
        descriptor = open(path, flags | O_EXCL, 0666);
    }
    if (descriptor < 0) {
        xx_io_extract_fail(errno);
        return NULL;
    }
    file = fdopen(descriptor, update ? "w+b" : "wb");
    if (!file) {
        xx_io_extract_fail(errno);
        close(descriptor);
    } else {
        xx_io_extract_opened();
    }
    return file;
}

void *xx_io_platform_file_open(const char *path, const char *mode)
{
    FILE *file;
    if (!xx_io_policy_file_open_allowed(mode)) return NULL;
    if (!path || !mode) {
        return NULL;
    }
    if (mode[0] == 'w' && !strchr(mode, 'x') && xx_io_extract_root()) return (void *)xx_io_posix_open_replacing(path, mode);
    file = fopen(path, mode);
    if (mode[0] != 'r' || strchr(mode, '+')) {
        if (file) xx_io_extract_opened();
        else xx_io_extract_fail(errno);
    }
    return (void *)file;
}

void *xx_io_platform_file_open_w(const wchar_t *path, const char *mode)
{
    char *utf8;
    void *res;
    if (!path) return NULL;
    utf8 = xx_string_platform_wide_to_mb(path, XX_CODEPAGE_UTF8);
    if (!utf8) return NULL;
    res = xx_io_platform_file_open(utf8, mode);
    xx_mem_free(utf8);
    return res;
}

/* An anonymous temporary file in the folder TMPDIR names, as tmpfile()
 * itself picks on most systems (glibc's always uses /tmp), else tmpfile()'s.
 */
void *xx_io_platform_temp_open(void)
{
    static const char name[] = "/xxfcXXXXXX";
    const char *folder;
    if (!xx_io_policy_mutation_allowed()) return NULL;
    folder = getenv("TMPDIR");
    if (folder && folder[0]) {
        size_t length = strlen(folder);
        char *path = length <= SIZE_MAX - sizeof(name) ? (char *)xx_rt_malloc(length + sizeof(name)) : NULL;
        if (path) {
            int descriptor;
            memcpy(path, folder, length);
            memcpy(path + length, name, sizeof(name));
            descriptor = mkstemp(path);
            if (descriptor >= 0) {
                FILE *file;
                (void)unlink(path);
                file = fdopen(descriptor, "w+b");
                if (file) {
                    xx_rt_free(path);
                    return (void *)file;
                }
                (void)close(descriptor);
            }
            xx_rt_free(path);
        }
    }
    return (void *)tmpfile();
}

ssize_t xx_io_platform_file_read(void *handle, void *buf, size_t n)
{
    if (!handle || !buf) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n > (size_t)PTRDIFF_MAX) {
        n = (size_t)PTRDIFF_MAX;
    }

    FILE *fp = (FILE *)handle;
    size_t bytes_read = fread(buf, 1, n, fp);
    if (bytes_read == 0) {
        if (ferror(fp)) {
            return -1;
        }
        return 0; /* EOF */
    }
    return (ssize_t)bytes_read;
}

ssize_t xx_io_platform_file_write(void *handle, const void *buf, size_t n)
{
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

    FILE *fp = (FILE *)handle;
    size_t bytes_written = fwrite(buf, 1, n, fp);
    if (bytes_written < n && ferror(fp)) xx_io_extract_fail(errno);
    if (bytes_written == 0 && ferror(fp)) {
        return -1;
    }
    return (ssize_t)bytes_written;
}

int xx_io_platform_file_seek(void *handle, long off, int whence)
{
    return xx_io_platform_file_seek64(handle, (int64_t)off, whence);
}

int xx_io_platform_file_seek64(void *handle, int64_t off, int whence)
{
    if (!handle) {
        return -1;
    }
    if ((int64_t)(off_t)off != off) {
        return -1;
    }
    FILE *fp = (FILE *)handle;
    return fseeko(fp, (off_t)off, whence);
}

int64_t xx_io_platform_file_tell(void *handle)
{
    if (!handle) {
        return -1;
    }
    off_t pos = ftello((FILE *)handle);
    if (pos < 0 || (uintmax_t)pos > INT64_MAX) {
        return -1;
    }
    return (int64_t)pos;
}

int xx_io_platform_file_close(void *handle)
{
    if (!handle) {
        return -1;
    }
    FILE *fp = (FILE *)handle;
    int result = fclose(fp);
    if (result != 0) xx_io_extract_fail(errno);
    return result;
}

int64_t xx_io_platform_file_size(void *handle)
{
    if (!handle) {
        return -1;
    }
    FILE *fp = (FILE *)handle;
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

int64_t xx_io_platform_file_size_read_only(void *handle)
{
    struct stat info;
    int descriptor;

    if (!handle) {
        return -1;
    }
    descriptor = fileno((FILE *)handle);
    if (descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_size >= 0 && (uintmax_t)info.st_size <= INT64_MAX) {
        return (int64_t)info.st_size;
    }
    /* Preserve the existing handling of nonregular streams and failures.
     * Writable streams always use that path to flush pending output. */
    return xx_io_platform_file_size(handle);
}

intptr_t xx_io_platform_read_directory_open(const char *path)
{
#if defined(AT_FDCWD) && defined(O_DIRECTORY) && defined(O_NONBLOCK) && defined(O_NOCTTY)
    int descriptor;
    int flags = O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOCTTY;

    if (!path || !xx_io_policy_file_open_allowed("rb")) return -1;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    do {
        descriptor = open(path, flags);
    } while (descriptor < 0 && errno == EINTR);
    return (intptr_t)descriptor;
#else
    (void)path;
    return -1;
#endif
}

intptr_t xx_io_platform_read_regular_at(intptr_t directory, const char *name, int64_t *size)
{
#if defined(AT_FDCWD) && defined(O_DIRECTORY) && defined(O_NONBLOCK) && defined(O_NOCTTY)
    struct stat info;
    int descriptor;
    int flags = O_RDONLY | O_NONBLOCK | O_NOCTTY;

    if (size) *size = 0;
    if (directory < 0 || directory > INT_MAX || !name || !size ||
        !xx_io_policy_file_open_allowed("rb")) return -1;
    /* Preserve the loader's pre-open regular-file check: do not knowingly
     * open a device or other special entry. Flags 0 follow symlinks. */
    if (fstatat((int)directory, name, &info, 0) != 0 || !S_ISREG(info.st_mode)) return -1;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    /* Do not use O_NOFOLLOW: signature symlinks are supported. Nonblocking
     * open and O_NOCTTY prevent waiting on a FIFO or acquiring a terminal
     * before fstat can reject nonregular files, including a raced entry. */
    do {
        descriptor = openat((int)directory, name, flags);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) return -1;
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || (uintmax_t)info.st_size > INT64_MAX) {
        (void)close(descriptor);
        return -1;
    }
    *size = (int64_t)info.st_size;
    return (intptr_t)descriptor;
#else
    (void)directory;
    (void)name;
    if (size) *size = 0;
    return -1;
#endif
}

ssize_t xx_io_platform_read_descriptor(intptr_t handle, void *buf, size_t n)
{
    size_t done = 0;

    if (handle < 0 || handle > INT_MAX || !buf) return -1;
    if (n > (size_t)PTRDIFF_MAX) n = (size_t)PTRDIFF_MAX;
    while (done < n) {
        ssize_t amount = read((int)handle, (char *)buf + done, n - done);
        if (amount > 0) {
            done += (size_t)amount;
        } else if (amount == 0) {
            break;
        } else if (errno != EINTR) {
            /* Like fread, retain a prefix obtained before a later error. */
            return done ? (ssize_t)done : -1;
        }
    }
    return (ssize_t)done;
}

int xx_io_platform_close_descriptor(intptr_t handle)
{
    int result;

    if (handle < 0 || handle > INT_MAX) return -1;
    /* Do not retry close: a descriptor can already be released on EINTR. */
    result = close((int)handle);
    if (result != 0) xx_io_extract_fail(errno);
    return result;
}

bool xx_io_platform_file_can_resize(void *handle, const char *mode)
{
    struct stat info;
    int descriptor, flags;
    if (!handle || !mode || (mode[0] != 'w' && mode[0] != 'a' && !strchr(mode, '+'))) return false;
    descriptor = fileno((FILE *)handle);
    if (descriptor < 0) return false;
    flags = fcntl(descriptor, F_GETFL);
    return flags >= 0 && (flags & O_ACCMODE) != O_RDONLY && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
}

int xx_io_platform_file_resize(void *handle, int64_t new_size)
{
    FILE *file;
    off_t position;
    int descriptor;
    if (!handle || new_size < 0 || (int64_t)(off_t)new_size != new_size) {
        errno = !handle ? EBADF : (new_size < 0 ? EINVAL : EOVERFLOW);
        xx_io_extract_fail(errno);
        return -1;
    }
    if (!xx_io_policy_mutation_allowed()) return -1;
    file = (FILE *)handle;
    descriptor = fileno(file);
    position = ftello(file);
    if (descriptor < 0 || position < 0) {
        xx_io_extract_fail(errno);
        return -1;
    }
    /* Validate/reposition before changing EOF, preserving the logical cursor.
     * A stdio implementation may refill its input buffer during that seek.
     * POSIX fflush after ftruncate discards those now-stale input bytes as
     * well as synchronizing the descriptor with the logical file position. */
    if (fflush(file) != 0 || fseeko(file, position, SEEK_SET) != 0 ||
        ftruncate(descriptor, (off_t)new_size) != 0 || fflush(file) != 0) {
        xx_io_extract_fail(errno);
        return -1;
    }
    return 0;
}

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <time.h>
#include <string.h>

static bool xx_posix_wide_path(const wchar_t *path, char output[4096])
{
    size_t length;
    if (!path || !output) return false;
    length = wcstombs(output, path, 4095U);
    if (length == (size_t)-1 || length >= 4095U) return false;
    output[length] = '\0';
    return true;
}

bool xx_io_platform_file_exists_a(const char *path)
{
    struct stat info;
    return path && path[0] && lstat(path, &info) == 0;
}

bool xx_io_platform_file_exists_w(const wchar_t *path)
{
    char converted[4096];
    return xx_posix_wide_path(path, converted) && xx_io_platform_file_exists_a(converted);
}

bool xx_io_platform_file_remove_a(const char *path)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    return path && path[0] && unlink(path) == 0;
}

bool xx_io_platform_file_remove_w(const wchar_t *path)
{
    char converted[4096];
    return xx_posix_wide_path(path, converted) && xx_io_platform_file_remove_a(converted);
}

bool xx_io_platform_file_replace_a(const char *source, const char *destination, bool overwrite)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!source || !source[0] || !destination || !destination[0]) return false;
    if (overwrite) {
        if (rename(source, destination) == 0) return true;
        xx_io_extract_fail(errno);
        return false;
    }
    if (link(source, destination) != 0) {
        xx_io_extract_fail(errno);
        return false;
    }
    (void)unlink(source);
    return true;
}

bool xx_io_platform_file_replace_w(const wchar_t *source, const wchar_t *destination, bool overwrite)
{
    char source_path[4096], destination_path[4096];
    return xx_posix_wide_path(source, source_path) && xx_posix_wide_path(destination, destination_path) &&
           xx_io_platform_file_replace_a(source_path, destination_path, overwrite);
}

/* Whether the system can take a path of length bytes: shorter than PATH_MAX,
 * the limit of a path given to mkdir() or open(). How long one name may be is
 * the file system's to say: NAME_MAX counts 255 bytes, but HFS+, APFS, exFAT,
 * NTFS and CIFS take 255 characters. So mkdir() decides for a folder, and
 * lstat() for the file's own name. */
static bool xx_io_posix_path_fits(size_t length)
{
#ifdef PATH_MAX
    return length < (size_t)PATH_MAX;
#else
    (void)length;
    return true;
#endif
}

/* How much of path is the extraction root (see xx_io_extract_begin), ending
 * at a separator or at its end; 0 outside a scope or below another folder. */
static size_t xx_io_posix_root_length(const char *path)
{
    const char *root = xx_io_extract_root();
    size_t length;
    if (!root) return 0U;
    length = strlen(root);
    while (length > 1U && (root[length - 1U] == '/' || root[length - 1U] == '\\')) --length;
    if (!length || strncmp(path, root, length) != 0) return 0U;
    return path[length] == '\0' || path[length] == '/' || path[length] == '\\' ? length : 0U;
}

/* One folder of a path: made, or there already. follow: the folder may be a
 * link to one (a part of the extraction root). */
static bool xx_io_posix_make_dir(const char *path, bool follow, bool *made)
{
    struct stat info;
    *made = mkdir(path, 0755) == 0;
    if (*made) return true;
    if (errno != EEXIST) {
        xx_io_extract_fail(errno);
        return false;
    }
    if ((follow ? stat(path, &info) : lstat(path, &info)) != 0) {
        xx_io_extract_fail(errno);
        return false;
    }
    if (S_ISDIR(info.st_mode)) return true;
    xx_io_extract_fail(S_ISLNK(info.st_mode) ? ELOOP : ENOTDIR);
    return false;
}

bool xx_io_platform_create_dirs_a(const char *path, bool is_dir)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!path || !path[0]) {
        return false;
    }
    /* No buffer here limits the path; the limit is the operating system's
     * path limit (PATH_MAX, 4096 bytes on Linux and 1024 on macOS), since
     * every mkdir() below is given the whole prefix. A path beyond it fails
     * here, before a folder is made that would be left behind empty. */
    size_t len = strlen(path);
    size_t root = xx_io_posix_root_length(path);
    char *temp;
    /* made[i]: this call made the folder that ends at i. */
    unsigned char *made;
    bool result = true, created, made_any = false;
    size_t end = len;
    if (!xx_io_posix_path_fits(len)) {
        xx_io_extract_fail(ENAMETOOLONG);
        return false;
    }
    temp = (char *)xx_rt_malloc(len + 1U);
    made = (unsigned char *)xx_rt_malloc(len + 1U);
    if (!temp || !made) {
        xx_rt_free(temp);
        xx_rt_free(made);
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        temp[i] = path[i];
        made[i] = 0U;
    }
    temp[len] = '\0';
    made[len] = 0U;

    for (size_t i = 0; i < len && result; ++i) {
        if (temp[i] == '/' || temp[i] == '\\') {
            temp[i] = '\0';
            if (i > 0) {
                result = xx_io_posix_make_dir(temp, i <= root, &created);
                made[i] = created;
                made_any = made_any || created;
                if (!result) end = i;
            }
            temp[i] = '/';
        }
    }
    if (result && is_dir) {
        result = xx_io_posix_make_dir(temp, len <= root, &created);
        made[len] = created;
    } else if (result && made_any) {
        /* The file's own name, too long for the file system the new folders
         * are on, fails here, so that they do not stay behind empty. */
        struct stat info;
        if (lstat(temp, &info) != 0 && errno == ENAMETOOLONG) {
            xx_io_extract_fail(ENAMETOOLONG);
            result = false;
        }
    }
    if (!result) {
        /* The folders made for a path that then failed, deepest first. */
        for (size_t i = end + 1U; i-- > 0U;) {
            if (!made[i]) continue;
            temp[i] = '\0';
            (void)rmdir(temp);
            temp[i] = '/';
        }
    }
    xx_rt_free(made);
    xx_rt_free(temp);
    return result;
}

/* wchar_t is UCS-4 here, so each unit encodes to at most four UTF-8 bytes.
 * The earlier version truncated with a (char) cast into a 1024-byte buffer,
 * which mangled every non-ASCII name and silently cut long paths; the archive
 * readers worked around it by converting before the call. */
static char *xx_io_posix_wide_to_utf8(const wchar_t *path)
{
    size_t index;
    size_t size = 1U;
    char *out;
    char *write;

    for (index = 0U; path[index] != L'\0'; ++index) {
        unsigned long code = (unsigned long)path[index];
        size += (code < 0x80UL) ? 1U : (code < 0x800UL) ? 2U : (code < 0x10000UL) ? 3U : 4U;
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

bool xx_io_platform_create_dirs_w(const wchar_t *path, bool is_dir)
{
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

bool xx_io_platform_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
    if (!xx_io_policy_mutation_allowed()) return false;
    if (!path || !path[0]) {
        return false;
    }

    if (dos_date != 0 || dos_time != 0) {
        struct tm tm_val;
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_mday = (dos_date & 0x1F);
        tm_val.tm_mon = ((dos_date >> 5) & 0x0F) - 1;
        tm_val.tm_year = ((dos_date >> 9) & 0x7F) + 80;
        tm_val.tm_sec = (dos_time & 0x1F) * 2;
        tm_val.tm_min = (dos_time >> 5) & 0x3F;
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

bool xx_io_platform_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs)
{
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
bool xx_io_platform_wsegment_is_reserved(const wchar_t *segment, size_t length)
{
    (void)segment;
    (void)length;
    return false;
}

bool xx_io_platform_secure_random(uint8_t *output, size_t size)
{
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
            size_t amount = xx_rt_fread(output + offset, 1U, size - offset, random_file);
            if (amount == 0U) {
                xx_rt_fclose(random_file);
                return false;
            }
            offset += amount;
        }
        return xx_rt_fclose(random_file) == 0;
    }
}

wchar_t xx_io_platform_wseparator(void)
{
    return L'/';
}

#endif /* !_WIN32 */

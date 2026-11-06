/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "native_writer_backend.h"
/* Restricted builds link native_extra_formats.c for ar (the AR component)
 * and for ISO 9660 (a family build that sets XFU_NATIVE_EXTRA_WRITERS). */
#if !XFU_FORMAT_SUPPORT_FULL && (XFU_FORMAT_SUPPORT_AR_ONLY || defined(XFU_NATIVE_EXTRA_WRITERS))
#define NATIVE_EXTRA_WRITERS 1
#else
#define NATIVE_EXTRA_WRITERS 0
#endif
/* The LZMA-Alone filter is the LZMA format's writer. */
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_TAR_FILTERS || defined(XFU_SUPPORT_FORMAT_LZMA)
#define NATIVE_LZMA_FILTER 1
#else
#define NATIVE_LZMA_FILTER 0
#endif
#if XFU_FORMAT_SUPPORT_FULL || NATIVE_EXTRA_WRITERS
#include "native_extra_formats.h"
#endif
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_EXTRA_FILTERS
#include "native_filter_formats.h"
#endif
#if NATIVE_LZMA_FILTER
#include <xxfclib/formats/archive/lzma/xx_lzma.h>
#endif
#ifdef XFU_SUPPORT_FORMAT_TAR
#include "native_tar_writer.h"
#endif

#include "xxfc_readers.h"

#ifdef XFU_SUPPORT_FORMAT_ZSTD
#include <xxfclib/algo/zstd/xx_zstd.h>
#endif
#if XFU_FORMAT_SUPPORT_FULL
#include <xxfclib/algo/bzip2/xx_bzip2.h>
#include <xxfclib/formats/archive/lzma/xx_lzma.h>
#endif

#include <xxfclib/formats/archive/zip/xx_zip.h>
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef IsReparseTagNameSurrogate
#define IsReparseTagNameSurrogate(tag) (((tag) & 0x20000000UL) != 0)
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
/* POSIX.1-2008 flags; where one is missing, the identity checks still hold. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#endif

#define XFU_NATIVE_CHUNK 65536U
#define XFU_NATIVE_RECORD_LIMIT 1000000U
#define XFU_NATIVE_FOLDER_DEPTH 256U

typedef enum native_format {
    NATIVE_UNSUPPORTED = 0, NATIVE_RAW, NATIVE_TAR, NATIVE_CPIO,
    NATIVE_ZIP, NATIVE_7ZIP, NATIVE_EXTRA, NATIVE_TAR_VARIANT
} native_format;

typedef enum native_filter {
    FILTER_UNSUPPORTED = 0, FILTER_NONE, FILTER_GZIP, FILTER_BZIP2,
    FILTER_XZ, FILTER_ZSTD, FILTER_LZMA, FILTER_EXTRA
} native_filter;

typedef struct native_source {
    const char *path;
    char *owned_path; /* The path of a folder's member, built by the walk. */
    char *name;
    char *wire_name;
    int64_t size;
    uint64_t mtime;
    int64_t stamp; /* mtime as the file system gives it: negative before 1970. */
    uint64_t device, inode; /* POSIX file identity; zero elsewhere. */
    uint32_t mode;
    uint32_t uid, gid; /* POSIX owner; zero elsewhere. */
    bool directory;
    bool walked; /* Found in a folder, not named as an input. */
    bool skip; /* A repeated input (the same file or folder again). */
} native_source;

/* The members to write: the inputs, each folder followed by its contents. */
typedef struct native_list {
    native_source *items;
    size_t count, capacity;
} native_list;

/* CPIO does not label the member-name encoding. A requested legacy charset
 * must therefore be written as bytes in the header, without changing the
 * UTF-8 path used to open the input file. */
static char *native_cp437_name(const char *utf8)
{
    static const uint16_t high[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
    };
    wchar_t *wide = xx_str_utf8_to_unicode(utf8);
    char *encoded;
    size_t i, j = 0U;
    if (!wide) return NULL;
    encoded = (char *)malloc(strlen(utf8) + 1U);
    if (!encoded) { xx_str_wfree(wide); return NULL; }
    for (i = 0U; wide[i]; ++i) {
        unsigned codepoint = (unsigned)wide[i];
        size_t entry;
        if (codepoint < 0x80U) {
            encoded[j++] = (char)codepoint;
            continue;
        }
        for (entry = 0U; entry < 128U && high[entry] != codepoint; ++entry) {}
        if (entry == 128U) { free(encoded); xx_str_wfree(wide); return NULL; }
        encoded[j++] = (char)(entry + 0x80U);
    }
    encoded[j] = '\0';
    xx_str_wfree(wide);
    return encoded;
}

/* A line longer than the local buffer (a long path) is formatted again in a
 * heap buffer instead of being cut. */
static void native_log(const xfu_request *request, bool error, const char *format, ...)
{
    char local[2048], *line = local;
    va_list arguments, copy;
    int count;
    if (!request || !request->callbacks.log) return;
    va_start(arguments, format);
    va_copy(copy, arguments);
    count = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (count >= (int)sizeof(local)) {
        line = (char *)malloc((size_t)count + 1U);
        if (line) (void)vsnprintf(line, (size_t)count + 1U, format, copy);
        else line = local;
    }
    va_end(copy);
    request->callbacks.log(request->callbacks.user, error, line);
    if (line != local) free(line);
}

static bool native_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled &&
        request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

/* A writer's view of the request: its error lines are noted, so a failure it
 * already explained gets no generic line on top. The other callbacks pass
 * through; the writers use no others. Its warnings (xfu_native_warn) wait in
 * warnings, one after another with their '\0', until the archive is
 * published: a run that writes nothing does not say what it stored. */
typedef struct native_watch {
    const xfu_callbacks *callbacks; /* The caller's. */
    bool reported;
    char *warnings;
    size_t warnings_size;
} native_watch;

static void native_watch_log(void *user, bool error, const char *line)
{
    native_watch *watch = (native_watch *)user;
    if (error) watch->reported = true;
    watch->callbacks->log(watch->callbacks->user, error, line);
}

static void native_watch_progress(void *user, uint64_t completed, uint64_t total,
                                  const char *name)
{
    const native_watch *watch = (const native_watch *)user;
    watch->callbacks->progress(watch->callbacks->user, completed, total, name);
}

static bool native_watch_cancelled(void *user)
{
    const native_watch *watch = (const native_watch *)user;
    return watch->callbacks->cancelled(watch->callbacks->user);
}

static xfu_request native_watched(const xfu_request *request, native_watch *watch)
{
    xfu_request watched = *request;
    watch->callbacks = &request->callbacks;
    watch->reported = false;
    watch->warnings = NULL;
    watch->warnings_size = 0U;
    memset(&watched.callbacks, 0, sizeof(watched.callbacks));
    watched.callbacks.user = watch;
    if (request->callbacks.log) watched.callbacks.log = native_watch_log;
    if (request->callbacks.progress) watched.callbacks.progress = native_watch_progress;
    if (request->callbacks.cancelled) watched.callbacks.cancelled = native_watch_cancelled;
    return watched;
}

/* The warnings held back: logged (once the archive is published) or not,
 * and then dropped. */
static void native_watch_finish(native_watch *watch, bool publish)
{
    size_t at = 0U;
    while (publish && watch->callbacks && watch->callbacks->log && at < watch->warnings_size) {
        watch->callbacks->log(watch->callbacks->user, true, watch->warnings + at);
        at += strlen(watch->warnings + at) + 1U;
    }
    free(watch->warnings);
    watch->warnings = NULL;
    watch->warnings_size = 0U;
}

void xfu_native_warn(const xfu_request *request, const char *format, ...)
{
    char local[1024], *line = local;
    va_list arguments, copy;
    int count;
    if (!request || !request->callbacks.log) return;
    va_start(arguments, format);
    va_copy(copy, arguments);
    count = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (count >= (int)sizeof(local)) {
        line = (char *)malloc((size_t)count + 1U);
        if (line) (void)vsnprintf(line, (size_t)count + 1U, format, copy);
        else line = local;
    }
    va_end(copy);
    /* Past a writer's watch: a warning is not the reason of a failure, and
     * waits there until the archive is published (said at once only when it
     * cannot wait). */
    if (request->callbacks.log == native_watch_log) {
        native_watch *watch = (native_watch *)request->callbacks.user;
        size_t length = strlen(line) + 1U;
        char *grown = watch->warnings_size <= SIZE_MAX - length ?
            (char *)realloc(watch->warnings, watch->warnings_size + length) : NULL;
        if (grown) {
            memcpy(grown + watch->warnings_size, line, length);
            watch->warnings = grown;
            watch->warnings_size += length;
        } else watch->callbacks->log(watch->callbacks->user, true, line);
    } else request->callbacks.log(request->callbacks.user, true, line);
    if (line != local) free(line);
}

void xfu_native_civil(int64_t seconds, xfu_native_time *civil)
{
    /* Days to a proleptic Gregorian date (H. Hinnant's civil_from_days). */
    int64_t days = seconds / 86400, rest = seconds % 86400, era, of_era, year_of_era, of_year;
    unsigned month_index;
    if (rest < 0) { rest += 86400; --days; }
    days += 719468;
    era = (days >= 0 ? days : days - 146096) / 146097;
    of_era = days - era * 146097;
    year_of_era = (of_era - of_era / 1460 + of_era / 36524 - of_era / 146096) / 365;
    of_year = of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    month_index = (unsigned)((5 * of_year + 2) / 153);
    civil->day = (unsigned)(of_year - (153 * (int64_t)month_index + 2) / 5 + 1);
    civil->month = month_index < 10U ? month_index + 3U : month_index - 9U;
    civil->year = year_of_era + era * 400 + (civil->month <= 2U ? 1 : 0);
    civil->hour = (unsigned)(rest / 3600);
    civil->minute = (unsigned)(rest / 60 % 60);
    civil->second = (unsigned)(rest % 60);
}

/* "<date>", or "<date> <time>" when the other date of the line is the same
 * day (the times differ then). */
static void native_time_text(const xfu_native_time *moment, bool same_day, char *text,
                             size_t size)
{
    if (same_day)
        (void)snprintf(text, size, "%04" PRId64 "-%02u-%02u %02u:%02u:%02u", moment->year,
                       moment->month, moment->day, moment->hour, moment->minute,
                       moment->second);
    else
        (void)snprintf(text, size, "%04" PRId64 "-%02u-%02u", moment->year, moment->month,
                       moment->day);
}

/* The line of xfu_native_warn_time, for times already in the calendar the
 * format uses (later: the given time is past the format's range). */
static void native_warn_civil(const xfu_request *request, const char *path, const char *label,
                              const xfu_native_time *given, const xfu_native_time *instead,
                              bool later, bool kept)
{
    char given_text[64], stored_text[64];
    bool same_day = kept && given->year == instead->year && given->month == instead->month &&
        given->day == instead->day;
    native_time_text(given, same_day, given_text, sizeof(given_text));
    if (kept) {
        native_time_text(instead, same_day, stored_text, sizeof(stored_text));
        xfu_native_warn(request, "%s: its modification time (%s) is %s than %s can store; "
                        "stored as %s", path, given_text, later ? "later" : "earlier", label,
                        stored_text);
    } else
        xfu_native_warn(request, "%s: its modification time (%s) is %s than %s can store; "
                        "stored without one", path, given_text, later ? "later" : "earlier",
                        label);
}

void xfu_native_warn_time(const xfu_request *request, const char *path, const char *label,
                          int64_t mtime, int64_t stored, bool kept)
{
    xfu_native_time given, instead;
    xfu_native_civil(mtime, &given);
    xfu_native_civil(stored, &instead);
    native_warn_civil(request, path, label, &given, &instead, mtime > stored, kept);
}

/* Only claim names for which the native writer emits the requested family.
 * TAR's base ustar stream is also a valid interchange stream for members
 * whose names and metadata fit in the base header. */
static native_format native_parse_format(const char *name)
{
    if (!name) return NATIVE_UNSUPPORTED;
#if NATIVE_EXTRA_WRITERS
    if (xfu_native_extra_can_create(name)) return NATIVE_EXTRA;
#endif
#if !XFU_FORMAT_SUPPORT_FULL
#if XFU_FORMAT_SUPPORT_RAW_STREAMS
    if (!strcmp(name, "raw")) return NATIVE_RAW;
#endif
#ifdef XFU_SUPPORT_FORMAT_TAR
    if (xfu_native_tar_can_create(name)) return NATIVE_TAR_VARIANT;
#endif
#ifdef XFU_SUPPORT_FORMAT_CPIO
    if (!strcmp(name, "cpio") || !strcmp(name, "cpio_newc") || !strcmp(name, "newc")) return NATIVE_CPIO;
#endif
#ifdef XFU_SUPPORT_FORMAT_7ZIP
    if (!strcmp(name, "7zip") || !strcmp(name, "7z")) return NATIVE_7ZIP;
#endif
#endif
#if XFU_FORMAT_SUPPORT_FULL
    if (!strcmp(name, "raw")) return NATIVE_RAW;
    if (xfu_native_tar_can_create(name)) return NATIVE_TAR_VARIANT;
    if (!strcmp(name, "cpio_newc") || !strcmp(name, "newc")) return NATIVE_CPIO;
#endif
#ifdef XFU_SUPPORT_FORMAT_ZIP
    if (!strcmp(name, "zip")) return NATIVE_ZIP;
#endif
#if XFU_FORMAT_SUPPORT_FULL
    if (!strcmp(name, "7zip") || !strcmp(name, "7z")) return NATIVE_7ZIP;
    if (xfu_native_extra_can_create(name)) return NATIVE_EXTRA;
#endif
    return NATIVE_UNSUPPORTED;
}

static native_filter native_parse_filter(const char *name)
{
    if (!name || !strcmp(name, "none")) return FILTER_NONE;
#ifdef XFU_SUPPORT_FORMAT_GZ
    if (!strcmp(name, "gzip")) return FILTER_GZIP;
#endif
#ifdef XFU_SUPPORT_FORMAT_BZ2
    if (!strcmp(name, "bzip2")) return FILTER_BZIP2;
#endif
#ifdef XFU_SUPPORT_FORMAT_XZ
    if (!strcmp(name, "xz")) return FILTER_XZ;
#endif
#ifdef XFU_SUPPORT_FORMAT_ZSTD
    if (!strcmp(name, "zstd")) return FILTER_ZSTD;
#endif
#if NATIVE_LZMA_FILTER
    if (!strcmp(name, "lzma")) return FILTER_LZMA;
#endif
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_EXTRA_FILTERS
    if (xfu_native_filter_can_encode(name)) return FILTER_EXTRA;
#endif
    return FILTER_UNSUPPORTED;
}

bool xfu_native_can_create_format(const char *format)
{
    return native_parse_format(format) != NATIVE_UNSUPPORTED;
}

bool xfu_native_can_create_filter(const char *filter)
{
    return native_parse_filter(filter) != FILTER_UNSUPPORTED;
}

/* A folder separator in a path: '/', and on Windows also '\\'. Elsewhere
 * '\\' is part of a name ("back\\slash.txt" is one file). */
static bool native_separator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* The member name of an input path: relative paths keep their folders, and
 * absolute or parent-relative paths keep only their last component (the
 * established source-name rule). Empty and "." components are dropped, so
 * "./docs/" names "docs", and a path ending in ".." or "." (a folder above
 * or the current one) names the archive root: "" - never a ".." member. */
static char *native_member_name(const char *path)
{
    const char *cursor;
#ifdef _WIN32
    /* "C:" starts a drive's path on Windows only; elsewhere "9:00-notes.txt"
     * and "a:" are names. */
    bool drive = ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':';
#else
    const bool drive = false;
#endif
    bool basename = native_separator(path[0]) || drive;
    size_t used = 0U;
    char *name = (char *)malloc(strlen(path) + 1U);
    if (!name) return NULL;
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == '.' && cursor[1] == '.' &&
            (cursor == path || native_separator(cursor[-1])) &&
            (!cursor[2] || native_separator(cursor[2]))) basename = true;
    cursor = drive ? path + 2 : path;
    while (*cursor) {
        const char *begin = cursor;
        size_t length;
        while (*cursor && !native_separator(*cursor)) ++cursor;
        length = (size_t)(cursor - begin);
        while (native_separator(*cursor)) ++cursor;
        if (!length || (length == 1U && begin[0] == '.')) continue;
        if (basename) used = 0U; /* Every ".." component sets it, too. */
        if (length == 2U && begin[0] == '.' && begin[1] == '.') continue;
        if (used) name[used++] = '/';
        memcpy(name + used, begin, length);
        used += length;
    }
    name[used] = '\0';
    return name;
}

static char *native_copy_text(const char *text)
{
    size_t size = strlen(text) + 1U;
    char *copy = (char *)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

/* left, separator, right as one new string. */
static char *native_join(const char *left, char separator, const char *right)
{
    size_t a = strlen(left), b = strlen(right);
    bool joined = a && !native_separator(left[a - 1U]);
    char *text;
    if (a > SIZE_MAX - b - 2U) return NULL;
    text = (char *)malloc(a + b + 2U);
    if (!text) return NULL;
    memcpy(text, left, a);
    if (joined) text[a++] = separator;
    memcpy(text + a, right, b + 1U);
    return text;
}

static bool native_valid_utf8(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        size_t length = *p < 0x80U ? 1U : *p >= 0xC2U && *p <= 0xDFU ? 2U :
            *p >= 0xE0U && *p <= 0xEFU ? 3U : *p >= 0xF0U && *p <= 0xF4U ? 4U : 0U, i;
        if (!length) return false;
        for (i = 1U; i < length; ++i)
            if ((p[i] & 0xC0U) != 0x80U) return false;
        if (length > 1U && ((*p == 0xE0U && p[1] < 0xA0U) || (*p == 0xEDU && p[1] >= 0xA0U) ||
                            (*p == 0xF0U && p[1] < 0x90U) || (*p == 0xF4U && p[1] >= 0x90U)))
            return false;
        p += length;
    }
    return true;
}

#ifdef _WIN32
/* A symbolic link or a junction: a name-surrogate reparse point. Other
 * reparse points (deduplicated, cloud or compressed files) are ordinary files
 * and folders. A tag that cannot be read counts as a link. */
static bool native_name_surrogate(const wchar_t *path)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(path, &data);
    if (find == INVALID_HANDLE_VALUE) return true;
    FindClose(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        IsReparseTagNameSurrogate(data.dwReserved0);
}
#endif

#ifdef _WIN32
/* The errno of a Windows error, so that a failure reads as it does on POSIX
 * systems ("No such file or directory", "Permission denied", "File name too
 * long", ...); 0 for one with no counterpart. A file that another program
 * holds open without sharing it is "Permission denied", as the C runtime
 * says. */
static int native_windows_errno_of(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME: case ERROR_INVALID_DRIVE: case ERROR_BAD_PATHNAME: return ENOENT;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION:
    case ERROR_NETWORK_ACCESS_DENIED: return EACCES;
    case ERROR_INVALID_NAME: return EINVAL;
    case ERROR_DIRECTORY: return ENOTDIR;
    case ERROR_FILENAME_EXCED_RANGE: case ERROR_BUFFER_OVERFLOW: return ENAMETOOLONG;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return EEXIST;
    case ERROR_WRITE_PROTECT: return EROFS;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return ENOMEM;
    default: return 0;
    }
}

/* The errno for the last Windows error (EIO for one with no counterpart). */
static int native_windows_errno(void)
{
    int error = native_windows_errno_of(GetLastError());
    return error ? error : EIO;
}
#else
/* What the writers keep of a stat result. */
typedef struct native_entry {
    int64_t size, mtime;
    uint64_t device, inode;
    uint32_t mode, uid, gid; /* The type and permission bits; the owner. */
} native_entry;

static void native_entry_of(native_entry *entry, const struct stat *info)
{
    entry->size = (int64_t)info->st_size;
    entry->mtime = (int64_t)info->st_mtime;
    entry->device = (uint64_t)info->st_dev;
    entry->inode = (uint64_t)info->st_ino;
    entry->mode = (uint32_t)info->st_mode;
    entry->uid = (uint32_t)info->st_uid;
    entry->gid = (uint32_t)info->st_gid;
}

/* A file's or folder's metadata as the writers store it; 2 for what they do
 * not store (a link, a device or another special file). */
static int native_fill_source(native_source *source, const native_entry *entry)
{
    if ((!S_ISREG((mode_t)entry->mode) && !S_ISDIR((mode_t)entry->mode)) || entry->size < 0)
        return 2;
    source->directory = S_ISDIR((mode_t)entry->mode);
    source->size = source->directory ? 0 : entry->size;
    source->mode = entry->mode & 07777U;
    source->mtime = entry->mtime >= 0 ? (uint64_t)entry->mtime : 0U;
    source->stamp = entry->mtime;
    source->device = entry->device;
    source->inode = entry->inode;
    source->uid = entry->uid;
    source->gid = entry->gid;
    return 0;
}
#endif

/* 0: a regular file or a folder; 1: missing or unreadable metadata (errno
 * says why); 2: an input no writer stores (a link, a device or another
 * special file). follow: an input named on the command line, whose link is
 * followed to the file or folder it names, as the writers did before they
 * walked folders; a link met while walking a folder is refused (and so never
 * followed out of it). */
static int native_stat_source(native_source *source, bool follow)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(source->path);
    WIN32_FILE_ATTRIBUTE_DATA info = {0};
    DWORD attributes = 0;
    FILETIME written = {0, 0};
    uint64_t stamp, size = 0U;
    bool result = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &info);
    int error = wide ? (result ? 0 : native_windows_errno()) : EINVAL;
    if (result) {
        attributes = info.dwFileAttributes;
        size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        written = info.ftLastWriteTime;
    }
    if (result && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) && native_name_surrogate(wide)) {
        HANDLE handle;
        BY_HANDLE_FILE_INFORMATION target = {0};
        if (!follow) { xx_str_wfree(wide); return 2; }
        /* Opened through the link: the file or folder it names. */
        handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        result = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &target);
        if (!result) error = native_windows_errno();
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (result) {
            attributes = target.dwFileAttributes;
            size = ((uint64_t)target.nFileSizeHigh << 32) | target.nFileSizeLow;
            written = target.ftLastWriteTime;
        }
    }
    xx_str_wfree(wide);
    if (!result) { errno = error; return 1; }
    source->directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!source->directory && size > INT64_MAX) return 2;
    source->size = source->directory ? 0 : (int64_t)size;
    source->mode = source->directory ? 0755U :
        ((attributes & FILE_ATTRIBUTE_READONLY) ? 0444U : 0644U);
    stamp = ((uint64_t)written.dwHighDateTime << 32) | written.dwLowDateTime;
    source->mtime = stamp / UINT64_C(10000000) > UINT64_C(11644473600) ?
        stamp / UINT64_C(10000000) - UINT64_C(11644473600) : 0U;
    source->stamp = (int64_t)(stamp / UINT64_C(10000000)) - INT64_C(11644473600);
#else
    struct stat info;
    native_entry entry;
    if ((follow ? stat(source->path, &info) : lstat(source->path, &info)) != 0) return 1;
    native_entry_of(&entry, &info);
    return native_fill_source(source, &entry);
#endif
    return 0;
}

/* Why an input cannot be added, in one line that names it. */
static void native_log_unusable(const xfu_request *request, const char *path, int kind,
                                int error)
{
    if (kind == 2)
        native_log(request, true, "%s: links and special files cannot be added", path);
    else
        native_log(request, true, "%s: cannot read the input: %s", path,
                   error ? strerror(error) : "its metadata is unavailable");
}

static bool native_same_file(const char *left, const char *right)
{
    if (!strcmp(left, right)) return true;
#ifndef _WIN32
    {
        struct stat a, b;
        return stat(left, &a) == 0 && stat(right, &b) == 0 &&
            a.st_dev == b.st_dev && a.st_ino == b.st_ino;
    }
#else
    {
        /* The volume and file index, as core.c's same_file compares them. */
        wchar_t *a = xx_str_utf8_to_unicode(left), *b = xx_str_utf8_to_unicode(right);
        HANDLE ah = INVALID_HANDLE_VALUE, bh = INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION ai, bi;
        bool same = false;
        if (a && b) {
            ah = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            bh = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            same = ah != INVALID_HANDLE_VALUE && bh != INVALID_HANDLE_VALUE &&
                GetFileInformationByHandle(ah, &ai) && GetFileInformationByHandle(bh, &bi) &&
                ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber &&
                ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
        }
        if (ah != INVALID_HANDLE_VALUE) CloseHandle(ah);
        if (bh != INVALID_HANDLE_VALUE) CloseHandle(bh);
        xx_str_wfree(a);
        xx_str_wfree(b);
        return same;
    }
#endif
}

static native_source *native_list_add(native_list *list)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2U : 16U;
        native_source *grown;
        if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(*grown)) return NULL;
        grown = (native_source *)realloc(list->items, capacity * sizeof(*grown));
        if (!grown) return NULL;
        list->items = grown;
        list->capacity = capacity;
    }
    memset(&list->items[list->count], 0, sizeof(*list->items));
    return &list->items[list->count++];
}

static void native_list_free(native_list *list)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        free(list->items[i].owned_path);
        free(list->items[i].name);
        free(list->items[i].wire_name);
    }
    free(list->items);
}

static int native_compare_text(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static bool native_names_add(char ***names, size_t *count, size_t *capacity, char *name)
{
    if (!name) return false;
    if (*count == *capacity) {
        size_t grown_capacity = *capacity ? *capacity * 2U : 32U;
        char **grown;
        if (grown_capacity < *capacity || grown_capacity > SIZE_MAX / sizeof(*grown)) {
            free(name); return false;
        }
        grown = (char **)realloc(*names, grown_capacity * sizeof(*grown));
        if (!grown) { free(name); return false; }
        *names = grown;
        *capacity = grown_capacity;
    }
    (*names)[(*count)++] = name;
    return true;
}

#ifdef _WIN32
/* xx_str_unicode_to_utf8() allocates through xx_mem_alloc (the process heap
 * on Windows) and its result is released with xx_str_free: the name lists
 * hold malloc'd copies, which their owners free(). */
static char *native_utf8_copy(const wchar_t *wide)
{
    char *converted = xx_str_unicode_to_utf8(wide), *copy;
    if (!converted) return NULL;
    copy = native_copy_text(converted);
    xx_str_free(converted);
    return copy;
}
#endif

/* A folder's entries without "." and "..", in byte order, so archives of
 * one tree come out the same everywhere. On POSIX systems the folder is
 * opened once and must still be the folder the walk found (device and
 * inode; a walked folder through no link), and its entries are read through
 * that open folder, so a folder swapped for a link meanwhile is never walked
 * in its place. */
typedef struct native_folder {
    char **names;
    size_t count;
#ifndef _WIN32
    native_entry *infos; /* Each entry as lstat would give it. */
    int *errors; /* The errno of an entry that cannot be read, else 0. */
#endif
} native_folder;

static void native_folder_free(native_folder *folder)
{
    size_t i;
    for (i = 0; i < folder->count; ++i) free(folder->names[i]);
    free(folder->names);
#ifndef _WIN32
    free(folder->infos);
    free(folder->errors);
#endif
    memset(folder, 0, sizeof(*folder));
}

/* 0: read; 1: cannot be read (errno says why); 2: not the folder the walk
 * found. follow: a folder named as an input, reached through its link. */
static int native_folder_read(const char *path, const native_source *expected, bool follow,
                              native_folder *result)
{
    char **names = NULL;
    size_t count = 0U, capacity = 0U, i;
    bool success = false;
    int status = 1, error = 0;
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path), *pattern = NULL;
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    size_t length = wide ? wcslen(wide) : 0U;
    (void)expected;
    (void)follow;
    if (wide && length < SIZE_MAX / sizeof(*pattern) - 3U)
        pattern = (wchar_t *)malloc((length + 3U) * sizeof(*pattern));
    if (pattern) {
        memcpy(pattern, wide, length * sizeof(*pattern));
        if (length && pattern[length - 1U] != L'\\' && pattern[length - 1U] != L'/')
            pattern[length++] = L'\\';
        pattern[length++] = L'*';
        pattern[length] = 0;
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) {
            success = GetLastError() == ERROR_FILE_NOT_FOUND;
            if (!success) error = native_windows_errno();
        }
    } else error = ENOMEM;
    if (find != INVALID_HANDLE_VALUE) {
        for (;;) {
            const wchar_t *leaf = data.cFileName;
            if (!(leaf[0] == L'.' && (!leaf[1] || (leaf[1] == L'.' && !leaf[2]))) &&
                !native_names_add(&names, &count, &capacity, native_utf8_copy(leaf))) {
                error = ENOMEM;
                break;
            }
            if (!FindNextFileW(find, &data)) {
                success = GetLastError() == ERROR_NO_MORE_FILES;
                if (!success) error = native_windows_errno();
                break;
            }
        }
        FindClose(find);
    }
    free(pattern);
    xx_str_wfree(wide);
#else
    native_entry *infos = NULL;
    int *errors = NULL;
    struct stat info;
    DIR *folder = NULL;
    int descriptor = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC |
                                    (follow ? 0 : O_NOFOLLOW));
    if (descriptor < 0) {
        error = errno;
        /* A walked folder that is now a link or a file. */
        if (!follow && (error == ELOOP || error == EMLINK || error == ENOTDIR)) status = 2;
    } else if (fstat(descriptor, &info) != 0) error = errno;
    else if (!S_ISDIR(info.st_mode) || (uint64_t)info.st_dev != expected->device ||
             (uint64_t)info.st_ino != expected->inode) status = 2;
    else if (!(folder = fdopendir(descriptor))) error = errno;
    if (!folder && descriptor >= 0) (void)close(descriptor);
    if (folder) {
        for (;;) {
            struct dirent *item;
            errno = 0;
            item = readdir(folder);
            if (!item) { success = errno == 0; error = errno; break; }
            if (item->d_name[0] == '.' && (!item->d_name[1] ||
                (item->d_name[1] == '.' && !item->d_name[2]))) continue;
            if (!native_names_add(&names, &count, &capacity, native_copy_text(item->d_name))) {
                error = ENOMEM;
                break;
            }
        }
        if (success && count > 1U) qsort(names, count, sizeof(*names), native_compare_text);
        if (success && count) {
            infos = (native_entry *)calloc(count, sizeof(*infos));
            errors = (int *)calloc(count, sizeof(*errors));
            if (!infos || !errors) { success = false; error = ENOMEM; }
        }
        /* Through the open folder (fdopendir's descriptor, only read here). */
        for (i = 0; success && i < count; ++i) {
            if (fstatat(descriptor, names[i], &info, AT_SYMLINK_NOFOLLOW) != 0)
                errors[i] = errno ? errno : EIO;
            else native_entry_of(&infos[i], &info);
        }
        if (closedir(folder) != 0 && success) { success = false; error = errno; }
    }
    if (!success) {
        free(infos);
        free(errors);
    } else {
        result->infos = infos;
        result->errors = errors;
    }
#endif
    if (!success) {
        for (i = 0; i < count; ++i) free(names[i]);
        free(names);
        errno = error ? error : EIO;
        return status;
    }
#ifdef _WIN32
    if (count > 1U) qsort(names, count, sizeof(*names), native_compare_text);
#endif
    result->names = names;
    result->count = count;
    return 0;
}

typedef struct native_walk {
    const xfu_request *request;
    xx_pd_struct *pd;
    native_list *list;
    bool have_destination; /* The archive exists: its size and identity. */
    int64_t destination_size;
    uint64_t destination_device, destination_inode;
} native_walk;

static void native_find_destination(native_walk *walk)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(walk->request->archive_path);
    WIN32_FILE_ATTRIBUTE_DATA info = {0};
    walk->have_destination = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    if (walk->have_destination)
        walk->destination_size = (int64_t)(((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    xx_str_wfree(wide);
#else
    struct stat info;
    walk->have_destination = stat(walk->request->archive_path, &info) == 0 && S_ISREG(info.st_mode);
    if (walk->have_destination) {
        walk->destination_size = (int64_t)info.st_size;
        walk->destination_device = (uint64_t)info.st_dev;
        walk->destination_inode = (uint64_t)info.st_ino;
    }
#endif
}

/* The walk leaves out the archive being replaced (an input folder can hold
 * it): the new archive never holds its own previous version. */
static bool native_is_destination(const native_walk *walk, const native_source *item)
{
    if (!walk->have_destination || item->directory || item->size != walk->destination_size)
        return false;
#ifdef _WIN32
    return native_same_file(walk->request->archive_path, item->path);
#else
    return item->device == walk->destination_device && item->inode == walk->destination_inode;
#endif
}

static bool native_digits(const char **cursor, size_t limit, bool hex)
{
    const char *start = *cursor;
    while ((size_t)(*cursor - start) < limit &&
           ((**cursor >= '0' && **cursor <= '9') ||
            (hex && ((**cursor >= 'a' && **cursor <= 'f') || (**cursor >= 'A' && **cursor <= 'F') ||
                     **cursor == 'x' || **cursor == 'X'))))
        ++*cursor;
    return *cursor != start;
}

/* The stage of a write or an extraction that was cut short (by an interrupt):
 * ".xfu-<8 hex digits>-<n>.tmp" (native_stage, here and in the reader),
 * "<archive>.xfu-native-<pointer>-<time>-<n>.tmp" of earlier writers, or
 * xxfclib's ".xxfclib.tmp.<n>" (ZIP, RAR, ISO 9660, uImage) and
 * ".xxfclib.tmp.<32 hex digits>" (7z). A walk leaves them out, so a partial
 * archive or member never becomes a member of the next. */
static bool native_stage_leaf(const char *leaf)
{
    const char *cursor = leaf, *native = strstr(leaf, ".xfu-native-");
    if (!strncmp(leaf, ".xxfclib.tmp.", 13U)) {
        size_t digits = strspn(leaf + 13, "0123456789");
        if (digits && digits <= 10U && !leaf[13U + digits]) return true;
        return strspn(leaf + 13, "0123456789abcdef") == 32U && !leaf[45];
    }
    if (!strncmp(leaf, ".xfu-", 5U)) {
        cursor += 5;
        if (strspn(cursor, "0123456789abcdef") != 8U) return false;
        cursor += 8;
        if (*cursor++ != '-' || !native_digits(&cursor, 3U, false)) return false;
        return !strcmp(cursor, ".tmp");
    }
    if (!native || native == leaf) return false;
    cursor = native + 12;
    if (!native_digits(&cursor, 32U, true) || *cursor++ != '-' ||
        !native_digits(&cursor, 20U, false) || *cursor++ != '-' ||
        !native_digits(&cursor, 3U, false)) return false;
    return !strcmp(cursor, ".tmp");
}

/* Adds what a folder holds below its member name, each folder before its
 * contents. folder: the walked folder's identity; follow: a folder named as
 * an input. 0: done; 1: cancelled; 2: failed (logged). */
static int native_walk_folder(native_walk *walk, const char *path, const char *name,
                              const native_source *folder, bool follow, unsigned depth)
{
#ifdef _WIN32
    const char separator = '\\';
#else
    const char separator = '/';
#endif
    native_folder children;
    size_t i;
    int status = 0;
    memset(&children, 0, sizeof(children));
    if (depth > XFU_NATIVE_FOLDER_DEPTH) {
        native_log(walk->request, true, "%s: folders nested deeper than %u levels cannot be added",
                   path, XFU_NATIVE_FOLDER_DEPTH);
        return 2;
    }
    status = native_folder_read(path, folder, follow, &children);
    if (status) {
        if (status == 2) native_log(walk->request, true, "%s: source changed or cannot be read", path);
        else native_log(walk->request, true, "%s: cannot read the folder: %s", path, strerror(errno));
        return 2;
    }
    for (i = 0; i < children.count && !status; ++i) {
        native_source *item = NULL;
        char *child_path, *child_name;
        int kind, error = 0;
        if (native_stage_leaf(children.names[i])) continue;
        child_path = native_join(path, separator, children.names[i]);
        child_name = name[0] ? native_join(name, '/', children.names[i]) :
            native_copy_text(children.names[i]);
        if (native_cancelled(walk->request, walk->pd)) status = 1;
        else if (walk->list->count >= XFU_NATIVE_RECORD_LIMIT) {
            native_log(walk->request, true, "%s: the archive member limit is %u",
                       walk->request->archive_path, XFU_NATIVE_RECORD_LIMIT);
            status = 2;
        } else if (!child_path || !child_name || !(item = native_list_add(walk->list))) {
            native_log(walk->request, true, "cannot allocate the input list");
            status = 2;
        } else {
            item->path = item->owned_path = child_path;
            item->name = child_name;
            item->walked = true;
            child_path = child_name = NULL;
#ifdef _WIN32
            kind = native_stat_source(item, false);
            if (kind == 1) error = errno;
#else
            error = children.errors[i];
            kind = error ? 1 : native_fill_source(item, &children.infos[i]);
#endif
            if (kind) {
                native_log_unusable(walk->request, item->path, kind, error);
                status = 2;
            } else if (native_is_destination(walk, item)) {
                free(item->owned_path);
                free(item->name);
                --walk->list->count;
            } else if (item->directory) {
                /* item can move while the list grows: pass copies. */
                native_source walked = *item;
                status = native_walk_folder(walk, walked.path, walked.name, &walked, false,
                                            depth + 1U);
            }
        }
        free(child_path);
        free(child_name);
    }
    native_folder_free(&children);
    return status;
}

/* Component order: a folder's members follow it directly ('/' sorts first). */
static int native_compare_members(const void *left, const void *right)
{
    const native_source *x = *(const native_source *const *)left;
    const native_source *y = *(const native_source *const *)right;
    const unsigned char *a = (const unsigned char *)x->name, *b = (const unsigned char *)y->name;
    for (; *a && *a == *b; ++a, ++b) {}
    if (*a != *b) {
        unsigned ka = !*a ? 0U : *a == '/' ? 1U : *a + 1U;
        unsigned kb = !*b ? 0U : *b == '/' ? 1U : *b + 1U;
        return ka < kb ? -1 : 1;
    }
    return x < y ? -1 : x > y; /* Equal names keep their input order. */
}

static bool native_same_source(const native_source *a, const native_source *b)
{
#ifdef _WIN32
    return native_same_file(a->path, b->path);
#else
    return a->device == b->device && a->inode == b->inode;
#endif
}

/* One member per name. A file or folder named twice (overlapping inputs such
 * as "dir dir/file") is written once; folders of one name merge. Two
 * different files of one name, or one name for a file and a folder, are
 * refused: the archive could not be extracted as given. */
static bool native_check_names(const xfu_request *request, native_list *list)
{
    native_source **order;
    size_t i, kept = 0U;
    bool success = true;
    if (list->count < 2U) return true;
    if (list->count > SIZE_MAX / sizeof(*order) ||
        !(order = (native_source **)malloc(list->count * sizeof(*order)))) {
        native_log(request, true, "cannot allocate the input list");
        return false;
    }
    for (i = 0; i < list->count; ++i) order[i] = &list->items[i];
    qsort(order, list->count, sizeof(*order), native_compare_members);
    for (i = 1U; i < list->count && success; ++i) {
        native_source *first = order[i - 1U], *item = order[i];
        size_t length = strlen(first->name);
        if (!strcmp(first->name, item->name)) {
            if ((first->directory && item->directory) ||
                (!first->directory && !item->directory && native_same_source(first, item))) {
                item->skip = true;
                order[i] = order[i - 1U]; /* Compare what follows with the kept one. */
                continue;
            }
            native_log(request, true, "duplicate archive member name: %s", item->name);
            success = false;
        } else if (!first->directory && length && !strncmp(first->name, item->name, length) &&
                   item->name[length] == '/') {
            native_log(request, true, "%s: the name is both a file and a folder", first->name);
            success = false;
        }
    }
    free(order);
    if (!success) return false;
    for (i = 0; i < list->count; ++i) {
        if (list->items[i].skip) {
            free(list->items[i].owned_path);
            free(list->items[i].name);
            free(list->items[i].wire_name);
        } else list->items[kept++] = list->items[i];
    }
    list->count = kept;
    return true;
}

/* How a collection treats a folder input. */
typedef enum native_folders {
    NATIVE_FOLDERS_WALK = 0, /* Stored with everything it holds. */
    NATIVE_FOLDERS_REFUSE, /* The format has no folder members. */
    NATIVE_FOLDERS_RAW, /* A raw stream holds one file's bytes. */
    NATIVE_FOLDERS_STREAM /* core.c's compressed one-file streams. */
} native_folders;

/* A folder given to a one-file stream (stream: its filter or xxfclib kind),
 * with the archive that holds a folder instead. */
static void native_log_stream_folder(const xfu_request *request, const char *path,
                                     const char *stream)
{
    static const struct { const char *name, *article, *display, *suffix; } streams[] = {
        {"gz", "a", "gzip", ".tar.gz"}, {"gzip", "a", "gzip", ".tar.gz"},
        {"bz2", "a", "bzip2", ".tar.bz2"}, {"bzip2", "a", "bzip2", ".tar.bz2"},
        {"xz", "an", "xz", ".tar.xz"}, {"zstd", "a", "zstd", ".tar.zst"},
        {"lzma", "an", "lzma", ".tar.lzma"}, {"lz4", "an", "lz4", ".tar.lz4"},
        {"lzip", "an", "lzip", ".tar.lz"}, {"lzop", "an", "lzop", ".tar.lzo"},
        {"compress", "a", "compress", ".tar.Z"}, {"uuencode", "a", "uuencoded", ".tar.uue"},
        {"b64encode", "a", "Base64", ".tar.b64"}
    };
    size_t i;
    for (i = 0; stream && i < sizeof(streams) / sizeof(streams[0]); ++i) {
        if (!strcmp(stream, streams[i].name)) {
            native_log(request, true, "%s: %s %s stream holds one file, not a folder; use %s",
                       path, streams[i].article, streams[i].display, streams[i].suffix);
            return;
        }
    }
    native_log(request, true, "%s: a raw stream holds one file, not a folder; use an archive "
               "format such as --format=pax", path);
}

/* The members: each input, and for a folder what it holds, walked before
 * anything is written; each file or folder once, and every name checked.
 * label names the format in messages (for a raw stream, its filter). 0:
 * collected; 1: cancelled; 2: refused (logged). */
static int native_collect(const xfu_request *request, const char *label,
                          native_folders folders, bool needs_utf8,
                          native_list *list, xx_pd_struct *pd)
{
    native_walk walk;
    size_t i;
    memset(&walk, 0, sizeof(walk));
    walk.request = request;
    walk.pd = pd;
    walk.list = list;
    native_find_destination(&walk);
    for (i = 0; i < request->file_count; ++i) {
        native_source *item;
        int kind;
        if (native_cancelled(request, pd)) return 1;
        if (!request->files[i] || native_same_file(request->archive_path, request->files[i])) {
            native_log(request, true, "%s: cannot add output archive to itself",
                       request->archive_path);
            return 2;
        }
        if (!(item = native_list_add(list)) ||
            !(item->name = native_member_name(request->files[i]))) {
            native_log(request, true, "cannot allocate the input list");
            return 2;
        }
        item->path = request->files[i];
        kind = native_stat_source(item, true);
        if (kind) {
            native_log_unusable(request, request->files[i], kind, kind == 1 ? errno : 0);
            return 2;
        }
        if (!item->directory && !item->name[0]) {
            native_log(request, true, "%s: the input names no file to store", request->files[i]);
            return 2;
        }
        if (!item->directory) continue;
        if (folders == NATIVE_FOLDERS_RAW || folders == NATIVE_FOLDERS_STREAM) {
            native_log_stream_folder(request, request->files[i], label);
            return 2;
        }
        if (folders == NATIVE_FOLDERS_REFUSE) {
            native_log(request, true, "%s: %s archives cannot store folders; name its files",
                       request->files[i], label);
            return 2;
        }
        {
            /* The list can move while the walk adds to it. */
            const char *path = item->path;
            char *name = item->name;
            native_source folder = *item;
            int status;
            if (!name[0]) { /* "." or "..": its contents go to the archive root. */
                free(name);
                --list->count;
                name = NULL;
            }
            status = native_walk_folder(&walk, path, name ? name : "", &folder, true, 1U);
            if (status) return status;
        }
    }
    if (!native_check_names(request, list)) return 2;
    if (list->count > XFU_NATIVE_RECORD_LIMIT) {
        native_log(request, true, "%s: the archive member limit is %u", request->archive_path,
                   XFU_NATIVE_RECORD_LIMIT);
        return 2;
    }
    for (i = 0; needs_utf8 && i < list->count; ++i) {
        if (!native_valid_utf8(list->items[i].name)) {
            native_log(request, true, "%s: the name is not valid UTF-8, which %s cannot store",
                       list->items[i].path, label);
            return 2;
        }
    }
    return 0;
}

/* After publication, as core.c's writer did: each member added (a folder's
 * path ending in '/'), then the archive summary. */
static void native_log_members(const xfu_request *request, const native_list *list,
                               const char *label)
{
    uint64_t files = 0U, folders = 0U;
    size_t i;
    for (i = 0; i < list->count; ++i) {
        const char *path = list->items[i].path;
        size_t length = strlen(path);
        bool marked = !list->items[i].directory || (length && native_separator(path[length - 1U]));
        native_log(request, false, marked ? "  %s" : "  %s/", path);
        if (list->items[i].directory) ++folders;
        else ++files;
    }
    if (folders)
        native_log(request, false, "%s: %llu file(s) and %llu folder(s) as %s", request->archive_path,
                   (unsigned long long)files, (unsigned long long)folders, label);
    else
        native_log(request, false, "%s: %llu file(s) as %s", request->archive_path,
                   (unsigned long long)files, label);
}

static bool native_write_all(xx_io_device *output, const void *bytes, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)bytes;
    while (size) {
        ssize_t amount = xx_io_write(output, cursor, size);
        if (amount <= 0 || (size_t)amount > size) return false;
        cursor += amount;
        size -= (size_t)amount;
    }
    return true;
}

static bool native_copy(const xfu_request *request, xx_pd_struct *pd,
                        xx_io_device *source, xx_io_device *output)
{
    uint8_t buffer[XFU_NATIVE_CHUNK];
    ssize_t amount;
    if (xx_io_seek64(source, 0, SEEK_SET) != 0) return false;
    while ((amount = xx_io_read(source, buffer, sizeof(buffer))) > 0) {
        if (native_cancelled(request, pd) ||
            !native_write_all(output, buffer, (size_t)amount)) return false;
    }
    return amount == 0 && !native_cancelled(request, pd);
}

/* Every byte of an archive goes through this check. A stage's FILE keeps an
 * error once a write fails, but later calls can still report success (the C
 * library takes the bytes into its buffer and drops them, and fclose returns
 * 0 when nothing is left to flush), so a writer that loops over short writes
 * could finish and a cut stage be published. Here a failed or short write, a
 * failed seek or size query of a file and a failed close fail the run, and
 * every later write and seek fails at once. */
typedef struct native_checked {
    xx_io_device device; /* What the writer writes to. */
    xx_io_device *inner;
    xx_pd_struct *pd; /* Takes the reason of the first failure. */
    int64_t position, extent; /* The next write's offset; the end of the data. */
    int error; /* The errno of the first failure, or 0. */
    bool file; /* It can seek and has a size (not a FIFO). */
    bool failed;
} native_checked;

static native_checked *native_checked_of(xx_io_device *device)
{
    return (native_checked *)device->priv;
}

static void native_checked_fail(native_checked *checked, int error)
{
    char text[128];
    if (checked->failed) return;
    checked->failed = true;
    checked->error = error;
    (void)snprintf(text, sizeof(text), "the archive could not be written: %s",
                   error ? strerror(error) : "a write failed");
    if (checked->pd) xx_pd_set_error(checked->pd, error ? error : EIO, text);
}

static ssize_t native_checked_read(xx_io_device *device, void *buffer, size_t size)
{
    native_checked *checked = native_checked_of(device);
    ssize_t amount = checked->inner ? xx_io_read(checked->inner, buffer, size) : -1;
    if (amount > 0) checked->position += amount;
    return amount;
}

static ssize_t native_checked_write(xx_io_device *device, const void *buffer, size_t size)
{
    native_checked *checked = native_checked_of(device);
    ssize_t amount;
    if (checked->failed || !checked->inner) return -1;
    if (!size) return 0;
    errno = 0;
    amount = xx_io_write(checked->inner, buffer, size);
    if (amount < 0 || (size_t)amount != size || (int64_t)size > INT64_MAX - checked->position) {
        native_checked_fail(checked, errno);
        return -1;
    }
    checked->position += (int64_t)size;
    if (checked->position > checked->extent) checked->extent = checked->position;
    return amount;
}

static int native_checked_seek64(xx_io_device *device, int64_t offset, int whence)
{
    native_checked *checked = native_checked_of(device);
    int64_t position = whence == SEEK_SET ? offset :
        whence == SEEK_CUR && offset <= INT64_MAX - checked->position ? checked->position + offset : -1;
    if (checked->failed || !checked->inner) return -1;
    errno = 0;
    if (xx_io_seek64(checked->inner, offset, whence) != 0) {
        if (checked->file) native_checked_fail(checked, errno);
        return -1;
    }
    if (whence == SEEK_END || position < 0) position = xx_io_tell(checked->inner);
    if (position < 0) {
        if (checked->file) native_checked_fail(checked, errno);
        return checked->file ? -1 : 0;
    }
    checked->position = position;
    return 0;
}

static int native_checked_seek(xx_io_device *device, long offset, int whence)
{
    return native_checked_seek64(device, (int64_t)offset, whence);
}

static int64_t native_checked_tell(xx_io_device *device)
{
    native_checked *checked = native_checked_of(device);
    return checked->inner ? checked->position : -1;
}

static int64_t native_checked_size(xx_io_device *device)
{
    native_checked *checked = native_checked_of(device);
    int64_t size;
    if (!checked->inner) return -1;
    errno = 0;
    size = xx_io_total_size(checked->inner);
    if (size < 0 && checked->file) native_checked_fail(checked, errno);
    return size;
}

/* Closes what it checks; the check itself stays, owned by the caller. */
static int native_checked_close(xx_io_device *device)
{
    native_checked *checked = native_checked_of(device);
    if (checked->inner) {
        errno = 0;
        if (xx_io_close(checked->inner) != 0) native_checked_fail(checked, errno);
        checked->inner = NULL;
    }
    return checked->failed ? -1 : 0;
}

/* inner: a newly opened output, taken over (closed by xx_io_close). */
static xx_io_device *native_checked_open(native_checked *checked, xx_io_device *inner,
                                         xx_pd_struct *pd, bool file)
{
    memset(checked, 0, sizeof(*checked));
    checked->inner = inner;
    checked->pd = pd;
    checked->file = file;
    checked->device.read = native_checked_read;
    checked->device.write = native_checked_write;
    checked->device.seek = native_checked_seek;
    checked->device.seek64 = native_checked_seek64;
    checked->device.tell = native_checked_tell;
    checked->device.close = native_checked_close;
    checked->device.total_size = native_checked_size;
    checked->device.get_total_size = native_checked_size;
    checked->device.size = native_checked_size;
    checked->device.priv = checked;
    return &checked->device;
}

/* After the close: the file at path holds all the data written, or the run
 * fails (a stage cut by a full disk or a file size limit is never published,
 * whatever the writer's calls returned). */
static bool native_checked_complete(native_checked *checked, const char *path)
{
    xx_io_device *written;
    int64_t size = -1;
    if (checked->failed) return false;
    if ((written = xx_io_file_open(path, "rb"))) {
        size = xx_io_total_size(written);
        xx_io_close(written);
    }
    if (size != checked->extent) native_checked_fail(checked, size < 0 ? errno : EIO);
    return !checked->failed;
}

const char *xfu_native_output_failure(xx_io_device *output, char *buffer, size_t size)
{
    native_checked *checked;
    if (!output || output->write != native_checked_write) return NULL;
    checked = native_checked_of(output);
    if (!checked->failed) return NULL;
    (void)snprintf(buffer, size, "the archive could not be written: %s",
                   checked->error ? strerror(checked->error) : "a write failed");
    return buffer;
}

/* A file member, opened as the walk found it. */
typedef struct native_member_file {
    xx_io_device device;
#ifdef _WIN32
    HANDLE handle;
#else
    int descriptor;
#endif
} native_member_file;

#ifdef _WIN32
static ssize_t native_member_read(xx_io_device *device, void *buffer, size_t size)
{
    native_member_file *file = (native_member_file *)device->priv;
    DWORD amount = 0;
    if (size > 0x40000000U) size = 0x40000000U;
    if (!ReadFile(file->handle, buffer, (DWORD)size, &amount, NULL)) return -1;
    return (ssize_t)amount;
}

static int native_member_seek64(xx_io_device *device, int64_t offset, int whence)
{
    native_member_file *file = (native_member_file *)device->priv;
    LARGE_INTEGER distance;
    DWORD method = whence == SEEK_SET ? FILE_BEGIN : whence == SEEK_CUR ? FILE_CURRENT : FILE_END;
    distance.QuadPart = offset;
    return SetFilePointerEx(file->handle, distance, NULL, method) ? 0 : -1;
}

static int64_t native_member_tell(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    LARGE_INTEGER zero, position;
    zero.QuadPart = 0;
    return SetFilePointerEx(file->handle, zero, &position, FILE_CURRENT) ? (int64_t)position.QuadPart : -1;
}

static int64_t native_member_size(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    LARGE_INTEGER size;
    return GetFileSizeEx(file->handle, &size) ? (int64_t)size.QuadPart : -1;
}

static int native_member_close(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    int result = CloseHandle(file->handle) ? 0 : -1;
    free(file);
    return result;
}
#else
static ssize_t native_member_read(xx_io_device *device, void *buffer, size_t size)
{
    native_member_file *file = (native_member_file *)device->priv;
    ssize_t amount;
    if (size > (size_t)SSIZE_MAX) size = (size_t)SSIZE_MAX;
    do amount = read(file->descriptor, buffer, size);
    while (amount < 0 && errno == EINTR);
    return amount;
}

static int native_member_seek64(xx_io_device *device, int64_t offset, int whence)
{
    native_member_file *file = (native_member_file *)device->priv;
    if ((int64_t)(off_t)offset != offset) return -1;
    return lseek(file->descriptor, (off_t)offset, whence) < 0 ? -1 : 0;
}

static int64_t native_member_tell(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    off_t position = lseek(file->descriptor, 0, SEEK_CUR);
    return position < 0 ? -1 : (int64_t)position;
}

static int64_t native_member_size(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    struct stat info;
    return fstat(file->descriptor, &info) != 0 ? -1 : (int64_t)info.st_size;
}

static int native_member_close(xx_io_device *device)
{
    native_member_file *file = (native_member_file *)device->priv;
    int result = close(file->descriptor);
    free(file);
    return result;
}
#endif

static int native_member_seek(xx_io_device *device, long offset, int whence)
{
    return native_member_seek64(device, (int64_t)offset, whence);
}

xx_io_device *xfu_native_open_member(const xfu_native_member *member)
{
    native_member_file *file;
#ifdef _WIN32
    /* A walked member's link or junction is opened itself, never followed,
     * and is refused below: the walk found no link there. Another reparse
     * point (a deduplicated or cloud file) is an ordinary file: it is opened
     * again through its filter, and must be the same file. */
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    wchar_t *wide = member && member->path ? xx_str_utf8_to_unicode(member->path) : NULL;
    BY_HANDLE_FILE_INFORMATION info = {0}, again = {0};
    FILE_ATTRIBUTE_TAG_INFO tag = {0};
    HANDLE handle = wide ? CreateFileW(wide, GENERIC_READ, share, NULL, OPEN_EXISTING,
                                       FILE_FLAG_SEQUENTIAL_SCAN |
                                       (member->walked ? FILE_FLAG_OPEN_REPARSE_POINT : 0U), NULL) :
        INVALID_HANDLE_VALUE, reopened;
    /* Why it could not be opened; a check below that fails means "changed". */
    int failure = handle == INVALID_HANDLE_VALUE && wide ? native_windows_errno_of(GetLastError()) : 0;
    if (handle != INVALID_HANDLE_VALUE &&
        (!GetFileInformationByHandle(handle, &info) ||
         ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && member->walked &&
          (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
           IsReparseTagNameSurrogate(tag.ReparseTag))))) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    if (handle != INVALID_HANDLE_VALUE && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        member->walked) {
        reopened = CreateFileW(wide, GENERIC_READ, share, NULL, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (reopened == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(reopened, &again) ||
            again.dwVolumeSerialNumber != info.dwVolumeSerialNumber ||
            again.nFileIndexHigh != info.nFileIndexHigh || again.nFileIndexLow != info.nFileIndexLow) {
            if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
            reopened = INVALID_HANDLE_VALUE;
        }
        CloseHandle(handle);
        handle = reopened;
        if (reopened != INVALID_HANDLE_VALUE) info = again;
    }
    xx_str_wfree(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        /* As on POSIX systems, a file gone since the walk changed. */
        errno = failure == ENOENT ? 0 : failure;
        return NULL;
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (int64_t)(((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow) != member->size ||
        !(file = (native_member_file *)calloc(1U, sizeof(*file)))) {
        CloseHandle(handle);
        errno = 0;
        return NULL;
    }
    file->handle = handle;
#else
    /* O_NONBLOCK: a FIFO put in a file's place does not stop the run. */
    struct stat info;
    int flags, descriptor;
    if (!member || !member->path) {
        errno = 0;
        return NULL;
    }
    descriptor = open(member->path, O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC |
                                        (member->walked ? O_NOFOLLOW : 0));
    /* Gone, or a link put in a walked file's place: changed since found. */
    if (descriptor < 0) {
        if (errno == ENOENT || errno == ELOOP) errno = 0;
        return NULL;
    }
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        (int64_t)info.st_size != member->size ||
        (uint64_t)info.st_dev != member->device || (uint64_t)info.st_ino != member->inode ||
        (flags = fcntl(descriptor, F_GETFL)) < 0 ||
        fcntl(descriptor, F_SETFL, flags & ~O_NONBLOCK) != 0 ||
        !(file = (native_member_file *)calloc(1U, sizeof(*file)))) {
        (void)close(descriptor);
        errno = 0;
        return NULL;
    }
    file->descriptor = descriptor;
#endif
    file->device.read = native_member_read;
    file->device.seek = native_member_seek;
    file->device.seek64 = native_member_seek64;
    file->device.tell = native_member_tell;
    file->device.close = native_member_close;
    file->device.total_size = native_member_size;
    file->device.get_total_size = native_member_size;
    file->device.size = native_member_size;
    file->device.priv = file;
    return &file->device;
}

const char *xfu_native_open_failure(int error, char *buffer, size_t size)
{
    if (error) snprintf(buffer, size, "cannot read the input: %s", strerror(error));
    else snprintf(buffer, size, "source changed or cannot be read");
    return buffer;
}

/* Where the archive is published: the destination, or for a destination that
 * is a symbolic link, the file the link names (the link stays a link, as when
 * the archive was written through it), and the mode of an archive there. */
typedef struct native_target {
    char *path;
    bool replaces; /* A regular file is there: the stage takes its mode. */
    bool direct; /* A device or FIFO: written as it is, with no stage. */
    uint32_t mode, uid, gid;
} native_target;

#ifndef _WIN32
/* The path a symbolic link names, beside the link when it is relative. */
static char *native_link_target(const char *link, size_t hint)
{
    size_t capacity = hint > 0U && hint < 65536U ? hint + 1U : 256U, folder = 0U, i;
    char *text = NULL, *joined;
    ssize_t length;
    for (;;) {
        char *grown = (char *)realloc(text, capacity);
        if (!grown) { free(text); return NULL; }
        text = grown;
        length = readlink(link, text, capacity);
        if (length < 0) { free(text); return NULL; }
        if ((size_t)length < capacity) break;
        if (capacity > 65536U) { free(text); return NULL; }
        capacity *= 2U;
    }
    text[length] = '\0';
    for (i = 0; link[i]; ++i)
        if (link[i] == '/') folder = i + 1U;
    if (text[0] == '/' || !folder) return text;
    joined = (char *)malloc(folder + (size_t)length + 1U);
    if (joined) {
        memcpy(joined, link, folder);
        memcpy(joined + folder, text, (size_t)length + 1U);
    }
    free(text);
    return joined;
}

#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

/* The rule the kernel applies when it follows a link itself (Linux
 * fs.protected_symlinks): in a sticky folder that everyone may write, a link
 * is followed only when it belongs to the caller or to the folder's owner.
 * Following it here must not let another user's link name the file written. */
static bool native_link_trusted(const char *link, const struct stat *info)
{
    size_t folder = 0U, i;
    struct stat parent;
    char *path;
    bool trusted;
    if (info->st_uid == geteuid()) return true;
    for (i = 0; link[i]; ++i)
        if (link[i] == '/') folder = i;
    if (folder == 0U && link[0] == '/') folder = 1U;
    path = native_copy_text(folder ? link : ".");
    if (!path) return false;
    if (folder) path[folder] = '\0';
    trusted = stat(path, &parent) == 0 &&
        (!(parent.st_mode & S_ISVTX) || !(parent.st_mode & S_IWOTH) ||
         info->st_uid == parent.st_uid);
    free(path);
    return trusted;
}
#endif

/* The folder part of a path, for messages: "dir/", or "." for none. */
static char *native_folder_text(const char *path)
{
    size_t folder = 0U, i;
    char *text;
    for (i = 0; path[i]; ++i) {
#ifdef _WIN32
        if (path[i] == '/' || path[i] == '\\') folder = i + 1U;
#else
        if (path[i] == '/') folder = i + 1U;
#endif
    }
    if (!folder) return native_copy_text(".");
    if (!(text = (char *)malloc(folder + 1U))) return NULL;
    memcpy(text, path, folder);
    text[folder] = '\0';
    return text;
}

static void native_refuse_destination(const xfu_request *request, const char *reason)
{
    native_log(request, true, "%s: cannot create the archive: %s", request->archive_path, reason);
}

#ifndef _WIN32
/* The longest file name the destination's folder (or, while it is missing,
 * the nearest folder above it) takes. */
static long native_name_max(const char *path)
{
    char *folder = native_copy_text(path);
    size_t length = folder ? strlen(folder) : 0U;
    long limit = -1;
    /* Each folder above the name in turn, the nearest first. */
    while (folder) {
        while (length && folder[length - 1U] != '/') --length;
        if (!length) {
            limit = pathconf(".", _PC_NAME_MAX);
            break;
        }
        folder[length == 1U ? 1U : length - 1U] = '\0'; /* "a/b/" is "a/b"; "/" stays. */
        errno = 0;
        limit = pathconf(folder, _PC_NAME_MAX);
        if (limit >= 0 || errno != ENOENT || length == 1U) break;
        --length;
    }
    free(folder);
#ifdef NAME_MAX
    if (limit < 0) limit = NAME_MAX;
#else
    if (limit < 0) limit = 255;
#endif
    return limit;
}
#endif

/* What is at the destination decides how it is written, before anything is:
 * nothing (a new archive), a regular file this account may write (replaced),
 * or a device or FIFO (written as it is). A folder, a write-protected archive
 * (as open(O_TRUNC) and the other archivers refuse it) and a name the folder
 * cannot take are refused. false: refused (logged). */
static bool native_find_target(const xfu_request *request, native_target *target)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(request->archive_path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    bool link = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) && native_name_surrogate(wide);
    const wchar_t *leaf = wide, *cursor;
    memset(target, 0, sizeof(*target));
    for (cursor = wide; cursor && *cursor; ++cursor)
        if (*cursor == L'/' || *cursor == L'\\' || *cursor == L':') leaf = cursor + 1;
    if (leaf && wcslen(leaf) > 255U) {
        xx_str_wfree(wide);
        native_refuse_destination(request, strerror(ENAMETOOLONG));
        return false;
    }
    xx_str_wfree(wide);
    /* MoveFileEx would replace the link itself. */
    if (link) {
        native_log(request, true, "%s: the destination is a link; name the archive it points to",
                   request->archive_path);
        return false;
    }
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))) {
        native_refuse_destination(request, strerror((attributes & FILE_ATTRIBUTE_DIRECTORY) ?
                                                    EISDIR : EACCES));
        return false;
    }
    target->replaces = attributes != INVALID_FILE_ATTRIBUTES;
    if (!(target->path = native_copy_text(request->archive_path))) {
        native_log(request, true, "cannot allocate the input list");
        return false;
    }
    return true;
#else
    struct stat info;
    unsigned hops;
    const char *leaf, *cursor;
    int error;
    memset(target, 0, sizeof(*target));
    target->path = native_copy_text(request->archive_path);
    for (hops = 0; target->path && lstat(target->path, &info) == 0 && S_ISLNK(info.st_mode);
         ++hops) {
        char *next;
        if (!native_link_trusted(target->path, &info)) {
            native_log(request, true, "%s: the destination is another user's symbolic link "
                       "in a shared folder; it is not followed", request->archive_path);
            free(target->path);
            target->path = NULL;
            return false;
        }
        next = hops < 40U ? native_link_target(target->path, (size_t)info.st_size) : NULL;
        if (!next) {
            native_log(request, true, "%s: cannot follow the symbolic link to the archive",
                       request->archive_path);
            free(target->path);
            target->path = NULL;
            return false;
        }
        free(target->path);
        target->path = next;
    }
    if (!target->path) {
        native_log(request, true, "cannot allocate the input list");
        return false;
    }
    if (stat(target->path, &info) == 0) {
        if (S_ISREG(info.st_mode)) {
            if (access(target->path, W_OK) != 0) {
                native_refuse_destination(request, strerror(errno));
                goto refused;
            }
            target->replaces = true;
            target->mode = (uint32_t)(info.st_mode & 0777U);
            target->uid = (uint32_t)info.st_uid;
            target->gid = (uint32_t)info.st_gid;
        } else if (S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode) || S_ISFIFO(info.st_mode)) {
            target->direct = true;
        } else {
            native_refuse_destination(request, S_ISDIR(info.st_mode) ? strerror(EISDIR) :
                                      "a socket or another special file is there");
            goto refused;
        }
        return true;
    }
    error = errno;
    if (error == ENOENT && stat(request->archive_path, &info) == 0 &&
        (S_ISCHR(info.st_mode) || S_ISFIFO(info.st_mode))) {
        /* A link that only the kernel resolves (/dev/stdout names
         * "pipe:[...]"): the pipe or terminal it is now. */
        free(target->path);
        if (!(target->path = native_copy_text(request->archive_path))) {
            native_log(request, true, "cannot allocate the input list");
            return false;
        }
        target->direct = true;
        return true;
    }
    if (error != ENOENT) {
        /* A folder on the way that is a file, or cannot be searched; a name
         * too long. */
        native_refuse_destination(request, strerror(error));
        goto refused;
    }
    for (leaf = cursor = target->path; *cursor; ++cursor)
        if (*cursor == '/') leaf = cursor + 1;
    if (!*leaf || (long)strlen(leaf) > native_name_max(target->path)) {
        native_refuse_destination(request, strerror(*leaf ? ENAMETOOLONG : EISDIR));
        goto refused;
    }
    return true;
refused:
    free(target->path);
    target->path = NULL;
    return false;
#endif
}

#ifndef _WIN32
/* The stage, written through the descriptor of its exclusive create (with
 * stdio's buffering, as xx_io_file_open's files): never reopened by name, so
 * a link put in its place is never written through. */
typedef struct native_stage_file {
    xx_io_device device;
    FILE *file;
} native_stage_file;

static ssize_t native_stage_read(xx_io_device *device, void *buffer, size_t size)
{
    FILE *file = ((native_stage_file *)device->priv)->file;
    size_t count = size ? fread(buffer, 1, size, file) : 0U;
    return count == 0U && ferror(file) ? -1 : (ssize_t)count;
}

static ssize_t native_stage_write(xx_io_device *device, const void *buffer, size_t size)
{
    FILE *file = ((native_stage_file *)device->priv)->file;
    size_t count = size ? fwrite(buffer, 1, size, file) : 0U;
    return count == 0U && size && ferror(file) ? -1 : (ssize_t)count;
}

static int native_stage_seek64(xx_io_device *device, int64_t offset, int whence)
{
    if ((int64_t)(off_t)offset != offset) return -1;
    return fseeko(((native_stage_file *)device->priv)->file, (off_t)offset, whence) != 0 ? -1 : 0;
}

static int native_stage_seek(xx_io_device *device, long offset, int whence)
{
    return native_stage_seek64(device, (int64_t)offset, whence);
}

static int64_t native_stage_tell(xx_io_device *device)
{
    off_t position = ftello(((native_stage_file *)device->priv)->file);
    return position < 0 ? -1 : (int64_t)position;
}

static int64_t native_stage_size(xx_io_device *device)
{
    FILE *file = ((native_stage_file *)device->priv)->file;
    struct stat info;
    return fflush(file) != 0 || fstat(fileno(file), &info) != 0 ? -1 : (int64_t)info.st_size;
}

static int native_stage_close(xx_io_device *device)
{
    native_stage_file *stage = (native_stage_file *)device->priv;
    int result = fclose(stage->file);
    free(stage);
    return result;
}

/* NULL (descriptor closed, errno set) when it cannot be wrapped. */
static xx_io_device *native_stage_wrap(int descriptor)
{
    native_stage_file *stage = (native_stage_file *)calloc(1U, sizeof(*stage));
    if (!stage || !(stage->file = fdopen(descriptor, "w+b"))) {
        int error = stage ? errno : ENOMEM;
        free(stage);
        (void)close(descriptor);
        errno = error;
        return NULL;
    }
    stage->device.read = native_stage_read;
    stage->device.write = native_stage_write;
    stage->device.seek = native_stage_seek;
    stage->device.seek64 = native_stage_seek64;
    stage->device.tell = native_stage_tell;
    stage->device.close = native_stage_close;
    stage->device.total_size = native_stage_size;
    stage->device.get_total_size = native_stage_size;
    stage->device.size = native_stage_size;
    stage->device.priv = stage;
    return &stage->device;
}
#endif

/* Why xx_io_file_open failed: errno, or on Windows the system's last error
 * (which it leaves, and errno not). */
static int native_open_errno(void)
{
#ifdef _WIN32
    int error = native_windows_errno_of(GetLastError());
#else
    int error = errno;
#endif
    return error ? error : EACCES;
}

/* A short name in the destination's folder (the same file system, so the
 * final rename is atomic), whatever the length of the destination's own name:
 * ".xfu-<8 hex digits>-<n>.tmp", the reader's stage pattern, which a folder
 * walk leaves out. Created exclusively; a stage that will replace an archive
 * is open to its owner only until it takes that archive's mode. NULL: error
 * holds the errno. */
static xx_io_device *native_stage(const native_target *target, char **stage,
                                  const void *nonce, int *error)
{
    const char *destination = target->path;
    size_t length = strlen(destination), folder = 0U, i;
    unsigned long tag = (unsigned long)(((uintptr_t)nonce >> 4) ^ (uintptr_t)time(NULL)) & 0xFFFFFFFFUL;
    char *name;
    unsigned attempt;
    *stage = NULL;
    *error = ENOMEM;
    for (i = 0; i < length; ++i)
        if (native_separator(destination[i])) folder = i + 1U;
    if (folder > SIZE_MAX - 32U) return NULL;
    name = (char *)malloc(folder + 32U);
    if (!name) return NULL;
    memcpy(name, destination, folder);
    *error = EEXIST;
    for (attempt = 0; attempt < 128U; ++attempt) {
        xx_io_device *output = NULL;
        (void)snprintf(name + folder, 32U, ".xfu-%08lx-%u.tmp", tag, attempt);
        if (!strcmp(name, destination)) continue;
#ifndef _WIN32
        {
            int descriptor = open(name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                                  target->replaces ? 0600 : 0666);
            if (descriptor < 0) {
                *error = errno;
                if (*error == EEXIST) continue;
                break; /* The folder takes no file: no other name helps. */
            }
            if (!(output = native_stage_wrap(descriptor))) {
                *error = errno ? errno : EIO;
                (void)xx_io_file_remove_a(name);
            }
        }
#else
        output = xx_io_file_open(name, "wbx");
        if (!output) {
            *error = native_open_errno();
            if (*error == EEXIST) continue;
            break; /* The folder takes no file: no other name helps. */
        }
#endif
        if (output) { *stage = name; return output; }
    }
    free(name);
    return NULL;
}

/* The destination's missing folders. Folders that are there are used as
 * they are, through a link (or, on Windows, another reparse point) too:
 * xx_io_create_dirs_a, made for extraction, refuses every such component,
 * even of the destination the user named. false: errno says why. */
static bool native_create_folders(const char *destination)
{
    char *path = native_copy_text(destination);
    size_t cut = 0U, i;
    bool success = true;
    int error = 0;
#ifdef _WIN32
    wchar_t *wide;
    DWORD attributes;
#else
    struct stat info;
#endif
    if (!path) { errno = ENOMEM; return false; }
    for (i = 0; path[i]; ++i) {
#ifdef _WIN32
        if (path[i] == '/' || path[i] == '\\') cut = i;
#else
        if (path[i] == '/') cut = i;
#endif
    }
    if (!cut) { free(path); return true; } /* The current folder or the root. */
    path[cut] = '\0';
#ifdef _WIN32
    wide = xx_str_utf8_to_unicode(path);
    attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    xx_str_wfree(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        success = xx_io_create_dirs_a(destination, false);
        if (!success) error = errno ? errno : EACCES;
    }
#else
    /* As mkdir -p: each missing folder in turn. */
    for (i = 1U; success && i <= cut; ++i) {
        char saved = path[i];
        if (i < cut && path[i] != '/') continue;
        path[i] = '\0';
        if (!(stat(path, &info) == 0 && S_ISDIR(info.st_mode)) && mkdir(path, 0777) != 0 &&
            !(errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode))) {
            success = false;
            error = errno == EEXIST ? ENOTDIR : errno;
        }
        path[i] = saved;
    }
#endif
    free(path);
    if (!success) errno = error;
    return success;
}

/* The replaced archive's permissions (POSIX), with its owner and group where
 * this account may give them; its group's permissions are not given to
 * another group. Hard links to it keep the previous archive. */
static void native_stage_mode(const native_target *target, xx_io_device *stage)
{
#ifndef _WIN32
    mode_t mode = (mode_t)target->mode;
    int descriptor;
    /* Through the stage's own descriptor (native_stage_wrap), not its name. */
    if (!target->replaces || !stage || stage->close != native_stage_close) return;
    descriptor = fileno(((native_stage_file *)stage->priv)->file);
    if (fchown(descriptor, (uid_t)target->uid, (gid_t)target->gid) != 0 &&
        fchown(descriptor, (uid_t)-1, (gid_t)target->gid) != 0) mode &= ~(mode_t)070;
    (void)fchmod(descriptor, mode);
#else
    (void)target;
    (void)stage;
#endif
}

/* Where the writer's bytes go: a stage beside the destination, renamed over
 * it when complete; or, for an archive in a folder that takes no new file, a
 * temporary stage copied over the archive when complete; or the device
 * itself; or, for a FIFO (or another device that cannot seek), a temporary
 * stage copied to it when complete, since a writer may go back to finish a
 * header. */
typedef struct native_output {
    native_checked checked;
    char *stage; /* The stage beside the destination. */
    xx_io_device *final; /* The FIFO a temporary stage goes to. */
    bool open; /* checked holds an open device. */
    bool in_place; /* A temporary stage, copied to the destination at the end. */
    bool published;
#ifndef _WIN32
    struct stat stage_identity; /* The stage as created: it is what is published. */
#endif
} native_output;

/* false: refused (logged), before anything is written. */
static bool native_open_output(const xfu_request *request, const native_target *target,
                               native_output *output, xx_pd_struct *pd)
{
    xx_io_device *device;
    int error = 0;
    memset(output, 0, sizeof(*output));
    if (target->direct) {
        /* As "cat > device" would. */
        errno = 0;
        if (!(device = xx_io_file_open(target->path, "wb"))) {
            native_refuse_destination(request, strerror(native_open_errno()));
            return false;
        }
        if (xx_io_seek64(device, 0, SEEK_CUR) != 0) {
            output->final = device;
            errno = 0;
            if (!(device = xx_io_temp_open())) {
                native_log(request, true, "cannot create temporary archive stream: %s",
                           strerror(errno ? errno : EIO));
                (void)xx_io_close(output->final);
                output->final = NULL;
                return false;
            }
            output->in_place = true;
        }
    } else {
        if (!native_create_folders(target->path)) {
            native_log(request, true, "%s: cannot create its folder: %s", request->archive_path,
                       strerror(errno));
            return false;
        }
        device = native_stage(target, &output->stage, output, &error);
#ifndef _WIN32
        if (device && fstat(fileno(((native_stage_file *)device->priv)->file),
                            &output->stage_identity) != 0) {
            error = errno;
            (void)xx_io_close(device);
            device = NULL;
            (void)xx_io_file_remove_a(output->stage);
            free(output->stage);
            output->stage = NULL;
        }
#endif
        if (!device && target->replaces && (error == EACCES || error == EPERM)) {
            /* The folder takes no new file, but the archive itself may be
             * written: it is built aside and then written over in place. */
            if ((device = xx_io_temp_open())) output->in_place = true;
        }
        if (!device) {
            char *folder = native_folder_text(target->path);
            native_log(request, true, "%s: cannot create a staging file in %s: %s",
                       request->archive_path, folder ? folder : ".", strerror(error));
            free(folder);
            return false;
        }
    }
    /* A device written directly could seek: a failed seek or size query
     * (which flushes) is a write error there too. */
    (void)native_checked_open(&output->checked, device, pd, true);
    output->open = true;
    return true;
}

/* Writes the complete temporary stage over the archive. */
static bool native_write_in_place(const xfu_request *request, const native_target *target,
                                  native_output *output, xx_pd_struct *pd)
{
    native_checked written;
    uint8_t buffer[XFU_NATIVE_CHUNK];
    xx_io_device *file, *stage = &output->checked.device;
    int64_t copied = 0;
    ssize_t amount = 0;
    bool success;
    errno = 0;
    if (xx_io_seek64(stage, 0, SEEK_SET) != 0) return false;
    if (output->final) {
        file = output->final;
        output->final = NULL;
    } else if (!(file = xx_io_file_open(target->path, "wb"))) {
        native_refuse_destination(request, strerror(native_open_errno()));
        return false;
    }
    (void)native_checked_open(&written, file, pd, !target->direct);
    while ((amount = xx_io_read(stage, buffer, sizeof(buffer))) > 0 &&
           native_write_all(&written.device, buffer, (size_t)amount))
        copied += amount;
    success = amount == 0 && copied == output->checked.extent;
    if (xx_io_close(&written.device) != 0) success = false;
    if (!success)
        native_log(request, true, "%s: the archive could not be written %s: %s",
                   request->archive_path, target->direct ? "completely" :
                   "in place, and is incomplete", strerror(written.error ? written.error : EIO));
    return success;
}

/* Not an errno: what is at the stage is not the stage that was written. */
#define NATIVE_STAGE_REPLACED (-1)

#ifndef _WIN32
/* Puts the complete stage, checked just before, at the destination: over
 * the archive it replaces (rename), or, where there was none, without
 * replacing anything (linkat, which fails on a name that appeared meanwhile;
 * where the file system has no hard links, rename once nothing is there).
 * What is at the destination afterwards is left alone: another writer that
 * put its own file there meanwhile (a second run, a program that saves with
 * rename) keeps it, as with rename alone. 0, or the errno; EEXIST: a file
 * appeared at a new destination. */
static int native_publish_stage(const native_output *output, const native_target *target)
{
    struct stat info;
    int error;
    if (target->replaces) {
        if (rename(output->stage, target->path) != 0) return errno ? errno : EIO;
    } else if (linkat(AT_FDCWD, output->stage, AT_FDCWD, target->path, 0) == 0) {
        (void)unlink(output->stage);
    } else {
        error = errno;
        if (error == EEXIST || lstat(target->path, &info) == 0) return EEXIST;
        if (errno != ENOENT) return errno ? errno : EIO;
        if (rename(output->stage, target->path) != 0) return errno ? errno : EIO;
    }
    return 0;
}
#endif

/* After a writer finished: the output closed and checked, and published.
 * 0: published; 1: failed (logged). */
static int native_publish(const xfu_request *request, const native_target *target,
                          native_output *output, xx_pd_struct *pd)
{
    int error;
    bool complete;
    if (output->in_place) {
        /* The temporary stage goes when it is closed: copied first. */
        complete = !output->checked.failed && native_write_in_place(request, target, output, pd);
        output->open = false;
        if (xx_io_close(&output->checked.device) != 0) complete = false;
        if (complete) output->published = true;
        else if (output->checked.failed)
            native_log(request, true, "%s: the archive could not be written: %s",
                       request->archive_path,
                       strerror(output->checked.error ? output->checked.error : EIO));
        return complete ? 0 : 1;
    }
    output->open = false;
    if (output->stage) native_stage_mode(target, output->checked.inner);
    complete = xx_io_close(&output->checked.device) == 0 &&
        (!output->stage || native_checked_complete(&output->checked, output->stage));
    if (!complete) {
        native_log(request, true, "%s: the archive could not be written: %s",
                   request->archive_path,
                   strerror(output->checked.error ? output->checked.error : EIO));
        return 1;
    }
    if (!output->stage) { output->published = true; return 0; } /* A device or FIFO. */
#ifndef _WIN32
    {
        /* Another file (a link) put in the stage's place is not published. */
        struct stat info;
        if (lstat(output->stage, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_dev != output->stage_identity.st_dev ||
            info.st_ino != output->stage_identity.st_ino) error = NATIVE_STAGE_REPLACED;
        else error = native_publish_stage(output, target);
    }
#else
    /* MoveFileEx, replacing only the archive that was there. */
    error = xx_io_file_replace_a(output->stage, target->path, target->replaces) ? 0 :
        native_windows_errno();
#endif
    if (error) {
        native_log(request, true, "%s: cannot publish completed archive: %s",
                   request->archive_path,
                   error == NATIVE_STAGE_REPLACED ? "its staging file was replaced" :
                   error == EEXIST ? "another file was created at its name meanwhile, and is "
                   "left as it is" : strerror(error));
        return 1;
    }
    output->published = true;
    return 0;
}

/* A run's end: an unpublished stage is removed. */
static void native_close_output(native_output *output)
{
    if (output->open) (void)xx_io_close(&output->checked.device);
    if (output->final) (void)xx_io_close(output->final);
    output->open = false;
    output->final = NULL;
    if (output->stage && !output->published) (void)xx_io_file_remove_a(output->stage);
    free(output->stage);
    output->stage = NULL;
}

static bool native_method(native_format format, const char *name, uint64_t *id)
{
    static const struct { const char *name; uint64_t zip, seven; } methods[] = {
        {"store", 0U, 0U}, {"stored", 0U, 0U}, {"deflate", 8U, UINT64_C(0x040108)},
        {"deflate64", 9U, UINT64_C(0x040109)},
        {"bzip2", 12U, UINT64_C(0x040202)},
        {"lzma", 14U, UINT64_C(0x030101)},
        {"lzma1", 14U, UINT64_C(0x030101)},
        {"lzma2", UINT64_MAX, 0x21U}, {"xz", 95U, UINT64_MAX},
        {"zstd", 93U, UINT64_C(0x04F71101)},
        {"ppmd", 98U, UINT64_C(0x030401)},
        {"ppmd8", 98U, UINT64_C(0x030401)}
    };
    size_t i;
    if (!name) return true;
    if (format != NATIVE_ZIP && format != NATIVE_7ZIP) return false;
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (!strcmp(methods[i].name, name)) {
            *id = format == NATIVE_ZIP ? methods[i].zip : methods[i].seven;
            return *id != UINT64_MAX;
        }
    }
    return false;
}

static bool native_level_valid(native_format format, native_filter filter, int level)
{
    if (filter != FILTER_NONE) {
        if (filter == FILTER_ZSTD) return level >= -7 && level <= 22;
        if (filter == FILTER_BZIP2 || filter == FILTER_LZMA) return level >= 1 && level <= 9;
        return level >= 0 && level <= 9;
    }
    if (format == NATIVE_ZIP || format == NATIVE_7ZIP) return level >= 0 && level <= 9;
    return false;
}

static bool native_ascii_same(const char *text, size_t length, const char *word)
{
    size_t i;
    if (strlen(word) != length) return false;
    for (i = 0; i < length; ++i) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != word[i]) return false;
    }
    return true;
}

/* The length of a component's base (before its first '.') that names a Win32
 * device, as xxfclib's TAR and CPIO writers find them (COM and LPT also with
 * a UTF-8 superscript digit), or 0. */
static size_t native_device_name(const char *component, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    const unsigned char *bytes = (const unsigned char *)component;
    size_t base = 0U, i;
    while (base < length && component[base] != '.') ++base;
    for (i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (native_ascii_same(component, base, devices[i])) return base;
    if (base < 4U ||
        !(native_ascii_same(component, 3U, "COM") || native_ascii_same(component, 3U, "LPT")))
        return 0U;
    if (base == 4U && bytes[3] >= '1' && bytes[3] <= '9') return base;
    return base == 5U && bytes[3] == 0xC2U &&
        (bytes[4] == 0xB9U || bytes[4] == 0xB2U || bytes[4] == 0xB3U) ? base : 0U;
}

/* The rules of xxfclib's writers (xx_tar_record_name_utf8, cpio_safe_output_name,
 * x7w_text and x7w_name, wimw_component and the TAR and CPIO size fields),
 * checked before anything is written: they refuse without saying why. They
 * take '\' for a separator too, and so would store a POSIX name with a '\'
 * in it as folders: such a name is refused there. */
bool xfu_native_xxfclib_member_fits(const char *kind, const char *label,
                                    const xfu_native_member *member,
                                    char *reason, size_t capacity)
{
    bool tar = !strncmp(kind, "tar", 3U), cpio = !strcmp(kind, "cpio");
    bool wim = !strcmp(kind, "wim"), sevenzip = !strcmp(kind, "7z") || !strcmp(kind, "7zip");
    bool safe_names = tar || cpio; /* The Windows-safe names of TAR and CPIO. */
    bool separators = safe_names || wim || sevenzip, kept = false;
    const char *name = member->name;
    char problem[64];
    size_t length = strlen(name), end = length, start, at;
    problem[0] = '\0';
    if (!member->directory && ((tar && member->size > INT64_C(077777777777)) ||
                               (cpio && member->size > INT64_C(0xFFFFFFFF)))) {
        (void)snprintf(reason, capacity, "the file is %s or larger, which %s cannot store",
                       tar ? "8 GiB" : "4 GiB", label);
        return false;
    }
#ifndef _WIN32
    if ((separators || !strcmp(kind, "zip")) && strchr(name, '\\')) {
        (void)snprintf(reason, capacity, "the name contains '\\', which %s cannot store%s", label,
                       tar ? "; --format=pax stores it" : "");
        return false;
    }
#endif
    if (!separators) {
        /* The ZIP writer turns '\' into '/', and a final '/' marks a folder:
         * the file's data would be lost. */
        if (!strcmp(kind, "zip") && !member->directory && length && name[length - 1U] == '\\') {
            (void)snprintf(reason, capacity, "the name ends in '\\' as a folder's does, "
                           "which %s cannot store", label);
            return false;
        }
        return true;
    }
    for (at = 0; at < length && !problem[0]; ++at) {
        unsigned char c = (unsigned char)name[at];
        if (c == ':' || (safe_names && strchr("<>\"|?*", c)))
            (void)snprintf(problem, sizeof(problem), "contains '%c'", c);
        else if (safe_names && (c < 0x20U || (cpio && c == 0x7FU)))
            (void)snprintf(problem, sizeof(problem), "contains a control character");
    }
    /* A final separator marks a folder: WIM drops one, the others every one. */
    while (end && name[end - 1U] == '\\' && (end == length || !wim)) --end;
    if (!problem[0] && end < length && !member->directory)
        (void)snprintf(problem, sizeof(problem), "ends in '\\' as a folder's does");
    for (start = at = 0; !problem[0] && at <= end; ++at) {
        size_t size = at - start, device;
        if (at < end && name[at] != '/' && name[at] != '\\') continue;
        if (size == 2U && name[start] == '.' && name[start + 1U] == '.')
            (void)snprintf(problem, sizeof(problem), "has a \"..\" component");
        else if (!size || (size == 1U && name[start] == '.')) {
            /* CPIO drops these, but not every component. */
            if (!cpio || (at == end && !kept))
                (void)snprintf(problem, sizeof(problem), "has an empty or \".\" component");
        } else if (safe_names && (name[at - 1U] == '.' || name[at - 1U] == ' '))
            (void)snprintf(problem, sizeof(problem), "has a component ending in %s",
                           name[at - 1U] == '.' ? "'.'" : "a space");
        else if (safe_names && (device = native_device_name(name + start, size)) != 0U)
            (void)snprintf(problem, sizeof(problem), "has the Windows device name \"%.*s\"",
                           (int)device, name + start);
        else kept = true;
        start = at + 1U;
    }
    if (!problem[0]) return true;
    (void)snprintf(reason, capacity, "the name %s, which %s cannot store%s", problem, label,
                   tar ? "; --format=pax stores it" : "");
    return false;
}

/* A DOS date and time (local time, as ZIP stores them), clamped to the years
 * 1980 to 2107 that it holds. -1: clamped up to 1980; 1: down to 2107; 0:
 * stored as it is. A time the C library cannot convert (before 1970 on
 * Windows, or past its range) counts as before 1980 or after 2107. */
static int native_dos_time(int64_t seconds, uint16_t *date, uint16_t *time_of_day)
{
    time_t value = (time_t)seconds;
    struct tm local, *parts = NULL;
#ifdef _WIN32
    if ((int64_t)value == seconds && (parts = localtime(&value))) {
        local = *parts;
        parts = &local;
    }
#else
    if ((int64_t)value == seconds) parts = localtime_r(&value, &local);
#endif
    if (parts ? parts->tm_year < 80 : seconds < INT64_C(315532800)) {
        *date = (uint16_t)((1U << 5) | 1U);
        *time_of_day = 0U;
        return -1;
    }
    if (!parts || parts->tm_year > 207) {
        *date = (uint16_t)((127U << 9) | (12U << 5) | 31U);
        *time_of_day = (uint16_t)((23U << 11) | (59U << 5) | 29U);
        return 1;
    }
    *date = (uint16_t)(((unsigned)(parts->tm_year - 80) << 9) |
                       ((unsigned)(parts->tm_mon + 1) << 5) | (unsigned)parts->tm_mday);
    *time_of_day = (uint16_t)(((unsigned)parts->tm_hour << 11) |
                              ((unsigned)parts->tm_min << 5) | ((unsigned)parts->tm_sec / 2U));
    return 0;
}

/* The FILETIME seconds offset (1601 to 1970) and the last second a FILETIME
 * (100 ns units in 64 bits) holds. */
#define NATIVE_FILETIME_EPOCH INT64_C(11644473600)
#define NATIVE_FILETIME_LAST (INT64_C(1844674407370) - NATIVE_FILETIME_EPOCH)

/* What the xxfclib writer of kind keeps of a modification time: the earliest
 * and latest seconds it holds. false: kind stores no time. */
static bool native_time_range(const char *kind, int64_t *first, int64_t *last)
{
    if (!strncmp(kind, "tar", 3U) || !strcmp(kind, "cpio")) {
        /* ustar keeps 11 octal digits of time, newc 8 hex digits. */
        *first = 0;
        *last = !strcmp(kind, "cpio") ? INT64_C(0xFFFFFFFF) : INT64_C(077777777777);
        return true;
    }
    if (!strcmp(kind, "7z") || !strcmp(kind, "7zip")) {
        *first = -NATIVE_FILETIME_EPOCH;
        *last = NATIVE_FILETIME_LAST;
        return true;
    }
    return false;
}

/* The time in local time, as ZIP stores it; in UTC when the C library
 * cannot convert it. */
static void native_local_civil(int64_t seconds, xfu_native_time *civil)
{
    time_t value = (time_t)seconds;
    struct tm local, *parts = NULL;
#ifdef _WIN32
    if ((int64_t)value == seconds && (parts = localtime(&value))) {
        local = *parts;
        parts = &local;
    }
#else
    if ((int64_t)value == seconds) parts = localtime_r(&value, &local);
#endif
    if (!parts) {
        xfu_native_civil(seconds, civil);
        return;
    }
    civil->year = (int64_t)parts->tm_year + 1900;
    civil->month = (unsigned)parts->tm_mon + 1U;
    civil->day = (unsigned)parts->tm_mday;
    civil->hour = (unsigned)parts->tm_hour;
    civil->minute = (unsigned)parts->tm_min;
    civil->second = (unsigned)parts->tm_sec;
}

/* What the xxfclib TAR writers (ustar headers of 7 octal digits) store for
 * an owner or group id they cannot hold: 65534, nobody's, never 0 (root's). */
#define NATIVE_NOBODY_ID 65534U
#define NATIVE_USTAR_ID_LAST 07777777U

void xfu_native_record_warnings(const xfu_request *request, const char *kind,
                                const char *label, const xfu_native_member *member)
{
    int64_t first, last;
    if (!request || !kind || !member) return;
    if (!strncmp(kind, "tar", 3U) &&
        (member->uid > NATIVE_USTAR_ID_LAST || member->gid > NATIVE_USTAR_ID_LAST)) {
        bool owner = member->uid > NATIVE_USTAR_ID_LAST, group = member->gid > NATIVE_USTAR_ID_LAST;
        if (owner && group)
            xfu_native_warn(request, "%s: its owner and group ids (%lu, %lu) are larger than %s "
                            "can store; stored as %u (nobody); --format=pax stores them",
                            member->path, (unsigned long)member->uid,
                            (unsigned long)member->gid, label, NATIVE_NOBODY_ID);
        else
            xfu_native_warn(request, "%s: its %s id (%lu) is larger than %s can store; stored "
                            "as %u (nobody); --format=pax stores it", member->path,
                            owner ? "owner" : "group",
                            (unsigned long)(owner ? member->uid : member->gid), label,
                            NATIVE_NOBODY_ID);
    }
    if (!strcmp(kind, "zip")) {
        uint16_t date, time_of_day;
        int clamped = native_dos_time(member->mtime, &date, &time_of_day);
        /* The DOS limits, in local time as ZIP stores it: 1980-01-01 00:00:00
         * and 2107-12-31 23:59:58. */
        if (clamped) {
            xfu_native_time given, instead;
            native_local_civil(member->mtime, &given);
            memset(&instead, 0, sizeof(instead));
            instead.year = clamped < 0 ? 1980 : 2107;
            instead.month = clamped < 0 ? 1U : 12U;
            instead.day = clamped < 0 ? 1U : 31U;
            if (clamped > 0) {
                instead.hour = 23U;
                instead.minute = 59U;
                instead.second = 58U;
            }
            native_warn_civil(request, member->path, label, &given, &instead, clamped > 0, true);
        }
        return;
    }
    if (!native_time_range(kind, &first, &last)) return;
    if (member->mtime < first || member->mtime > last)
        xfu_native_warn_time(request, member->path, label, member->mtime,
                             member->mtime < first ? first : last,
                             strcmp(kind, "7z") && strcmp(kind, "7zip"));
}

bool xfu_native_record_metadata(xx_archive_record *record, const char *kind,
                                const xfu_native_member *member)
{
    const uint32_t mode = member->mode & 07777U;
    const uint32_t unix_mode = (member->directory ? 0040000U : 0100000U) | mode;
    /* FILE_ATTRIBUTE_DIRECTORY or _ARCHIVE, and _READONLY without owner write. */
    const uint32_t windows = (member->directory ? 0x10U : 0x20U) | ((mode & 0200U) ? 0U : 0x01U);
    int64_t first, last;
    if (!record || !kind || !member) return false;
    if (!strncmp(kind, "tar", 3U) || !strcmp(kind, "cpio")) {
        int64_t mtime = member->mtime;
        /* ustar's owner fields hold 7 octal digits (a larger id is stored as
         * nobody's); newc's hold 32 bits, every id. */
        const bool tar = !strncmp(kind, "tar", 3U);
        (void)native_time_range(kind, &first, &last);
        mtime = mtime < first ? first : mtime > last ? last : mtime;
        return xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, mode) &&
            xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, (uint64_t)mtime) &&
            xx_archive_record_set_meta_u64(record, XX_META_ID_OWNER_ID,
                                           tar && member->uid > NATIVE_USTAR_ID_LAST ?
                                               NATIVE_NOBODY_ID : member->uid) &&
            xx_archive_record_set_meta_u64(record, XX_META_ID_GROUP_ID,
                                           tar && member->gid > NATIVE_USTAR_ID_LAST ?
                                               NATIVE_NOBODY_ID : member->gid);
    }
    if (!strcmp(kind, "zip")) {
        uint16_t date, time_of_day;
        (void)native_dos_time(member->mtime, &date, &time_of_day);
        return xx_archive_record_set_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS,
                                              ((uint64_t)unix_mode << 16U) | windows) &&
#ifndef _WIN32
            /* Made on Unix: readers apply the mode in the attributes. */
            xx_archive_record_set_meta_u64(record, XX_META_ID_VERSION_MADE_BY, 3U << 8U) &&
#endif
            xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME, time_of_day) &&
            xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE, date);
    }
    if (!strcmp(kind, "7z") || !strcmp(kind, "7zip")) {
        /* FILETIME: 100 ns units since 1601. 7-Zip marks the Unix mode in the
         * high half with 0x8000, as it does on POSIX systems. */
        uint64_t attributes = windows;
#ifndef _WIN32
        attributes |= 0x8000U | ((uint64_t)unix_mode << 16U);
#endif
        if (!xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, attributes)) return false;
        (void)native_time_range(kind, &first, &last);
        if (member->mtime < first || member->mtime > last) return true;
        return xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                              (uint64_t)(member->mtime + NATIVE_FILETIME_EPOCH) *
                                                  UINT64_C(10000000));
    }
    return true;
}

static bool native_pack_records(const xfu_request *request, native_format kind,
                                const char *label, const native_source *sources,
                                const xfu_native_member *members, xx_io_device *output,
                                uint64_t method, bool have_method, xx_pd_struct *pd)
{
    xxfc_release_fn release = NULL;
    Abstractformat *format = NULL;
    xx_archive_write_state *state = NULL;
    const char *writer = kind == NATIVE_TAR ? "tar" : kind == NATIVE_CPIO ? "cpio" :
        kind == NATIVE_ZIP ? "zip" : "7zip";
    size_t i;
    bool success = false;
    for (i = 0; i < request->file_count; ++i) {
        char reason[256];
        xfu_native_member member;
        memset(&member, 0, sizeof(member));
        member.name = sources[i].wire_name ? sources[i].wire_name : sources[i].name;
        member.size = sources[i].size;
        member.directory = sources[i].directory;
        if (!xfu_native_xxfclib_member_fits(writer, label, &member, reason, sizeof(reason))) {
            native_log(request, true, "%s: %s", sources[i].path, reason);
            return false;
        }
    }
    format = xxfc_make_writer(writer, output, &release);
    if (!format) return false;
    if (request->password && !xx_format_set_password(format, request->password)) goto done;
    state = xx_format_create_archive_records_writing(format, NULL, pd);
    if (!state) goto done;
    success = true;
    for (i = 0; i < request->file_count; ++i) {
        const native_source *item = &sources[i];
        xx_io_device *source = NULL;
        xx_archive_record record;
        bool packed;
        if (native_cancelled(request, pd)) { success = false; break; }
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i,
                                        request->file_count, item->path);
        if (!item->directory && !(source = xfu_native_open_member(&members[i]))) {
            char why[128];
            native_log(request, true, "%s: %s", item->path,
                       xfu_native_open_failure(errno, why, sizeof(why)));
            success = false; break;
        }
        xfu_native_record_warnings(request, writer, label, &members[i]);
        xx_archive_record_init(&record);
        packed = xx_archive_record_set_original_name(&record,
                                                     item->wire_name ? item->wire_name : item->name) &&
            xx_archive_record_set_meta_bool(&record, XX_META_ID_IS_FOLDER, item->directory) &&
            xfu_native_record_metadata(&record, writer, &members[i]) &&
            (!have_method || item->directory ||
             xx_archive_record_set_meta_u64(&record, XX_META_ID_COMPRESSION_METHOD, method)) &&
            (!request->compression_level_set || item->directory ||
             xx_archive_record_set_meta_i64(&record, XX_META_ID_COMPRESSION_LEVEL,
                                            request->compression_level)) &&
            (!(request->password && kind == NATIVE_ZIP && !item->directory) ||
             xx_archive_record_set_meta_u64(&record, XX_META_ID_ENCRYPTION_METHOD,
                                            XX_ZIP_ENCRYPTION_AES_256)) &&
            xx_format_pack_archive_record(format, state, &record, source, pd);
        xx_archive_record_cleanup(&record);
        if (source && xx_io_close(source) != 0) packed = false;
        if (!packed) {
            /* A Cancel is logged once, by the caller. */
            if (!native_cancelled(request, pd))
                native_log(request, true, "%s: cannot pack member: %s", item->path,
                           pd && pd->error_string[0] ? pd->error_string :
                           "the writer failed (a read or write error, or one of its limits)");
            success = false; break;
        }
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i + 1U,
                                        request->file_count, NULL);
    }
    if (success && !native_cancelled(request, pd)) {
        success = xx_format_finalize_archive_records_writing(format, state, pd);
        if (!success && !native_cancelled(request, pd))
            native_log(request, true, "%s: cannot finish archive%s%s",
                                 request->archive_path,
                                 pd && pd->error_string[0] ? ": " : "",
                                 pd && pd->error_string[0] ? pd->error_string : "");
    } else success = false;
done:
    if (state) xx_format_free_archive_records_writing(format, state);
    release(format);
    return success;
}

#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_COMMON_WRITERS
static bool native_raw_input(const xfu_request *request, const xfu_native_member *item,
                             xx_io_device *output, xx_pd_struct *pd)
{
    xx_io_device *source;
    bool success;
    if (request->file_count != 1U || item->directory) return false;
    if (!(source = xfu_native_open_member(item))) {
        char why[128];
        native_log(request, true, "%s: %s", item->path,
                   xfu_native_open_failure(errno, why, sizeof(why)));
        return false;
    }
    success = native_copy(request, pd, source, output);
    if (xx_io_close(source) != 0) success = false;
    return success;
}

static bool native_stream_filter(const xfu_request *request, native_filter filter,
                                 xx_io_device *input, xx_io_device *output,
                                 const char *name, xx_pd_struct *pd)
{
    xxfc_release_fn release = NULL;
    Abstractformat *format;
    xx_archive_write_state *state;
    xx_archive_record record;
    bool success;
    const char *kind = filter == FILTER_GZIP ? "gz" :
        filter == FILTER_BZIP2 ? "bz2" : "xz";
    format = xxfc_make_writer(kind, output, &release);
    if (!format) return false;
    state = xx_format_create_archive_records_writing(format, NULL, pd);
    if (!state) { release(format); return false; }
    xx_archive_record_init(&record);
    success = (!name || xx_archive_record_set_original_name(&record, name)) &&
        (!request->compression_level_set ||
         xx_archive_record_set_meta_i64(&record, XX_META_ID_COMPRESSION_LEVEL,
                                        request->compression_level)) &&
        xx_format_pack_archive_record(format, state, &record, input, pd) &&
        xx_format_finalize_archive_records_writing(format, state, pd);
    xx_archive_record_cleanup(&record);
    xx_format_free_archive_records_writing(format, state);
    release(format);
    return success;
}

#if NATIVE_LZMA_FILTER
static bool native_lzma_filter(const xfu_request *request, xx_io_device *input,
                               xx_io_device *output, int64_t size, xx_pd_struct *pd)
{
    int level = request->compression_level_set ? request->compression_level : 6;
    return size >= 0 &&
        xx_lzma_pack_to_device(input, 0, size, output, level, pd);
}

#endif
#endif

/* The name a one-file stream carries (gzip's FNAME): a raw stream's input
 * file, without its folders. A filtered archive's stream stays unnamed. */
static const char *native_stream_name(native_format format, const xfu_native_member *members,
                                      size_t count)
{
    const char *name, *at;
    if (format != NATIVE_RAW || count != 1U || !members || !members[0].name) return NULL;
    name = members[0].name;
    for (at = name; *at; ++at)
        if (native_separator(*at)) name = at + 1;
    return *name ? name : NULL;
}

static bool native_apply_filter(const xfu_request *request, native_filter filter,
                                const char *filter_name, const char *stream_name,
                                xx_io_device *input, xx_io_device *output,
                                xx_pd_struct *pd)
{
#if !XFU_FORMAT_SUPPORT_FULL && !XFU_FORMAT_SUPPORT_COMMON_WRITERS
    (void)filter_name;
    (void)stream_name;
    if (filter != FILTER_NONE || xx_io_seek64(input, 0, SEEK_SET) != 0) return false;
    return native_copy(request, pd, input, output);
#else
    int64_t size = xx_io_total_size(input);
    int level = request->compression_level_set ? request->compression_level : 3;
    (void)level;
    (void)filter_name;
    (void)stream_name;
    if (size < 0 || xx_io_seek64(input, 0, SEEK_SET) != 0) return false;
    switch (filter) {
        case FILTER_NONE: return native_copy(request, pd, input, output);
        case FILTER_GZIP: case FILTER_BZIP2: case FILTER_XZ:
            return native_stream_filter(request, filter, input, output, stream_name, pd);
#ifdef XFU_SUPPORT_FORMAT_ZSTD
        case FILTER_ZSTD: return xx_zstd_pack_device(input, 0, size, output, level, pd);
#endif
#if NATIVE_LZMA_FILTER
        case FILTER_LZMA: return native_lzma_filter(request, input, output, size, pd);
#endif
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_EXTRA_FILTERS
        case FILTER_EXTRA: return xfu_native_filter_encode(request, filter_name, stream_name,
                                                           input, output, pd);
#endif
        default: return false;
    }
#endif
}

/* Formats with a member for a folder. ar, shar and WARC records hold file
 * data only, and a raw stream holds one file's bytes. */
static bool native_stores_folders(native_format format, const char *format_name)
{
#if !XFU_FORMAT_SUPPORT_FULL
#if NATIVE_EXTRA_WRITERS
    if (format == NATIVE_EXTRA) return xfu_native_extra_stores_folders(format_name);
#else
    (void)format_name;
#endif
    return format == NATIVE_ZIP || format == NATIVE_7ZIP ||
           format == NATIVE_CPIO || format == NATIVE_TAR_VARIANT;
#else
    if (format == NATIVE_RAW) return false;
    return format != NATIVE_EXTRA || xfu_native_extra_stores_folders(format_name);
#endif
}

/* ZIP and 7-Zip store names as UTF-8 and UTF-16; XAR's XML table, WARC's
 * target URI and mtree's specification (as xxfclib reads them) are UTF-8.
 * TAR, CPIO, ar and ISO 9660 store the bytes; a raw stream stores no name. */
static bool native_needs_utf8(native_format format, const char *format_name)
{
#if !XFU_FORMAT_SUPPORT_FULL
#if NATIVE_EXTRA_WRITERS
    if (format == NATIVE_EXTRA) return xfu_native_extra_needs_utf8(format_name);
#else
    (void)format_name;
#endif
    return format == NATIVE_ZIP || format == NATIVE_7ZIP || format == NATIVE_TAR_VARIANT;
#else
    return format == NATIVE_ZIP || format == NATIVE_7ZIP ||
        (format == NATIVE_EXTRA && xfu_native_extra_needs_utf8(format_name));
#endif
}

/* The writers' view of the collected members (borrowing their strings). */
static xfu_native_member *native_members(const native_list *list)
{
    xfu_native_member *members;
    size_t i;
    if (list->count > SIZE_MAX / sizeof(*members) ||
        !(members = (xfu_native_member *)malloc((list->count ? list->count : 1U) *
                                                sizeof(*members)))) return NULL;
    for (i = 0; i < list->count; ++i) {
        const native_source *item = &list->items[i];
        members[i].path = item->path;
        members[i].name = item->name;
        members[i].size = item->size;
        members[i].directory = item->directory;
        members[i].mtime = item->stamp;
        members[i].mode = item->mode;
        members[i].uid = item->uid;
        members[i].gid = item->gid;
        members[i].device = item->device;
        members[i].inode = item->inode;
        members[i].walked = item->walked;
    }
    return members;
}

int xfu_native_create(const xfu_request *request, const char *format_name,
                      const char *filter_name, xx_pd_struct *pd)
{
    native_format format = native_parse_format(format_name);
    native_filter filter = native_parse_filter(filter_name);
    native_list list = {0};
    native_watch watch = {0};
    native_target target = {0};
    native_output output;
    native_checked container;
    xfu_request expanded;
    const char **paths = NULL;
    xfu_native_member *members = NULL;
    xx_io_device *temporary, *archive = NULL;
    size_t i;
    uint64_t method = 0;
    bool cp437_names = false;
    bool success;
    int result = 2;
    memset(&output, 0, sizeof(output));
    if (!request || !request->archive_path || !pd) return 2;
    if (format == NATIVE_UNSUPPORTED || filter == FILTER_UNSUPPORTED) {
        native_log(request, true, "unsupported native writer format/filter: %s / %s",
                   format_name ? format_name : "(null)", filter_name ? filter_name : "none");
        return 2;
    }
    if (request->archive_options && request->archive_options[0]) {
        cp437_names = format == NATIVE_CPIO &&
            !strcmp(request->archive_options, "cpio:hdrcharset=CP437");
        if (!cp437_names) {
            native_log(request, true, "unsupported writer option: %s", request->archive_options);
            return 2;
        }
    }
    if (request->file_count > XFU_NATIVE_RECORD_LIMIT || (request->file_count && !request->files)) {
        native_log(request, true, "%s: the archive member limit is %u", request->archive_path,
                   XFU_NATIVE_RECORD_LIMIT);
        return 2;
    }
    if (format == NATIVE_RAW && request->file_count != 1U) {
        native_log(request, true, "a raw stream needs exactly one input file");
        return 2;
    }
    if (request->password && (format != NATIVE_ZIP && format != NATIVE_7ZIP)) {
        native_log(request, true, "password protection is available for ZIP and 7-Zip only");
        return 2;
    }
    if (request->compression_method &&
        !(filter != FILTER_NONE && !strcmp(request->compression_method, filter_name)) &&
        !native_method(format, request->compression_method, &method)) {
        native_log(request, true, "unsupported compression method for %s: %s",
                   format_name, request->compression_method);
        return 2;
    }
    if (request->compression_level_set &&
        !native_level_valid(format, filter, request->compression_level)) {
        native_log(request, true, "unsupported compression level %d", request->compression_level);
        return 2;
    }
    if (!native_find_target(request, &target)) return 2;
    /* The members: each input, and for a folder what it holds, walked before
     * anything is written. A folder input's member is the folder itself. */
    result = native_collect(request, format == NATIVE_RAW ? filter_name : format_name,
                            format == NATIVE_RAW ? NATIVE_FOLDERS_RAW :
                            native_stores_folders(format, format_name) ? NATIVE_FOLDERS_WALK :
                            NATIVE_FOLDERS_REFUSE,
                            native_needs_utf8(format, format_name), &list, pd);
    if (result) goto done;
    result = 2;
    for (i = 0; cp437_names && i < list.count; ++i) {
        if (!(list.items[i].wire_name = native_cp437_name(list.items[i].name))) {
            native_log(request, true, "%s: member name cannot be represented in CP437",
                       list.items[i].name);
            goto done;
        }
    }
    if (list.count > SIZE_MAX / sizeof(*paths) ||
        !(paths = (const char **)malloc((list.count ? list.count : 1U) * sizeof(*paths))) ||
        !(members = native_members(&list))) {
        native_log(request, true, "cannot allocate the input list");
        goto done;
    }
    for (i = 0; i < list.count; ++i) paths[i] = list.items[i].path;
    /* The writers see the members as the request's files. */
    expanded = native_watched(request, &watch);
    expanded.files = paths;
    expanded.file_count = list.count;
    if (native_cancelled(request, pd)) { result = 1; goto done; }
    /* Where the archive goes is settled before anything is compressed. */
    if (!native_open_output(request, &target, &output, pd)) goto done;
    errno = 0;
    if (!(temporary = xx_io_temp_open())) {
        native_log(request, true, "cannot create temporary archive stream: %s",
                   strerror(errno ? errno : EIO));
        goto done;
    }
    archive = native_checked_open(&container, temporary, pd, true);
#if XFU_FORMAT_SUPPORT_FULL || NATIVE_EXTRA_WRITERS
    if (format == NATIVE_EXTRA)
        success = xfu_native_extra_write(&expanded, format_name, members, archive, pd);
    else
#endif
#if XFU_FORMAT_SUPPORT_FULL || XFU_FORMAT_SUPPORT_COMMON_WRITERS
    if (format == NATIVE_RAW) success = native_raw_input(&expanded, members, archive, pd);
#ifdef XFU_SUPPORT_FORMAT_TAR
    else if (format == NATIVE_TAR_VARIANT)
        success = xfu_native_tar_write(&expanded, format_name, members, archive, pd);
#endif
    else
#endif
    success = native_pack_records(&expanded, format, format_name, list.items, members,
                                       archive, method, request->compression_method &&
                                       !(filter != FILTER_NONE &&
                                         !strcmp(request->compression_method, filter_name)), pd);
    if (!success || container.failed) {
        if (native_cancelled(request, pd)) result = 1;
        else if (!watch.reported || container.failed)
            native_log(request, true, "%s: cannot construct native archive%s%s",
                       request->archive_path, container.failed ? ": " : "",
                       container.failed ? pd->error_string : "");
        goto done;
    }
    if (native_cancelled(request, pd)) { result = 1; goto done; }
    success = native_apply_filter(request, filter, filter_name,
                                  native_stream_name(format, members, list.count), archive,
                                  &output.checked.device, pd);
    if (!success || container.failed || output.checked.failed || native_cancelled(request, pd)) {
        result = 1;
        if (!native_cancelled(request, pd))
            native_log(request, true, "%s: cannot finish native archive%s%s",
                       request->archive_path,
                       pd->error_string[0] ? ": " : "",
                       pd->error_string[0] ? pd->error_string : "");
        goto done;
    }
    if ((result = native_publish(request, &target, &output, pd)) != 0) goto done;
    native_watch_finish(&watch, true);
    native_log_members(request, &list, format_name);
done:
    native_watch_finish(&watch, false);
    if (result == 1 && native_cancelled(request, pd)) native_log(request, true, "operation cancelled");
    if (archive) xx_io_close(archive);
    native_close_output(&output);
    free(target.path);
    free(paths);
    free(members);
    native_list_free(&list);
    return result;
}

int xfu_native_create_with(const xfu_request *request, const char *label, unsigned flags,
                           xfu_native_member_writer writer, void *context, xx_pd_struct *pd)
{
    native_list list = {0};
    native_watch watch = {0};
    native_target target = {0};
    native_output output;
    xfu_request watched;
    xfu_native_member *members = NULL;
    bool success;
    int result = 2;
    memset(&output, 0, sizeof(output));
    if (!request || !request->archive_path || !label || !writer || !pd) return 2;
    if (request->file_count > XFU_NATIVE_RECORD_LIMIT || (request->file_count && !request->files)) {
        native_log(request, true, "%s: the archive member limit is %u", request->archive_path,
                   XFU_NATIVE_RECORD_LIMIT);
        return 2;
    }
    if (!native_find_target(request, &target)) return 2;
    result = native_collect(request, label,
                            (flags & XFU_NATIVE_WITH_STREAM) ? NATIVE_FOLDERS_STREAM :
                            (flags & XFU_NATIVE_WITH_FOLDERS) ? NATIVE_FOLDERS_WALK :
                            NATIVE_FOLDERS_REFUSE,
                            (flags & XFU_NATIVE_WITH_UTF8) != 0U, &list, pd);
    if (result) goto done;
    result = 2;
    if (!(members = native_members(&list))) {
        native_log(request, true, "cannot allocate the input list");
        goto done;
    }
    if (native_cancelled(request, pd)) { result = 1; goto done; }
    if (!native_open_output(request, &target, &output, pd)) goto done;
    /* The writer fills the stage; the destination stays as it was until the
     * stage is complete. */
    watched = native_watched(request, &watch);
    success = writer(context, &watched, members, list.count, &output.checked.device, pd);
    if (!success || native_cancelled(request, pd)) {
        if (native_cancelled(request, pd)) result = 1;
        else if (!watch.reported)
            native_log(request, true, "%s: cannot write %s archive%s%s", request->archive_path,
                       label, output.checked.failed ? ": " : "",
                       output.checked.failed ? strerror(output.checked.error ?
                                                        output.checked.error : EIO) : "");
        goto done;
    }
    if ((result = native_publish(request, &target, &output, pd)) != 0) goto done;
    native_watch_finish(&watch, true);
    native_log_members(request, &list, label);
done:
    native_watch_finish(&watch, false);
    if (result == 1 && native_cancelled(request, pd)) native_log(request, true, "operation cancelled");
    native_close_output(&output);
    free(target.path);
    free(members);
    native_list_free(&list);
    return result;
}

char *xfu_native_capabilities_text(void)
{
    static const struct { const char *name; bool read; } formats[] = {
#if XFU_FORMAT_SUPPORT_TAR_FILTERS
        {"tar", true}, {"pax", true}, {"pax_restricted", true},
        {"gnutar", true}, {"ustar", true}, {"v7tar", true},
        {"tar_solaris", true},
#elif XFU_FORMAT_SUPPORT_AR_ONLY
        {"ar", true}, {"ar_bsd", true}, {"ar_svr4", true}, {"ar_gnu", true},
#elif !XFU_FORMAT_SUPPORT_FULL
#define XX_SUPPORTED_FORMAT(type, name, label) {#name, true},
#include "xxfclib/formats/xx_format_support_rows.inc"
#undef XX_SUPPORTED_FORMAT
#if XFU_FORMAT_SUPPORT_RAW_STREAMS
        {"raw", false},
#endif
#else
        {"tar", true}, {"pax", true}, {"pax_restricted", true},
        {"gnutar", true}, {"ustar", true}, {"v7tar", true},
        {"tar_solaris", true}, {"cpio", true}, {"cpio_newc", true},
        {"cpio_odc", true}, {"cpio_bin", true}, {"cpio_bin_le", true},
        {"cpio_bin_be", true}, {"cpio_crc", true},
        {"cpio_afio", true}, {"cpio_pwb", true},
        {"ar", true}, {"ar_bsd", true}, {"ar_svr4", true},
        {"mtree", true}, {"mtree_classic", true},
        {"iso9660", true}, {"shar", true}, {"shar_dump", true},
        {"zip", true}, {"7zip", true}, {"warc", true}, {"xar", true},
        {"raw", true}, {"cab", true}, {"lha", true}, {"rar", true},
        {"rar5", true}, {"rpm", true}, {"empty", true}
#endif
    };
    static const struct { const char *name; bool read; } filters[] = {
#if !XFU_FORMAT_SUPPORT_FULL
        {"none", true},
#ifdef XFU_SUPPORT_FORMAT_GZ
        {"gzip", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_BZ2
        {"bzip2", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_XZ
        {"xz", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_ZSTD
        {"zstd", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_UNIX_COMPRESS
        {"compress", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZMA
        {"lzma", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZIP
        {"lzip", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZ4
        {"lz4", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZOP
        {"lzop", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_UUE
        {"uuencode", true},
#endif
#ifdef XFU_SUPPORT_FORMAT_BASE64
        {"b64encode", true},
#endif
#else
        {"none", true}, {"gzip", true}, {"bzip2", true},
        {"compress", true}, {"lzma", true}, {"xz", true},
        {"lzip", true}, {"lz4", true}, {"lzop", true},
        {"zstd", true}, {"uuencode", true}, {"b64encode", true},
        {"rpm", true}, {"lrzip", false}, {"grzip", false}
#endif
    };
    size_t i, used = 0U;
    size_t capacity = 8192U;
    char *text = (char *)malloc(capacity);
    int amount;
    if (!text) return NULL;
    amount = snprintf(text, capacity, "backend native xxfclib\n");
    if (amount < 0 || (size_t)amount >= capacity) { free(text); return NULL; }
    used = (size_t)amount;
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        if (i && !strcmp(formats[i].name, formats[i - 1U].name)) continue;
        amount = snprintf(text + used, capacity - used,
                          "format %s read=%s create=%s\n", formats[i].name,
                          formats[i].read ? "yes" : "no",
                          xfu_native_can_create_format(formats[i].name) ? "yes" : "no");
        if (amount < 0 || (size_t)amount >= capacity - used) { free(text); return NULL; }
        used += (size_t)amount;
    }
    for (i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
        amount = snprintf(text + used, capacity - used,
                          "filter %s read=%s create=%s\n", filters[i].name,
                          filters[i].read ? "yes" : "no",
                          xfu_native_can_create_filter(filters[i].name) ? "yes" : "no");
        if (amount < 0 || (size_t)amount >= capacity - used) { free(text); return NULL; }
        used += (size_t)amount;
    }
    return text;
}

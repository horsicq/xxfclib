/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "pea_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/algo/crc/xx_crc.h>
#include <xxfclib/algo/deflate/xx_deflate.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define PEA_BLOCK_SIZE (1024U * 1024U)
#define PEA_IO_CHUNK 65536U
#define PEA_MAX_FILES 1048576U

typedef struct pea_source {
    char *path;
    const char *basename;
    int64_t size;
    uint32_t date_time;
    uint32_t attributes;
} pea_source;

typedef struct pea_output {
    const xfu_request *request;
    xx_pd_struct *pd;
    xx_io_device *device;
    uint32_t stream_crc;
    uint32_t volume_crc;
} pea_output;

/* A line longer than the local buffer (a long path) is formatted again in
 * a heap buffer instead of being cut. */
static void pea_log(const xfu_request *request, bool error, const char *format, ...)
{
    char local[2048], *message = local;
    va_list args, copy;
    int count;
    if (!request->callbacks.log) return;
    va_start(args, format);
    va_copy(copy, args);
    count = vsnprintf(local, sizeof(local), format, args);
    va_end(args);
    if (count >= (int)sizeof(local)) {
        message = (char *)malloc((size_t)count + 1U);
        if (message) vsnprintf(message, (size_t)count + 1U, format, copy);
        else message = local;
    }
    va_end(copy);
    request->callbacks.log(request->callbacks.user, error, message);
    if (message != local) free(message);
}

static bool pea_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled && request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static void pea_u32(unsigned char *bytes, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8U * i));
}

static void pea_u64(unsigned char *bytes, uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (8U * i));
}

static bool pea_write(pea_output *output, const unsigned char *data, size_t size, bool stream, bool volume)
{
    while (size) {
        size_t chunk = size > PEA_IO_CHUNK ? PEA_IO_CHUNK : size;
        ssize_t count;
        if (pea_cancelled(output->request, output->pd)) return false;
        count = xx_io_write(output->device, data, chunk);
        if (count <= 0 || (size_t)count > chunk) return false;
        if (stream) output->stream_crc = xx_crc32_calc(output->stream_crc, data, (size_t)count);
        if (volume) output->volume_crc = xx_crc32_calc(output->volume_crc, data, (size_t)count);
        data += count;
        size -= (size_t)count;
    }
    return true;
}

static uint32_t pea_dos_time(int year, int month, int day, int hour, int minute, int second)
{
    if (year < 1980) return 0x00210000U;
    if (year > 2107) return 0xFF9FBF7DU;
    return ((uint32_t)(year - 1980) << 25) | ((uint32_t)month << 21) | ((uint32_t)day << 16) | ((uint32_t)hour << 11) | ((uint32_t)minute << 5) | (uint32_t)(second / 2);
}

static bool pea_file_info(const char *path, pea_source *source)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path);
    WIN32_FILE_ATTRIBUTE_DATA info = {0};
    FILETIME local;
    SYSTEMTIME date;
    bool success =
        wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    xx_str_wfree(wide);
    if (!success || info.nFileSizeHigh > INT32_MAX) return false;
    source->size = (int64_t)(((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    source->attributes = info.dwFileAttributes & 0x27U;
    source->date_time = 0;
    if (FileTimeToLocalFileTime(&info.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &date))
        source->date_time = pea_dos_time(date.wYear, date.wMonth, date.wDay, date.wHour, date.wMinute, date.wSecond);
    return true;
#else
    struct stat info;
    struct tm date;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) return false;
    source->size = (int64_t)info.st_size;
    source->attributes = (info.st_mode & S_IWUSR) ? 0x20U : 0x21U;
    source->date_time = 0;
    if (localtime_r(&info.st_mtime, &date)) source->date_time = pea_dos_time(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min, date.tm_sec);
    return true;
#endif
}

static char *pea_full_path(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path), *full = NULL;
    char *converted = NULL, *result = NULL;
    DWORD length = wide ? GetFullPathNameW(wide, 0, NULL, NULL) : 0;
    if (length && length <= 32768U) {
        DWORD received;
        full = (wchar_t *)malloc((size_t)length * sizeof(*full));
        received = full ? GetFullPathNameW(wide, length, full, NULL) : 0;
        if (received && received < length) converted = xx_str_unicode_to_utf8(full);
    }
    if (converted) {
        size_t size = strlen(converted) + 1;
        result = (char *)malloc(size);
        if (result) memcpy(result, converted, size);
    }
    xx_str_free(converted);
    xx_str_wfree(wide);
    free(full);
    return result;
#else
    return realpath(path, NULL);
#endif
}

static bool pea_same_file(const char *left, const char *right)
{
#ifdef _WIN32
    wchar_t *a = xx_str_utf8_to_unicode(left), *b = xx_str_utf8_to_unicode(right);
    HANDLE ah = INVALID_HANDLE_VALUE, bh = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION ai, bi;
    bool same = false;
    if (a && b) {
        ah = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bh = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        same = ah != INVALID_HANDLE_VALUE && bh != INVALID_HANDLE_VALUE && GetFileInformationByHandle(ah, &ai) && GetFileInformationByHandle(bh, &bi) &&
               ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber && ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
    }
    if (ah != INVALID_HANDLE_VALUE) CloseHandle(ah);
    if (bh != INVALID_HANDLE_VALUE) CloseHandle(bh);
    xx_str_wfree(a);
    xx_str_wfree(b);
    return same;
#else
    struct stat a, b;
    return (stat(left, &a) == 0 && stat(right, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) || !strcmp(left, right);
#endif
}

static int pea_name_compare(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return *a == *b ? 0 : (*a ? 1 : -1);
}

static int pea_source_compare(const void *a, const void *b)
{
    const pea_source *const *left = (const pea_source *const *)a;
    const pea_source *const *right = (const pea_source *const *)b;
    return pea_name_compare((*left)->basename, (*right)->basename);
}

static bool pea_safe_basename(const char *name)
{
    size_t length = strlen(name), stem;
    const unsigned char *at = (const unsigned char *)name;
    char reserved[5] = {0};
    if (!length || name[length - 1] == '.' || name[length - 1] == ' ') return false;
    for (; *at; ++at)
        if (*at < 32 || *at == 127 || strchr("<>:\"/\\|?*", *at)) return false;
    for (stem = 0; stem < 4 && name[stem] && name[stem] != '.'; ++stem) {
        unsigned char c = (unsigned char)name[stem];
        reserved[stem] = (char)(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
    }
    if (stem == 3 &&
        (!pea_name_compare(reserved, "CON") || !pea_name_compare(reserved, "PRN") || !pea_name_compare(reserved, "AUX") || !pea_name_compare(reserved, "NUL")))
        return false;
    if (stem == 4 && (!name[stem] || name[stem] == '.') && (!memcmp(reserved, "COM", 3) || !memcmp(reserved, "LPT", 3)) && reserved[3] >= '1' && reserved[3] <= '9')
        return false;
    return true;
}

static xx_io_device *pea_stage(const char *destination, char **stage, const void *nonce)
{
    size_t length = strlen(destination);
    char *name;
    unsigned attempt;
    *stage = NULL;
    if (length > SIZE_MAX - 96) return NULL;
    name = (char *)malloc(length + 96);
    if (!name) return NULL;
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *output;
        snprintf(name, length + 96, "%s.xfu-%p-%lu-%u.tmp", destination, nonce, (unsigned long)time(NULL), attempt);
        output = xx_io_file_open(name, "wbx");
        if (output) {
            *stage = name;
            return output;
        }
    }
    free(name);
    return NULL;
}

static bool pea_read_block(const xfu_request *request, xx_pd_struct *pd, xx_io_device *source, unsigned char *buffer, size_t size)
{
    size_t done = 0;
    while (done < size) {
        size_t wanted = size - done > PEA_IO_CHUNK ? PEA_IO_CHUNK : size - done;
        ssize_t count;
        if (pea_cancelled(request, pd)) return false;
        count = xx_io_read(source, buffer + done, wanted);
        if (count <= 0 || (size_t)count > wanted) return false;
        done += (size_t)count;
    }
    return true;
}

int xfu_pea_create(const xfu_request *request, xx_pd_struct *pd)
{
    pea_source *sources = NULL;
    pea_source **ordered = NULL;
    xx_io_device *source = NULL;
    pea_output output = {0};
    unsigned char *plain = NULL, *packed = NULL, *header = NULL;
    char *stage = NULL;
    const char *failure = NULL;
    size_t i;
    size_t packed_capacity = xx_zlib_stream_compress_bound(PEA_BLOCK_SIZE);
    int quality, result = 2, level = -1, member_level = -1;
    static const int zlib_levels[4] = {0, 3, 6, 9};
    unsigned char archive_header[20] = {0xEA, 1, 0, 2, 0, 0, 0x10, 1, 0, 0, 0, 0, 'P', 'O', 'D', 0, 0, 0, 2, 2};
    if (!request || !request->archive_path) return 2;
    if (!request->files || !request->file_count || request->file_count > PEA_MAX_FILES || request->file_count > SIZE_MAX / sizeof(*sources)) {
        pea_log(request, true, "PEA creation requires regular input files");
        return 2;
    }
    quality = request->compression_level_set ? request->compression_level : 2;
    if (request->compression_method && strcmp(request->compression_method, "pea") && strcmp(request->compression_method, "zlib") &&
        strcmp(request->compression_method, "deflate")) {
        if (strcmp(request->compression_method, "store") && strcmp(request->compression_method, "stored")) {
            pea_log(request, true, "PEA supports the pea, zlib, deflate and store compression methods");
            return 2;
        }
        if (request->compression_level_set && quality != 0) {
            pea_log(request, true, "PEA store compression requires level 0");
            return 2;
        }
        quality = 0;
    }
    if (quality < 0 || quality > 3) {
        pea_log(request, true, "PEA compression level must be between 0 and 3");
        return 2;
    }
    if (request->password || (request->archive_filter && strcmp(request->archive_filter, "none")) || (request->archive_options && request->archive_options[0])) {
        pea_log(request, true, "PEA writer does not support passwords, outer filters or archive options");
        return 2;
    }
    sources = (pea_source *)calloc(request->file_count, sizeof(*sources));
    ordered = (pea_source **)malloc(request->file_count * sizeof(*ordered));
    plain = (unsigned char *)malloc(PEA_BLOCK_SIZE);
    packed = quality ? (unsigned char *)malloc(packed_capacity) : NULL;
    if (!sources || !ordered || !plain || (quality && !packed)) {
        failure = "cannot allocate PEA buffers";
        goto done;
    }
    for (i = 0; i < request->file_count; ++i) {
        const char *at;
        if (pea_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        if (!request->files[i] || !pea_file_info(request->files[i], &sources[i]) || !(sources[i].path = pea_full_path(request->files[i]))) {
            failure = "input must be a readable regular file, not a link or directory";
            goto done;
        }
        if (pea_same_file(request->archive_path, sources[i].path)) {
            failure = "cannot add the archive to itself";
            goto done;
        }
        sources[i].basename = sources[i].path;
#ifndef _WIN32
        if (strchr(sources[i].path, '\\')) {
            failure = "backslashes in source names cannot be represented safely in PEA";
            goto done;
        }
#endif
        for (at = sources[i].path; *at; ++at)
            if (*at == '/' || *at == '\\') sources[i].basename = at + 1;
        if (strlen(sources[i].path) > 32767U || !pea_safe_basename(sources[i].basename)) {
            failure = "input filename cannot be represented safely in PEA";
            goto done;
        }
        ordered[i] = &sources[i];
        source = xx_io_file_open(sources[i].path, "rb");
        if (!source || xx_io_total_size(source) != sources[i].size) {
            failure = "cannot open input file";
            goto done;
        }
        if (xx_io_close(source) != 0) {
            source = NULL;
            failure = "cannot close input file";
            goto done;
        }
        source = NULL;
    }
    qsort(ordered, request->file_count, sizeof(*ordered), pea_source_compare);
    for (i = 1; i < request->file_count; ++i) {
        if (pea_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        if (!pea_name_compare(ordered[i - 1]->basename, ordered[i]->basename)) {
            failure = "PEA flat input filenames must have unique basenames";
            goto done;
        }
    }
    free(ordered);
    ordered = NULL;
    if (pea_cancelled(request, pd)) {
        result = 1;
        goto done;
    }
    if (!xx_io_create_dirs_a(request->archive_path, false) || !(output.device = pea_stage(request->archive_path, &stage, sources))) {
        failure = "cannot create archive staging file";
        goto done;
    }
    output.request = request;
    output.pd = pd;
    output.stream_crc = 0U;
    output.volume_crc = 0U;
    archive_header[16] = (unsigned char)quality;
#ifdef _WIN64
    archive_header[5] = 0x12;
#elif defined(_WIN32)
    archive_header[5] = 0x11;
#elif defined(__linux__)
    archive_header[5] = 0x33;
#elif defined(__APPLE__)
    archive_header[5] = 0x37;
#else
    archive_header[5] = 0x30;
#endif
#if defined(_M_X64) || defined(__x86_64__)
    archive_header[8] = 0x21;
#elif defined(_M_IX86) || defined(__i386__)
    archive_header[8] = 0x13;
#endif
    level = xx_pd_enter_level(pd, request->file_count, "creating PEA archive");
    result = 1;
    if (!pea_write(&output, archive_header, sizeof(archive_header), true, true)) {
        failure = "cannot write archive header";
        goto done;
    }
    if (quality) {
        unsigned char size[4];
        pea_u32(size, PEA_BLOCK_SIZE);
        if (!pea_write(&output, size, sizeof(size), true, true)) {
            failure = "cannot write stream header";
            goto done;
        }
    }
    for (i = 0; i < request->file_count; ++i) {
        pea_source current = {0};
        size_t name_size = strlen(sources[i].path), header_size = name_size + 18;
        uint64_t position = 0;
        uint32_t last = 0;
        uint32_t object_crc = 0U;
        unsigned char field[4];
        if (pea_cancelled(request, pd)) goto done;
        if (!pea_file_info(sources[i].path, &current) || current.size != sources[i].size || !(source = xx_io_file_open(sources[i].path, "rb")) ||
            xx_io_total_size(source) != sources[i].size) {
            failure = "input file changed during compression";
            goto done;
        }
        header = (unsigned char *)malloc(header_size);
        if (!header) {
            failure = "cannot allocate object header";
            goto done;
        }
        header[0] = (unsigned char)name_size;
        header[1] = (unsigned char)(name_size >> 8);
        memcpy(header + 2, sources[i].path, name_size);
        pea_u32(header + 2 + name_size, sources[i].date_time);
        pea_u32(header + 6 + name_size, sources[i].attributes);
        pea_u64(header + 10 + name_size, (uint64_t)sources[i].size);
        object_crc = xx_crc32_calc(object_crc, header, header_size);
        if (!pea_write(&output, header, header_size, true, true)) {
            failure = "cannot write object header";
            goto done;
        }
        free(header);
        header = NULL;
        member_level = xx_pd_enter_level(pd, (uint64_t)sources[i].size, request->files[i]);
        if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i, request->file_count, request->files[i]);
        while (position < (uint64_t)sources[i].size) {
            uint64_t remaining = (uint64_t)sources[i].size - position;
            size_t block = remaining > PEA_BLOCK_SIZE ? PEA_BLOCK_SIZE : (size_t)remaining;
            const unsigned char *wire = plain;
            size_t wire_size = block;
            if (!pea_read_block(request, pd, source, plain, block)) {
                failure = "cannot read complete input file";
                goto done;
            }
            if (quality) {
                wire_size = packed_capacity;
                if (!xx_zlib_stream_encode_memory(plain, block, packed, packed_capacity, &wire_size, zlib_levels[quality])) {
                    failure = "PEA zlib encoding failed";
                    goto done;
                }
                if (wire_size < block) wire = packed;
                else wire_size = block;
                pea_u32(field, (uint32_t)wire_size);
                object_crc = xx_crc32_calc(object_crc, field, sizeof(field));
                if (!pea_write(&output, field, sizeof(field), true, true)) {
                    failure = "cannot write compressed block size";
                    goto done;
                }
            }
            object_crc = xx_crc32_calc(object_crc, plain, block);
            if (!pea_write(&output, wire, (size_t)wire_size, true, true)) {
                failure = "cannot write object data";
                goto done;
            }
            position += block;
            last = (uint32_t)block;
            xx_pd_set_current(pd, member_level, position);
            if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i, request->file_count, request->files[i]);
        }
        if (xx_io_read(source, plain, 1) != 0 || xx_io_total_size(source) != sources[i].size) {
            failure = "input file changed during compression";
            goto done;
        }
        if (xx_io_close(source) != 0) {
            source = NULL;
            failure = "cannot close input file";
            goto done;
        }
        source = NULL;
        if (quality && position) {
            pea_u32(field, last);
            object_crc = xx_crc32_calc(object_crc, field, sizeof(field));
            if (!pea_write(&output, field, sizeof(field), true, true)) {
                failure = "cannot write final block size";
                goto done;
            }
        }
        pea_u32(field, (uint32_t)object_crc);
        if (!pea_write(&output, field, sizeof(field), true, true)) {
            failure = "cannot write object checksum";
            goto done;
        }
        xx_pd_leave_level(pd, member_level);
        member_level = -1;
        xx_pd_set_current(pd, level, i + 1);
        if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i + 1, request->file_count, NULL);
    }
    {
        const unsigned char end[6] = {0, 0, 'E', 'O', 'A', 0};
        unsigned char crc[4];
        if (!pea_write(&output, end, sizeof(end), true, true)) {
            failure = "cannot write end marker";
            goto done;
        }
        pea_u32(crc, (uint32_t)output.stream_crc);
        if (!pea_write(&output, crc, sizeof(crc), false, true)) {
            failure = "cannot write stream checksum";
            goto done;
        }
        pea_u32(crc, (uint32_t)output.volume_crc);
        if (!pea_write(&output, crc, sizeof(crc), false, false)) {
            failure = "cannot write volume checksum";
            goto done;
        }
    }
    if (xx_io_close(output.device) != 0) {
        output.device = NULL;
        failure = "cannot close archive";
        goto done;
    }
    output.device = NULL;
    if (pea_cancelled(request, pd)) goto done;
    if (!xx_io_file_replace_a(stage, request->archive_path, true)) {
        failure = "cannot publish completed archive";
        goto done;
    }
    result = 0;
    pea_log(request, false, "%s: PEA archive created", request->archive_path);
done:
    if (source) xx_io_close(source);
    if (output.device) xx_io_close(output.device);
    if (stage && result) (void)xx_io_file_remove_a(stage);
    if (sources) {
        for (i = 0; i < request->file_count; ++i) free(sources[i].path);
        free(sources);
    }
    free(stage);
    free(plain);
    free(packed);
    free(header);
    free(ordered);
    xx_pd_leave_level(pd, member_level);
    xx_pd_leave_level(pd, level);
    if (result && pea_cancelled(request, pd)) pea_log(request, true, "operation cancelled");
    else if (failure) pea_log(request, true, "%s: %s", request->archive_path, failure);
    return result;
}

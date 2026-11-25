/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "freearc_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/algo/crc/xx_crc.h>
#include <xxfclib/algo/lzma/xx_lzma.h>
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

#define FW_BUFFER_SIZE 65536U
#define FW_MAX_FILE ((int64_t)256 * 1024 * 1024)
#define FW_MAX_METADATA ((size_t)64 * 1024 * 1024)
#define FW_MAX_FILES 100000U
#define FW_MAX_NAME 32768U
#define FW_MAX_DICTIONARY (16U * 1024U * 1024U)
#define FW_MAX_ENCODER_MEMORY (UINT64_C(256) * 1024U * 1024U)

typedef struct fw_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} fw_buffer;

typedef struct fw_file {
    char *name;
    char *directory;
    const char *basename;
    int64_t size;
    int64_t offset;
    int64_t packed;
    uint32_t crc;
    char method[96];
} fw_file;

/* A line longer than the local buffer (a long path) is formatted again in
 * a heap buffer instead of being cut. */
static void fw_log(const xfu_request *request, bool error, const char *format, ...)
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

static bool fw_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled && request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static bool fw_regular(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    xx_str_wfree(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

static bool fw_same_file(const char *left, const char *right)
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

static xx_io_device *fw_stage(const char *path, char **stage, const void *nonce)
{
    size_t length = strlen(path);
    unsigned attempt;
    char *name;
    *stage = NULL;
    if (length > SIZE_MAX - 96U) return NULL;
    name = (char *)malloc(length + 96U);
    if (!name) return NULL;
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *output;
        snprintf(name, length + 96U, "%s.xfu-%p-%lu-%u.tmp", path, nonce, (unsigned long)time(NULL), attempt);
        output = xx_io_file_open(name, "wbx");
        if (output) {
            *stage = name;
            return output;
        }
    }
    free(name);
    return NULL;
}

static bool fw_write(const xfu_request *request, xx_pd_struct *pd, xx_io_device *output, const void *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        size_t wanted = size - done > FW_BUFFER_SIZE ? FW_BUFFER_SIZE : size - done;
        ssize_t count;
        if (fw_cancelled(request, pd)) return false;
        count = xx_io_write(output, (const unsigned char *)data + done, wanted);
        if (count <= 0 || (size_t)count > wanted) return false;
        done += (size_t)count;
    }
    return true;
}

static bool fw_bytes(fw_buffer *buffer, const void *data, size_t size)
{
    size_t capacity;
    unsigned char *grown;
    if (size > FW_MAX_METADATA - buffer->size) return false;
    if (buffer->size + size > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256U;
        while (capacity < buffer->size + size) {
            if (capacity > FW_MAX_METADATA / 2U) {
                capacity = FW_MAX_METADATA;
                break;
            }
            capacity *= 2U;
        }
        grown = (unsigned char *)realloc(buffer->data, capacity);
        if (!grown) return false;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (size) memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return true;
}

static bool fw_u32(fw_buffer *buffer, uint32_t value)
{
    unsigned char bytes[4];
    unsigned i;
    for (i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (8U * i));
    return fw_bytes(buffer, bytes, sizeof(bytes));
}

/* FreeArc's low-one-bit prefix gives seven value bits per encoded byte. */
static bool fw_num(fw_buffer *buffer, uint64_t value)
{
    unsigned char bytes[9];
    unsigned extra = 0, i;
    uint64_t encoded;
    while (extra < 8U && value >= ((uint64_t)1 << (7U * (extra + 1U)))) ++extra;
    if (extra == 8U) {
        bytes[0] = 255;
        for (i = 0; i < 8; ++i) bytes[i + 1U] = (unsigned char)(value >> (8U * i));
        return fw_bytes(buffer, bytes, sizeof(bytes));
    }
    encoded = (value << (extra + 1U)) | (((uint64_t)1 << extra) - 1U);
    for (i = 0; i <= extra; ++i) bytes[i] = (unsigned char)(encoded >> (8U * i));
    return fw_bytes(buffer, bytes, (size_t)extra + 1U);
}

static bool fw_string(fw_buffer *buffer, const char *value)
{
    return fw_bytes(buffer, value, strlen(value) + 1U);
}

static bool fw_descriptor(const xfu_request *request, xx_pd_struct *pd, xx_io_device *output, unsigned type, const fw_buffer *data, uint32_t *crc)
{
    fw_buffer descriptor = {0};
    bool success;
    *crc = xx_crc32_calc(0, data->data, data->size);
    success = fw_bytes(&descriptor, "ArC\1", 4) && fw_num(&descriptor, type) && fw_string(&descriptor, "storing") && fw_num(&descriptor, data->size) &&
              fw_num(&descriptor, data->size) && fw_u32(&descriptor, *crc);
    if (success) success = fw_u32(&descriptor, xx_crc32_calc(0, descriptor.data, descriptor.size));
    if (success) success = fw_write(request, pd, output, data->data, data->size) && fw_write(request, pd, output, descriptor.data, descriptor.size);
    free(descriptor.data);
    return success;
}

static char *fw_member_name(const char *path)
{
    const char *start = path, *last = path, *p;
    bool basename = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    size_t length, at = 0, out = 0;
    char *name;
    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
        if (*p == '.' && p[1] == '.' && (p == path || p[-1] == '/' || p[-1] == '\\') && (!p[2] || p[2] == '/' || p[2] == '\\')) basename = true;
    }
    if (basename) start = last;
    length = strlen(start);
    if (!length || length >= FW_MAX_NAME - 1U) return NULL;
    name = (char *)malloc(length + 1U);
    if (!name) return NULL;
    while (at < length) {
        size_t component, count, i;
        while (at < length && (start[at] == '/' || start[at] == '\\')) ++at;
        component = at;
        while (at < length && start[at] != '/' && start[at] != '\\') ++at;
        count = at - component;
        if (!count || (count == 1 && start[component] == '.')) continue;
        if (out) name[out++] = '/';
        for (i = 0; i < count; ++i) {
            unsigned char c = (unsigned char)start[component + i];
            if (c < 32 || c == 127 || strchr(":*?\"<>|", c)) {
                free(name);
                return NULL;
            }
            name[out++] = (char)c;
        }
    }
    if (!out) {
        free(name);
        return NULL;
    }
    name[out] = 0;
    return name;
}

static int fw_name_compare(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static bool fw_lzma(const xfu_request *request, xx_pd_struct *pd, xx_io_device *source, xx_io_device *output, fw_file *file, unsigned char *input, int level,
                    unsigned quality, size_t index)
{
    uint8_t properties[XX_LZMA_PROPS_SIZE];
    size_t properties_size = sizeof(properties);
    int64_t unpacked_size = 0, packed_size = 0;
    uint32_t dictionary, crc = 0;
    (void)input;
    if (fw_cancelled(request, pd) || !xx_lzma_pack_source(source, NULL, &unpacked_size, &packed_size, &crc, output, (int)quality, properties, &properties_size, pd) ||
        unpacked_size != file->size || properties_size != XX_LZMA_PROPS_SIZE)
        return false;
    dictionary = (uint32_t)properties[1] | ((uint32_t)properties[2] << 8U) | ((uint32_t)properties[3] << 16U) | ((uint32_t)properties[4] << 24U);
    if (!dictionary || dictionary > FW_MAX_DICTIONARY || packed_size < 0) return false;
    file->crc = crc;
    snprintf(file->method, sizeof(file->method), "lzma:%ukb:lc3:lp0:pb2", dictionary / 1024U);
    xx_pd_set_current(pd, level, (uint64_t)file->size);
    if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, index, request->file_count, file->name);
    return !fw_cancelled(request, pd);
}

static bool fw_directory(fw_buffer *directory, const fw_file *files, size_t count, int64_t offset)
{
    size_t i;
    unsigned char zero = 0;
    if (!fw_num(directory, count)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_num(directory, 1)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_string(directory, files[i].method)) return false;
    for (i = 0; i < count; ++i)
        if (files[i].offset > offset || !fw_num(directory, (uint64_t)(offset - files[i].offset))) return false;
    for (i = 0; i < count; ++i)
        if (!fw_num(directory, (uint64_t)files[i].packed)) return false;
    if (!fw_num(directory, count)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_string(directory, files[i].directory)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_string(directory, files[i].basename)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_num(directory, i)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_num(directory, (uint64_t)files[i].size)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_u32(directory, 0)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_bytes(directory, &zero, 1)) return false;
    for (i = 0; i < count; ++i)
        if (!fw_u32(directory, files[i].crc)) return false;
    return fw_num(directory, 0);
}

static bool fw_footer_block(fw_buffer *footer, unsigned type, int64_t offset, int64_t footer_offset, size_t size, uint32_t crc)
{
    return offset <= footer_offset && fw_num(footer, type) && fw_string(footer, "storing") && fw_num(footer, (uint64_t)(footer_offset - offset)) &&
           fw_num(footer, size) && fw_num(footer, size) && fw_u32(footer, crc);
}

int xfu_freearc_create(const xfu_request *request, xx_pd_struct *pd)
{
    static const unsigned char header_bytes[8] = {'A', 'r', 'C', 1, 0, 0, 6, 7};
    fw_file *files = NULL;
    fw_buffer header = {0}, directory = {0}, footer = {0};
    xx_io_device *source = NULL, *output = NULL;
    unsigned char *buffer = NULL;
    char *stage = NULL;
    const char **sorted_names = NULL;
    const char *failure = NULL;
    uint32_t header_crc = 0, directory_crc = 0, footer_crc = 0;
    int64_t directory_offset, footer_offset;
    size_t i, name_bytes = 0;
    int result = 2, progress_level = -1;
    unsigned char unlocked = 0;
    bool compressed;
    int quality;
    if (!request || !request->archive_path || !request->archive_path[0]) return 2;
    if (!request->files || !request->file_count || request->file_count > FW_MAX_FILES) {
        fw_log(request, true, "FreeArc requires 1 through %u regular input files", FW_MAX_FILES);
        return 2;
    }
    compressed = request->compression_method && !strcmp(request->compression_method, "lzma");
    quality = request->compression_level_set ? request->compression_level : 5;
    if ((compressed && (quality < 0 || quality > 5)) || (!compressed && request->compression_level_set && request->compression_level != 0) ||
        (request->compression_method && strcmp(request->compression_method, "store") && strcmp(request->compression_method, "stored") &&
         strcmp(request->compression_method, "storing") && strcmp(request->compression_method, "lzma"))) {
        fw_log(request, true, "FreeArc supports stored level 0 or explicit LZMA levels 0 through 5");
        return 2;
    }
    if (request->password || (request->archive_options && request->archive_options[0]) || (request->archive_filter && strcmp(request->archive_filter, "none"))) {
        fw_log(request, true, "FreeArc creation does not support passwords, options or outer filters");
        return 2;
    }
    files = (fw_file *)calloc(request->file_count, sizeof(*files));
    sorted_names = (const char **)malloc(request->file_count * sizeof(*sorted_names));
    buffer = (unsigned char *)malloc(FW_BUFFER_SIZE);
    if (!files || !sorted_names || !buffer) {
        failure = "cannot allocate writer workspace";
        goto done;
    }
    for (i = 0; i < request->file_count; ++i) {
        const char *slash;
        size_t directory_length;
        if (fw_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        if (!request->files[i] || !fw_regular(request->files[i]) || fw_same_file(request->archive_path, request->files[i])) {
            failure = "input must be a regular file other than the destination archive";
            goto done;
        }
        files[i].name = fw_member_name(request->files[i]);
        if (!files[i].name) {
            failure = "input has an unsafe or oversized archive name";
            goto done;
        }
        sorted_names[i] = files[i].name;
        strcpy(files[i].method, "storing");
        if (strlen(files[i].name) + 1U > FW_MAX_METADATA - name_bytes) {
            failure = "archive names exceed the metadata limit";
            goto done;
        }
        name_bytes += strlen(files[i].name) + 1U;
        slash = strrchr(files[i].name, '/');
        files[i].basename = slash ? slash + 1 : files[i].name;
        directory_length = slash ? (size_t)(slash - files[i].name) : 0;
        files[i].directory = (char *)malloc(directory_length + 1U);
        if (!files[i].directory) {
            failure = "cannot allocate member directory";
            goto done;
        }
        memcpy(files[i].directory, files[i].name, directory_length);
        files[i].directory[directory_length] = 0;
        source = xx_io_file_open(request->files[i], "rb");
        if (!source || (files[i].size = xx_io_total_size(source)) < 0 || files[i].size > FW_MAX_FILE) {
            failure = "cannot read input metadata or input exceeds 256 MiB";
            goto done;
        }
        if (xx_io_close(source) != 0) {
            source = NULL;
            failure = "cannot close input file";
            goto done;
        }
        source = NULL;
    }
    qsort(sorted_names, request->file_count, sizeof(*sorted_names), fw_name_compare);
    for (i = 1; i < request->file_count; ++i)
        if (!strcmp(sorted_names[i - 1], sorted_names[i])) {
            failure = "duplicate archive member name";
            goto done;
        }
    if (fw_cancelled(request, pd)) {
        result = 1;
        goto done;
    }
    if (!xx_io_create_dirs_a(request->archive_path, false) || !(output = fw_stage(request->archive_path, &stage, files))) {
        failure = "cannot create archive staging file";
        goto done;
    }
    if (!fw_bytes(&header, header_bytes, sizeof(header_bytes)) || !fw_descriptor(request, pd, output, 1, &header, &header_crc)) {
        failure = "cannot write FreeArc header";
        goto done;
    }
    for (i = 0; i < request->file_count; ++i) {
        int64_t loaded = 0;
        if (fw_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        source = xx_io_file_open(request->files[i], "rb");
        if (!source || !fw_regular(request->files[i]) || xx_io_total_size(source) != files[i].size || fw_same_file(request->archive_path, request->files[i])) {
            failure = "input file changed before archiving";
            goto done;
        }
        files[i].offset = xx_io_tell(output);
        if (files[i].offset < 0) {
            failure = "cannot determine archive position";
            goto done;
        }
        progress_level = xx_pd_enter_level(pd, (uint64_t)files[i].size, request->files[i]);
        if (compressed && !fw_lzma(request, pd, source, output, &files[i], buffer, progress_level, (unsigned)quality, i)) {
            failure = "FreeArc LZMA encoding failed or exceeded its memory limit";
            goto done;
        }
        while (!compressed && loaded < files[i].size) {
            size_t wanted = files[i].size - loaded > FW_BUFFER_SIZE ? FW_BUFFER_SIZE : (size_t)(files[i].size - loaded);
            ssize_t count;
            if (fw_cancelled(request, pd)) {
                result = 1;
                goto done;
            }
            count = xx_io_read(source, buffer, wanted);
            if (count <= 0 || (size_t)count > wanted || !fw_write(request, pd, output, buffer, (size_t)count)) {
                failure = "cannot read or write complete member data";
                goto done;
            }
            files[i].crc = xx_crc32_calc(files[i].crc, buffer, (size_t)count);
            loaded += count;
            xx_pd_set_current(pd, progress_level, (uint64_t)loaded);
            if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, i, request->file_count, files[i].name);
        }
        files[i].packed = xx_io_tell(output) - files[i].offset;
        if (files[i].packed < 0) {
            failure = "cannot determine packed member size";
            goto done;
        }
        if (xx_io_read(source, buffer, 1) != 0 || xx_io_total_size(source) != files[i].size) {
            failure = "input file changed during archiving";
            goto done;
        }
        if (xx_io_close(source) != 0) {
            source = NULL;
            failure = "cannot close input file";
            goto done;
        }
        source = NULL;
        xx_pd_leave_level(pd, progress_level);
        progress_level = -1;
    }
    directory_offset = xx_io_tell(output);
    if (directory_offset < 0 || !fw_directory(&directory, files, request->file_count, directory_offset) ||
        !fw_descriptor(request, pd, output, 3, &directory, &directory_crc)) {
        failure = "cannot write FreeArc directory";
        goto done;
    }
    footer_offset = xx_io_tell(output);
    if (footer_offset < 0 || !fw_num(&footer, 2) || !fw_footer_block(&footer, 1, 0, footer_offset, header.size, header_crc) ||
        !fw_footer_block(&footer, 3, directory_offset, footer_offset, directory.size, directory_crc) || !fw_bytes(&footer, &unlocked, 1) || !fw_num(&footer, 0) ||
        !fw_string(&footer, "") || !fw_num(&footer, 0) || !fw_descriptor(request, pd, output, 4, &footer, &footer_crc)) {
        failure = "cannot write FreeArc footer";
        goto done;
    }
    if (xx_io_close(output) != 0) {
        output = NULL;
        failure = "cannot close completed archive";
        goto done;
    }
    output = NULL;
    if (fw_cancelled(request, pd)) {
        result = 1;
        goto done;
    }
    if (!xx_io_file_replace_a(stage, request->archive_path, true)) {
        failure = "cannot publish completed archive";
        goto done;
    }
    result = 0;
    if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, request->file_count, request->file_count, NULL);
    fw_log(request, false, "%s: %s FreeArc archive created", request->archive_path, compressed ? "LZMA-compressed" : "stored");
done:
    if (source) xx_io_close(source);
    if (output) xx_io_close(output);
    if (stage && result) (void)xx_io_file_remove_a(stage);
    if (files)
        for (i = 0; i < request->file_count; ++i) {
            free(files[i].name);
            free(files[i].directory);
        }
    free(files);
    free(sorted_names);
    free(buffer);
    free(stage);
    free(header.data);
    free(directory.data);
    free(footer.data);
    xx_pd_leave_level(pd, progress_level);
    if (result && fw_cancelled(request, pd)) {
        fw_log(request, true, "operation cancelled");
        result = 1;
    } else if (failure) fw_log(request, true, "%s: %s", request->archive_path, failure);
    return result;
}

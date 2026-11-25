/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "sfx_backend.h"
#include "native_writer_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>

#ifdef XFU_HAVE_7ZIP_SFX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

extern const unsigned char xfu_7zip_sfx_data[];
extern const size_t xfu_7zip_sfx_size;
#endif

/* A line longer than the local buffer (a long path) is formatted again in
 * a heap buffer instead of being cut. */
static void sfx_log(const xfu_request *request, bool error, const char *format, ...)
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

#ifdef XFU_HAVE_7ZIP_SFX
static bool sfx_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled && request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static bool sfx_same_file(const char *left, const char *right)
{
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
}

static xx_io_device *sfx_stage(const char *destination, const char *kind, char **stage, const void *nonce)
{
    size_t length = strlen(destination);
    char *name;
    unsigned attempt;
    *stage = NULL;
    if (length > SIZE_MAX - 112) return NULL;
    name = (char *)malloc(length + 112);
    if (!name) return NULL;
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *output;
        snprintf(name, length + 112, "%s.xfu-sfx-%s-%p-%lu-%u.tmp", destination, kind, nonce, (unsigned long)time(NULL), attempt);
        output = xx_io_file_open(name, "wbx");
        if (output) {
            *stage = name;
            return output;
        }
    }
    free(name);
    return NULL;
}

static bool sfx_write(const xfu_request *request, xx_pd_struct *pd, xx_io_device *destination, const unsigned char *data, size_t size)
{
    while (size) {
        size_t chunk = size > 65536U ? 65536U : size;
        ssize_t count;
        if (sfx_cancelled(request, pd)) return false;
        count = xx_io_write(destination, data, chunk);
        if (count <= 0 || (size_t)count > chunk) return false;
        data += count;
        size -= (size_t)count;
    }
    return true;
}
#endif

int xfu_sfx_create(const xfu_request *request, xx_pd_struct *pd)
{
    if (!request || !request->archive_path) return 2;
#ifndef XFU_HAVE_7ZIP_SFX
    (void)pd;
    sfx_log(request, true, "7-Zip SFX creation is unavailable in this build");
    return 2;
#else
    xfu_request payload_request = *request;
    xx_io_device *payload = NULL, *destination = NULL;
    char *payload_path = NULL, *stage = NULL;
    unsigned char *buffer = NULL;
    const char *failure = NULL, *method = request->compression_method;
    int result = 2;
    size_t i;
    if (request->password || (request->archive_filter && strcmp(request->archive_filter, "none")) || (request->archive_options && request->archive_options[0])) {
        sfx_log(request, true, "7-Zip SFX writer does not support passwords, outer filters or archive options");
        return 2;
    }
    if (method && strcmp(method, "store") && strcmp(method, "stored") && strcmp(method, "lzma1") && strcmp(method, "lzma2") && strcmp(method, "ppmd")) {
        sfx_log(request, true, "7-Zip SFX supports only store, lzma1, lzma2 and ppmd compression");
        return 2;
    }
    if (request->compression_level_set && (request->compression_level < 0 || request->compression_level > 9)) {
        sfx_log(request, true, "7-Zip SFX compression level must be between 0 and 9");
        return 2;
    }
    if (!request->files || !request->file_count) {
        sfx_log(request, true, "7-Zip SFX creation requires regular input files");
        return 2;
    }
    for (i = 0; i < request->file_count; ++i) {
        if (sfx_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        if (!request->files[i] || sfx_same_file(request->archive_path, request->files[i])) {
            failure = "cannot add the SFX archive to itself";
            goto done;
        }
    }
    if (xfu_7zip_sfx_size < 1024U || xfu_7zip_sfx_data[0] != 'M' || xfu_7zip_sfx_data[1] != 'Z') {
        failure = "embedded 7-Zip SFX stub is invalid";
        goto done;
    }
    if (!xx_io_create_dirs_a(request->archive_path, false) || !(payload = sfx_stage(request->archive_path, "payload", &payload_path, request))) {
        failure = "cannot create SFX payload staging file";
        goto done;
    }
    if (xx_io_close(payload) != 0) {
        payload = NULL;
        failure = "cannot close payload staging file";
        goto done;
    }
    payload = NULL;
    payload_request.archive_path = payload_path;
    payload_request.archive_format = "7zip";
    payload_request.compression_method = method ? method : "lzma2";
    result = xfu_native_create(&payload_request, "7zip", "none", pd);
    if (result) goto done;
    result = 1;
    payload = xx_io_file_open(payload_path, "rb");
    buffer = (unsigned char *)malloc(65536U);
    if (!payload || !buffer || !(destination = sfx_stage(request->archive_path, "archive", &stage, request))) {
        failure = "cannot open completed SFX payload";
        goto done;
    }
    if (!sfx_write(request, pd, destination, xfu_7zip_sfx_data, xfu_7zip_sfx_size)) {
        failure = "cannot write SFX stub";
        goto done;
    }
    while (!sfx_cancelled(request, pd)) {
        ssize_t count = xx_io_read(payload, buffer, 65536U);
        if (count < 0 || count > 65536) {
            failure = "cannot read SFX payload";
            goto done;
        }
        if (!count) break;
        if (!sfx_write(request, pd, destination, buffer, (size_t)count)) {
            failure = "cannot write SFX payload";
            goto done;
        }
    }
    if (sfx_cancelled(request, pd)) goto done;
    if (xx_io_close(payload) != 0) {
        payload = NULL;
        failure = "cannot close SFX payload";
        goto done;
    }
    payload = NULL;
    if (xx_io_close(destination) != 0) {
        destination = NULL;
        failure = "cannot close SFX output";
        goto done;
    }
    destination = NULL;
    if (sfx_cancelled(request, pd)) goto done;
    if (!xx_io_file_replace_a(stage, request->archive_path, true)) {
        failure = "cannot publish completed SFX archive";
        goto done;
    }
    result = 0;
    sfx_log(request, false, "%s: 7-Zip SFX archive created", request->archive_path);
done:
    if (payload) xx_io_close(payload);
    if (destination) xx_io_close(destination);
    if (payload_path) (void)xx_io_file_remove_a(payload_path);
    if (stage && result) (void)xx_io_file_remove_a(stage);
    free(payload_path);
    free(stage);
    free(buffer);
    if (result && sfx_cancelled(request, pd)) sfx_log(request, true, "operation cancelled");
    else if (failure) sfx_log(request, true, "%s: %s", request->archive_path, failure);
    return result;
#endif
}

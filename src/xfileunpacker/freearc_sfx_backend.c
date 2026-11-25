/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "freearc_sfx_backend.h"
#include "freearc_backend.h"
#include "freearc_sfx_locator.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>

#ifdef XFU_HAVE_FREEARC_SFX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "freearc_sfx_names.h"
#include <xxfclib/formats/archive/freearc/xx_freearc.h>
extern const unsigned char xfu_freearc_sfx_data[];
extern const size_t xfu_freearc_sfx_size;
#endif

int64_t xfu_freearc_sfx_payload_offset(xx_io_device *source)
{
    return fs_locate_payload(source);
}

/* A line longer than the local buffer (a long path) is formatted again in
 * a heap buffer instead of being cut. */
static void fs_log(const xfu_request *request, bool error, const char *format, ...)
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

#ifdef XFU_HAVE_FREEARC_SFX
static bool fs_cancelled(const xfu_request *request, xx_pd_struct *pd)
{
    if (request->callbacks.cancelled && request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

static bool fs_same_file(const char *left, const char *right)
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

static xx_io_device *fs_stage(const char *destination, const char *kind, char **stage, const void *nonce)
{
    size_t length = strlen(destination);
    unsigned attempt;
    char *name;
    *stage = NULL;
    if (length > SIZE_MAX - 112 || !(name = (char *)malloc(length + 112))) return NULL;
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *file;
        snprintf(name, length + 112, "%s.xfu-freearc-sfx-%s-%p-%lu-%u.tmp", destination, kind, nonce, (unsigned long)time(NULL), attempt);
        if ((file = xx_io_file_open(name, "wbx"))) {
            *stage = name;
            return file;
        }
    }
    free(name);
    return NULL;
}

static bool fs_write(const xfu_request *request, xx_pd_struct *pd, xx_io_device *destination, const unsigned char *data, size_t size)
{
    while (size) {
        size_t part = size > 65536U ? 65536U : size;
        ssize_t count;
        if (fs_cancelled(request, pd)) return false;
        count = xx_io_write(destination, data, part);
        if (count <= 0 || (size_t)count > part) return false;
        data += count;
        size -= (size_t)count;
    }
    return true;
}

static int fs_name_compare(const void *left, const void *right)
{
    int compared = CompareStringOrdinal(*(const wchar_t *const *)left, -1, *(const wchar_t *const *)right, -1, TRUE);
    return compared == CSTR_LESS_THAN ? -1 : compared == CSTR_GREATER_THAN ? 1 : 0;
}

static int fs_hash_compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* The payload writer rejects unsafe paths. Also reject aliases according to
 * Windows Unicode comparison before publishing a self-extracting archive. */
static bool fs_check_names(xx_io_device *payload, const xfu_request *request, xx_pd_struct *pd)
{
    xx_freearc *archive = xx_freearc_create(payload, 0);
    xx_archive_record_state *state = NULL;
    wchar_t **names = NULL;
    uint64_t *hashes = NULL;
    size_t count = 0, i;
    bool success = false;
    if (!archive || !xx_freearc_handle_base_info(&archive->format, pd) || !(state = xx_freearc_create_archive_records_reading(&archive->format, NULL, pd)) ||
        state->total_records < 0 || (uint64_t)state->total_records != request->file_count || !(names = (wchar_t **)calloc(request->file_count, sizeof(*names))) ||
        !(hashes = (uint64_t *)calloc(request->file_count, sizeof(*hashes))))
        goto done;
    while (xx_freearc_get_current_archive_record(&archive->format, state)) {
        const char *name = xx_archive_record_get_original_name(xx_freearc_get_current_archive_record(&archive->format, state));
        if (count >= request->file_count || fs_cancelled(request, pd) || !fs_safe_name(name) || !(names[count] = xx_str_utf8_to_unicode(name))) goto done;
        hashes[count++] = fs_name_hash(name);
        if (!xx_freearc_archive_record_move_to_next(&archive->format, state, pd)) break;
    }
    if (count != request->file_count || pd->last_error) goto done;
    qsort(hashes, count, sizeof(*hashes), fs_hash_compare);
    for (i = 1; i < count; ++i)
        if (hashes[i - 1] == hashes[i]) goto done;
    qsort(names, count, sizeof(*names), fs_name_compare);
    for (i = 1; i < count; ++i)
        if (!fs_name_compare(&names[i - 1], &names[i])) goto done;
    for (i = 0; i < count; ++i) {
        wchar_t *copy = _wcsdup(names[i]), *p;
        if (!copy) goto done;
        for (p = copy; *p; ++p)
            if (*p == L'/') {
                wchar_t *key = copy;
                bool collision;
                *p = 0;
                collision = bsearch(&key, names, count, sizeof(*names), fs_name_compare) != NULL;
                *p = L'/';
                if (collision) {
                    free(copy);
                    goto done;
                }
            }
        free(copy);
    }
    success = true;
done:
    if (names) {
        for (i = 0; i < request->file_count; ++i) xx_str_wfree(names[i]);
        free(names);
    }
    free(hashes);
    if (state) xx_freearc_free_archive_records_reading(&archive->format, state);
    if (archive) xx_freearc_free(archive);
    return success;
}
#endif

int xfu_freearc_sfx_create(const xfu_request *request, xx_pd_struct *pd)
{
    if (!request || !request->archive_path) return 2;
#ifndef XFU_HAVE_FREEARC_SFX
    (void)pd;
    fs_log(request, true, "FreeArc SFX creation is unavailable in this build");
    return 2;
#else
    xfu_request payload_request = *request;
    xx_io_device *payload = NULL, *output = NULL;
    unsigned char locator[24] = "XFUFreeArcSFX01", *buffer = NULL;
    char *payload_path = NULL, *stage = NULL;
    const char *failure = NULL, *method = request->compression_method;
    size_t i;
    uint64_t offset;
    int result = 2;
    bool compressed = method && !strcmp(method, "lzma");
    if (request->password || (request->archive_filter && strcmp(request->archive_filter, "none")) || (request->archive_options && request->archive_options[0]) ||
        (method && strcmp(method, "store") && strcmp(method, "stored") && strcmp(method, "storing") && strcmp(method, "lzma")) ||
        (request->compression_level_set && (compressed ? request->compression_level < 0 || request->compression_level > 5 : request->compression_level != 0))) {
        fs_log(request, true, "FreeArc SFX supports stored level 0 or explicit LZMA levels 0 through 5; no passwords, filters or options");
        return 2;
    }
    if (!request->files || !request->file_count) {
        fs_log(request, true, "FreeArc SFX requires regular input files");
        return 2;
    }
    for (i = 0; i < request->file_count; ++i) {
        if (fs_cancelled(request, pd)) {
            result = 1;
            goto done;
        }
        if (!request->files[i] || fs_same_file(request->archive_path, request->files[i])) {
            failure = "cannot add the SFX archive to itself";
            goto done;
        }
    }
    if (xfu_freearc_sfx_size < 1024 || xfu_freearc_sfx_size > 16777216 || memcmp(xfu_freearc_sfx_data, "MZ", 2)) {
        failure = "embedded FreeArc SFX stub is invalid";
        goto done;
    }
    if (!xx_io_create_dirs_a(request->archive_path, false) || !(payload = fs_stage(request->archive_path, "payload", &payload_path, request))) {
        failure = "cannot create SFX payload staging file";
        goto done;
    }
    if (xx_io_close(payload)) {
        payload = NULL;
        failure = "cannot close SFX payload stage";
        goto done;
    }
    payload = NULL;
    payload_request.archive_path = payload_path;
    payload_request.archive_format = "freearc";
    result = xfu_freearc_create(&payload_request, pd);
    if (result) goto done;
    result = 1;
    if (!(payload = xx_io_file_open(payload_path, "rb")) || !fs_check_names(payload, request, pd) || xx_io_seek64(payload, 0, SEEK_SET)) {
        failure = "unsafe or colliding Windows SFX member names";
        goto done;
    }
    if (!(buffer = (unsigned char *)malloc(65536U)) || !(output = fs_stage(request->archive_path, "archive", &stage, request))) {
        failure = "cannot create final SFX staging file";
        goto done;
    }
    offset = (uint64_t)xfu_freearc_sfx_size + sizeof(locator);
    for (i = 0; i < 8; ++i) locator[16 + i] = (unsigned char)(offset >> (i * 8));
    if (!fs_write(request, pd, output, xfu_freearc_sfx_data, xfu_freearc_sfx_size) || !fs_write(request, pd, output, locator, sizeof(locator))) {
        failure = "cannot write FreeArc SFX stub";
        goto done;
    }
    for (;;) {
        ssize_t count = xx_io_read(payload, buffer, 65536U);
        if (count < 0 || count > 65536) {
            failure = "cannot read FreeArc SFX payload";
            goto done;
        }
        if (!count) break;
        if (!fs_write(request, pd, output, buffer, (size_t)count)) {
            failure = "cannot write FreeArc SFX payload";
            goto done;
        }
    }
    if (xx_io_close(payload)) {
        payload = NULL;
        failure = "cannot close SFX payload";
        goto done;
    }
    payload = NULL;
    if (xx_io_close(output)) {
        output = NULL;
        failure = "cannot close SFX output";
        goto done;
    }
    output = NULL;
    if (fs_cancelled(request, pd)) goto done;
    if (!xx_io_file_replace_a(stage, request->archive_path, true)) {
        failure = "cannot publish completed SFX";
        goto done;
    }
    result = 0;
    fs_log(request, false, "%s: FreeArc SFX archive created", request->archive_path);
done:
    if (payload) xx_io_close(payload);
    if (output) xx_io_close(output);
    if (payload_path) (void)xx_io_file_remove_a(payload_path);
    if (stage && result) (void)xx_io_file_remove_a(stage);
    free(payload_path);
    free(stage);
    free(buffer);
    if (result && fs_cancelled(request, pd)) fs_log(request, true, "operation cancelled");
    else if (failure) fs_log(request, true, "%s: %s", request->archive_path, failure);
    return result;
#endif
}

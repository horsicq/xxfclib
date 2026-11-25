/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Expose mkstemp/fdopen in strict C11 builds before any system header. */
#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#endif
#include <xxfclib/io/xx_io_snapshot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
#endif
struct xx_io_snapshot {
    char *path;
    uint64_t size;
    bool persistent;
    size_t references;
};
static void snapshot_error(char *error, size_t cap, const char *message)
{
    if (error && cap) (void)snprintf(error, cap, "%s", message);
}
void xx_io_snapshot_destroy(xx_io_snapshot *snapshot)
{
    if (!snapshot) return;
    if (snapshot->references > 1) {
        --snapshot->references;
        return;
    }
#ifdef _WIN32
    if (snapshot->path && !snapshot->persistent) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot->path, -1, NULL, 0);
        wchar_t *p = n ? malloc((size_t)n * sizeof(*p)) : NULL;
        if (p) {
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot->path, -1, p, n);
            DeleteFileW(p);
            free(p);
        }
    }
#else
    if (snapshot->path && !snapshot->persistent) (void)remove(snapshot->path);
#endif
    free(snapshot->path);
    free(snapshot);
}
xx_io_snapshot *xx_io_snapshot_create(uint64_t size, xx_io_snapshot_reader reader, void *reader_user, xx_io_snapshot_progress progress, void *progress_user,
                                      bool *cancelled, char *error, size_t cap)
{
    unsigned char buffer[65536];
    FILE *file = NULL;
    xx_io_snapshot *snapshot = NULL;
    if (error && cap) error[0] = 0;
    if (cancelled) *cancelled = false;
    if (!reader || !cancelled || size > INT64_MAX) {
        snapshot_error(error, cap, "Invalid snapshot source.");
        return NULL;
    }
    if (!(snapshot = calloc(1, sizeof(*snapshot)))) goto failure;
#ifdef _WIN32
    wchar_t dir[MAX_PATH + 1], name[MAX_PATH + 1];
    if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"cxh", 0, name)) goto failure;
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, NULL, 0, NULL, NULL);
    snapshot->path = n ? malloc((size_t)n) : NULL;
    if (!snapshot->path) {
        DeleteFileW(name);
        goto failure;
    }
    WideCharToMultiByte(CP_UTF8, 0, name, -1, snapshot->path, n, NULL, NULL);
    file = _wfopen(name, L"wb");
#else
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    size_t n = strlen(dir) + 32;
    snapshot->path = malloc(n);
    if (!snapshot->path) goto failure;
    (void)snprintf(snapshot->path, n, "%s/cxhv-snapshot-XXXXXX", dir);
    int fd = mkstemp(snapshot->path);
    if (fd >= 0) {
        file = fdopen(fd, "wb");
        if (!file) (void)close(fd);
    }
#endif
    if (!file) goto failure;
    for (uint64_t at = 0; at < size;) {
        size_t length = size - at > sizeof(buffer) ? sizeof(buffer) : (size_t)(size - at);
        if (progress && !progress(at, size, progress_user)) {
            *cancelled = true;
            goto failure;
        }
        if (!reader(reader_user, at, buffer, length) || fwrite(buffer, 1, length, file) != length) goto failure;
        at += length;
    }
    if (progress && !progress(size, size, progress_user)) {
        *cancelled = true;
        goto failure;
    }
    if (fclose(file)) {
        file = NULL;
        goto failure;
    }
    file = NULL;
    snapshot->size = size;
    snapshot->references = 1;
    return snapshot;
failure:
    if (file) (void)fclose(file);
    xx_io_snapshot_destroy(snapshot);
    if (!*cancelled) snapshot_error(error, cap, "Cannot create a complete immutable snapshot.");
    return NULL;
}
const char *xx_io_snapshot_path(const xx_io_snapshot *snapshot)
{
    return snapshot ? snapshot->path : NULL;
}
uint64_t xx_io_snapshot_size(const xx_io_snapshot *snapshot)
{
    return snapshot ? snapshot->size : 0;
}
bool xx_io_snapshot_is_persistent(const xx_io_snapshot *snapshot)
{
    return snapshot && snapshot->persistent;
}
xx_io_device *xx_io_snapshot_open(const xx_io_snapshot *snapshot)
{
    return snapshot ? xx_io_file_open(snapshot->path, "rb") : NULL;
}

static bool snapshot_report(xx_io_snapshot_progress progress, void *user, uint64_t current, uint64_t total, bool *cancelled)
{
    if (progress && !progress(current, total, user)) *cancelled = true;
    return !*cancelled;
}
/* Both platforms stage in the destination directory. A hard link avoids
 * copying an immutable file on the same volume. Unsupported/cross-volume
 * links use the same bounded, cancellable streaming copy. */
static bool snapshot_copy(FILE *in, FILE *out, uint64_t size, xx_io_snapshot_progress progress, void *user, bool *cancelled)
{
    unsigned char buffer[65536];
    for (uint64_t at = 0; at < size;) {
        size_t n = size - at > sizeof(buffer) ? sizeof(buffer) : (size_t)(size - at);
        if (!snapshot_report(progress, user, at, size, cancelled) || fread(buffer, 1, n, in) != n || fwrite(buffer, 1, n, out) != n) return false;
        at += n;
    }
    return fgetc(in) == EOF && !ferror(in);
}
bool xx_io_snapshot_persist_progress(xx_io_snapshot *snapshot, const char *path, xx_io_snapshot_progress progress, void *user, bool *cancelled, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (cancelled) *cancelled = false;
    if (!snapshot || !path || !*path || !cancelled) {
        snapshot_error(error, cap, "Invalid durable snapshot path.");
        return false;
    }
    if (!snapshot_report(progress, user, 0, snapshot->size, cancelled)) return false;
    if (!strcmp(snapshot->path, path)) {
        if (!snapshot_report(progress, user, snapshot->size, snapshot->size, cancelled)) return false;
        snapshot->persistent = true;
        return true;
    }
    char *new_path = malloc(strlen(path) + 1);
    if (!new_path) {
        snapshot_error(error, cap, "Cannot allocate durable snapshot path.");
        return false;
    }
    strcpy(new_path, path);
    bool ok = false;
    FILE *in = NULL, *out = NULL;
#ifdef _WIN32
    int a = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot->path, -1, NULL, 0), b = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *old = a ? malloc((size_t)a * sizeof(*old)) : NULL, *next = b ? malloc((size_t)b * sizeof(*next)) : NULL, *directory = NULL;
    wchar_t stage[MAX_PATH + 1] = {0};
    if (!old || !next) goto done;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, snapshot->path, -1, old, a) || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, next, b))
        goto done;
    HANDLE source_handle = CreateFileW(old, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE target_handle = CreateFileW(next, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    BY_HANDLE_FILE_INFORMATION source_info, target_info;
    bool same_file = source_handle != INVALID_HANDLE_VALUE && target_handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(source_handle, &source_info) &&
                     GetFileInformationByHandle(target_handle, &target_info) && source_info.dwVolumeSerialNumber == target_info.dwVolumeSerialNumber &&
                     source_info.nFileIndexHigh == target_info.nFileIndexHigh && source_info.nFileIndexLow == target_info.nFileIndexLow;
    if (source_handle != INVALID_HANDLE_VALUE) CloseHandle(source_handle);
    if (target_handle != INVALID_HANDLE_VALUE) CloseHandle(target_handle);
    if (same_file) {
        if (!snapshot_report(progress, user, snapshot->size, snapshot->size, cancelled)) goto done;
        free(snapshot->path);
        snapshot->path = new_path;
        snapshot->persistent = true;
        free(old);
        free(next);
        return true;
    }
    directory = malloc(((size_t)b + 3) * sizeof(*directory));
    if (!directory) goto done;
    wcscpy(directory, next);
    wchar_t *slash = wcsrchr(directory, L'\\'), *forward = wcsrchr(directory, L'/');
    if (forward && (!slash || forward > slash)) slash = forward;
    if (slash) slash[1] = 0;
    else wcscpy(directory, L".");
    if (!GetTempFileNameW(directory, L"cxh", 0, stage) || !DeleteFileW(stage)) goto done;
    if (!CreateHardLinkW(stage, old, NULL)) {
        out = _wfopen(stage, L"wbx");
        in = _wfopen(old, L"rb");
        if (!in || !out || !snapshot_copy(in, out, snapshot->size, progress, user, cancelled)) goto done;
        if (fclose(in)) {
            in = NULL;
            goto done;
        }
        in = NULL;
        if (fclose(out)) {
            out = NULL;
            goto done;
        }
        out = NULL;
    }
    if (!snapshot_report(progress, user, snapshot->size, snapshot->size, cancelled)) goto done;
    if (!MoveFileExW(stage, next, MOVEFILE_REPLACE_EXISTING)) goto done;
    stage[0] = 0;
    if (!snapshot->persistent) (void)DeleteFileW(old);
    ok = true;
done:
    if (in) (void)fclose(in);
    if (out) (void)fclose(out);
    if (stage[0]) (void)DeleteFileW(stage);
    free(old);
    free(next);
    free(directory);
#else
    char *stage = NULL;
    struct stat source_info, target_info;
    /* Different spellings can name the same cache entry. Retain its name
     * without unlinking the underlying entry after publication. */
    if (!stat(snapshot->path, &source_info) && !stat(path, &target_info) && source_info.st_dev == target_info.st_dev && source_info.st_ino == target_info.st_ino) {
        if (!snapshot_report(progress, user, snapshot->size, snapshot->size, cancelled)) goto done;
        free(snapshot->path);
        snapshot->path = new_path;
        snapshot->persistent = true;
        return true;
    }
    const char *slash = strrchr(path, '/');
    size_t directory = slash ? (size_t)(slash - path) + 1 : 0;
    static const char suffix[] = ".cxhv-snapshot-XXXXXX";
    stage = calloc(directory + sizeof(suffix), 1);
    if (!stage) goto done;
    memcpy(stage, path, directory);
    memcpy(stage + directory, suffix, sizeof(suffix));
    int descriptor = mkstemp(stage);
    if (descriptor < 0) goto done;
    if (close(descriptor) || unlink(stage)) goto done;
    if (link(snapshot->path, stage)) {
        descriptor = open(stage, O_WRONLY | O_CREAT | O_EXCL, 0600);
        out = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
        if (descriptor >= 0 && !out) (void)close(descriptor);
        in = fopen(snapshot->path, "rb");
        if (!in || !out || !snapshot_copy(in, out, snapshot->size, progress, user, cancelled)) goto done;
        if (fclose(in)) {
            in = NULL;
            goto done;
        }
        in = NULL;
        if (fclose(out)) {
            out = NULL;
            goto done;
        }
        out = NULL;
    }
    if (!snapshot_report(progress, user, snapshot->size, snapshot->size, cancelled)) goto done;
    if (rename(stage, path)) goto done;
    stage[0] = 0;
    if (!snapshot->persistent) (void)remove(snapshot->path);
    ok = true;
done:
    if (in) (void)fclose(in);
    if (out) (void)fclose(out);
    if (stage && stage[0]) (void)remove(stage);
    free(stage);
#endif
    if (!ok) {
        free(new_path);
        if (!*cancelled) snapshot_error(error, cap, "Cannot publish a complete durable snapshot.");
        return false;
    }
    free(snapshot->path);
    snapshot->path = new_path;
    snapshot->persistent = true;
    return true;
}
bool xx_io_snapshot_persist(xx_io_snapshot *snapshot, const char *path, char *error, size_t cap)
{
    bool cancelled = false;
    return xx_io_snapshot_persist_progress(snapshot, path, NULL, NULL, &cancelled, error, cap);
}

xx_io_snapshot *xx_io_snapshot_retain(xx_io_snapshot *snapshot)
{
    if (snapshot) ++snapshot->references;
    return snapshot;
}
xx_io_snapshot *xx_io_snapshot_create_ranges(const char *source, uint64_t size, const xx_io_snapshot_range *ranges, size_t count, xx_io_snapshot_reader reader,
                                             void *reader_user, xx_io_snapshot_progress progress, void *user, bool *cancelled, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (cancelled) *cancelled = false;
    if (!source || !reader || !cancelled || size > INT64_MAX || (count && !ranges)) {
        snapshot_error(error, cap, "Invalid snapshot ranges.");
        return NULL;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (ranges[i].offset > size || ranges[i].length > size - ranges[i].offset || ranges[i].length > UINT64_MAX - total) {
            snapshot_error(error, cap, "Invalid snapshot range bounds.");
            return NULL;
        }
        total += ranges[i].length;
    }
#ifdef __APPLE__
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    xx_io_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return NULL;
    size_t length = strlen(dir) + 40;
    snapshot->path = malloc(length);
    if (!snapshot->path) {
        xx_io_snapshot_destroy(snapshot);
        return NULL;
    }
    snprintf(snapshot->path, length, "%s/cxhv-revision-XXXXXX", dir);
    int fd = mkstemp(snapshot->path);
    if (fd < 0) {
        xx_io_snapshot_destroy(snapshot);
        return NULL;
    }
    close(fd);
    unlink(snapshot->path);
    if (progress && !progress(0, total, user)) {
        *cancelled = true;
        xx_io_snapshot_destroy(snapshot);
        return NULL;
    }
    if (!clonefile(source, snapshot->path, 0)) {
        FILE *out = fopen(snapshot->path, "r+b");
        bool ok = out != NULL;
        unsigned char bytes[65536];
        uint64_t done = 0;
        if (ok && (fseeko(out, 0, SEEK_END) || ftello(out) != (off_t)size)) ok = false;
        for (size_t i = 0; ok && i < count; ++i) {
            if (fseeko(out, (off_t)ranges[i].offset, SEEK_SET)) {
                ok = false;
                break;
            }
            for (uint64_t at = 0; ok && at < ranges[i].length;) {
                size_t n = ranges[i].length - at > sizeof(bytes) ? sizeof(bytes) : (size_t)(ranges[i].length - at);
                if (progress && !progress(done, total, user)) {
                    *cancelled = true;
                    ok = false;
                    break;
                }
                ok = reader(reader_user, ranges[i].offset + at, bytes, n) && fwrite(bytes, 1, n, out) == n;
                at += n;
                done += n;
            }
        }
        if (ok && progress && !progress(total, total, user)) {
            *cancelled = true;
            ok = false;
        }
        if (out && fclose(out)) ok = false;
        if (ok) {
            snapshot->size = size;
            snapshot->references = 1;
            return snapshot;
        }
        xx_io_snapshot_destroy(snapshot);
        if (!*cancelled) snapshot_error(error, cap, "Cannot complete cloned snapshot.");
        return NULL;
    }
    xx_io_snapshot_destroy(snapshot);
#endif
    return xx_io_snapshot_create(size, reader, reader_user, progress, user, cancelled, error, cap);
}

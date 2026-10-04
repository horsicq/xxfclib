/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#include "xx_settings_platform.h"
#include "../../io/xx_io_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

xxfc_status_t xx_settings_platform_read_file(const char *path, char **text, size_t *size) {
    *text = NULL; *size = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? XXFC_OK : XXFC_ERR_IO;
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > XX_SETTINGS_MAX_FILE_SIZE) { close(fd); return XXFC_ERR_IO; }
    char *data = (char *)xx_rt_malloc((size_t)info.st_size + 1);
    if (!data) { close(fd); return XXFC_ERR_OUT_OF_MEMORY; }
    size_t offset = 0;
    while (offset < (size_t)info.st_size) {
        ssize_t count = read(fd, data + offset, (size_t)info.st_size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { xx_rt_free(data); close(fd); return XXFC_ERR_IO; }
        offset += (size_t)count;
    }
    close(fd); data[offset] = 0; *text = data; *size = offset;
    return XXFC_OK;
}

static bool create_parents(char *path) {
    for (size_t i = 1; path[i]; ++i) if (path[i] == '/') {
        path[i] = 0;
        bool ok = mkdir(path, 0700) == 0;
        if (!ok && errno == EEXIST) { struct stat info; ok = stat(path, &info) == 0 && S_ISDIR(info.st_mode); }
        path[i] = '/';
        if (!ok) return false;
    }
    return true;
}

xxfc_status_t xx_settings_platform_write_file(const char *path, const char *text, size_t size) {
    if (!xx_io_policy_mutation_allowed()) return XXFC_ERR_IO;
    char *directory = xx_settings_duplicate(path, xx_rt_strlen(path));
    if (!directory) return XXFC_ERR_OUT_OF_MEMORY;
    bool ok = create_parents(directory);
    xx_rt_free(directory);
    if (!ok) return XXFC_ERR_IO;
    xx_settings_buffer temporary = {0};
    if (!xx_settings_buffer_text(&temporary, path) || !xx_settings_buffer_text(&temporary, ".xx-settings-XXXXXX")) { xx_rt_free(temporary.data); return XXFC_ERR_OUT_OF_MEMORY; }
    int fd = mkstemp(temporary.data);
    if (fd < 0) { xx_rt_free(temporary.data); return XXFC_ERR_IO; }
    struct stat old;
    if (stat(path, &old) == 0 && fchmod(fd, old.st_mode & 0777)) ok = false;
    size_t offset = 0;
    while (ok && offset < size) {
        ssize_t count = write(fd, text + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += (size_t)count;
    }
    if (ok && fsync(fd)) ok = false;
    if (close(fd)) ok = false;
    if (ok && rename(temporary.data, path)) ok = false;
    if (!ok) unlink(temporary.data);
    xx_rt_free(temporary.data);
    return ok ? XXFC_OK : XXFC_ERR_IO;
}

bool xx_settings_platform_file_writable(const char *path) {
    char *parent = xx_settings_duplicate(path, xx_rt_strlen(path));
    if (!parent) return false;
    struct stat info;
    bool result = true;
    if (stat(path, &info) == 0) { if (!S_ISREG(info.st_mode) || access(path, W_OK)) result = false; }
    else if (errno != ENOENT) result = false;
    for (;;) {
        size_t size = xx_rt_strlen(parent);
        while (size && parent[size - 1] != '/') --size;
        if (!size) { xx_rt_free(parent); return result && access(".", W_OK | X_OK) == 0; }
        parent[size == 1 ? 1 : size - 1] = 0;
        if (stat(parent, &info) == 0) { result = result && S_ISDIR(info.st_mode) && access(parent, W_OK | X_OK) == 0; break; }
        if (errno != ENOENT || size == 1) { result = false; break; }
    }
    xx_rt_free(parent);
    return result;
}
#endif

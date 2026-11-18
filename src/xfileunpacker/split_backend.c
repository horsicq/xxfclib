/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "split_backend.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xxfclib/formats/xx_format.h>
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

#define SP_MAX_PARTS 999U
#define SP_BUFFER_SIZE 65536U
#define SP_DEFAULT_VOLUME (UINT64_C(10) * 1024U * 1024U)

typedef struct sp_identity {
    uint64_t device;
    uint64_t file;
} sp_identity;

typedef struct sp_part {
    char *path;
    char *stage;
    int64_t size;
    sp_identity identity;
    bool published;
} sp_part;

/* A line longer than the local buffer (a long archive path) is formatted
 * again in a heap buffer instead of being cut. */
static void sp_log(const xfu_request *request, bool error, const char *format, ...) {
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

static bool sp_cancelled(const xfu_request *request, xx_pd_struct *pd) {
    if (request->callbacks.cancelled &&
        request->callbacks.cancelled(request->callbacks.user)) xx_pd_stop(pd);
    return xx_pd_is_stopped(pd);
}

bool xfu_split_is_first_volume(const char *path) {
    size_t length = path ? strlen(path) : 0;
    return length >= 4U && !strcmp(path + length - 4U, ".001");
}

/* kind: 0 absent, 1 ordinary file, 2 directory, 3 link or other entry. */
static bool sp_probe(const char *path, int *kind, sp_identity *identity) {
    *kind = 0;
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path);
    DWORD attributes, error;
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
    if (!wide) return false;
    attributes = GetFileAttributesW(wide);
    error = GetLastError();
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        xx_str_wfree(wide);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    *kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 2 : (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 3 : 1;
    if (*kind != 1 || !identity) { xx_str_wfree(wide); return true; }
    handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    xx_str_wfree(wide);
    if (handle == INVALID_HANDLE_VALUE) return false;
    if (!GetFileInformationByHandle(handle, &info)) { CloseHandle(handle); return false; }
    CloseHandle(handle);
    identity->device = info.dwVolumeSerialNumber;
    identity->file = ((uint64_t)info.nFileIndexHigh << 32U) | info.nFileIndexLow;
#else
    struct stat info;
    if (lstat(path, &info) != 0) return errno == ENOENT;
    *kind = S_ISREG(info.st_mode) ? 1 : S_ISDIR(info.st_mode) ? 2 : 3;
    if (identity && *kind == 1) {
        identity->device = (uint64_t)info.st_dev;
        identity->file = (uint64_t)info.st_ino;
    }
#endif
    return true;
}

static bool sp_same_identity(const sp_identity *a, const sp_identity *b) {
    return a->device == b->device && a->file == b->file;
}

static char *sp_prefix(const char *path) {
    size_t length = strlen(path);
    char *prefix;
    if (xfu_split_is_first_volume(path)) length -= 4U;
    if (!length) return NULL;
    prefix = (char *)malloc(length + 1U);
    if (!prefix) return NULL;
    memcpy(prefix, path, length); prefix[length] = 0;
    return prefix;
}

static char *sp_part_path(const char *prefix, unsigned number) {
    size_t length = strlen(prefix);
    char *path;
    if (length > SIZE_MAX - 16U) return NULL;
    path = (char *)malloc(length + 16U);
    if (path) snprintf(path, length + 16U, "%s.%03u", prefix, number);
    return path;
}

/* A short name in the path's folder (the same file system, so publication
 * is a rename), whatever the length of the path's own name: the reader's
 * ".xfu-<8 hex digits>-<n>.tmp", which the writers' folder walk leaves out.
 * Each nonce gives its own names. */
static xx_io_device *sp_stage(const char *path, char **stage, const void *nonce) {
    size_t length = strlen(path), folder = 0, i;
    unsigned long tag = (unsigned long)(((uintptr_t)nonce >> 4) ^ (uintptr_t)time(NULL)) & 0xFFFFFFFFUL;
    unsigned attempt;
    char *name;
    *stage = NULL;
    for (i = 0; i < length; ++i) if (path[i] == '/' || path[i] == '\\') folder = i + 1U;
    if (folder > SIZE_MAX - 32U) return NULL;
    name = (char *)malloc(folder + 32U);
    if (!name) return NULL;
    memcpy(name, path, folder);
    for (attempt = 0; attempt < 128; ++attempt) {
        xx_io_device *output;
        snprintf(name + folder, 32U, ".xfu-%08lx-%u.tmp", tag, attempt);
        if (!strcmp(name, path)) continue;
        output = xx_io_file_open(name, "wbx");
        if (output) { *stage = name; return output; }
    }
    free(name); return NULL;
}

/* A file takes all the bytes or fails: a short count is a write error, after
 * which the C library may accept bytes it then drops. */
static bool sp_write(const xfu_request *request, xx_pd_struct *pd,
                     xx_io_device *output, const unsigned char *data, size_t size) {
    if (sp_cancelled(request, pd)) return false;
    return !size || xx_io_write(output, data, size) == (ssize_t)size;
}

/* The size of the file at path, or -1. */
static int64_t sp_file_size(const char *path) {
    xx_io_device *file = xx_io_file_open(path, "rb");
    int64_t size = file ? xx_io_total_size(file) : -1;
    if (file) xx_io_close(file);
    return size;
}

static bool sp_leaf_safe(const char *leaf) {
    size_t i, length = strlen(leaf), stem = strcspn(leaf, ".");
    char device[5] = {0};
    if (!length || !strcmp(leaf, ".") || !strcmp(leaf, "..") ||
        leaf[length - 1U] == '.' || leaf[length - 1U] == ' ') return false;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)leaf[i];
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) return false;
    }
    if (stem <= 4U) {
        for (i = 0; i < stem; ++i) {
            unsigned char c = (unsigned char)leaf[i];
            device[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
        if (!strcmp(device, "CON") || !strcmp(device, "PRN") ||
            !strcmp(device, "AUX") || !strcmp(device, "NUL") ||
            (stem == 4U && (!memcmp(device, "COM", 3) || !memcmp(device, "LPT", 3)) &&
             device[3] >= '1' && device[3] <= '9')) return false;
    }
    return true;
}

static const char *sp_leaf(const char *path) {
    const char *leaf = path, *p;
    for (p = path; *p; ++p) if (*p == '/' || *p == '\\') leaf = p + 1;
    return leaf;
}

static char *sp_output_path(const char *directory, const char *leaf) {
    size_t dl = strlen(directory), nl = strlen(leaf);
    char *path;
    if (dl > SIZE_MAX - nl - 2U) return NULL;
    path = (char *)malloc(dl + nl + 2U);
    if (path) snprintf(path, dl + nl + 2U, "%s/%s", directory, leaf);
    return path;
}

/* Never follow a pre-existing link in a new volume's path while creating its
 * folders (creation runs outside any extraction scope). */
static bool sp_safe_parents(const char *path) {
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path), *full;
    DWORD length, written = 0;
    size_t i, start = 3U;
    bool safe = true;
    if (!wide) return false;
    length = GetFullPathNameW(wide, 0, NULL, NULL);
    full = length ? (wchar_t *)malloc((size_t)length * sizeof(*full)) : NULL;
    if (full) written = GetFullPathNameW(wide, length, full, NULL);
    if (!full || !written || written >= length) {
        xx_str_wfree(wide); free(full); return false;
    }
    xx_str_wfree(wide);
    if (full[0] == '\\' && full[1] == '\\') {
        start = 2U;
        while (full[start] && full[start] != '\\') ++start;
        if (full[start]) ++start;
        while (full[start] && full[start] != '\\') ++start;
        if (full[start]) ++start;
    }
    for (i = start; full[i]; ++i) if (full[i] == '\\' || full[i] == '/') {
        wchar_t saved = full[i];
        DWORD attributes, error;
        full[i] = 0;
        attributes = GetFileAttributesW(full); error = GetLastError();
        full[i] = saved;
        if ((attributes != INVALID_FILE_ATTRIBUTES &&
             (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))) ||
            (attributes == INVALID_FILE_ATTRIBUTES && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
            safe = false; break;
        }
    }
    free(full); return safe;
#else
    size_t length = strlen(path), i;
    char *copy = (char *)malloc(length + 1U);
    bool safe = true;
    if (!copy) return false;
    memcpy(copy, path, length + 1U);
    for (i = 1; i < length; ++i) if (copy[i] == '/') {
        struct stat info;
        int status;
        copy[i] = 0;
        status = lstat(copy, &info);
        if ((status == 0 && !S_ISDIR(info.st_mode)) ||
            (status != 0 && errno != ENOENT)) safe = false;
        copy[i] = '/';
        if (!safe) break;
    }
    free(copy); return safe;
#endif
}

static int sp_create(const xfu_request *request, xx_pd_struct *pd) {
    sp_part *parts = NULL;
    char *prefix = NULL;
    unsigned char *buffer = NULL;
    xx_io_device *source = NULL, *output = NULL;
    const char *failure = NULL;
    uint64_t volume = request->volume_size ? request->volume_size : SP_DEFAULT_VOLUME;
    uint64_t loaded = 0, count = 0;
    int64_t source_size;
    unsigned i, created = 0;
    int result = 2, progress_level = -1, kind;
    if (request->file_count != 1 || !request->files || !request->files[0] ||
        request->password || (request->archive_options && request->archive_options[0]) ||
        (request->archive_filter && strcmp(request->archive_filter, "none")) ||
        (request->compression_level_set && request->compression_level != 0) ||
        (request->compression_method && strcmp(request->compression_method, "store") &&
         strcmp(request->compression_method, "stored") && strcmp(request->compression_method, "split"))) {
        sp_log(request, true, "split requires one regular file and no encryption, compression or filter options");
        return 2;
    }
    prefix = sp_prefix(request->archive_path);
    if (!prefix || !sp_leaf_safe(sp_leaf(prefix))) { failure = "unsafe split series basename"; goto done; }
    if (!sp_probe(request->files[0], &kind, NULL) || kind != 1 ||
        !(source = xx_io_file_open(request->files[0], "rb")) ||
        (source_size = xx_io_total_size(source)) < 0) {
        failure = "split input must be a readable regular file"; goto done;
    }
    count = source_size ? 1U + ((uint64_t)source_size - 1U) / volume : 1U;
    if (count > SP_MAX_PARTS) { failure = "split would exceed 999 volumes"; goto done; }
    parts = (sp_part *)calloc((size_t)count, sizeof(*parts));
    buffer = (unsigned char *)malloc(SP_BUFFER_SIZE);
    if (!parts || !buffer) { failure = "cannot allocate split workspace"; goto done; }
    for (i = 0; i <= SP_MAX_PARTS + 1U; ++i) {
        char *path = sp_part_path(prefix, i);
        bool valid = path && sp_probe(path, &kind, NULL);
        free(path);
        if (sp_cancelled(request, pd)) { result = 1; goto done; }
        if (!valid || kind) { failure = "split destination series already exists or cannot be inspected"; goto done; }
    }
    progress_level = xx_pd_enter_level(pd, (uint64_t)source_size, "creating split volumes");
    for (i = 0; i < (unsigned)count; ++i) {
        uint64_t part_size = (uint64_t)source_size - loaded;
        uint64_t position = 0;
        if (part_size > volume) part_size = volume;
        parts[i].path = sp_part_path(prefix, i + 1U);
        if (!parts[i].path || !sp_safe_parents(parts[i].path) ||
            !xx_io_create_dirs_a(parts[i].path, false) ||
            !(output = sp_stage(parts[i].path, &parts[i].stage, &parts[i]))) {
            failure = "cannot create split staging file"; goto done;
        }
        created = i + 1U;
        while (position < part_size) {
            size_t wanted = part_size - position > SP_BUFFER_SIZE ? SP_BUFFER_SIZE : (size_t)(part_size - position);
            ssize_t received;
            if (sp_cancelled(request, pd)) { result = 1; goto done; }
            received = xx_io_read(source, buffer, wanted);
            if (received <= 0 || (size_t)received > wanted ||
                !sp_write(request, pd, output, buffer, (size_t)received)) {
                failure = "cannot read or write complete split data"; goto done;
            }
            position += (size_t)received; loaded += (size_t)received;
            xx_pd_set_current(pd, progress_level, loaded);
            if (request->callbacks.progress)
                request->callbacks.progress(request->callbacks.user, i, count, parts[i].path);
        }
        if (xx_io_close(output) != 0) { output = NULL; failure = "cannot finalize split volume"; goto done; }
        output = NULL;
        if (!sp_probe(parts[i].stage, &kind, &parts[i].identity) || kind != 1) {
            failure = "cannot inspect staged split volume"; goto done;
        }
        /* A volume cut by a full disk or a file size limit is never published. */
        if (sp_file_size(parts[i].stage) != (int64_t)part_size) {
            failure = "cannot read or write complete split data"; goto done;
        }
    }
    if (xx_io_read(source, buffer, 1) != 0 || xx_io_total_size(source) != source_size) {
        failure = "split input changed during creation"; goto done;
    }
    if (xx_io_close(source) != 0) { source = NULL; failure = "cannot close split input"; goto done; }
    source = NULL;
    for (i = 0; i < (unsigned)count; ++i) {
        if (sp_cancelled(request, pd)) { result = 1; goto done; }
        if (!xx_io_file_replace_a(parts[i].stage, parts[i].path, false)) {
            failure = "cannot publish split volume without overwriting existing data"; goto done;
        }
        parts[i].published = true;
    }
    if (sp_cancelled(request, pd)) { result = 1; goto done; }
    result = 0;
    if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, count, count, NULL);
    /* The native writer's shape: the file added, then the archive summary. */
    sp_log(request, false, "  %s", request->files[0]);
    sp_log(request, false, "%s.001: 1 file(s) as split in %llu volume(s)", prefix, (unsigned long long)count);
done:
    if (source) xx_io_close(source);
    if (output) xx_io_close(output);
    if (parts) for (i = 0; i < (unsigned)count; ++i) {
        if (result && parts[i].published) {
            sp_identity current;
            if (sp_probe(parts[i].path, &kind, &current) && kind == 1 &&
                sp_same_identity(&parts[i].identity, &current)) (void)xx_io_file_remove_a(parts[i].path);
        }
        if (parts[i].stage && (result || i >= created)) (void)xx_io_file_remove_a(parts[i].stage);
        free(parts[i].stage); free(parts[i].path);
    }
    free(parts); free(prefix); free(buffer);
    xx_pd_leave_level(pd, progress_level);
    if (result && sp_cancelled(request, pd)) { sp_log(request, true, "operation cancelled"); result = 1; }
    else if (failure) sp_log(request, true, "%s: %s", request->archive_path, failure);
    return result;
}

static int sp_read(const xfu_request *request, xx_pd_struct *pd) {
    sp_part *parts = NULL;
    char *prefix = NULL, *destination = NULL, *stage = NULL;
    unsigned char *buffer = NULL;
    xx_io_device *source = NULL, *output = NULL;
    const char *failure = NULL, *name = NULL;
    uint64_t total = 0, loaded = 0;
    unsigned count = 0, i, percent = 0;
    int result = 2, progress_level = -1, kind;
    bool gap = false, complete = false, entry_emitted = false, cut, replacing = false;
    xx_file_type_t types[2] = {XX_FILE_TYPE_BINARY, XX_FILE_TYPE_NUMBERED_SPLIT};
    xfu_entry entry = {0};
    if (!xfu_split_is_first_volume(request->archive_path) ||
        (request->file_type != XX_FILE_TYPE_UNKNOWN && request->file_type != XX_FILE_TYPE_NUMBERED_SPLIT) ||
        (request->reader_name && strcmp(request->reader_name, "split")) ||
        (request->archive_options && request->archive_options[0])) {
        sp_log(request, true, "split reading requires the .001 first volume and a matching interpretation");
        return 2;
    }
    if (request->callbacks.file_types)
        request->callbacks.file_types(request->callbacks.user, types, 2, XX_FILE_TYPE_NUMBERED_SPLIT);
    if (request->callbacks.unpack_handle_types) {
        xx_unpack_handle_t handle = {XX_FILE_TYPE_NUMBERED_SPLIT, XX_UNPACK_CARRIER_NONE};
        request->callbacks.unpack_handle_types(request->callbacks.user, &handle, 1, handle);
    }
    if (request->extract_selected && (request->selected_record_count != 1 ||
        !request->selected_records || request->selected_records[0] != 0)) {
        failure = "split has only record index 0"; goto done;
    }
    prefix = sp_prefix(request->archive_path);
    name = prefix ? sp_leaf(prefix) : NULL;
    if (!name || !sp_leaf_safe(name)) { failure = "unsafe split payload basename"; goto done; }
    parts = (sp_part *)calloc(SP_MAX_PARTS, sizeof(*parts));
    buffer = (unsigned char *)malloc(SP_BUFFER_SIZE);
    if (!parts || !buffer) { failure = "cannot allocate split workspace"; goto done; }
    for (i = 0; i < SP_MAX_PARTS; ++i) {
        sp_part *part = &parts[i];
        if (sp_cancelled(request, pd)) { result = 1; goto done; }
        part->path = sp_part_path(prefix, i + 1U);
        if (!part->path || !sp_probe(part->path, &kind, &part->identity)) {
            failure = "cannot inspect split volume"; goto done;
        }
        if (!kind) { gap = true; continue; }
        if (kind != 1 || gap) { failure = "split volumes must be regular and contiguous from .001"; goto done; }
        source = xx_io_file_open(part->path, "rb");
        if (!source || (part->size = xx_io_total_size(source)) < 0 ||
            (uint64_t)part->size > (uint64_t)INT64_MAX - total) {
            failure = "cannot read split volume size"; goto done;
        }
        total += (uint64_t)part->size;
        if (xx_io_close(source) != 0) { source = NULL; failure = "cannot close split volume"; goto done; }
        source = NULL; ++count;
    }
    {
        char *overflow = sp_part_path(prefix, SP_MAX_PARTS + 1U);
        bool valid = overflow && sp_probe(overflow, &kind, NULL);
        free(overflow);
        if (!valid || kind) { failure = "split series exceeds the 999-volume limit"; goto done; }
    }
    if (!count) { failure = "first split volume is missing"; goto done; }
    for (i = 0; i < count; ++i)
        if ((i + 1U < count && parts[i].size != parts[0].size) ||
            parts[i].size > parts[0].size || (count > 1U && !parts[i].size)) {
            failure = "split volume sizes are inconsistent"; goto done;
        }
    entry.name = name; entry.packed_size = (int64_t)total; entry.unpacked_size = (int64_t)total;
    /* Logged like the native walk: the type, each member, then a summary. */
    sp_log(request, false, "%s: %s", request->archive_path,
        xx_format_file_type_to_string(XX_FILE_TYPE_NUMBERED_SPLIT));
    if (request->command == XFU_COMMAND_LIST)
        sp_log(request, false, "  %12lld  %s", (long long)entry.packed_size, entry.name);
    if (request->callbacks.entry) request->callbacks.entry(request->callbacks.user, &entry);
    entry_emitted = true;
    if (request->command == XFU_COMMAND_LIST) { complete = true; result = 0; goto done; }
    if (request->command == XFU_COMMAND_EXTRACT) {
        destination = sp_output_path(request->output_dir ? request->output_dir : ".", name);
        /* Read under core's extraction scope: the output folder and the
         * folders above it may be links (xx_io_create_dirs_a follows those
         * alone). What is at the payload's own path, a link too, is
         * replaced by the rename below, as the walk replaces it (core.h,
         * output_dir); a folder there is refused. */
        if (!destination || !sp_probe(destination, &kind, NULL) || kind == 2) {
            failure = "unsafe split output path"; goto done;
        }
        replacing = kind != 0;
        for (i = 0; i < count; ++i) {
            sp_identity target;
            if (!sp_probe(destination, &kind, &target) ||
                (kind == 1 && sp_same_identity(&target, &parts[i].identity))) {
                failure = "split output aliases an input volume"; goto done;
            }
        }
        if (!xx_io_create_dirs_a(destination, false) || !(output = sp_stage(destination, &stage, parts))) {
            failure = "cannot create split extraction staging file"; goto done;
        }
    }
    progress_level = xx_pd_enter_level(pd, total, "reading split volumes");
    if (request->command == XFU_COMMAND_TEST && request->callbacks.test_progress)
        request->callbacks.test_progress(request->callbacks.user, 0);
    for (i = 0; i < count; ++i) {
        uint64_t position = 0;
        sp_identity current;
        /* The series' one member is read volume by volume: the volumes are
         * the steps (as for creation), so a bar moves before the end. */
        if (request->callbacks.progress)
            request->callbacks.progress(request->callbacks.user, i, count, parts[i].path);
        if (!sp_probe(parts[i].path, &kind, &current) || kind != 1 ||
            !sp_same_identity(&current, &parts[i].identity) ||
            !(source = xx_io_file_open(parts[i].path, "rb")) ||
            xx_io_total_size(source) != parts[i].size) {
            failure = "split volume changed before reading"; goto done;
        }
        while (position < (uint64_t)parts[i].size) {
            size_t wanted = (uint64_t)parts[i].size - position > SP_BUFFER_SIZE ?
                SP_BUFFER_SIZE : (size_t)((uint64_t)parts[i].size - position);
            ssize_t received;
            if (sp_cancelled(request, pd)) { result = 1; goto done; }
            received = xx_io_read(source, buffer, wanted);
            if (received <= 0 || (size_t)received > wanted ||
                (output && !sp_write(request, pd, output, buffer, (size_t)received))) {
                failure = "cannot read or write complete split payload"; goto done;
            }
            position += (size_t)received; loaded += (size_t)received;
            xx_pd_set_current(pd, progress_level, loaded);
            if (request->command == XFU_COMMAND_TEST && total && request->callbacks.test_progress) {
                unsigned next = (unsigned)((long double)loaded * 100.0L / (long double)total);
                if (next > 99U) next = 99U;
                if (next > percent) { percent = next; request->callbacks.test_progress(request->callbacks.user, percent); }
            }
        }
        if (xx_io_read(source, buffer, 1) != 0 || xx_io_total_size(source) != parts[i].size) {
            failure = "split volume changed during reading"; goto done;
        }
        if (xx_io_close(source) != 0) { source = NULL; failure = "cannot close split volume"; goto done; }
        source = NULL;
    }
    if (sp_cancelled(request, pd)) { result = 1; goto done; }
    if (output) {
        if (xx_io_close(output) != 0) { output = NULL; failure = "cannot close split output"; goto done; }
        output = NULL;
        if (sp_cancelled(request, pd)) { result = 1; goto done; }
        if (!xx_io_file_replace_a(stage, destination, true)) { failure = "cannot publish split output"; goto done; }
    }
    complete = true; result = 0;
    if (request->callbacks.progress) request->callbacks.progress(request->callbacks.user, count, count, NULL);
done:
    if (source) xx_io_close(source);
    if (output) xx_io_close(output);
    if (stage && result) (void)xx_io_file_remove_a(stage);
    cut = result && sp_cancelled(request, pd);
    if (cut) result = 1;
    if (entry_emitted) {
        /* The member was started: as in the native walk, one member line and
         * a summary. A member cut short by Cancel is neither reported as
         * failed nor counted; a failed one leaves a partial failure (1). */
        if (cut) sp_log(request, true, "  %s -- cancelled", name);
        else if (request->command != XFU_COMMAND_LIST) {
            if (request->command == XFU_COMMAND_TEST && request->callbacks.test_result)
                request->callbacks.test_result(request->callbacks.user, &entry, !result);
            else if (!result && replacing)
                sp_log(request, false, "  %s -- replaced an existing file", name);
            else if (!result)
                sp_log(request, false, request->command == XFU_COMMAND_TEST ? "  %s -- OK" : "  %s", name);
            else sp_log(request, true, "  %s -- FAILED", name);
            if (result && failure) sp_log(request, true, "%s: %s", request->archive_path, failure);
            complete = true; /* The walk reached the end; its member may have failed. */
            /* As the native walk: 100% only for a member read in full. */
            if (!result && request->command == XFU_COMMAND_TEST && request->callbacks.test_progress)
                request->callbacks.test_progress(request->callbacks.user, 100);
            if (result) result = 1;
        }
        if (result && !cut)
            sp_log(request, request->command == XFU_COMMAND_TEST, "1 member(s), 1 failed");
        else sp_log(request, false, "%u member(s)", cut ? 0U : 1U);
    } else if (cut) sp_log(request, true, "operation cancelled");
    else if (failure) sp_log(request, true, "%s: %s", request->archive_path, failure);
    if (request->callbacks.walk_end) request->callbacks.walk_end(request->callbacks.user, complete);
    if (parts) for (i = 0; i < SP_MAX_PARTS; ++i) free(parts[i].path);
    free(parts); free(prefix); free(buffer); free(destination); free(stage);
    xx_pd_leave_level(pd, progress_level);
    return result;
}

int xfu_split_run(const xfu_request *request, xx_pd_struct *pd) {
    if (!request || !request->archive_path || !request->archive_path[0]) return 2;
    if (request->command == XFU_COMMAND_ADD) return sp_create(request, pd);
    if (request->command == XFU_COMMAND_LIST || request->command == XFU_COMMAND_TEST ||
        request->command == XFU_COMMAND_EXTRACT) return sp_read(request, pd);
    return 2;
}

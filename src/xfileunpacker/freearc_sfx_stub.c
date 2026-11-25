/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <xxfclib/formats/archive/freearc/xx_freearc.h>
#include "freearc_sfx_locator.h"
#include "freearc_sfx_names.h"

#define SFX_MEMBER_LIMIT 100000U
static volatile LONG interrupted;

typedef struct sfx_member {
    char *name;
    wchar_t *wide;
    uint64_t hash;
    bool directory;
} sfx_member;

static BOOL WINAPI control_handler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        InterlockedExchange(&interrupted, 1);
        return TRUE;
    }
    return FALSE;
}

static bool observe(const xx_pd_struct *pd, void *context)
{
    (void)pd;
    (void)context;
    return InterlockedCompareExchange(&interrupted, 0, 0) != 0;
}

static int name_compare(const void *a, const void *b)
{
    const sfx_member *left = (const sfx_member *)a, *right = (const sfx_member *)b;
    int result = CompareStringOrdinal(left->wide, -1, right->wide, -1, TRUE);
    return result == CSTR_LESS_THAN ? -1 : result == CSTR_GREATER_THAN ? 1 : 0;
}

static int hash_compare(const void *a, const void *b)
{
    uint64_t left = ((const sfx_member *)a)->hash, right = ((const sfx_member *)b)->hash;
    return left < right ? -1 : left > right ? 1 : 0;
}

static bool validate_names(sfx_member *members, size_t count)
{
    size_t i;
    if (count < 2) return true;
    qsort(members, count, sizeof(*members), hash_compare);
    for (i = 1; i < count; ++i)
        if (members[i - 1].hash == members[i].hash) return false;
    qsort(members, count, sizeof(*members), name_compare);
    for (i = 1; i < count; ++i)
        if (!name_compare(&members[i - 1], &members[i])) return false;
    for (i = 0; i < count; ++i) {
        wchar_t *copy = _wcsdup(members[i].wide);
        wchar_t *p;
        if (!copy) return false;
        for (p = copy; *p; ++p)
            if (*p == L'/') {
                sfx_member key = {0}, *parent;
                *p = 0;
                key.wide = copy;
                parent = (sfx_member *)bsearch(&key, members, count, sizeof(*members), name_compare);
                *p = L'/';
                if (parent && !parent->directory) {
                    free(copy);
                    return false;
                }
            }
        free(copy);
    }
    return true;
}

static wchar_t *join_path(const wchar_t *base, const wchar_t *name)
{
    size_t a = wcslen(base), b = wcslen(name);
    wchar_t *path;
    if (a + b + 2 > 32760) return NULL;
    path = (wchar_t *)malloc((a + b + 2) * sizeof(wchar_t));
    if (path) {
        memcpy(path, base, a * sizeof(wchar_t));
        path[a] = L'/';
        wcscpy(path + a + 1, name);
    }
    return path;
}

static bool secure_dirs(const wchar_t *path, bool directory)
{
    char *utf8 = xx_str_unicode_to_utf8(path);
    bool result = utf8 && xx_io_create_dirs_a(utf8, directory);
    xx_str_free(utf8);
    return result;
}

static wchar_t *private_stage(const wchar_t *root)
{
    unsigned attempt;
    for (attempt = 0; attempt < 32; ++attempt) {
        unsigned char random[16];
        wchar_t name[64], *path;
        unsigned i;
        if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) return NULL;
        wcscpy(name, L".xfu-freearc-sfx-");
        for (i = 0; i < 16; ++i) swprintf(name + 17 + i * 2, 3, L"%02x", (unsigned)random[i]);
        path = join_path(root, name);
        if (!path) return NULL;
        if (CreateDirectoryW(path, NULL)) return path;
        free(path);
        if (GetLastError() != ERROR_ALREADY_EXISTS) return NULL;
    }
    return NULL;
}

/* Only called for our exclusively created private tree; never follows links. */
static void remove_stage(const wchar_t *root, unsigned depth)
{
    wchar_t *pattern = join_path(root, L"*");
    WIN32_FIND_DATAW data;
    HANDLE search;
    if (!pattern || depth > 128) {
        free(pattern);
        return;
    }
    search = FindFirstFileW(pattern, &data);
    free(pattern);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *path;
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            path = join_path(root, data.cFileName);
            if (!path) continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_stage(path, depth + 1);
            else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
            else DeleteFileW(path);
            free(path);
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    RemoveDirectoryW(root);
}

static bool same_file(const wchar_t *left, const wchar_t *right)
{
    HANDLE a = CreateFileW(left, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE b = CreateFileW(right, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    BY_HANDLE_FILE_INFORMATION ai, bi;
    bool same = a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE && GetFileInformationByHandle(a, &ai) && GetFileInformationByHandle(b, &bi) &&
                ai.dwVolumeSerialNumber == bi.dwVolumeSerialNumber && ai.nFileIndexHigh == bi.nFileIndexHigh && ai.nFileIndexLow == bi.nFileIndexLow;
    if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
    if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
    return same;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t self[32768], output[32768], *stage = NULL;
    const wchar_t *requested_output = L".";
    char *self_utf8 = NULL, *stage_utf8 = NULL;
    xx_io_device *source = NULL;
    xx_freearc *archive = NULL;
    xx_archive_record_state *state = NULL;
    xx_pd_struct pd = xx_pd_init();
    xx_pd_observer previous;
    xx_list_t options;
    sfx_member *members = NULL;
    size_t count = 0, capacity = 0, name_bytes = 0, i;
    bool overwrite = false, extract = true, list = false, options_ready = false;
    int result = 1, arg;
    int64_t base;
    const char *failure = "cannot open embedded FreeArc archive";
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(control_handler, TRUE);
    previous = xx_pd_set_observer(&pd, observe, NULL);
    for (arg = 1; arg < argc; ++arg) {
        if (!wcscmp(argv[arg], L"t")) {
            extract = false;
            list = false;
        } else if (!wcscmp(argv[arg], L"l")) {
            extract = false;
            list = true;
        } else if (!wcscmp(argv[arg], L"x")) {
            extract = true;
            list = false;
        } else if (!wcscmp(argv[arg], L"-y")) overwrite = true;
        else if (!wcscmp(argv[arg], L"-o") && arg + 1 < argc) requested_output = argv[++arg];
        else if (!wcsncmp(argv[arg], L"-o", 2) && argv[arg][2]) requested_output = argv[arg] + 2;
        else {
            failure = "usage: archive.exe [x|t|l] [-y] [-oDIRECTORY]";
            goto done;
        }
    }
    SetLastError(ERROR_SUCCESS);
    if (!GetModuleFileNameW(NULL, self, 32768) || GetLastError() == ERROR_INSUFFICIENT_BUFFER || !(self_utf8 = xx_str_unicode_to_utf8(self)) ||
        !(source = xx_io_file_open(self_utf8, "rb")) || (base = fs_locate_payload(source)) < 0 || !(archive = xx_freearc_create(source, base)) ||
        !xx_freearc_check_is_valid(&archive->format, &pd) || !xx_freearc_handle_base_info(&archive->format, &pd) ||
        archive->archive_size != xx_io_total_size(source) - base || !(state = xx_freearc_create_archive_records_reading(&archive->format, NULL, &pd)))
        goto done;
    failure = "unsafe, ambiguous or excessive archive member names";
    while (xx_freearc_get_current_archive_record(&archive->format, state)) {
        const xx_archive_record *record = xx_freearc_get_current_archive_record(&archive->format, state);
        const char *name = xx_archive_record_get_original_name(record);
        sfx_member *member;
        if (xx_pd_is_stopped(&pd) || count >= SFX_MEMBER_LIMIT || !fs_safe_name(name)) goto done;
        if (strlen(name) + 1 > 67108864U - name_bytes) goto done;
        name_bytes += strlen(name) + 1;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 64;
            sfx_member *grown = (sfx_member *)realloc(members, next * sizeof(*members));
            if (!grown) goto done;
            members = grown;
            capacity = next;
        }
        member = &members[count++];
        memset(member, 0, sizeof(*member));
        member->name = _strdup(name);
        member->wide = xx_str_utf8_to_unicode(name);
        member->directory = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false);
        if (!member->name || !member->wide) goto done;
        member->hash = fs_name_hash(name);
        if (list) printf("%llu %s\n", (unsigned long long)xx_archive_record_get_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, 0), name);
        if (!xx_freearc_archive_record_move_to_next(&archive->format, state, &pd)) break;
    }
    if (pd.last_error || !validate_names(members, count)) goto done;
    xx_freearc_free_archive_records_reading(&archive->format, state);
    state = NULL;
    if (list) {
        result = 0;
        goto done;
    }
    failure = "archive data or checksum validation failed";
    state = xx_freearc_create_archive_records_reading(&archive->format, NULL, &pd);
    if (!state) goto done;
    for (i = 0; i < count; ++i) {
        if (xx_pd_is_stopped(&pd) || !xx_freearc_get_current_archive_record(&archive->format, state) ||
            !xx_freearc_unpack_current_archive_record(&archive->format, state, &pd))
            goto done;
        if (i + 1 < count && !xx_freearc_archive_record_move_to_next(&archive->format, state, &pd)) goto done;
    }
    xx_freearc_free_archive_records_reading(&archive->format, state);
    state = NULL;
    if (!extract) {
        result = 0;
        goto done;
    }
    failure = "cannot safely prepare extraction directory";
    {
        DWORD length = GetFullPathNameW(requested_output, 32768, output, NULL);
        if (!length || length >= 32768 || !secure_dirs(output, true)) goto done;
    }
    for (i = 0; i < count; ++i) {
        wchar_t *path = join_path(output, members[i].wide);
        DWORD attributes;
        bool safe;
        if (!path) goto done;
        attributes = GetFileAttributesW(path);
        safe =
            secure_dirs(path, members[i].directory) && !same_file(path, self) &&
            (members[i].directory || attributes == INVALID_FILE_ATTRIBUTES || (overwrite && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))));
        free(path);
        if (!safe) {
            failure = "unsafe or existing destination (use -y to replace regular files)";
            goto done;
        }
    }
    if (!(stage = private_stage(output)) || !(stage_utf8 = xx_str_unicode_to_utf8(stage))) goto done;
    xx_list_init(&options, sizeof(xx_meta), xx_meta_free_elem);
    options_ready = true;
    {
        xx_meta option;
        xx_meta_init(&option, XX_META_ID_OPT_UNPACK_PATH);
        if (!xx_var_set_str(&option.var, stage_utf8) || !xx_list_append(&options, &option)) {
            xx_meta_cleanup(&option);
            goto done;
        }
    }
    failure = "cannot extract archive into private staging directory";
    state = xx_freearc_create_archive_records_reading(&archive->format, &options, &pd);
    if (!state) goto done;
    for (i = 0; i < count; ++i) {
        if (xx_pd_is_stopped(&pd) || !xx_freearc_get_current_archive_record(&archive->format, state) ||
            !xx_freearc_unpack_current_archive_record(&archive->format, state, &pd))
            goto done;
        if (i + 1 < count && !xx_freearc_archive_record_move_to_next(&archive->format, state, &pd)) goto done;
    }
    failure = "cannot publish extracted archive member";
    for (i = 0; i < count; ++i) {
        wchar_t *from = join_path(stage, members[i].wide), *to = join_path(output, members[i].wide);
        bool published = from && to && !xx_pd_is_stopped(&pd) && secure_dirs(to, members[i].directory) && !same_file(to, self) &&
                         (members[i].directory || xx_io_file_replace_w(from, to, overwrite));
        free(from);
        free(to);
        if (!published) goto done;
        printf("%s\n", members[i].name);
    }
    result = 0;
done:
    if (state) xx_freearc_free_archive_records_reading(&archive->format, state);
    if (options_ready) xx_list_cleanup(&options);
    if (archive) xx_freearc_free(archive);
    if (source) xx_io_close(source);
    if (stage) remove_stage(stage, 0);
    for (i = 0; i < count; ++i) {
        free(members[i].name);
        xx_str_wfree(members[i].wide);
    }
    free(members);
    free(stage);
    xx_str_free(stage_utf8);
    xx_str_free(self_utf8);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    SetConsoleCtrlHandler(control_handler, FALSE);
    if (result) fprintf(stderr, "%s\n", xx_pd_is_stopped(&pd) ? "operation cancelled" : failure);
    else printf("%llu archive members %s\n", (unsigned long long)count, extract ? "extracted" : list ? "listed" : "tested");
    return result;
}

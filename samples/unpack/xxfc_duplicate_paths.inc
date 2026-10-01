/* CLI-only preservation of members whose filesystem paths collide.  The
 * readers still see the requested output root and their normal member name;
 * immediately before an overwrite, the previous file is copied to a unique
 * sibling path.  No existing output file is ever used as a backup target. */

#include <xxfclib/algo/store/xx_store.h>
#include <xxfclib/io/xx_io.h>

#include <stdint.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
/* Some strict C11 POSIX builds hide the XSI declaration from stdlib.h. */
extern char *realpath(const char *restrict path,
                      char *restrict resolved_path);
#endif

typedef struct xxfc_seen_paths_s {
    char **slots;
    size_t capacity;
    size_t count;
} xxfc_seen_paths;

static bool xxfc_safe_relative_name(const char *name) {
    const char *part, *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        (name[0] && name[1] == ':')) return false;
    part = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '?' || c == '*' || c == '"' || c == '<' ||
            c == '>' || c == '|' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t n = (size_t)(at - part);
            if (n == 0U || (n == 1U && part[0] == '.') ||
                (n == 2U && part[0] == '.' && part[1] == '.') ||
                part[n - 1U] == ' ' || part[n - 1U] == '.') return false;
            if (c == 0U) return true;
            part = at + 1;
        }
    }
}

static char *xxfc_normalized_name(const char *name) {
    size_t n = strlen(name), i;
    char *key = (char *)malloc(n + 1U);
    if (!key) return NULL;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        key[i] = (char)(c == '\\' ? '/' :
                        c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    key[n] = 0;
    return key;
}

static size_t xxfc_name_hash(const char *name) {
    uint32_t value = UINT32_C(2166136261);
    while (*name) {
        value ^= (unsigned char)*name++;
        value *= UINT32_C(16777619);
    }
    return (size_t)value;
}

static bool xxfc_seen_contains(const xxfc_seen_paths *seen, const char *key) {
    size_t at;
    if (!seen->capacity) return false;
    at = xxfc_name_hash(key) & (seen->capacity - 1U);
    while (seen->slots[at]) {
        if (strcmp(seen->slots[at], key) == 0) return true;
        at = (at + 1U) & (seen->capacity - 1U);
    }
    return false;
}

static bool xxfc_seen_insert(xxfc_seen_paths *seen, char *key) {
    size_t at, index;
    if (!key) return false;
    if (!seen->capacity || (seen->count + 1U) * 4U >= seen->capacity * 3U) {
        size_t next = seen->capacity ? seen->capacity * 2U : 256U;
        char **slots;
        if (next <= seen->capacity || next > SIZE_MAX / sizeof(*slots))
            return false;
        slots = (char **)calloc(next, sizeof(*slots));
        if (!slots) return false;
        for (index = 0U; index < seen->capacity; ++index) {
            char *old = seen->slots[index];
            if (!old) continue;
            at = xxfc_name_hash(old) & (next - 1U);
            while (slots[at]) at = (at + 1U) & (next - 1U);
            slots[at] = old;
        }
        free(seen->slots);
        seen->slots = slots;
        seen->capacity = next;
    }
    at = xxfc_name_hash(key) & (seen->capacity - 1U);
    while (seen->slots[at]) {
        if (strcmp(seen->slots[at], key) == 0) {
            free(key);
            return true;
        }
        at = (at + 1U) & (seen->capacity - 1U);
    }
    seen->slots[at] = key;
    ++seen->count;
    return true;
}

static void xxfc_seen_cleanup(xxfc_seen_paths *seen) {
    size_t index;
    for (index = 0U; index < seen->capacity; ++index)
        free(seen->slots[index]);
    free(seen->slots);
    memset(seen, 0, sizeof(*seen));
}

static char *xxfc_join_output_path(const char *root, const char *relative) {
    size_t a = strlen(root), b = strlen(relative);
    bool separator = a != 0U && root[a - 1U] != '/' && root[a - 1U] != '\\';
    char *path;
    if (a > SIZE_MAX - b - 2U) return NULL;
    path = (char *)malloc(a + b + 2U);
    if (!path) return NULL;
    memcpy(path, root, a);
    if (separator) path[a++] = '/';
    memcpy(path + a, relative, b + 1U);
    return path;
}

#ifdef _WIN32
static wchar_t *xxfc_final_path(const char *path, bool directory) {
    wchar_t *wide = xx_str_utf8_to_unicode(path), *resolved = NULL;
    HANDLE handle;
    DWORD length;
    if (!wide) return NULL;
    handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING,
                         directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U, NULL);
    xx_str_free_unicode(wide);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    length = GetFinalPathNameByHandleW(handle, NULL, 0U, FILE_NAME_NORMALIZED);
    if (length != 0U && length < 32768U &&
        length < (SIZE_MAX / sizeof(*resolved)) - 1U) {
        resolved = (wchar_t *)malloc(((size_t)length + 1U) * sizeof(*resolved));
        if (resolved) {
            DWORD actual = GetFinalPathNameByHandleW(
                handle, resolved, length + 1U, FILE_NAME_NORMALIZED);
            if (actual == 0U || actual > length) {
                free(resolved);
                resolved = NULL;
            }
        }
    }
    CloseHandle(handle);
    return resolved;
}
#endif

static bool xxfc_regular_file_under_root(const char *root, const char *path) {
#ifdef _WIN32
    wchar_t *base = xxfc_final_path(root, true);
    wchar_t *file = xxfc_final_path(path, false);
    size_t length;
    bool valid = false;
    if (base && file) {
        length = wcslen(base);
        while (length > 0U && (base[length - 1U] == L'\\' ||
                               base[length - 1U] == L'/')) --length;
        valid = wcslen(file) > length &&
                _wcsnicmp(base, file, length) == 0 &&
                (file[length] == L'\\' || file[length] == L'/');
    }
    free(base);
    free(file);
    return valid;
#else
    struct stat statbuf;
    char *base = realpath(root, NULL);
    char *file = realpath(path, NULL);
    size_t length;
    bool valid = false;
    if (base && file && stat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        length = strlen(base);
        while (length > 1U && base[length - 1U] == '/') --length;
        valid = strncmp(base, file, length) == 0 &&
                (length == 1U && base[0] == '/'
                     ? file[1] != 0
                     : file[length] == '/');
    }
    free(base);
    free(file);
    return valid;
#endif
}

static char *xxfc_duplicate_name(const char *name, unsigned long long number) {
    const char *last_slash = strrchr(name, '/');
    const char *last_backslash = strrchr(name, '\\');
    const char *leaf = !last_slash ? last_backslash :
                       !last_backslash ? last_slash :
                       last_slash > last_backslash ? last_slash : last_backslash;
    const char *dot = strrchr(name, '.');
    size_t prefix, size = strlen(name);
    char suffix[48], *result;
    int count = snprintf(suffix, sizeof(suffix), ".__xxfc_dup%llu", number);
    if (count <= 0 || (size_t)count >= sizeof(suffix)) return NULL;
    if (dot && (leaf == NULL || dot > leaf + 1U)) prefix = (size_t)(dot - name);
    else prefix = size;
    if (size > SIZE_MAX - (size_t)count - 1U) return NULL;
    result = (char *)malloc(size + (size_t)count + 1U);
    if (!result) return NULL;
    memcpy(result, name, prefix);
    memcpy(result + prefix, suffix, (size_t)count);
    memcpy(result + prefix + (size_t)count, name + prefix, size - prefix + 1U);
    return result;
}

static bool xxfc_copy_previous_member(const char *root, const char *name,
                                      unsigned long long ordinal,
                                      xxfc_seen_paths *seen, char **saved_name) {
    char *source = xxfc_join_output_path(root, name);
    unsigned attempt;
    bool result = false;
    *saved_name = NULL;
    if (!source) goto done;
    if (!xx_io_file_exists_a(source)) {
        result = true; /* Reader did not create this path; nothing to save. */
        goto done;
    }
    if (!xxfc_regular_file_under_root(root, source)) goto done;
    for (attempt = 0U; attempt < 1000U; ++attempt) {
        char *relative = xxfc_duplicate_name(name, ordinal + attempt);
        char *key = relative ? xxfc_normalized_name(relative) : NULL;
        char *destination = relative ? xxfc_join_output_path(root, relative) : NULL;
        xx_io_device *input = NULL, *output = NULL;
        bool created = false, copied = false;
        int64_t size;
        if (!relative || !key || !destination) {
            free(relative); free(key); free(destination);
            break;
        }
        if (xxfc_seen_contains(seen, key) || xx_io_file_exists_a(destination)) {
            free(relative); free(key); free(destination);
            continue;
        }
        input = xx_io_file_open(source, "rb");
        size = input ? xx_io_total_size(input) : -1;
        output = size >= 0 ? xx_io_file_open(destination, "wbx") : NULL;
        created = output != NULL;
        if (output) {
            copied = xx_store_unpack_device(input, 0, size, output, NULL);
            if (xx_io_close(output) != 0) copied = false;
            output = NULL;
        }
        if (copied) {
            if (xxfc_seen_insert(seen, key)) {
                key = NULL;
                *saved_name = relative;
                relative = NULL;
                result = true;
            }
        }
        if (created && !result)
            xx_io_file_remove_a(destination);
        if (input) xx_io_close(input);
        free(relative); free(key); free(destination);
        break;
    }
done:
    free(source);
    return result;
}

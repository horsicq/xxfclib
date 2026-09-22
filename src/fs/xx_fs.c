/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/fs/xx_fs.h"

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include "platforms/xx_fs_platform.h"

/* --------------------------------------------------------------- queries */

bool xx_fs_exists(const char *path) {
    return xx_fs_platform_stat(path) != XX_FS_PLATFORM_MISSING;
}

bool xx_fs_is_dir(const char *path) {
    return xx_fs_platform_stat(path) == XX_FS_PLATFORM_DIR;
}

bool xx_fs_is_file(const char *path) {
    return xx_fs_platform_stat(path) == XX_FS_PLATFORM_FILE;
}

char *xx_fs_read_file(const char *path, int64_t *size) {
    xx_io_device *device;
    int64_t total;
    char *data;
    ssize_t got;

    if (size) {
        *size = 0;
    }
    if (!path) {
        return NULL;
    }

    /* Reading goes through a file device rather than the runtime's stdio
     * wrappers, so the library has one code path for opening and sizing a
     * file no matter who asks. */
    device = xx_io_file_open(path, "rb");
    if (!device) {
        return NULL;
    }
    total = xx_io_total_size(device);
    if (total < 0) {
        xx_io_close(device);
        return NULL;
    }
    /* The whole file lands in one allocation, so the size has to fit a size_t
     * with room left for the terminator. */
    if ((uint64_t)total >= (uint64_t)(size_t)-1) {
        xx_io_close(device);
        return NULL;
    }
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) {
        xx_io_close(device);
        return NULL;
    }

    data = (char *)xx_mem_alloc((size_t)total + 1U);
    if (!data) {
        xx_io_close(device);
        return NULL;
    }
    got = total > 0 ? xx_io_read(device, data, (size_t)total) : 0;
    xx_io_close(device);

    if (got < 0) {
        xx_mem_free(data);
        return NULL;
    }
    /* A short read is reported as the shorter length rather than treated as a
     * failure: a file can legitimately shrink between the size query and the
     * read. */
    data[(size_t)got] = '\0';
    if (size) {
        *size = (int64_t)got;
    }
    return data;
}

/* ----------------------------------------------------------- directories */

void xx_fs_entry_free(xx_fs_entry_t *entry) {
    if (!entry) {
        return;
    }
    xx_str_free(entry->name);
    xx_str_free(entry->path);
    xx_mem_free(entry);
}

static xx_fs_entry_t *xx_fs_entry_create(const char *directory,
                                         const char *name,
                                         xx_fs_entry_type_t type) {
    xx_fs_entry_t *entry = (xx_fs_entry_t *)xx_mem_alloc(sizeof(*entry));
    if (!entry) {
        return NULL;
    }
    entry->name = xx_str_dup(name);
    entry->path = xx_fs_path_join(directory, name);
    entry->type = type;
    if (!entry->name || !entry->path) {
        xx_fs_entry_free(entry);
        return NULL;
    }
    return entry;
}

/* By name alone. Used when the caller's order is the directory's own name
 * order rather than a presentation order. */
static int xx_fs_entry_compare_name(const void *left, const void *right) {
    const xx_fs_entry_t *a = *(const xx_fs_entry_t *const *)left;
    const xx_fs_entry_t *b = *(const xx_fs_entry_t *const *)right;

    return xx_rt_strcmp(a->name, b->name);
}

/* Directories first, then by name, so a listing is stable across platforms
 * whose enumeration order differs. */
static int xx_fs_entry_compare(const void *left, const void *right) {
    const xx_fs_entry_t *a = *(const xx_fs_entry_t *const *)left;
    const xx_fs_entry_t *b = *(const xx_fs_entry_t *const *)right;

    if (a->type != b->type) {
        return a->type == XX_FS_ENTRY_DIR ? -1 : 1;
    }
    return xx_rt_strcmp(a->name, b->name);
}

static bool xx_fs_name_is_dot(const char *name) {
    return name && name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/* Context for xx_fs_collect_entry: where to append, and whether an append has
 * already failed. */
typedef struct xx_fs_collector_s {
    const char *directory;
    xx_list_t *list;
    bool ok;
} xx_fs_collector;

static bool xx_fs_collect_entry(void *context, const char *name, bool is_dir) {
    xx_fs_collector *collector = (xx_fs_collector *)context;
    xx_fs_entry_t *entry = xx_fs_entry_create(
        collector->directory, name, is_dir ? XX_FS_ENTRY_DIR : XX_FS_ENTRY_FILE);

    if (!entry || !xx_list_append(collector->list, &entry)) {
        xx_fs_entry_free(entry);
        collector->ok = false;
        return false;
    }
    return true;
}

bool xx_fs_list_dir(const char *path, xx_list_t *list) {
    return xx_fs_list_dir_sorted(path, list, XX_FS_SORT_DIRS_FIRST);
}

bool xx_fs_list_dir_sorted(const char *path, xx_list_t *list,
                           xx_fs_sort_t order) {
    xx_fs_collector collector;
    size_t first;

    if (!path || !list) {
        return false;
    }
    first = xx_list_count(list);
    collector.directory = path;
    collector.list = list;
    collector.ok = true;

    if (!xx_fs_platform_enumerate(path, xx_fs_collect_entry, &collector) ||
        !collector.ok) {
        return false;
    }

    {
        size_t added = xx_list_count(list) - first;
        if (added > 1U) {
            xx_rt_qsort((char *)xx_list_at(list, first), added,
                        sizeof(xx_fs_entry_t *),
                        (order == XX_FS_SORT_NAME) ? xx_fs_entry_compare_name
                                                   : xx_fs_entry_compare);
        }
    }
    return true;
}

bool xx_fs_find_files(const char *path, xx_list_t *list, bool recursive) {
    xx_list_t *entries;
    size_t index;
    size_t count;
    bool result = true;

    if (!path || !list) {
        return false;
    }
    if (xx_fs_is_file(path)) {
        char *copy = xx_str_dup(path);
        if (!copy || !xx_list_append(list, &copy)) {
            xx_str_free(copy);
            return false;
        }
        return true;
    }
    if (!xx_fs_is_dir(path)) {
        return false;
    }

    entries = xx_list_create(sizeof(xx_fs_entry_t *), NULL);
    if (!entries) {
        return false;
    }
    if (!xx_fs_list_dir(path, entries)) {
        xx_list_destroy(entries);
        return false;
    }

    count = xx_list_count(entries);
    for (index = 0U; index < count; ++index) {
        xx_fs_entry_t *entry = *(xx_fs_entry_t **)xx_list_at(entries, index);
        if (!entry) {
            continue;
        }
        if (entry->type == XX_FS_ENTRY_DIR) {
            if (recursive && result) {
                result = xx_fs_find_files(entry->path, list, true);
            }
        } else if (result) {
            char *copy = xx_str_dup(entry->path);
            if (!copy || !xx_list_append(list, &copy)) {
                xx_str_free(copy);
                result = false;
            }
        }
        xx_fs_entry_free(entry);
    }
    xx_list_destroy(entries);
    return result;
}

/* ----------------------------------------------------------------- paths */

bool xx_fs_is_separator(char c) {
    return c == '/' || c == '\\';
}

char *xx_fs_path_join(const char *left, const char *right) {
    xx_buf_t buf;

    xx_buf_init(&buf);
    xx_buf_append_str(&buf, left);
    /* The separator is only worth adding when there is something to separate.
     * Joining a path with nothing yields the path, not the path plus a
     * dangling '/'. */
    if (right && right[0]) {
        if (buf.size > 0U && !xx_fs_is_separator(buf.data[buf.size - 1U])) {
            xx_buf_append_char(&buf, '/');
        }
        xx_buf_append_str(&buf, right);
    }
    return xx_buf_detach(&buf, NULL);
}

static const char *xx_fs_last_separator(const char *path) {
    const char *found = NULL;
    const char *cursor;

    for (cursor = path; *cursor; ++cursor) {
        if (xx_fs_is_separator(*cursor)) {
            found = cursor;
        }
    }
    return found;
}

/* xx_str_dup of a bounded range; xxfclib has no strndup. */
static char *xx_fs_dup_range(const char *text, size_t length) {
    char *result = (char *)xx_mem_alloc(length + 1U);
    if (!result) {
        return NULL;
    }
    if (length != 0U) {
        xx_rt_memcpy(result, text, length);
    }
    result[length] = '\0';
    return result;
}

char *xx_fs_path_dir(const char *path) {
    const char *separator;

    if (!path) {
        return NULL;
    }
    separator = xx_fs_last_separator(path);
    if (!separator) {
        return xx_str_dup("");
    }
    return xx_fs_dup_range(path, (size_t)(separator - path));
}

char *xx_fs_path_file_name(const char *path) {
    const char *separator;

    if (!path) {
        return NULL;
    }
    separator = xx_fs_last_separator(path);
    return xx_str_dup(separator ? separator + 1 : path);
}

char *xx_fs_path_base_name(const char *path) {
    char *file_name = xx_fs_path_file_name(path);
    char *dot;

    if (!file_name) {
        return NULL;
    }
    dot = xx_rt_strrchr(file_name, '.');
    /* A leading dot starts a hidden name rather than a suffix, so it is kept. */
    if (dot && dot != file_name) {
        *dot = '\0';
    }
    return file_name;
}

/* Shared by the suffix accessors. They differ in which dot they find, and in
 * whether a dot in position 0 is skipped first.
 *
 * Skipping it is the default and the self-consistent rule: a leading dot
 * marks a hidden name rather than introducing a suffix, so ".hidden.txt" has
 * suffix "txt" under both accessors. Not skipping it is what cdie does, and
 * what the DIE script API's getFileCompleteSuffix still has to return --
 * there ".hidden.txt" has complete suffix "" while its plain suffix is
 * "txt", a complete suffix shorter than the plain one. Both rules are
 * reachable; only the first is the default. */
static char *xx_fs_suffix_from(const char *path, bool first_dot,
                               bool skip_leading_dot) {
    char *file_name = xx_fs_path_file_name(path);
    char *search;
    char *dot;
    char *result;

    if (!file_name) {
        return NULL;
    }
    search = (skip_leading_dot && file_name[0] == '.') ? file_name + 1
                                                       : file_name;
    dot = first_dot ? xx_rt_strchr(search, '.') : xx_rt_strrchr(search, '.');
    /* Without the skip, a dot in position 0 is the one found, and there is no
     * suffix to report after it. */
    if (dot == file_name) {
        dot = NULL;
    }
    result = xx_str_dup(dot ? dot + 1 : "");
    xx_str_free(file_name);
    return result;
}

char *xx_fs_path_suffix(const char *path) {
    return xx_fs_suffix_from(path, false, true);
}

char *xx_fs_path_complete_suffix(const char *path) {
    return xx_fs_suffix_from(path, true, true);
}

char *xx_fs_path_complete_suffix_ex(const char *path, bool skip_leading_dot) {
    return xx_fs_suffix_from(path, true, skip_leading_dot);
}

char *xx_fs_path_native(const char *path) {
    char *result;
    size_t index;

    if (!path) {
        return NULL;
    }
    result = xx_str_dup(path);
    if (!result) {
        return NULL;
    }
    /* Both separators are accepted everywhere (see xx_fs_is_separator), so
     * this is one loop to the native one rather than a fork: on Windows it
     * rewrites '/', elsewhere it rewrites '\'. */
    {
        char native = xx_fs_platform_separator();
        for (index = 0U; result[index]; ++index) {
            if (xx_fs_is_separator(result[index])) {
                result[index] = native;
            }
        }
    }
    return result;
}

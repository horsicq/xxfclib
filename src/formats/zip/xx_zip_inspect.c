/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zip/xx_zip.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/json/xx_json.h"
#include "xxfclib/buf/xx_buf.h"
#include <wchar.h>

#define ZIP_INSPECT_LIMIT ((size_t)16 * 1024 * 1024)
typedef struct { xx_list_s names; char *manifest, *package; char jvm[24]; } zip_analysis;
static void zip_name_free(void *element) { xx_str_free(*(char **)element); }
void xx_zip_cleanup_analysis(xx_zip *zip) {
    zip_analysis *a;
    if (!zip || !(a = (zip_analysis *)zip->analysis)) return;
    xx_list_cleanup(&a->names); xx_str_free(a->manifest); xx_str_free(a->package); xx_mem_free(a); zip->analysis = NULL;
}
static void zip_jvm_version(const uint8_t *data, size_t size, char out[24]) {
    static const char *old[] = {"JDK 1.1","JDK 1.2","JDK 1.3","JDK 1.4","Java SE 5.0"};
    unsigned major, minor;
    if (size <= 10 || xx_rt_memcmp(data, "\xCA\xFE\xBA\xBE", 4)) return;
    minor = ((unsigned)data[4] << 8) | data[5]; major = ((unsigned)data[6] << 8) | data[7];
    if (major < 45 || major > 74) return;
    if (major < 50) (void)xx_rt_snprintf(out, 24, "%s", old[major - 45]);
    else (void)xx_rt_snprintf(out, 24, "Java SE %u", major - 44);
    if (minor) { size_t n = xx_rt_strlen(out); (void)xx_rt_snprintf(out + n, 24 - n, ".%u", minor); }
}
bool xx_zip_analyze(xx_zip *zip, xx_pd_struct *pd) {
    zip_analysis *a;
    xx_archive_record_state *state = NULL;
    size_t total = 0;
    bool ok = false;
    size_t name_index;
    if (!zip || xx_pd_is_stopped(pd)) return false;
    if (zip->analysis) return true;
    if (!xx_zip_handle_base_info(&zip->format, pd)) return false;
    a = (zip_analysis *)xx_mem_calloc(1, sizeof(*a)); if (!a) return false;
    if (!xx_list_init(&a->names, sizeof(char *), zip_name_free)) { xx_mem_free(a); return false; }
    zip->analysis = a;
    state = xx_zip_create_archive_records_reading(&zip->format, NULL, NULL);
    if (!state) goto done;
    while (state->has_record && !xx_pd_is_stopped(pd)) {
        const xx_archive_record *record = xx_zip_get_current_archive_record(&zip->format, state);
        const char *name = xx_archive_record_get_original_name(record);
        char *converted_name = NULL;
        char *owned; size_t n;
        if (!name) {
            const wchar_t *wide_name = xx_archive_record_get_original_name_w(record);
            if (wide_name) converted_name = xx_str_unicode_to_utf8(wide_name);
            name = converted_name;
        }
        if (!name || a->names.count >= 100000) { xx_str_free(converted_name); goto done; }
        n = xx_rt_strlen(name) + 1;
        if (total > ZIP_INSPECT_LIMIT * 2 || n > ZIP_INSPECT_LIMIT * 2 - total) {
            xx_str_free(converted_name); goto done;
        }
        owned = xx_str_create(name); xx_str_free(converted_name); if (!owned) goto done;
        if (!xx_list_append(&a->names, &owned)) { xx_str_free(owned); goto done; }
        total += n;
        /* The iterator reuses its current-record storage after every advance. */
        if (!xx_zip_archive_record_move_to_next(&zip->format, state, NULL)) break;
    }
    ok = !xx_pd_is_stopped(pd) && a->names.count == zip->number_of_records;
    if (state) {
        xx_zip_free_archive_records_reading(&zip->format, state);
        state = NULL;
    }
    if (ok) {
        bool tried_manifest = false, tried_package = false;
        bool found_class_header = false;
        unsigned class_attempts = 0;
        /* Open member readers only after the central-directory state is gone.
         * Each selected read creates its own ZIP view over the shared device. */
        for (name_index = 0; name_index < a->names.count &&
                             !xx_pd_is_stopped(pd); ++name_index) {
            const char *name = *(char **)xx_list_at(&a->names, name_index);
            size_t n = xx_rt_strlen(name) + 1U;
            uint8_t *data = NULL;
            size_t size = 0;
            if (!tried_manifest && !xx_rt_strcmp(name, "META-INF/MANIFEST.MF")) {
                tried_manifest = true;
                if (xx_zip_read_file(zip, name, ZIP_INSPECT_LIMIT,
                                     &data, &size, pd))
                    a->manifest = (char *)data;
            } else if (!tried_package &&
                       !xx_rt_strcmp(name, "package/package.json")) {
                tried_package = true;
                if (xx_zip_read_file(zip, name, ZIP_INSPECT_LIMIT,
                                     &data, &size, pd))
                    a->package = (char *)data;
            } else if (!found_class_header && class_attempts < 8U &&
                       ((!xx_rt_strcmp(name, "class")) ||
                        (n >= 7U && !xx_rt_strcmp(name + n - 7U, ".class")))) {
                ++class_attempts;
                if (xx_zip_read_file(zip, name, ZIP_INSPECT_LIMIT,
                                     &data, &size, pd)) {
                    found_class_header = size > 10U &&
                        !xx_rt_memcmp(data, "\xCA\xFE\xBA\xBE", 4U);
                    zip_jvm_version(data, size, a->jvm);
                    xx_mem_free(data);
                }
            }
        }
    }
done:
    if (state) xx_zip_free_archive_records_reading(&zip->format, state);
    if (!ok) xx_zip_cleanup_analysis(zip);
    return ok;
}
const xx_list_s *xx_zip_get_record_names(const xx_zip *zip) { return zip && zip->analysis ? &((zip_analysis *)zip->analysis)->names : NULL; }
bool xx_zip_record_present(const xx_zip *zip, const char *name) {
    const xx_list_s *names = xx_zip_get_record_names(zip); size_t i;
    for (i = 0; name && names && i < names->count; ++i)
        if (!xx_rt_strcmp(*(char **)xx_list_at(names, i), name)) return true;
    return false;
}
const char *xx_zip_get_jvm_version(const xx_zip *zip) { return zip && zip->analysis ? ((zip_analysis *)zip->analysis)->jvm : ""; }
char *xx_zip_manifest_record(const xx_zip *zip, const char *key) {
    const zip_analysis *a = zip ? (const zip_analysis *)zip->analysis : NULL;
    const char *p, *end_text; size_t n;
    if (!a || !a->manifest || !key) return xx_str_create("");
    n = xx_rt_strlen(key); end_text = a->manifest + xx_rt_strlen(a->manifest);
    for (p = a->manifest; *p; ++p) {
        size_t left = (size_t)(end_text - p);
        if (left >= 2 && n <= left - 2 && !xx_rt_strncmp(p, key, n) && p[n] == ':' && p[n + 1] == ' ') {
            const char *end = p + n + 2; xx_buf_t out;
            while (*end && *end != '\n') ++end;
            if (*end != '\n') return xx_str_create("");
            xx_buf_init(&out); p += n + 2;
            while (p < end) { if (*p != '\r') (void)xx_buf_append_char(&out, *p); ++p; }
            return xx_buf_detach(&out, NULL);
        }
    }
    return xx_str_create("");
}
char *xx_zip_packagejson_record(const xx_zip *zip, const char *key) {
    const zip_analysis *a = zip ? (const zip_analysis *)zip->analysis : NULL;
    xx_json json;
    if (!a || !a->package || !key) return xx_str_create("");
    xx_json_init(&json, a->package, xx_rt_strlen(a->package));
    if (!xx_json_object_begin(&json) || xx_json_object_empty(&json)) return xx_str_create("");
    for (;;) {
        char *name = NULL; bool match;
        if (!xx_json_object_key(&json, &name)) break;
        match = name && !xx_rt_strcmp(name, key); xx_str_free(name);
        if (match) {
            char *value = NULL;
            if (xx_json_peek(&json) == XX_JSON_TYPE_STRING && xx_json_string(&json, &value)) return value;
            xx_str_free(value); break;
        }
        if (!xx_json_skip(&json) || !xx_json_more(&json)) break;
    }
    return xx_str_create("");
}

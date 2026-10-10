/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/rt/xx_rt.h"

#define XX_APK_MANIFEST_NAME "AndroidManifest.xml"
#define XX_APK_MANIFEST_LIMIT (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))

#define XX_APK_MAX_DEX 128
#define XX_APK_MAX_NATIVE_LIBRARIES 1024
#define XX_APK_MAX_NATIVE_ABIS 64

typedef struct xx_apk_native_analysis {
    size_t count, abi_count;
    xx_apk_native_library_info libraries[XX_APK_MAX_NATIVE_LIBRARIES];
    const char *abis[XX_APK_MAX_NATIVE_ABIS];
} xx_apk_native_analysis;

static void xx_apk_free_native_analysis(xx_apk_native_analysis *analysis)
{
    size_t i;
    if (!analysis) return;
    for (i = 0; i < analysis->count; ++i) {
        xx_mem_free((void *)analysis->libraries[i].entry_name);
        xx_mem_free((void *)analysis->libraries[i].abi);
        xx_mem_free((void *)analysis->libraries[i].name);
    }
    xx_mem_free(analysis);
}

typedef struct xx_apk_dex_member {
    char *name;
    uint32_t number;
} xx_apk_dex_member;

typedef struct xx_apk_dex_analysis {
    size_t count;
    xx_apk_dex_member members[XX_APK_MAX_DEX];
} xx_apk_dex_analysis;

static void xx_apk_free_dex_analysis(xx_apk_dex_analysis *analysis)
{
    size_t i;
    if (!analysis) return;
    for (i = 0; i < analysis->count; ++i) xx_mem_free(analysis->members[i].name);
    xx_mem_free(analysis);
}

static uint32_t xx_apk_dex_number(const char *name)
{
    uint32_t value = 0;
    const char *p;
    if (!name) return 0;
    if (!xx_rt_strcmp(name, "classes.dex")) return 1;
    if (xx_rt_strncmp(name, "classes", 7)) return 0;
    p = name + 7;
    if (*p < '1' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (value > (UINT32_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    return value >= 2 && !xx_rt_strcmp(p, ".dex") ? value : 0;
}

static void xx_apk_apply_identity(Abstractformat *format)
{
    if (!format) {
        return;
    }
    format->file_type = XX_FILE_TYPE_APK;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_ANDROID;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/vnd.android.package-archive");
    xx_format_set_extension(format, "apk");
}

static void xx_apk_vtable_destroy(Abstractformat *self)
{
    if (self) {
        xx_apk_destroy((xx_apk *)self);
    }
}

void xx_apk_init(xx_apk *apk, xx_io_device *dev, int64_t base_address)
{
    Abstractformat *format;

    if (!apk) {
        return;
    }
    xx_mem_zero(apk, sizeof(*apk));
    xx_zip_init(&apk->zip, dev, base_address);
    format = &apk->zip.format;
    xx_apk_apply_identity(format);

    /* All remaining callbacks stay wired directly to xx_zip.c. */
    format->check_is_valid = xx_apk_check_is_valid;
    format->handle_base_info = xx_apk_handle_base_info;
    format->destroy = xx_apk_vtable_destroy;
}

xx_apk *xx_apk_create(xx_io_device *dev, int64_t base_address)
{
    xx_apk *apk = (xx_apk *)xx_mem_alloc(sizeof(xx_apk));
    if (!apk) {
        return NULL;
    }
    xx_apk_init(apk, dev, base_address);
    return apk;
}

void xx_apk_destroy(xx_apk *apk)
{
    if (apk) {
        xx_mem_free(apk->manifest_text);
        apk->manifest_text = NULL;
        xx_mem_free(apk->package_name);
        apk->package_name = NULL;
        xx_mem_free(apk->launcher_activity);
        apk->launcher_activity = NULL;
        xx_apk_free_dex_analysis((xx_apk_dex_analysis *)apk->dex_analysis);
        apk->dex_analysis = NULL;
        xx_apk_free_native_analysis((xx_apk_native_analysis *)apk->native_analysis);
        apk->native_analysis = NULL;
        xx_zip_destroy(&apk->zip);
    }
}

void xx_apk_free(xx_apk *apk)
{
    if (!apk) {
        return;
    }
    xx_apk_destroy(apk);
    xx_mem_free(apk);
}

bool xx_apk_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_zip_has_valid_file(self, XX_APK_MANIFEST_NAME, XX_APK_MANIFEST_LIMIT, pd);
}

bool xx_apk_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    bool result;

    if (!self) {
        return false;
    }
    result = xx_zip_handle_base_info(self, pd);
    if (result) {
        result = xx_zip_has_valid_file(self, XX_APK_MANIFEST_NAME, XX_APK_MANIFEST_LIMIT, pd);
    }
    xx_apk_apply_identity(self);
    self->is_valid = result;
    return result;
}

bool xx_apk_analyze_dex(xx_apk *apk, xx_pd_struct *pd)
{
    const xx_list_s *names;
    xx_apk_dex_analysis *analysis;
    size_t i;
    bool analyzed;
    if (!apk || xx_pd_is_stopped(pd)) return false;
    if (apk->dex_analysis) {
        xx_apk_apply_identity(&apk->zip.format);
        return true;
    }
    analyzed = xx_zip_analyze(&apk->zip, pd);
    /* The inherited inspection calls ZIP's handler directly. Keep the public
     * derived object identified as APK on both success and failure. */
    xx_apk_apply_identity(&apk->zip.format);
    if (!analyzed) return false;
    names = xx_zip_get_record_names(&apk->zip);
    if (!names) return false;
    analysis = (xx_apk_dex_analysis *)xx_mem_calloc(1, sizeof(*analysis));
    if (!analysis) return false;
    for (i = 0; i < xx_list_count(names); ++i) {
        const char *name = *(char *const *)xx_list_at(names, i);
        uint32_t number = xx_apk_dex_number(name);
        size_t at;
        char *copy;
        if (xx_pd_is_stopped(pd)) goto invalid;
        if (!number) continue;
        if (analysis->count == XX_APK_MAX_DEX) goto invalid;
        for (at = 0; at < analysis->count && analysis->members[at].number < number; ++at) {
        }
        if (at < analysis->count && analysis->members[at].number == number) goto invalid;
        copy = xx_str_create(name);
        if (!copy) goto invalid;
        if (at < analysis->count) {
            xx_rt_memmove(analysis->members + at + 1, analysis->members + at, (analysis->count - at) * sizeof(*analysis->members));
        }
        analysis->members[at].name = copy;
        analysis->members[at].number = number;
        ++analysis->count;
    }
    apk->dex_analysis = analysis;
    return true;
invalid:
    xx_apk_free_dex_analysis(analysis);
    return false;
}

size_t xx_apk_get_dex_count(const xx_apk *apk)
{
    const xx_apk_dex_analysis *analysis = apk ? (const xx_apk_dex_analysis *)apk->dex_analysis : NULL;
    return analysis ? analysis->count : 0;
}

const char *xx_apk_get_dex_name(const xx_apk *apk, size_t index)
{
    const xx_apk_dex_analysis *analysis = apk ? (const xx_apk_dex_analysis *)apk->dex_analysis : NULL;
    return analysis && index < analysis->count ? analysis->members[index].name : NULL;
}

bool xx_apk_read_dex(xx_apk *apk, size_t index, size_t limit, uint8_t **data, size_t *size, xx_pd_struct *pd)
{
    const char *name;
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!data || !size || !xx_apk_analyze_dex(apk, pd)) return false;
    name = xx_apk_get_dex_name(apk, index);
    return name && xx_zip_read_file(&apk->zip, name, limit, data, size, pd);
}

static bool xx_apk_native_parts(const char *entry, size_t *abi_size, const char **name)
{
    const char *p, *start;
    size_t length;
    if (!entry || xx_rt_strncmp(entry, "lib/", 4)) return false;
    start = p = entry + 4;
    while ((size_t)(p - start) <= 63 && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) ++p;
    if (*p != '/' || p == start || (size_t)(p - start) > 63) return false;
    *abi_size = (size_t)(p - start);
    start = ++p;
    while ((size_t)(p - start) <= 255 && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) ++p;
    length = (size_t)(p - start);
    if (*p || length < 7 || length > 255 || xx_rt_strncmp(start, "lib", 3) || xx_rt_strcmp(p - 3, ".so")) return false;
    *name = start;
    return true;
}

bool xx_apk_analyze_native_libraries(xx_apk *apk, xx_pd_struct *pd)
{
    const xx_list_s *names;
    xx_apk_native_analysis *analysis;
    size_t i;
    bool analyzed;
    if (!apk || xx_pd_is_stopped(pd)) return false;
    if (apk->native_analysis) {
        xx_apk_apply_identity(&apk->zip.format);
        return true;
    }
    analyzed = xx_zip_analyze(&apk->zip, pd);
    xx_apk_apply_identity(&apk->zip.format);
    if (!analyzed || !(names = xx_zip_get_record_names(&apk->zip))) return false;
    analysis = (xx_apk_native_analysis *)xx_mem_calloc(1, sizeof(*analysis));
    if (!analysis) return false;
    for (i = 0; i < xx_list_count(names); ++i) {
        const char *entry = *(char *const *)xx_list_at(names, i), *name;
        char abi[64];
        size_t abi_size, at;
        xx_apk_native_library_info info;
        if (xx_pd_is_stopped(pd)) goto invalid;
        if (!xx_apk_native_parts(entry, &abi_size, &name)) continue;
        if (analysis->count == XX_APK_MAX_NATIVE_LIBRARIES) goto invalid;
        xx_rt_memcpy(abi, entry + 4, abi_size);
        abi[abi_size] = 0;
        for (at = 0; at < analysis->count; ++at) {
            int comparison = xx_rt_strcmp(analysis->libraries[at].abi, abi);
            if (!comparison) comparison = xx_rt_strcmp(analysis->libraries[at].name, name);
            if (comparison >= 0) break;
        }
        if (at < analysis->count && !xx_rt_strcmp(analysis->libraries[at].entry_name, entry)) goto invalid;
        info.entry_name = xx_str_create(entry);
        info.abi = (char *)xx_mem_alloc(abi_size + 1);
        info.name = xx_str_create(name);
        if (!info.entry_name || !info.abi || !info.name) {
            xx_mem_free((void *)info.entry_name);
            xx_mem_free((void *)info.abi);
            xx_mem_free((void *)info.name);
            goto invalid;
        }
        xx_rt_memcpy((char *)info.abi, entry + 4, abi_size);
        ((char *)info.abi)[abi_size] = 0;
        if (at < analysis->count) xx_rt_memmove(analysis->libraries + at + 1, analysis->libraries + at, (analysis->count - at) * sizeof(*analysis->libraries));
        analysis->libraries[at] = info;
        ++analysis->count;
    }
    for (i = 0; i < analysis->count; ++i) {
        const char *abi = analysis->libraries[i].abi;
        if (analysis->abi_count && !xx_rt_strcmp(analysis->abis[analysis->abi_count - 1], abi)) continue;
        if (analysis->abi_count == XX_APK_MAX_NATIVE_ABIS) goto invalid;
        analysis->abis[analysis->abi_count++] = abi;
    }
    apk->native_analysis = analysis;
    return true;
invalid:
    xx_apk_free_native_analysis(analysis);
    return false;
}

size_t xx_apk_get_native_library_count(const xx_apk *apk)
{
    const xx_apk_native_analysis *analysis = apk ? (const xx_apk_native_analysis *)apk->native_analysis : NULL;
    return analysis ? analysis->count : 0;
}

const xx_apk_native_library_info *xx_apk_get_native_library(const xx_apk *apk, size_t index)
{
    const xx_apk_native_analysis *analysis = apk ? (const xx_apk_native_analysis *)apk->native_analysis : NULL;
    return analysis && index < analysis->count ? analysis->libraries + index : NULL;
}

size_t xx_apk_get_native_abi_count(const xx_apk *apk)
{
    const xx_apk_native_analysis *analysis = apk ? (const xx_apk_native_analysis *)apk->native_analysis : NULL;
    return analysis ? analysis->abi_count : 0;
}

const char *xx_apk_get_native_abi(const xx_apk *apk, size_t index)
{
    const xx_apk_native_analysis *analysis = apk ? (const xx_apk_native_analysis *)apk->native_analysis : NULL;
    return analysis && index < analysis->abi_count ? analysis->abis[index] : NULL;
}

bool xx_apk_read_native_library(xx_apk *apk, const char *abi, const char *name, size_t limit, uint8_t **data, size_t *size, xx_pd_struct *pd)
{
    const xx_apk_native_analysis *analysis;
    size_t i;
    bool result;
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!data || !size || !abi || !name || !xx_apk_analyze_native_libraries(apk, pd)) return false;
    analysis = (const xx_apk_native_analysis *)apk->native_analysis;
    for (i = 0; i < analysis->count; ++i) {
        const xx_apk_native_library_info *info = analysis->libraries + i;
        if (xx_rt_strcmp(info->abi, abi) || xx_rt_strcmp(info->name, name)) continue;
        result = xx_zip_read_file(&apk->zip, info->entry_name, limit, data, size, pd);
        xx_apk_apply_identity(&apk->zip.format);
        return result;
    }
    return false;
}

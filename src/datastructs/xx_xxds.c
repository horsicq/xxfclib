/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Host of the DataStructs scripts. A .xxds script (src/datastructs/RULES.md)
 * names every structure of a file type and reports where they occur; this
 * file runs it in the JS engine and answers the public DataStructs stream
 * API from the result. Record values are still decoded by the common
 * formatter (xx_data_struct_record_populate), so every format displays alike.
 *
 * One JS context is kept per script and all script execution is serialized
 * by one mutex: a context is single-threaded and DataStructs is an
 * inspection feature, not a hot path.
 */
#include "xx_xxds_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/xx_memory_map.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/js/xx_js.h"
#include "xxfclib/js/xx_js_debug.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/threads/xx_threads.h"

#define XXDS_MAX_ITEMS 262144U
/* Address argument asking the host for the mapped address of the offset. */
#define XXDS_ADDRESS_MAPPED (-2)
#define XXDS_MAX_FIELDS 70000U
#define XXDS_MAX_SCRIPT (8U * 1024U * 1024U)
#define XXDS_MAX_TEXT 4096U

typedef struct xxds_embedded {
    const char *name;
    const unsigned char *data;
    size_t size;
    const xx_file_type_t *types;
    size_t type_count;
    int stub; /* placeholder: served only from a script directory */
    size_t packed; /* bytes of `data` when it is a raw deflate stream of `size` bytes of text, else 0 */
} xxds_embedded;

/* The generated schema scripts live in this sub-folder of the script directory (src/datastructs/sx). */
#define XXDS_SUBDIR "sx"

#include "xx_xxds_scripts.inc"

typedef struct xxds_entry {
    uint32_t id;
    char *name;
    char *description; /* what the structure is, or NULL */
    char **aliases; /* further names string_to_id accepts */
    size_t alias_count;
    int endian; /* 0 LE, 1 BE, 2 FILE */
} xxds_entry;

typedef struct xxds_script {
    struct xxds_script *next;
    char name[64];
    JSCtx *ctx;
    bool failed;
    xxds_entry *entries;
    size_t entry_count;
} xxds_script;

/* What the natives act on while one API call runs a script. */
typedef struct xxds_call {
    xxds_script *script;
    Abstractformat *format;
    xx_pd_struct *pd;
    int64_t base;
    int64_t size; /* bytes of the device from the format base */
    xx_data_struct *items;
    size_t count, capacity;
    bool collecting;
    /* One window of the device, so that the many small reads of a walker (File.u8/u16/u32, short File.bytes)
     * cost a copy instead of a device read each. Filled on demand, dropped by xxds_end. */
    uint8_t *cache;
    int64_t cache_at; /* format-relative offset of cache[0] */
    size_t cache_size;
    /* Counters of one scan, printed by xxds_end when the environment variable XXDS_PROFILE is set. */
    uint64_t prof_natives, prof_reads, prof_read_bytes, prof_device_reads, prof_device_bytes, prof_scanned;
} xxds_call;

#define XXDS_CACHE_WINDOW 65536U

struct xx_xxds_table {
    JSCtx *ctx;
    JSVal rows;
    JSVal row;
    bool has_row;
    bool failed;
};

typedef struct xxds_prepared_field {
    xx_data_struct_field_desc desc;
    wchar_t *name, *type, *comment, *display, *description;
    bool big_endian;
    bool no_comment; /* the comment hook returned null: drop the standard comment */
    int logical;     /* 0 none, 1 bool, 2 unsigned, 3 signed: the value is a bit field */
    double logical_value;
} xxds_prepared_field;

typedef struct xxds_records {
    xxds_prepared_field *fields;
    size_t count;
    Abstractformat *format; /* the format the script ran on: the decoded one for a wrapper type */
    Abstractformat *owned;  /* canonical reader of an alias type, closed with the records */
} xxds_records;

typedef struct xxds_items {
    xx_data_struct *items;
    size_t count;
} xxds_items;

/* A script found in the script directory and one file type it serves. */
typedef struct xxds_directory_route {
    xx_file_type_t type;
    char name[64];
} xxds_directory_route;

static xx_once_t g_xxds_once = XX_ONCE_INIT;
static xxds_directory_route *g_xxds_routes;
static size_t g_xxds_route_count;
static xx_mutex_t *g_xxds_mutex;
static xxds_script *g_xxds_scripts;
static char g_xxds_directory[1024];
static char g_xxds_error[512];

static const char xxds_boot[] =
    "var __xxds_cat = structs();\n"
    "(function () {\n"
    "    var i, e;\n"
    "    for (i = 0; i < __xxds_cat.length; i++) {\n"
    "        e = __xxds_cat[i];\n"
    "        if (e.fields && typeof e.fields !== 'function') e.fields = layout(e.fields);\n"
    "    }\n"
    "})();\n"
    "function __xxds_fields(i, ds) {\n"
    "    var f = __xxds_cat[i].fields;\n"
    "    if (typeof f === 'function') f = f(ds);\n"
    "    return f;\n"
    "}\n"
    "function __xxds_note(c, v, ds) {\n"
    "    if (typeof c === 'function') c = c(v, ds);\n"
    "    else if (c !== null && typeof c === 'object') c = c[v];\n"
    "    return c === undefined ? undefined : c === null ? null : '' + c;\n"
    "}\n";

static void xxds_init(void *arg)
{
    (void)arg;
    g_xxds_mutex = xx_mutex_create();
}

static bool xxds_lock(void)
{
    xx_once(&g_xxds_once, xxds_init, NULL);
    if (!g_xxds_mutex) return false;
    xx_mutex_lock(g_xxds_mutex);
    return true;
}

static void xxds_unlock(void)
{
    xx_mutex_unlock(g_xxds_mutex);
}

static void xxds_set_error(const char *script, const char *message)
{
    xx_rt_snprintf(g_xxds_error, sizeof(g_xxds_error), "%s: %s", script ? script : "xxds", message && *message ? message : "script failed");
}

static void xxds_script_error(xxds_script *script)
{
    xxds_set_error(script->name, js_error(script->ctx));
    js_clear_error(script->ctx);
}

/* ------------------------------------------------------------ script text */

static const xxds_embedded *xxds_find_embedded(const char *name)
{
    for (size_t i = 0; i < sizeof(xxds_embedded_scripts) / sizeof(xxds_embedded_scripts[0]); ++i) {
        if (!xx_rt_strcmp(xxds_embedded_scripts[i].name, name)) return &xxds_embedded_scripts[i];
    }
    return NULL;
}

/* Owned, NUL-terminated source of a script: the directory copy when a script
 * directory is set and has one, the embedded copy otherwise. */
static char *xxds_source(const char *name)
{
    const xxds_embedded *embedded;
    char *text;
    for (int place = 0; g_xxds_directory[0] && place < 2; ++place) {
        char path[1200];
        xx_io_device *device;
        int length = place ? xx_rt_snprintf(path, sizeof(path), "%s/" XXDS_SUBDIR "/%s.xxds", g_xxds_directory, name)
                           : xx_rt_snprintf(path, sizeof(path), "%s/%s.xxds", g_xxds_directory, name);
        if (length > 0 && length < (int)sizeof(path) && (device = xx_io_file_open(path, "rb"))) {
            int64_t size = xx_io_total_size(device);
            text = size >= 0 && (uint64_t)size <= XXDS_MAX_SCRIPT ? (char *)xx_mem_alloc((size_t)size + 1U) : NULL;
            if (text && size && !xx_io_read_at(device, 0, text, (size_t)size)) {
                xx_mem_free(text);
                text = NULL;
            }
            xx_io_close(device);
            if (text) {
                text[size] = '\0';
                return text;
            }
        }
    }
    embedded = xxds_find_embedded(name);
    if (!embedded) return NULL;
    text = (char *)xx_mem_alloc(embedded->size + 1U);
    if (!text) return NULL;
    if (embedded->packed) {
        size_t written = 0;
        if (!xx_deflate_decompress_memory(embedded->data, embedded->packed, text, embedded->size, &written, false) || written != embedded->size) {
            xx_mem_free(text);
            return NULL;
        }
    } else {
        xx_rt_memcpy(text, embedded->data, embedded->size);
    }
    text[embedded->size] = '\0';
    return text;
}

static bool xxds_eval(xxds_script *script, const char *name)
{
    char *text = xxds_source(name);
    int ok;
    if (!text) {
        xxds_set_error(name, "script not found");
        return false;
    }
    ok = js_eval_nested(script->ctx, text, name);
    xx_mem_free(text);
    if (!ok || js_has_exception(script->ctx)) {
        xxds_script_error(script);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- helpers */

static xxds_call *xxds_current(JSCtx *ctx)
{
    return (xxds_call *)js_get_user(ctx);
}

static bool xxds_stopped(const xxds_call *call)
{
    return call->pd && xx_pd_is_stopped(call->pd);
}

static bool xxds_range(const xxds_call *call, int64_t offset, int64_t size)
{
    return offset >= 0 && size >= 0 && offset <= call->size && size <= call->size - offset;
}

static bool xxds_read(JSCtx *ctx, xxds_call *call, int64_t offset, void *out, size_t size)
{
    if (!call || xxds_stopped(call)) {
        js_throw(ctx, "stopped");
        return false;
    }
    if (!xxds_range(call, offset, (int64_t)size)) {
        js_throw(ctx, "read outside the file: offset %lld size %llu", (long long)offset, (unsigned long long)size);
        return false;
    }
    if (!size) return true;
    call->prof_reads++;
    call->prof_read_bytes += size;
    if (size <= XXDS_CACHE_WINDOW / 4U) {
        if (!call->cache || offset < call->cache_at || (uint64_t)(offset - call->cache_at) + size > call->cache_size) {
            int64_t start = offset & ~(int64_t)0xFFF, left = call->size - start;
            size_t fill = (size_t)(left < (int64_t)XXDS_CACHE_WINDOW ? left : (int64_t)XXDS_CACHE_WINDOW);
            if (!call->cache) call->cache = (uint8_t *)xx_mem_alloc(XXDS_CACHE_WINDOW);
            call->cache_size = 0;
            call->prof_device_reads++;
            call->prof_device_bytes += fill;
            if (call->cache && xx_io_read_at(call->format->device, call->base + start, call->cache, fill)) {
                call->cache_at = start;
                call->cache_size = fill;
            }
        }
        if (call->cache && call->cache_size && offset >= call->cache_at && (uint64_t)(offset - call->cache_at) + size <= call->cache_size) {
            xx_rt_memcpy(out, call->cache + (size_t)(offset - call->cache_at), size);
            return true;
        }
    }
    call->prof_device_reads++;
    call->prof_device_bytes += size;
    if (!xx_io_read_at(call->format->device, call->base + offset, out, size)) {
        js_throw(ctx, "read outside the file: offset %lld size %llu", (long long)offset, (unsigned long long)size);
        return false;
    }
    return true;
}

static int64_t xxds_arg_i64(JSCtx *ctx, int argc, JSVal *argv, int index, int64_t fallback)
{
    return index < argc && !js_is_undefined(argv[index]) ? js_to_int64(ctx, argv[index]) : fallback;
}

static bool xxds_arg_bool(JSCtx *ctx, int argc, JSVal *argv, int index)
{
    return index < argc && js_to_bool(ctx, argv[index]);
}

static wchar_t *xxds_widen(const char *text, size_t size)
{
    wchar_t *wide = (wchar_t *)xx_mem_alloc((size + 1U) * sizeof(*wide));
    if (!wide) return NULL;
    for (size_t i = 0; i < size; ++i) wide[i] = (wchar_t)(unsigned char)text[i];
    wide[size] = L'\0';
    return wide;
}

/* Owned wide copy of a script string value; NULL for undefined/null. */
static wchar_t *xxds_wide_value(JSCtx *ctx, JSVal value);

/* The same for text the script marked as UTF-8; NULL when it is not valid. */
static wchar_t *xxds_wide_utf8(JSCtx *ctx, JSVal value, bool *ok)
{
    wchar_t *wide = NULL;
    char *text;
    if (value.tag == JT_UNDEF || value.tag == JT_NULL) return NULL;
    text = js_to_cstr(ctx, value);
    if (text) wide = xx_str_utf8_to_unicode(text);
    xx_rt_free(text);
    if (!wide) *ok = false;
    return wide;
}

static wchar_t *xxds_wide_value(JSCtx *ctx, JSVal value)
{
    wchar_t *wide;
    JSVal text;
    if (value.tag == JT_UNDEF || value.tag == JT_NULL) return NULL;
    text = js_to_string(ctx, value);
    wide = text.tag == JT_STR && js_str_len(text) <= XXDS_MAX_TEXT ? xxds_widen(js_str_data(text), js_str_len(text)) : NULL;
    js_release(ctx, text);
    return wide;
}

static int64_t xxds_get_i64(JSCtx *ctx, JSVal object, const char *key, int64_t fallback)
{
    JSVal value = js_get(ctx, object, key);
    int64_t result = js_is_undefined(value) ? fallback : js_to_int64(ctx, value);
    js_release(ctx, value);
    return result;
}

static const xxds_entry *xxds_entry_by_id(const xxds_script *script, uint32_t id, size_t *index)
{
    for (size_t i = 0; i < script->entry_count; ++i) {
        if (script->entries[i].id == id) {
            if (index) *index = i;
            return &script->entries[i];
        }
    }
    return NULL;
}

static const xxds_entry *xxds_entry_by_name(const xxds_script *script, const char *name)
{
    for (size_t i = 0; i < script->entry_count; ++i) {
        if (!xx_rt_strcmp(script->entries[i].name, name)) return &script->entries[i];
    }
    for (size_t i = 0; i < script->entry_count; ++i) {
        for (size_t a = 0; a < script->entries[i].alias_count; ++a) {
            if (!xx_rt_strcmp(script->entries[i].aliases[a], name)) return &script->entries[i];
        }
    }
    return NULL;
}

static char *xxds_copy_text(const char *text, size_t size)
{
    char *copy = (char *)xx_mem_alloc(size + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, text, size);
    copy[size] = '\0';
    return copy;
}

/* Reads the optional `aliases` array of a catalog entry. */
static bool xxds_read_aliases(JSCtx *ctx, JSVal entry, xxds_entry *slot)
{
    JSVal aliases = js_get(ctx, entry, "aliases");
    int64_t count = js_is_array(aliases) ? js_array_length(ctx, aliases) : 0;
    bool ok = count >= 0 && count <= 16;
    if (ok && count) {
        slot->aliases = (char **)xx_mem_calloc((size_t)count, sizeof(*slot->aliases));
        ok = slot->aliases != NULL;
    }
    for (int64_t i = 0; ok && i < count; ++i) {
        JSVal alias = js_get_index(ctx, aliases, i);
        ok = alias.tag == JT_STR && js_str_len(alias) > 0 && js_str_len(alias) < 128U;
        if (ok) ok = (slot->aliases[slot->alias_count] = xxds_copy_text(js_str_data(alias), js_str_len(alias))) != NULL;
        if (ok) ++slot->alias_count;
        js_release(ctx, alias);
    }
    js_release(ctx, aliases);
    return ok;
}

/* ------------------------------------------------------------ File object */

enum {
    XF_SIZE, XF_VALID, XF_U8, XF_U16, XF_U32, XF_U64, XF_I8, XF_I16, XF_I32, XF_I64, XF_U64HI, XF_U64LO,
    XF_BYTES, XF_CSTR, XF_CSTRSIZE, XF_FIND, XF_CRC32, XF_READ,
    XM_TYPE, XM_SIZE, XM_BIG, XM_MAPPED, XM_VA2OFF, XM_RVA2OFF, XM_OFF2VA, XM_VAAT, XM_TABLE, XM_RVARANGE, XM_VARANGE, XM_VARANGEAT, XM_MAPRECORD, XM_IMAGEBASE, XM_ENSURE, XM_TYPENAME, XM_BASESHIFT, XM_VERSION, XM_EXTRA,
    XS_ADD, XS_TABLE, XS_PUT, XS_RAW, XS_COUNT,
    XU_UTC, XU_HEX, XU_INDEXOF, XU_CRC, XU_HEAPSORT, XG_INCLUDE
};

static bool xxds_read_uint(JSCtx *ctx, xxds_call *call, int64_t offset, unsigned width, bool big, uint64_t *value)
{
    uint8_t bytes[8];
    uint64_t result = 0;
    if (!xxds_read(ctx, call, offset, bytes, width)) return false;
    for (unsigned i = 0; i < width; ++i) result |= (uint64_t)bytes[i] << ((big ? width - i - 1U : i) * 8U);
    *value = result;
    return true;
}

static JSVal xxds_file(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    xxds_call *call = xxds_current(ctx);
    int op = (int)(size_t)user;
    int64_t offset = xxds_arg_i64(ctx, argc, argv, 0, 0);
    uint64_t value;
    (void)this_value;
    if (!call) return js_throw(ctx, "File is not available here");
    call->prof_natives++;
    switch (op) {
        case XF_SIZE: return js_num((double)call->size);
        case XF_READ: {
            /* File.read(offset, size, array[, at]): the bytes as numbers into array[at...]. */
            int64_t size = xxds_arg_i64(ctx, argc, argv, 1, 0), at = xxds_arg_i64(ctx, argc, argv, 3, 0), done = 0;
            uint8_t chunk[4096];
            if (argc < 3 || !js_is_array(argv[2]) || size < 0 || at < 0 || (uint64_t)size > XXDS_MAX_SCRIPT) return js_throw(ctx, "File.read: bad arguments");
            if (!xxds_range(call, offset, size)) return js_throw(ctx, "read outside the file: offset %lld size %lld", (long long)offset, (long long)size);
            while (done < size) {
                size_t part = (size_t)(size - done < (int64_t)sizeof(chunk) ? size - done : (int64_t)sizeof(chunk));
                if (!xxds_read(ctx, call, offset + done, chunk, part)) return js_undefined();
                for (size_t i = 0; i < part; ++i) js_set_index(ctx, argv[2], at + done + (int64_t)i, js_num((double)chunk[i]));
                done += (int64_t)part;
            }
            return js_bool(1);
        }
        case XF_VALID: return js_bool(xxds_range(call, offset, xxds_arg_i64(ctx, argc, argv, 1, 0)));
        case XF_U8: return xxds_read_uint(ctx, call, offset, 1, false, &value) ? js_num((double)value) : js_undefined();
        case XF_U16: return xxds_read_uint(ctx, call, offset, 2, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)value) : js_undefined();
        case XF_U32: return xxds_read_uint(ctx, call, offset, 4, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)value) : js_undefined();
        case XF_U64: return xxds_read_uint(ctx, call, offset, 8, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)value) : js_undefined();
        case XF_I8: return xxds_read_uint(ctx, call, offset, 1, false, &value) ? js_num((double)(int8_t)value) : js_undefined();
        case XF_I16: return xxds_read_uint(ctx, call, offset, 2, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)(int16_t)value) : js_undefined();
        case XF_I32: return xxds_read_uint(ctx, call, offset, 4, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)(int32_t)value) : js_undefined();
        case XF_I64: return xxds_read_uint(ctx, call, offset, 8, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)(int64_t)value) : js_undefined();
        case XF_U64HI: return xxds_read_uint(ctx, call, offset, 8, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)(uint32_t)(value >> 32)) : js_undefined();
        case XF_U64LO: return xxds_read_uint(ctx, call, offset, 8, xxds_arg_bool(ctx, argc, argv, 1), &value) ? js_num((double)(uint32_t)value) : js_undefined();
        case XF_BYTES: {
            int64_t size = xxds_arg_i64(ctx, argc, argv, 1, 0);
            char *bytes;
            JSVal result;
            if (size < 0 || (uint64_t)size > XXDS_MAX_SCRIPT) return js_throw(ctx, "File.bytes: bad size");
            bytes = (char *)xx_mem_alloc((size_t)size + 1U);
            if (!bytes) return js_throw(ctx, "out of memory");
            result = xxds_read(ctx, call, offset, bytes, (size_t)size) ? js_strn(ctx, bytes, (size_t)size) : js_undefined();
            xx_mem_free(bytes);
            return result;
        }
        case XF_CRC32: {
            /* CRC-32 (IEEE) of a file range, for structures that carry their own checksum. */
            int64_t size = xxds_arg_i64(ctx, argc, argv, 1, 0), done = 0;
            uint8_t chunk[4096];
            uint32_t crc = 0U;
            if (!xxds_range(call, offset, size)) return js_throw(ctx, "File.crc32: range outside the file");
            while (done < size) {
                size_t part = (size_t)(size - done < (int64_t)sizeof(chunk) ? size - done : (int64_t)sizeof(chunk));
                if (!xxds_read(ctx, call, offset + done, chunk, part)) return js_undefined();
                crc = xx_crc32_calc(crc, chunk, part);
                done += (int64_t)part;
            }
            return js_num((double)crc);
        }
        case XF_CSTR:
        case XF_CSTRSIZE: {
            int64_t maximum = xxds_arg_i64(ctx, argc, argv, 1, 0), length = 0;
            char chunk[256], *text = NULL;
            bool found = false;
            JSVal result;
            if (offset < 0 || offset > call->size || maximum < 0) return js_throw(ctx, "File.cstr: bad range");
            if (maximum > call->size - offset) maximum = call->size - offset;
            while (length < maximum && !found) {
                size_t part = (size_t)(maximum - length < (int64_t)sizeof(chunk) ? maximum - length : (int64_t)sizeof(chunk)), used = 0;
                char *grown;
                if (!xxds_read(ctx, call, offset + length, chunk, part)) {
                    xx_mem_free(text);
                    return js_undefined();
                }
                while (used < part && chunk[used]) ++used;
                found = used < part;
                if (op == XF_CSTR) {
                    if ((uint64_t)length + used > XXDS_MAX_SCRIPT || !(grown = (char *)xx_mem_realloc(text, (size_t)length + used + 1U))) {
                        xx_mem_free(text);
                        return js_throw(ctx, "out of memory");
                    }
                    text = grown;
                    xx_rt_memcpy(text + length, chunk, used);
                }
                length += (int64_t)used;
            }
            if (op == XF_CSTRSIZE) return js_num(found ? (double)(length + 1) : -1.0);
            result = js_strn(ctx, text ? text : "", (size_t)length);
            xx_mem_free(text);
            return result;
        }
        case XF_FIND: {
            int64_t size = xxds_arg_i64(ctx, argc, argv, 1, 0), position;
            JSVal needle = argc > 2 ? js_to_string(ctx, argv[2]) : js_undefined();
            size_t needle_size = needle.tag == JT_STR ? js_str_len(needle) : 0U;
            uint8_t window[4096];
            double found = -1.0;
            if (!needle_size || needle_size > 256U || !xxds_range(call, offset, size)) {
                js_release(ctx, needle);
                return js_num(-1.0);
            }
            for (position = offset; found < 0 && position + (int64_t)needle_size <= offset + size;) {
                int64_t left = offset + size - position;
                size_t part = (size_t)(left < (int64_t)sizeof(window) ? left : (int64_t)sizeof(window));
                if (!xxds_read(ctx, call, position, window, part)) {
                    js_release(ctx, needle);
                    return js_undefined();
                }
                for (size_t i = 0; i + needle_size <= part; ++i) {
                    if (!xx_mem_compare(window + i, js_str_data(needle), needle_size)) {
                        found = (double)(position + (int64_t)i);
                        break;
                    }
                }
                position += (int64_t)(part - needle_size + 1U);
            }
            js_release(ctx, needle);
            return js_num(found);
        }
        default: return js_undefined();
    }
}

/* ---------------------------------------------------------- Format object */

bool xx_xxds_table_row(xx_xxds_table *table)
{
    if (!table || table->failed) return false;
    if (table->has_row) js_array_push(table->ctx, table->rows, table->row);
    table->row = js_new_object(table->ctx);
    table->has_row = true;
    if (js_array_length(table->ctx, table->rows) >= (int64_t)XXDS_MAX_ITEMS) table->failed = true;
    return !table->failed;
}

bool xx_xxds_table_int(xx_xxds_table *table, const char *key, int64_t value)
{
    if (!table || table->failed || !table->has_row || !key) return false;
    js_set(table->ctx, table->row, key, js_num((double)value));
    return true;
}

bool xx_xxds_table_str(xx_xxds_table *table, const char *key, const char *value, size_t size)
{
    if (!table || table->failed || !table->has_row || !key) return false;
    js_set(table->ctx, table->row, key, js_strn(table->ctx, value ? value : "", value ? size : 0U));
    return true;
}

static const char *xxds_type_name_of(xx_file_type_t type);

static JSVal xxds_relative(const xxds_call *call, int64_t absolute)
{
    return js_num(absolute < call->base ? -1.0 : (double)(absolute - call->base));
}

/* File offset (relative to the format) of `size` mapped bytes at a virtual
 * address, or -1. The whole range must be backed by file data. */
static double xxds_map_va(const xxds_call *call, const xx_memory_map *map, uint64_t va, int64_t size)
{
    int64_t offset, total = call->base + call->size;
    if (!map || va == 0U || size <= 0 || !xx_memory_map_is_physical_address_range(map, va, size)) return -1.0;
    offset = xx_memory_map_address_to_offset(map, va);
    if (offset < call->base || offset > total || size > total - offset) return -1.0;
    return (double)(offset - call->base);
}

static double xxds_map_rva(const xxds_call *call, const xx_memory_map *map, int64_t rva, int64_t size)
{
    int64_t offset, total = call->base + call->size;
    uint64_t address;
    if (!map || rva < 0 || size <= 0) return -1.0;
    address = xx_memory_map_relative_address_to_address(map, rva);
    if (address == XX_INVALID_ADDRESS || !xx_memory_map_is_physical_address_range(map, address, size)) return -1.0;
    offset = xx_memory_map_relative_address_to_offset(map, rva);
    if (offset < call->base || offset > total || size > total - offset) return -1.0;
    return (double)(offset - call->base);
}

static JSVal xxds_format(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    xxds_call *call = xxds_current(ctx);
    int op = (int)(size_t)user;
    Abstractformat *format;
    (void)this_value;
    if (!call) return js_throw(ctx, "Format is not available here");
    format = call->format;
    switch (op) {
        case XM_TYPE: return js_str(ctx, xx_format_file_type_to_string(format->file_type));
        case XM_BASESHIFT: return js_num((double)(format->base_address - call->base));
        case XM_VERSION: {
            const char *version = xx_format_get_version(format);
            return js_str(ctx, version ? version : "");
        }
        case XM_EXTRA: {
            /* Format.extra(metaId): a string extra parameter of the reader, or null. */
            const xx_var *value = xx_format_find_extra_parameter(format, (uint32_t)xxds_arg_i64(ctx, argc, argv, 0, 0));
            const char *text = value ? xx_var_get_str(value) : NULL;
            return text ? js_str(ctx, text) : js_null();
        }
        case XM_TYPENAME: return js_str(ctx, xxds_type_name_of(format->file_type));
        case XM_SIZE: return js_num(format->base_info_handled && format->is_valid && format->format_size > 0 ? (double)format->format_size : -1.0);
        case XM_ENSURE: return js_bool((format->base_info_handled || xx_format_handle_base_info(format, call->pd)) && format->is_valid);
        case XM_BIG: return js_bool(format->endian == XX_ENDIAN_BIG);
        case XM_MAPPED: return js_bool(format->is_mapped);
        case XM_VA2OFF: return xxds_relative(call, xx_format_address_to_offset(format, (uint64_t)xxds_arg_i64(ctx, argc, argv, 0, 0), call->pd));
        case XM_RVA2OFF: return xxds_relative(call, xx_format_rel_address_to_offset(format, xxds_arg_i64(ctx, argc, argv, 0, 0), call->pd));
        case XM_OFF2VA: {
            int64_t offset = xxds_arg_i64(ctx, argc, argv, 0, -1);
            return offset < 0 || offset > call->size ? js_num(-1.0) : js_num((double)xx_format_offset_to_address(format, call->base + offset, call->pd));
        }
        case XM_VAAT: {
            int64_t width = xxds_arg_i64(ctx, argc, argv, 1, 4);
            uint64_t pointer;
            if (width != 4 && width != 8) return js_throw(ctx, "Format.vaAt: size must be 4 or 8");
            if (!xxds_read_uint(ctx, call, xxds_arg_i64(ctx, argc, argv, 0, 0), (unsigned)width, xxds_arg_bool(ctx, argc, argv, 2), &pointer)) return js_undefined();
            return xxds_relative(call, xx_format_address_to_offset(format, pointer, call->pd));
        }
        case XM_RVARANGE:
            return js_num(xxds_map_rva(call, xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, call->pd), xxds_arg_i64(ctx, argc, argv, 0, -1),
                                       xxds_arg_i64(ctx, argc, argv, 1, 1)));
        case XM_VARANGE:
            return js_num(xxds_map_va(call, xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, call->pd), (uint64_t)xxds_arg_i64(ctx, argc, argv, 0, 0),
                                      xxds_arg_i64(ctx, argc, argv, 1, 1)));
        case XM_VARANGEAT: {
            int64_t width = xxds_arg_i64(ctx, argc, argv, 1, 4);
            uint64_t pointer;
            if (width != 4 && width != 8) return js_throw(ctx, "Format.vaRangeAt: pointer size must be 4 or 8");
            if (!xxds_read_uint(ctx, call, xxds_arg_i64(ctx, argc, argv, 0, 0), (unsigned)width, xxds_arg_bool(ctx, argc, argv, 3), &pointer)) return js_undefined();
            return js_num(xxds_map_va(call, xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, call->pd), pointer, xxds_arg_i64(ctx, argc, argv, 2, 1)));
        }
        case XM_IMAGEBASE: {
            const xx_memory_map *map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, call->pd);
            return map ? js_num((double)map->module_address) : js_num(-1.0);
        }
        case XM_MAPRECORD: {
            const xx_memory_map *map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, call->pd);
            const xx_memory_record *record = map ? xx_memory_map_record_by_relative_address(map, xxds_arg_i64(ctx, argc, argv, 0, -1)) : NULL;
            JSVal row;
            if (!record) return js_null();
            row = js_new_object(ctx);
            js_set(ctx, row, "offset", js_num(record->offset < call->base ? -1.0 : (double)(record->offset - call->base)));
            js_set(ctx, row, "address", js_num(record->address == XX_INVALID_ADDRESS ? -1.0 : (double)record->address));
            js_set(ctx, row, "size", js_num((double)record->size));
            js_set(ctx, row, "part", js_num((double)record->file_part));
            js_set(ctx, row, "partNumber", js_num((double)record->file_part_number));
            js_set(ctx, row, "name", js_str(ctx, record->name));
            js_set(ctx, row, "isVirtual", js_bool(record->is_virtual));
            return row;
        }
        case XM_TABLE: {
            xx_xxds_table table;
            char *name = argc > 0 ? js_to_cstr(ctx, argv[0]) : NULL;
            bool known;
            table.ctx = ctx;
            table.rows = js_new_array(ctx);
            table.row = js_undefined();
            table.has_row = false;
            table.failed = false;
            known = name && xx_xxds_reader_table(format, name, &table, call->pd);
            if (table.has_row) js_array_push(ctx, table.rows, table.row);
            if (!known || table.failed) {
                js_release(ctx, table.rows);
                js_throw(ctx, "Format.table: no table '%s'", name ? name : "");
                xx_rt_free(name);
                return js_undefined();
            }
            xx_rt_free(name);
            return table.rows;
        }
        default: return js_undefined();
    }
}

/* --------------------------------------------------------- Structs object */

static bool xxds_push(JSCtx *ctx, xxds_call *call, uint32_t id, int64_t offset, int64_t entry, int64_t total, uint64_t count, int kind, int64_t address)
{
    xx_data_struct *item;
    if (!call->collecting) {
        js_throw(ctx, "Structs can only be filled by scan()");
        return false;
    }
    if (!xxds_range(call, offset, total < 0 ? 0 : total)) {
        js_throw(ctx, "structure outside the file: offset %lld size %lld", (long long)offset, (long long)total);
        return false;
    }
    if (call->count >= XXDS_MAX_ITEMS) {
        js_throw(ctx, "too many structures");
        return false;
    }
    if (call->count == call->capacity) {
        size_t capacity = call->capacity ? call->capacity * 2U : 64U;
        void *grown = xx_mem_realloc(call->items, capacity * sizeof(*call->items));
        if (!grown) {
            js_throw(ctx, "out of memory");
            return false;
        }
        call->items = (xx_data_struct *)grown;
        call->capacity = capacity;
    }
    if (address == XXDS_ADDRESS_MAPPED) {
        /* Exact 64-bit arithmetic: a script number could not carry it. */
        uint64_t mapped = xx_format_offset_to_address(call->format, call->base + offset, call->pd);
        address = mapped > (uint64_t)INT64_MAX ? -1 : (int64_t)mapped;
    }
    item = &call->items[call->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = call->base + offset;
    item->address = address;
    item->entry_size = entry;
    item->total_size = total;
    item->count = count;
    item->type = (xx_data_struct_type_t)kind;
    return true;
}

/* Resolves a catalog name or id argument. 0 with an exception when unknown. */
static uint32_t xxds_struct_id(JSCtx *ctx, xxds_call *call, JSVal value)
{
    const xxds_entry *entry;
    if (value.tag == JT_NUM) {
        uint32_t id = (uint32_t)js_to_int64(ctx, value);
        if (id == XX_DATA_STRUCT_ID_RAW_DATA || xxds_entry_by_id(call->script, id, NULL)) return id;
        js_throw(ctx, "unknown structure id %u", (unsigned)id);
        return 0;
    }
    if (value.tag == JT_STR) {
        if (!xx_rt_strcmp(js_str_data(value), "RAW_DATA")) return XX_DATA_STRUCT_ID_RAW_DATA;
        if ((entry = xxds_entry_by_name(call->script, js_str_data(value)))) return entry->id;
        js_throw(ctx, "unknown structure '%s'", js_str_data(value));
        return 0;
    }
    js_throw(ctx, "structure name expected");
    return 0;
}

static JSVal xxds_structs(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    xxds_call *call = xxds_current(ctx);
    int op = (int)(size_t)user;
    int64_t offset, entry, total, address, mapped;
    uint64_t count = 1U;
    uint32_t id;
    int kind;
    (void)this_value;
    if (!call) return js_throw(ctx, "Structs is not available here");
    if (op == XS_COUNT) return js_num((double)call->count);
    if (xxds_stopped(call)) return js_throw(ctx, "stopped");
    if (op == XS_RAW) {
        offset = xxds_arg_i64(ctx, argc, argv, 0, -1);
        total = xxds_arg_i64(ctx, argc, argv, 1, -1);
        id = argc > 2 && !js_is_undefined(argv[2]) ? xxds_struct_id(ctx, call, argv[2]) : XX_DATA_STRUCT_ID_RAW_DATA;
        if (!id) return js_undefined();
        if (total == 0) return js_bool(1);
        xxds_push(ctx, call, id, offset, total, total, 1U, XX_DATA_STRUCT_TYPE_RAW_DATA, call->format->is_mapped ? call->base + offset : -1);
        return js_bool(1);
    }
    if (argc < 3 || !(id = xxds_struct_id(ctx, call, argv[0]))) return js_has_exception(ctx) ? js_undefined() : js_throw(ctx, "Structs: missing arguments");
    offset = xxds_arg_i64(ctx, argc, argv, 1, -1);
    mapped = call->format->is_mapped ? call->base + offset : -1;
    if (op == XS_ADD) {
        entry = total = xxds_arg_i64(ctx, argc, argv, 2, -1);
        kind = (int)xxds_arg_i64(ctx, argc, argv, 3, XX_DATA_STRUCT_TYPE_STRUCT);
        address = xxds_arg_i64(ctx, argc, argv, 4, mapped);
    } else if (op == XS_TABLE) {
        int64_t rows = xxds_arg_i64(ctx, argc, argv, 3, 0);
        entry = xxds_arg_i64(ctx, argc, argv, 2, -1);
        if (rows == 0) return js_bool(1);
        if (entry <= 0 || rows < 0 || entry > INT64_MAX / rows) return js_throw(ctx, "Structs.table: bad entry size or count");
        count = (uint64_t)rows;
        total = entry * rows;
        kind = (int)xxds_arg_i64(ctx, argc, argv, 4, XX_DATA_STRUCT_TYPE_STRUCT);
        address = xxds_arg_i64(ctx, argc, argv, 5, mapped);
    } else {
        entry = xxds_arg_i64(ctx, argc, argv, 2, -1);
        total = xxds_arg_i64(ctx, argc, argv, 3, -1);
        count = (uint64_t)xxds_arg_i64(ctx, argc, argv, 4, 1);
        kind = (int)xxds_arg_i64(ctx, argc, argv, 5, XX_DATA_STRUCT_TYPE_STRUCT);
        address = xxds_arg_i64(ctx, argc, argv, 6, mapped);
    }
    if (kind < XX_DATA_STRUCT_TYPE_UNKNOWN || kind > XX_DATA_STRUCT_TYPE_RAW_DATA) return js_throw(ctx, "Structs: bad kind");
    xxds_push(ctx, call, id, offset, entry, total, count, kind, address);
    return js_bool(1);
}

/* ------------------------------------------------------- Util and include */

/* Util.heapSort: the library's Windows sort (xx_sort_context_impl, src/sort/xx_sort_algorithms.inc) over the
 * elements [from, from + count) of a script array, so equal elements end in the order the C left them in.
 * compare(a, b) gets two elements, or two one-element arrays when `boxed` is set (scalar elements). */
typedef struct xxds_sort {
    JSCtx *ctx;
    JSVal array, compare;
    int64_t from;
    bool boxed, failed;
} xxds_sort;

static double xxds_sort_order(xxds_sort *sort, int64_t left, int64_t right)
{
    JSVal argv[2], result;
    double order = 0.0;
    if (sort->failed) return 0.0;
    argv[0] = js_get_index(sort->ctx, sort->array, sort->from + left);
    argv[1] = js_get_index(sort->ctx, sort->array, sort->from + right);
    if (sort->boxed) {
        JSVal a = js_new_array(sort->ctx), b = js_new_array(sort->ctx);
        js_set_index(sort->ctx, a, 0, argv[0]);
        js_set_index(sort->ctx, b, 0, argv[1]);
        argv[0] = a;
        argv[1] = b;
    }
    result = js_call(sort->ctx, sort->compare, js_undefined(), 2, argv);
    if (js_has_exception(sort->ctx)) sort->failed = true;
    else order = js_to_number(sort->ctx, result);
    js_release(sort->ctx, result);
    js_release(sort->ctx, argv[0]);
    js_release(sort->ctx, argv[1]);
    return order;
}

static void xxds_sort_swap(xxds_sort *sort, int64_t left, int64_t right)
{
    JSVal a = js_get_index(sort->ctx, sort->array, sort->from + left), b = js_get_index(sort->ctx, sort->array, sort->from + right);
    js_set_index(sort->ctx, sort->array, sort->from + left, b);
    js_set_index(sort->ctx, sort->array, sort->from + right, a);
}

static void xxds_sort_sift(xxds_sort *sort, int64_t root, int64_t end)
{
    while (!sort->failed && root <= (end - 1) / 2) {
        int64_t child = root * 2 + 1, swap = root;
        if (xxds_sort_order(sort, swap, child) < 0) swap = child;
        if (child < end && xxds_sort_order(sort, swap, child + 1) < 0) swap = child + 1;
        if (swap == root) return;
        xxds_sort_swap(sort, root, swap);
        root = swap;
    }
}

static JSVal xxds_util(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    int op = (int)(size_t)user;
    char text[40];
    (void)this_value;
    if (op == XU_INDEXOF) {
        /* Util.indexOf(array, value, from[, to]): first index in [from, to) whose element is the number value. */
        int64_t length = argc > 0 && js_is_array(argv[0]) ? js_array_length(ctx, argv[0]) : -1;
        int64_t from = xxds_arg_i64(ctx, argc, argv, 2, 0), to = xxds_arg_i64(ctx, argc, argv, 3, length);
        double wanted = argc > 1 ? js_to_number(ctx, argv[1]) : 0.0;
        if (length < 0) return js_throw(ctx, "Util.indexOf: array expected");
        if (to > length) to = length;
        for (int64_t i = from < 0 ? 0 : from; i < to; ++i) {
            JSVal item = js_get_index(ctx, argv[0], i);
            if (xxds_current(ctx)) xxds_current(ctx)->prof_scanned++;
            bool hit = item.tag == JT_NUM && item.u.n == wanted;
            js_release(ctx, item);
            if (hit) return js_num((double)i);
        }
        return js_num(-1.0);
    }
    if (op == XU_HEAPSORT) {
        xxds_sort sort;
        int64_t count = xxds_arg_i64(ctx, argc, argv, 2, 0), start, end;
        if (argc < 4 || !js_is_array(argv[0]) || !js_is_callable(argv[3]) || count < 0 || count > (int64_t)XXDS_MAX_ITEMS * 4) return js_throw(ctx, "Util.heapSort: bad arguments");
        sort.ctx = ctx;
        sort.array = argv[0];
        sort.compare = argv[3];
        sort.from = xxds_arg_i64(ctx, argc, argv, 1, 0);
        sort.boxed = xxds_arg_bool(ctx, argc, argv, 4);
        sort.failed = false;
        if (count < 2) return js_bool(1);
        start = count / 2;
        end = count - 1;
        while (start != 0 && !sort.failed) xxds_sort_sift(&sort, --start, end);
        while (end != 0 && !sort.failed) {
            xxds_sort_swap(&sort, 0, end);
            --end;
            if (end != 0) xxds_sort_sift(&sort, 0, end);
        }
        return sort.failed ? js_undefined() : js_bool(1);
    }
    if (op == XU_CRC) {
        /* Util.crc(kind, seed, array, at, n): a library checksum over n byte elements of a script array. */
        char *kind = argc > 0 ? js_to_cstr(ctx, argv[0]) : NULL;
        int64_t seed = xxds_arg_i64(ctx, argc, argv, 1, 0), at = xxds_arg_i64(ctx, argc, argv, 3, 0), size = xxds_arg_i64(ctx, argc, argv, 4, 0);
        uint8_t *bytes = NULL;
        double result = -1.0;
        if (kind && argc > 2 && js_is_array(argv[2]) && at >= 0 && size >= 0 && (uint64_t)size <= XXDS_MAX_SCRIPT && (bytes = (uint8_t *)xx_mem_alloc((size_t)size + 1U))) {
            for (int64_t i = 0; i < size; ++i) {
                JSVal item = js_get_index(ctx, argv[2], at + i);
                bytes[i] = item.tag == JT_NUM ? (uint8_t)(int64_t)item.u.n : 0U;
                js_release(ctx, item);
            }
            if (!xx_rt_strcmp(kind, "crc32")) result = (double)xx_crc32_calc((uint32_t)seed, bytes, (size_t)size);
            else if (!xx_rt_strcmp(kind, "crc32c")) result = (double)xx_crc32c_calc((uint32_t)seed, bytes, (size_t)size);
            else if (!xx_rt_strcmp(kind, "crc16xmodem")) result = (double)xx_crc16_xmodem_calc((uint16_t)seed, bytes, (size_t)size);
        }
        xx_mem_free(bytes);
        xx_rt_free(kind);
        return result < 0 ? js_throw(ctx, "Util.crc: bad arguments") : js_num(result);
    }
    if (op == XU_UTC) {
        /* Civil date from a day count (proleptic Gregorian, Unix epoch). */
        int64_t seconds = xxds_arg_i64(ctx, argc, argv, 0, 0), days, rest, era, doe, yoe, doy, mp, year, month, day;
        if (seconds < 0 || seconds > INT64_C(253402300799)) return js_undefined();
        days = seconds / 86400;
        rest = seconds % 86400;
        days += 719468;
        era = days / 146097;
        doe = days - era * 146097;
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        mp = (5 * doy + 2) / 153;
        day = doy - (153 * mp + 2) / 5 + 1;
        month = mp < 10 ? mp + 3 : mp - 9;
        year = yoe + era * 400 + (month <= 2 ? 1 : 0);
        xx_rt_snprintf(text, sizeof(text), "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", (long long)year, (long long)month, (long long)day,
                       (long long)(rest / 3600), (long long)(rest % 3600 / 60), (long long)(rest % 60));
        return js_str(ctx, text);
    }
    if (op == XU_HEX) {
        int digits = (int)xxds_arg_i64(ctx, argc, argv, 1, 0);
        double number = argc > 0 ? js_to_number(ctx, argv[0]) : 0.0;
        uint64_t value = number < 0 ? (uint64_t)(int64_t)number : (uint64_t)number;
        if (digits < 0 || digits > 16) digits = 0;
        xx_rt_snprintf(text, sizeof(text), "0x%0*llX", digits, (unsigned long long)value);
        return js_str(ctx, text);
    }
    return js_undefined();
}

static JSVal xxds_include(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    xxds_call *call = xxds_current(ctx);
    xxds_script *script = (xxds_script *)user;
    char *name = argc > 0 ? js_to_cstr(ctx, argv[0]) : NULL;
    char *text;
    int ok;
    (void)this_value;
    (void)call;
    text = name ? xxds_source(name) : NULL;
    if (!text) {
        js_throw(ctx, "include: no script '%s'", name ? name : "");
        xx_rt_free(name);
        return js_undefined();
    }
    ok = js_eval_nested(ctx, text, name);
    xx_mem_free(text);
    xx_rt_free(name);
    (void)script;
    return ok ? js_bool(1) : js_undefined();
}

/* ---------------------------------------------------------------- loading */

static void xxds_define(JSCtx *ctx, const char *object_name, JSNativeFn fn, const char *const *names, const int *ops, size_t count)
{
    JSVal global = js_global(ctx), object = js_new_object(ctx);
    for (size_t i = 0; i < count; ++i) js_def_method(ctx, object, names[i], fn, 0, (void *)(size_t)ops[i]);
    js_set(ctx, global, object_name, object);
    js_release(ctx, global);
}

static void xxds_script_release(xxds_script *script)
{
    if (script->ctx) js_free(script->ctx);
    script->ctx = NULL;
    for (size_t i = 0; i < script->entry_count; ++i) {
        for (size_t a = 0; a < script->entries[i].alias_count; ++a) xx_mem_free(script->entries[i].aliases[a]);
        xx_mem_free(script->entries[i].aliases);
        xx_mem_free(script->entries[i].description);
        xx_mem_free(script->entries[i].name);
    }
    xx_mem_free(script->entries);
    script->entries = NULL;
    script->entry_count = 0;
}

/* Reads the catalog the boot code stored in __xxds_cat into the C index. */
static bool xxds_read_catalog(xxds_script *script)
{
    JSCtx *ctx = script->ctx;
    JSVal global = js_global(ctx), catalog = js_get(ctx, global, "__xxds_cat");
    int64_t count = js_is_array(catalog) ? js_array_length(ctx, catalog) : -1;
    bool ok = count >= 0 && count <= (int64_t)XXDS_MAX_ITEMS;
    if (ok && count) {
        script->entries = (xxds_entry *)xx_mem_calloc((size_t)count, sizeof(*script->entries));
        ok = script->entries != NULL;
    }
    for (int64_t i = 0; ok && i < count; ++i) {
        JSVal entry = js_get_index(ctx, catalog, i), name = js_get(ctx, entry, "name");
        int64_t id = xxds_get_i64(ctx, entry, "id", 0);
        xxds_entry *slot = &script->entries[script->entry_count];
        ok = name.tag == JT_STR && js_str_len(name) > 0 && js_str_len(name) < 128U && id > 0 && id < (int64_t)XX_DATA_STRUCT_ID_RAW_DATA &&
             xx_rt_strcmp(js_str_data(name), "RAW_DATA") && !xxds_entry_by_id(script, (uint32_t)id, NULL) && !xxds_entry_by_name(script, js_str_data(name));
        if (ok) {
            size_t size = js_str_len(name);
            slot->name = (char *)xx_mem_alloc(size + 1U);
            ok = slot->name != NULL;
            if (ok) {
                xx_rt_memcpy(slot->name, js_str_data(name), size);
                slot->name[size] = '\0';
                slot->id = (uint32_t)id;
                slot->endian = (int)xxds_get_i64(ctx, entry, "endian", 0);
                ++script->entry_count;
                ok = xxds_read_aliases(ctx, entry, slot);
                if (ok) {
                    JSVal description = js_get(ctx, entry, "description");
                    if (description.tag == JT_STR && js_str_len(description) <= XXDS_MAX_TEXT)
                        ok = (slot->description = xxds_copy_text(js_str_data(description), js_str_len(description))) != NULL;
                    js_release(ctx, description);
                }
            }
        }
        js_release(ctx, name);
        js_release(ctx, entry);
    }
    js_release(ctx, catalog);
    js_release(ctx, global);
    if (!ok) xxds_set_error(script->name, "structs() must return entries with a unique id and name");
    return ok;
}

static bool xxds_load(xxds_script *script)
{
    static const char *const file_names[] = {"size", "valid", "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "u64hi", "u64lo", "bytes", "cstr", "cstrSize", "find", "crc32", "read"};
    static const int file_ops[] = {XF_SIZE, XF_VALID, XF_U8, XF_U16, XF_U32, XF_U64, XF_I8, XF_I16, XF_I32, XF_I64, XF_U64HI, XF_U64LO, XF_BYTES, XF_CSTR, XF_CSTRSIZE, XF_FIND, XF_CRC32, XF_READ};
    static const char *const format_names[] = {"type", "size", "isBigEndian", "isMapped", "vaToOffset", "rvaToOffset", "offsetToVa", "vaAt", "table", "rvaRange", "vaRange", "vaRangeAt", "mapRecord", "imageBase", "ensure", "typeName", "baseShift", "version", "extra"};
    static const int format_ops[] = {XM_TYPE, XM_SIZE, XM_BIG, XM_MAPPED, XM_VA2OFF, XM_RVA2OFF, XM_OFF2VA, XM_VAAT, XM_TABLE, XM_RVARANGE, XM_VARANGE, XM_VARANGEAT, XM_MAPRECORD, XM_IMAGEBASE, XM_ENSURE, XM_TYPENAME, XM_BASESHIFT, XM_VERSION, XM_EXTRA};
    static const char *const structs_names[] = {"add", "table", "put", "raw", "count"};
    static const int structs_ops[] = {XS_ADD, XS_TABLE, XS_PUT, XS_RAW, XS_COUNT};
    static const char *const util_names[] = {"utc", "hex", "indexOf", "crc", "heapSort"};
    static const int util_ops[] = {XU_UTC, XU_HEX, XU_INDEXOF, XU_CRC, XU_HEAPSORT};
    JSCtx *ctx;
    if (script->ctx) return true;
    if (script->failed) return false;
    script->failed = true;
    ctx = script->ctx = js_new();
    if (!ctx) {
        xxds_set_error(script->name, "cannot create a script context");
        return false;
    }
    js_set_user(ctx, NULL);
    xxds_define(ctx, "File", xxds_file, file_names, file_ops, sizeof(file_ops) / sizeof(file_ops[0]));
    xxds_define(ctx, "Format", xxds_format, format_names, format_ops, sizeof(format_ops) / sizeof(format_ops[0]));
    xxds_define(ctx, "Structs", xxds_structs, structs_names, structs_ops, sizeof(structs_ops) / sizeof(structs_ops[0]));
    xxds_define(ctx, "Util", xxds_util, util_names, util_ops, sizeof(util_ops) / sizeof(util_ops[0]));
    js_def_fn(ctx, "include", xxds_include, 1, script);
    if (!xxds_eval(script, "_prelude") || !xxds_eval(script, script->name)) {
        xxds_script_release(script);
        return false;
    }
    if (!js_eval_nested(ctx, xxds_boot, "xxds_boot") || js_has_exception(ctx)) {
        xxds_script_error(script);
        xxds_script_release(script);
        return false;
    }
    if (!xxds_read_catalog(script)) {
        xxds_script_release(script);
        return false;
    }
    script->failed = false;
    return true;
}

/* The loaded script of a name; created on first use. Caller holds the lock. */
static xxds_script *xxds_get_script(const char *name)
{
    xxds_script *script;
    for (script = g_xxds_scripts; script; script = script->next) {
        if (!xx_rt_strcmp(script->name, name)) break;
    }
    if (!script) {
        if (xx_rt_strlen(name) >= sizeof(script->name) || !(script = (xxds_script *)xx_mem_calloc(1U, sizeof(*script)))) return NULL;
        xx_rt_memcpy(script->name, name, xx_rt_strlen(name) + 1U);
        script->next = g_xxds_scripts;
        g_xxds_scripts = script;
    }
    return xxds_load(script) ? script : NULL;
}

/* XX_FILE_TYPE_ suffixes, the names fileTypes() uses. Only the script
 * directory needs them at run time; embedded scripts are resolved by the
 * generator. */
typedef struct xxds_type_name {
    const char *name;
    xx_file_type_t type;
} xxds_type_name;

static const xxds_type_name xxds_type_names[] = {
#include "xx_xxds_types.inc"
};

static xx_file_type_t xxds_type_by_name(const char *name, size_t size)
{
    for (size_t i = 0; i < sizeof(xxds_type_names) / sizeof(xxds_type_names[0]); ++i) {
        const char *known = xxds_type_names[i].name;
        if (xx_rt_strlen(known) == size && !xx_mem_compare(known, name, size)) return xxds_type_names[i].type;
    }
    return XX_FILE_TYPE_UNKNOWN;
}

/* The XX_FILE_TYPE_ suffix of a type ("PNG"); "UNKNOWN" when it has none. */
static const char *xxds_type_name_of(xx_file_type_t type)
{
    for (size_t i = 0; i < sizeof(xxds_type_names) / sizeof(xxds_type_names[0]); ++i) {
        if (xxds_type_names[i].type == type) return xxds_type_names[i].name;
    }
    return "UNKNOWN";
}

static const char *xxds_find_text(const char *text, const char *needle)
{
    size_t size = xx_rt_strlen(needle);
    for (; *text; ++text) {
        if (*text == *needle && !xx_mem_compare(text, needle, size)) return text;
    }
    return NULL;
}

/* Adds the types one directory script declares in its fileTypes(), read
 * textually like tools/generate_xxds_registry.py does. A type already
 * claimed keeps its first script. */
static void xxds_route_script(const char *name, const char *text)
{
    const char *cursor = xxds_find_text(text, "function fileTypes");
    const char *end;
    if (xxds_find_text(text, "\n// XXDS-STUB") || !cursor || !(cursor = xxds_find_text(cursor, "[")) || !(end = xxds_find_text(cursor, "]"))) return;
    while (cursor < end) {
        const char *open = xxds_find_text(cursor, "\""), *close;
        xx_file_type_t type;
        bool known = false;
        if (!open || open >= end || !(close = xxds_find_text(open + 1, "\"")) || close > end) break;
        cursor = close + 1;
        type = xxds_type_by_name(open + 1, (size_t)(close - open - 1));
        for (size_t i = 0; i < g_xxds_route_count && !known; ++i) known = g_xxds_routes[i].type == type;
        if (type != XX_FILE_TYPE_UNKNOWN && !known) {
            void *grown = xx_mem_realloc(g_xxds_routes, (g_xxds_route_count + 1U) * sizeof(*g_xxds_routes));
            if (!grown) return;
            g_xxds_routes = (xxds_directory_route *)grown;
            g_xxds_routes[g_xxds_route_count].type = type;
            xx_rt_memcpy(g_xxds_routes[g_xxds_route_count].name, name, xx_rt_strlen(name) + 1U);
            ++g_xxds_route_count;
        }
    }
}

/* Reads which file types the scripts of the script directory serve, so a
 * script can be tried there before it is embedded. Caller holds the lock. */
static void xxds_scan_folder(const char *folder)
{
    xx_list_t *entries;
    if (!(entries = xx_list_create(sizeof(xx_fs_entry_t *), NULL))) return;
    (void)xx_fs_list_dir_sorted(folder, entries, XX_FS_SORT_NAME);
    for (size_t i = 0; i < xx_list_count(entries); ++i) {
        xx_fs_entry_t *entry = NULL;
        size_t size;
        if (!xx_list_get(entries, i, &entry) || !entry) continue;
        size = entry->name ? xx_rt_strlen(entry->name) : 0U;
        if (entry->type == XX_FS_ENTRY_FILE && size > 5U && size - 5U < sizeof(g_xxds_routes->name) && entry->name[0] != '_' &&
            !xx_rt_strcmp(entry->name + size - 5U, ".xxds")) {
            char name[64];
            char *text = xx_fs_read_file(entry->path, NULL);
            xx_rt_memcpy(name, entry->name, size - 5U);
            name[size - 5U] = '\0';
            if (text) xxds_route_script(name, text);
            xx_str_free(text);
        }
        xx_fs_entry_free(entry);
    }
    xx_list_destroy(entries);
}

static void xxds_scan_directory(void)
{
    char sub[1200];
    xx_mem_free(g_xxds_routes);
    g_xxds_routes = NULL;
    g_xxds_route_count = 0;
    if (!g_xxds_directory[0]) return;
    xxds_scan_folder(g_xxds_directory);
    if (xx_rt_snprintf(sub, sizeof(sub), "%s/" XXDS_SUBDIR, g_xxds_directory) < (int)sizeof(sub)) xxds_scan_folder(sub);
}

/* Name of the script that serves a format, or NULL. No lock needed. */
static const char *xxds_script_name(Abstractformat *format)
{
    const char *name = xx_xxds_reader_script(format);
    const xxds_embedded *embedded;
    xx_file_type_t type = xx_xxds_reader_alias_type(format->file_type);
    if (name) return (embedded = xxds_find_embedded(name)) && (!embedded->stub || g_xxds_directory[0]) ? name : NULL;
    for (size_t i = 0; i < g_xxds_route_count; ++i) {
        if (g_xxds_routes[i].type == type) return g_xxds_routes[i].name;
    }
    for (size_t i = 0; i < sizeof(xxds_embedded_scripts) / sizeof(xxds_embedded_scripts[0]); ++i) {
        if (xxds_embedded_scripts[i].stub && !g_xxds_directory[0]) continue;
        for (size_t t = 0; t < xxds_embedded_scripts[i].type_count; ++t) {
            if (xxds_embedded_scripts[i].types[t] == type) return xxds_embedded_scripts[i].name;
        }
    }
    return NULL;
}

/* XXDS_PROFILE=2: statements executed per script function during a scan (through the engine's debug hook,
 * which slows the run several times; the counts, not the time, are the result). */
#define XXDS_PROFILE_SLOTS 4096U
typedef struct xxds_profile_slot {
    const char *name;
    uint64_t statements;
} xxds_profile_slot;
static xxds_profile_slot g_xxds_profile[XXDS_PROFILE_SLOTS];
static size_t g_xxds_profile_count;
static uint64_t g_xxds_profile_total;

static int xxds_profile_hook(JSCtx *ctx, const JSDebugEvent *event, void *user)
{
    const char *name = event->pFunctionName ? event->pFunctionName : "(top level)";
    size_t slot = ((size_t)(uintptr_t)name >> 4) % XXDS_PROFILE_SLOTS;
    (void)ctx;
    (void)user;
    if (event->type != JS_DEBUG_STATEMENT) return 1;
    ++g_xxds_profile_total;
    for (size_t probe = 0; probe < XXDS_PROFILE_SLOTS; ++probe, slot = (slot + 1U) % XXDS_PROFILE_SLOTS) {
        if (g_xxds_profile[slot].name == name || (g_xxds_profile[slot].name && !xx_rt_strcmp(g_xxds_profile[slot].name, name))) {
            ++g_xxds_profile[slot].statements;
            return 1;
        }
        if (!g_xxds_profile[slot].name) {
            g_xxds_profile[slot].name = name;
            g_xxds_profile[slot].statements = 1;
            ++g_xxds_profile_count;
            return 1;
        }
    }
    return 1;
}

static void xxds_profile_report(void)
{
    fprintf(stderr, "xxds profile: %llu statements in %llu functions; top functions:\n", (unsigned long long)g_xxds_profile_total,
            (unsigned long long)g_xxds_profile_count);
    for (int rank = 0; rank < 12; ++rank) {
        size_t best = XXDS_PROFILE_SLOTS;
        for (size_t i = 0; i < XXDS_PROFILE_SLOTS; ++i) {
            if (g_xxds_profile[i].name && g_xxds_profile[i].statements && (best == XXDS_PROFILE_SLOTS || g_xxds_profile[i].statements > g_xxds_profile[best].statements)) best = i;
        }
        if (best == XXDS_PROFILE_SLOTS) break;
        fprintf(stderr, "xxds profile:   %12llu  %s\n", (unsigned long long)g_xxds_profile[best].statements, g_xxds_profile[best].name);
        g_xxds_profile[best].statements = 0;
    }
    xx_mem_zero(g_xxds_profile, sizeof(g_xxds_profile));
    g_xxds_profile_count = 0;
    g_xxds_profile_total = 0;
}

static bool xxds_begin(xxds_call *call, Abstractformat *format, xx_pd_struct *pd)
{
    const char *name = xxds_script_name(format);
    int64_t total;
    xx_mem_zero(call, sizeof(*call));
    if (!name || !format->device || format->base_address < 0 || (total = xx_io_total_size(format->device)) < format->base_address) return false;
    if (!(call->script = xxds_get_script(name))) return false;
    call->format = format;
    call->pd = pd;
    call->base = format->base_address;
    {
        /* A reader with an authenticated outer container reports structures in it: the script then runs
         * on the container base and Format.baseShift() tells it where the format starts. */
        int64_t container = 0, container_size = 0;
        if (xx_xxds_reader_container(format, &container, &container_size) && container >= 0 && container <= format->base_address) call->base = container;
    }
    call->size = total - call->base;
    js_set_user(call->script->ctx, call);
    return true;
}

static void xxds_end(xxds_call *call)
{
    if (call->collecting && getenv("XXDS_PROFILE") && getenv("XXDS_PROFILE")[0] == '2' && call->script && call->script->ctx) {
        js_debug_set_hook(call->script->ctx, NULL, NULL);
        xxds_profile_report();
    }
    if (call->collecting && getenv("XXDS_PROFILE")) {
        fprintf(stderr, "xxds profile: %s: File natives %llu, reads %llu (%llu bytes), device reads %llu (%llu bytes), Util.indexOf elements %llu, items %llu\n",
                      call->script ? call->script->name : "?", (unsigned long long)call->prof_natives, (unsigned long long)call->prof_reads,
                      (unsigned long long)call->prof_read_bytes, (unsigned long long)call->prof_device_reads, (unsigned long long)call->prof_device_bytes,
                      (unsigned long long)call->prof_scanned, (unsigned long long)call->count);
    }
    xx_mem_free(call->cache);
    call->cache = NULL;
    call->cache_size = 0;
    if (call->script && call->script->ctx) js_set_user(call->script->ctx, NULL);
}

/* Calls a global script function. The result is owned; `ok` reports failure. */
static JSVal xxds_invoke(xxds_script *script, const char *function, int argc, JSVal *argv, bool *ok)
{
    JSCtx *ctx = script->ctx;
    JSVal global = js_global(ctx), fn = js_get(ctx, global, function), result = js_undefined();
    *ok = js_is_callable(fn) != 0;
    if (*ok) {
        result = js_call(ctx, fn, js_undefined(), argc, argv);
        if (js_has_exception(ctx)) {
            xxds_script_error(script);
            js_release(ctx, result);
            result = js_undefined();
            *ok = false;
        }
    } else {
        xxds_set_error(script->name, "missing function");
    }
    js_release(ctx, fn);
    js_release(ctx, global);
    return result;
}

/* ------------------------------------------------------------- structures */

static void xxds_items_free(void *pointer)
{
    xxds_items *items = (xxds_items *)pointer;
    if (!items) return;
    xx_mem_free(items->items);
    xx_mem_free(items);
}

static xx_data_struct_state *xxds_create_structs_on(Abstractformat *outer, Abstractformat *format, xx_pd_struct *pd);

static xx_data_struct_state *xxds_create_structs(Abstractformat *outer, xx_pd_struct *pd)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, pd) : NULL, *canonical = NULL;
    xx_data_struct_state *state;
    if (!format || !xx_xxds_reader_alias_open(format, pd, &canonical)) return NULL;
    state = xxds_create_structs_on(outer, canonical ? canonical : format, pd);
    xx_xxds_reader_alias_close(canonical); /* the occurrence list does not refer to the reader */
    return state;
}

static xx_data_struct_state *xxds_create_structs_on(Abstractformat *outer, Abstractformat *format, xx_pd_struct *pd)
{
    xx_data_struct_state *state = NULL;
    xxds_items *items = NULL;
    xxds_call call;
    JSVal result;
    bool ok;
    if (!format || (pd && xx_pd_is_stopped(pd)) || !xxds_lock()) return NULL;
    g_xxds_error[0] = '\0';
    if (!xxds_begin(&call, format, pd)) {
        xxds_unlock();
        return NULL;
    }
    call.collecting = true;
    if (getenv("XXDS_PROFILE") && getenv("XXDS_PROFILE")[0] == '2') js_debug_set_hook(call.script->ctx, xxds_profile_hook, NULL);
    result = xxds_invoke(call.script, "scan", 0, NULL, &ok);
    if (ok && result.tag == JT_BOOL && !result.u.b) ok = false;
    js_release(call.script->ctx, result);
    xxds_end(&call);
    xxds_unlock();
    if (ok && !(pd && xx_pd_is_stopped(pd))) {
        state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
        items = (xxds_items *)xx_mem_calloc(1U, sizeof(*items));
    }
    if (!state || !items) {
        xx_mem_free(state);
        xx_mem_free(items);
        xx_mem_free(call.items);
        return NULL;
    }
    items->items = call.items;
    items->count = call.count;
    xx_data_struct_state_init(state, outer);
    state->internal_state = items;
    state->free_internal = xxds_items_free;
    state->total_structs = (int64_t)items->count;
    if (items->count) {
        state->current_struct = items->items[0];
        state->current_index = 0;
        state->has_struct = true;
    }
    return state;
}

/* A reading state only answers the format it was created for. */
static const xx_data_struct *xxds_struct_current(Abstractformat *format, xx_data_struct_state *state)
{
    return format && state && state->format == format && state->has_struct ? &state->current_struct : NULL;
}

static bool xxds_struct_next(Abstractformat *format, xx_data_struct_state *state, xx_pd_struct *pd)
{
    xxds_items *items = state ? (xxds_items *)state->internal_state : NULL;
    if (!items || !format || state->format != format || !state->has_struct || (pd && xx_pd_is_stopped(pd))) return false;
    if (state->current_index < 0 || (uint64_t)state->current_index + 1U >= items->count) {
        state->has_struct = false;
        return false;
    }
    state->current_struct = items->items[++state->current_index];
    return true;
}

static const char *xxds_id_name(Abstractformat *outer, uint32_t id)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, NULL) : NULL;
    const char *name = format ? xxds_script_name(format) : NULL, *result = "UNKNOWN";
    xxds_script *script;
    const xxds_entry *entry;
    if (id == XX_DATA_STRUCT_ID_RAW_DATA) return "RAW_DATA";
    if (!name || !xxds_lock()) return result;
    /* Entry names live as long as the loaded script, like the old tables. */
    if ((script = xxds_get_script(name)) && (entry = xxds_entry_by_id(script, id, NULL))) result = entry->name;
    xxds_unlock();
    return result;
}

static uint32_t xxds_name_id(Abstractformat *outer, const char *text)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, NULL) : NULL;
    const char *name = format ? xxds_script_name(format) : NULL;
    xxds_script *script;
    const xxds_entry *entry;
    uint32_t id = 0;
    if (!text || !name) return 0;
    if (!xx_rt_strcmp(text, "RAW_DATA")) return XX_DATA_STRUCT_ID_RAW_DATA;
    if (!xxds_lock()) return 0;
    if ((script = xxds_get_script(name)) && (entry = xxds_entry_by_name(script, text))) id = entry->id;
    xxds_unlock();
    return id;
}

/* ---------------------------------------------------------------- records */

static void xxds_records_free(void *pointer)
{
    xxds_records *records = (xxds_records *)pointer;
    if (!records) return;
    for (size_t i = 0; i < records->count; ++i) {
        xx_mem_free(records->fields[i].name);
        xx_mem_free(records->fields[i].type);
        xx_mem_free(records->fields[i].comment);
        xx_mem_free(records->fields[i].display);
        xx_mem_free(records->fields[i].description);
    }
    xx_mem_free(records->fields);
    xx_xxds_reader_alias_close(records->owned);
    xx_mem_free(records);
}

/* The decoded value of a record as the argument of a comment/display hook. */
static JSVal xxds_record_value(JSCtx *ctx, const xx_data_struct_record *record)
{
    size_t size = 0;
    const void *bytes;
    switch (xx_var_get_type(&record->value)) {
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: return js_num((double)xx_var_get_i64(&record->value));
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return js_num((double)xx_var_get_u64(&record->value));
        case XX_VAR_TYPE_FLOAT:
        case XX_VAR_TYPE_DOUBLE: return js_num(xx_var_get_double(&record->value));
        case XX_VAR_TYPE_STRING:
        case XX_VAR_TYPE_STRING_VIEW: return js_str(ctx, xx_var_get_str(&record->value));
        case XX_VAR_TYPE_BYTES:
        case XX_VAR_TYPE_BYTES_VIEW:
            bytes = xx_var_get_bytes(&record->value, &size);
            return js_strn(ctx, (const char *)bytes, bytes ? size : 0U);
        default: return js_undefined();
    }
}

/* Runs one optional hook (comment or display) of a field. NULL when the
 * field has none or it returned nothing; false in *ok on a script failure. */
static wchar_t *xxds_field_note(xxds_call *call, JSVal field, const char *key, const xx_data_struct_record *record, JSVal ds, bool *ok, bool *is_null)
{
    JSCtx *ctx = call->script->ctx;
    bool utf8 = key[0] == 'd' && key[7] == '\x01';
    JSVal hook = js_get(ctx, field, utf8 ? "display" : key), argv[3], result;
    wchar_t *text = NULL;
    if (hook.tag == JT_NULL && is_null) *is_null = true;
    if (hook.tag == JT_UNDEF || hook.tag == JT_NULL) return NULL;
    if (hook.tag == JT_STR) {
        text = utf8 ? xxds_wide_utf8(ctx, hook, ok) : xxds_wide_value(ctx, hook);
        js_release(ctx, hook);
        return text;
    }
    argv[0] = hook;
    argv[1] = xxds_record_value(ctx, record);
    argv[2] = ds;
    result = xxds_invoke(call->script, "__xxds_note", 3, argv, ok);
    if (*ok) text = utf8 ? xxds_wide_utf8(ctx, result, ok) : xxds_wide_value(ctx, result);
    if (*ok && result.tag == JT_NULL && is_null) *is_null = true;
    js_release(ctx, result);
    js_release(ctx, argv[1]);
    js_release(ctx, hook);
    return text;
}

/* A bit-field record: the raw value stays the packed word, the value becomes
 * the field itself and the standard display is derived from it, so location
 * roles format the field and not the word. */
static bool xxds_apply_logical(xx_data_struct_record *record, const xxds_prepared_field *field)
{
    const wchar_t *type = field->desc.type;
    unsigned width = 64U;
    char ascii[32];
    wchar_t wide[32];
    int length;
    if (!field->logical) return true;
    for (const wchar_t *digit = type; digit && *digit; ++digit) {
        if (*digit >= L'0' && *digit <= L'9') {
            width = 0U;
            while (*digit >= L'0' && *digit <= L'9') width = width * 10U + (unsigned)(*digit++ - L'0');
            break;
        }
    }
    xx_var_cleanup(&record->value);
    xx_var_init(&record->value);
    if (field->logical == 1) {
        xx_var_set_bool(&record->value, field->logical_value != 0.0);
        length = xx_rt_snprintf(ascii, sizeof(ascii), "%s", field->logical_value != 0.0 ? "true" : "false");
    } else if (field->logical == 3) {
        int64_t value = (int64_t)field->logical_value;
        if (width == 8U) xx_var_set_i8(&record->value, (int8_t)value);
        else if (width == 16U) xx_var_set_i16(&record->value, (int16_t)value);
        else if (width == 32U) xx_var_set_i32(&record->value, (int32_t)value);
        else xx_var_set_i64(&record->value, value);
        length = xx_rt_snprintf(ascii, sizeof(ascii), "%lld", (long long)xx_var_get_i64(&record->value));
    } else {
        uint64_t value = field->logical_value < 0.0 ? (uint64_t)(int64_t)field->logical_value : (uint64_t)field->logical_value;
        if (width == 8U) xx_var_set_u8(&record->value, (uint8_t)value);
        else if (width == 16U) xx_var_set_u16(&record->value, (uint16_t)value);
        else if (width == 32U) xx_var_set_u32(&record->value, (uint32_t)value);
        else xx_var_set_u64(&record->value, value);
        length = xx_rt_snprintf(ascii, sizeof(ascii), "%llu", (unsigned long long)xx_var_get_u64(&record->value));
    }
    if (length < 0 || (size_t)length >= sizeof(ascii)) return false;
    for (int i = 0; i <= length; ++i) wide[i] = (wchar_t)(unsigned char)ascii[i];
    return xx_data_struct_record_set_comment(record, NULL) && xx_data_struct_record_set_display_value(record, wide);
}

static bool xxds_record_load(xx_data_struct_record_state *state)
{
    xxds_records *records = (xxds_records *)state->internal_state;
    const xxds_prepared_field *field;
    if (state->current_index < 0 || (uint64_t)state->current_index >= records->count) return false;
    field = &records->fields[state->current_index];
    if (!xx_xxds_populate_field(&state->current_record, records->format, &state->parent_struct, &field->desc, field->big_endian)) return false;
    if (!xxds_apply_logical(&state->current_record, field)) {
        xx_data_struct_record_cleanup(&state->current_record);
        return false;
    }
    if (field->no_comment) xx_data_struct_record_set_comment(&state->current_record, NULL);
    if (field->description && !xx_data_struct_record_set_description(&state->current_record, field->description)) {
        xx_data_struct_record_cleanup(&state->current_record);
        return false;
    }
    if ((field->display && !xx_data_struct_record_set_display_value(&state->current_record, field->display)) ||
        (field->comment && !xx_data_struct_record_set_comment(&state->current_record, field->comment))) {
        xx_data_struct_record_cleanup(&state->current_record);
        return false;
    }
    state->has_record = true;
    return true;
}

static xx_data_struct_record_state *xxds_create_records_on(Abstractformat *outer, Abstractformat *format, const xx_data_struct *ds, xx_pd_struct *pd);

static xx_data_struct_record_state *xxds_create_records(Abstractformat *outer, const xx_data_struct *ds, xx_pd_struct *pd)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, pd) : NULL, *canonical = NULL;
    xx_data_struct_record_state *state;
    if (!format || !xx_xxds_reader_alias_open(format, pd, &canonical)) return NULL;
    state = xxds_create_records_on(outer, canonical ? canonical : format, ds, pd);
    if (state) ((xxds_records *)state->internal_state)->owned = canonical; /* records are decoded lazily from it */
    else xx_xxds_reader_alias_close(canonical);
    return state;
}

static xx_data_struct_record_state *xxds_create_records_on(Abstractformat *outer, Abstractformat *format, const xx_data_struct *ds, xx_pd_struct *pd)
{
    xx_data_struct_record_state *state = NULL;
    xxds_records *records = NULL;
    const xxds_entry *entry;
    xxds_call call;
    JSCtx *ctx;
    JSVal argv[2], fields = js_undefined(), ds_value;
    size_t index = 0;
    int64_t count = 0;
    bool ok;
    if (!format || !ds || ds->id == XX_DATA_STRUCT_ID_RAW_DATA || (pd && xx_pd_is_stopped(pd)) || !xxds_lock()) return NULL;
    g_xxds_error[0] = '\0';
    if (!xxds_begin(&call, format, pd)) {
        xxds_unlock();
        return NULL;
    }
    ctx = call.script->ctx;
    entry = xxds_entry_by_id(call.script, ds->id, &index);
    ok = entry && ds->offset >= call.base && ds->offset - call.base <= call.size;
    ds_value = js_new_object(ctx);
    if (ok) {
        js_set(ctx, ds_value, "id", js_num((double)ds->id));
        js_set(ctx, ds_value, "name", js_str(ctx, entry->name));
        js_set(ctx, ds_value, "offset", js_num((double)(ds->offset - call.base)));
        js_set(ctx, ds_value, "address", js_num((double)ds->address));
        js_set(ctx, ds_value, "entrySize", js_num((double)ds->entry_size));
        js_set(ctx, ds_value, "totalSize", js_num((double)ds->total_size));
        js_set(ctx, ds_value, "count", js_num((double)ds->count));
        argv[0] = js_num((double)index);
        argv[1] = ds_value;
        fields = xxds_invoke(call.script, "__xxds_fields", 2, argv, &ok);
        count = ok && js_is_array(fields) ? js_array_length(ctx, fields) : 0;
        ok = ok && count > 0 && count <= (int64_t)XXDS_MAX_FIELDS;
    }
    if (ok) {
        records = (xxds_records *)xx_mem_calloc(1U, sizeof(*records));
        if (records) records->fields = (xxds_prepared_field *)xx_mem_calloc((size_t)count, sizeof(*records->fields));
        ok = records && records->fields;
        if (ok) records->format = format;
    }
    for (int64_t i = 0; ok && i < count; ++i) {
        JSVal field = js_get_index(ctx, fields, i), name = js_get(ctx, field, "name"), type = js_get(ctx, field, "type");
        xxds_prepared_field *prepared = &records->fields[records->count];
        xx_data_struct_record record;
        int64_t endian = xxds_get_i64(ctx, field, "endian", entry->endian);
        prepared->name = xxds_wide_value(ctx, name);
        prepared->type = xxds_wide_value(ctx, type);
        prepared->desc.name = prepared->name;
        prepared->desc.type = prepared->type;
        prepared->desc.rel_offset = xxds_get_i64(ctx, field, "offset", -1);
        prepared->desc.size = xxds_get_i64(ctx, field, "size", -1);
        prepared->desc.property = (xx_data_struct_record_property_t)xxds_get_i64(ctx, field, "role", 0);
        prepared->big_endian = endian == 1 || (endian == 2 && format->endian == XX_ENDIAN_BIG);
        {
            JSVal description = js_get(ctx, field, "description");
            if (description.tag == JT_STR) prepared->description = xxds_wide_value(ctx, description);
            js_release(ctx, description);
        }
        ++records->count;
        ok = prepared->name && prepared->type && prepared->desc.rel_offset >= 0 && prepared->desc.size >= 0 &&
             (ds->total_size < 0 || (prepared->desc.rel_offset <= ds->total_size && prepared->desc.size <= ds->total_size - prepared->desc.rel_offset));
        if (ok && (js_has(ctx, field, "comment") || js_has(ctx, field, "display") || js_has(ctx, field, "logical"))) {
            /* Hooks see the decoded value, so decode the record once here. */
            ok = xx_xxds_populate_field(&record, format, ds, &prepared->desc, prepared->big_endian);
            if (ok && js_has(ctx, field, "logical")) {
                JSVal hook = js_get(ctx, field, "logical"), hook_argv[2], result;
                hook_argv[0] = xxds_record_value(ctx, &record);
                hook_argv[1] = ds_value;
                result = js_call(ctx, hook, js_undefined(), 2, hook_argv);
                if (js_has_exception(ctx)) {
                    xxds_script_error(call.script);
                    ok = false;
                } else if (result.tag == JT_BOOL) {
                    prepared->logical = 1;
                    prepared->logical_value = result.u.b ? 1.0 : 0.0;
                } else if (result.tag == JT_NUM) {
                    prepared->logical = prepared->type[0] == L'i' ? 3 : 2;
                    prepared->logical_value = result.u.n;
                }
                js_release(ctx, result);
                js_release(ctx, hook_argv[0]);
                js_release(ctx, hook);
                if (ok && !xxds_apply_logical(&record, prepared)) ok = false;
                if (!ok) xx_data_struct_record_cleanup(&record);
            }
            if (ok) {
                prepared->comment = xxds_field_note(&call, field, "comment", &record, ds_value, &ok, &prepared->no_comment);
                if (ok) prepared->display = xxds_field_note(&call, field, js_has(ctx, field, "utf8") ? "display\x01" : "display", &record, ds_value, &ok, NULL);
                xx_data_struct_record_cleanup(&record);
            }
        }
        js_release(ctx, type);
        js_release(ctx, name);
        js_release(ctx, field);
    }
    js_release(ctx, fields);
    js_release(ctx, ds_value);
    xxds_end(&call);
    xxds_unlock();
    if (ok) state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xxds_records_free(records);
        return NULL;
    }
    xx_data_struct_record_state_init(state, outer, ds);
    state->internal_state = records;
    state->free_internal = xxds_records_free;
    state->total_records = (int64_t)records->count;
    state->current_index = 0;
    if (!xxds_record_load(state)) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    return state;
}

static const xx_data_struct_record *xxds_record_current(Abstractformat *format, xx_data_struct_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

static bool xxds_record_next(Abstractformat *format, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    xxds_records *records = state ? (xxds_records *)state->internal_state : NULL;
    if (!records || !format || state->format != format || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    if (state->current_index < 0 || (uint64_t)state->current_index + 1U >= records->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    state->has_record = false;
    ++state->current_index;
    return xxds_record_load(state);
}

static const char *xxds_description(Abstractformat *outer, uint32_t id)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, NULL) : NULL;
    const char *name = format ? xxds_script_name(format) : NULL, *result = NULL;
    xxds_script *script;
    const xxds_entry *entry;
    if (!name || !xxds_lock()) return NULL;
    /* Like the names, descriptions live as long as the loaded script. */
    if ((script = xxds_get_script(name)) && (entry = xxds_entry_by_id(script, id, NULL))) result = entry->description;
    xxds_unlock();
    return result;
}

static xx_io_device *xxds_device(Abstractformat *outer, xx_pd_struct *pd)
{
    Abstractformat *format = outer ? xx_xxds_reader_inner(outer, pd) : NULL;
    return format ? format->device : NULL;
}

/* ------------------------------------------------------------- public API */

const StreamDataStructs *xx_xxds_get_stream_datastructs(Abstractformat *format)
{
    static const StreamDataStructs stream = {
        .data_struct_id_to_string = xxds_id_name,
        .data_struct_string_to_id = xxds_name_id,
        .create_data_structs_reading = xxds_create_structs,
        .get_current_data_struct = xxds_struct_current,
        .data_struct_move_to_next = xxds_struct_next,
        .create_data_struct_records_reading = xxds_create_records,
        .get_current_data_struct_record = xxds_record_current,
        .data_struct_record_move_to_next = xxds_record_next,
        .data_struct_device = xxds_device,
        .data_struct_description = xxds_description,
    };
    return format && xxds_script_name(format) ? &stream : NULL;
}

void xx_xxds_set_script_directory(const char *path)
{
    size_t size = path ? xx_rt_strlen(path) : 0U;
    if (!xxds_lock()) return;
    if (size < sizeof(g_xxds_directory)) {
        if (size) xx_rt_memcpy(g_xxds_directory, path, size);
        g_xxds_directory[size] = '\0';
        /* Contexts hold the old sources. */
        for (xxds_script *script = g_xxds_scripts; script; script = script->next) {
            xxds_script_release(script);
            script->failed = false;
        }
        xxds_scan_directory();
    }
    xxds_unlock();
}

const char *xx_xxds_last_error(void)
{
    return g_xxds_error;
}

void xx_xxds_shutdown(void)
{
    if (!xxds_lock()) return;
    while (g_xxds_scripts) {
        xxds_script *script = g_xxds_scripts;
        g_xxds_scripts = script->next;
        xxds_script_release(script);
        xx_mem_free(script);
    }
    xxds_unlock();
}

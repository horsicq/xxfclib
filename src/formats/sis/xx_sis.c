/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Symbian / EPOC installation packages: EPOC SIS (r3..r6) and SISX
 * (Symbian OS 9).  xx_sis.h carries the field tables.
 *
 * The EPOC record walk and the language-code table follow Deark's sis.c
 * (modules/sis.c, Deark 1.7.3, Copyright (C) 2018 Jason Summers, MIT
 * licence); the SISX field walk is written from the Symbian "SIS file
 * format" specification.
 *
 * Every offset, length and count read from the package is untrusted: all
 * reads are bounds-checked against the device, lengths and counts are
 * capped before anything is allocated, and every walk is step-bounded.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sis/xx_sis.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef SIS
#define XX_SIS_FILE_TYPE XX_FILE_TYPE_SIS
#else
#define XX_SIS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SIS_UID2_EPOC 0x1000006DU
#define SIS_UID2_EPOC6 0x10003A12U
#define SIS_UID3_EPOC 0x10000419U
#define SIS_UID1_SISX 0x10201A7AU

#define SIS_EPOC_HEADER 0x44U
#define SIS_EPOC_HEADER6 0x50U
#define SIS_EPOC_MAX_LANGS 256U
#define SIS_EPOC_OPT_UNICODE 0x0001U
#define SIS_EPOC_OPT_NOCOMPRESS 0x0008U

/* SISX field types used here. */
#define SISX_STRING 1U
#define SISX_ARRAY 2U
#define SISX_COMPRESSED 3U
#define SISX_CONTENTS 12U
#define SISX_CONTROLLER 13U
#define SISX_CAPABILITIES 41U
#define SISX_FILE_DESCRIPTION 24U
#define SISX_HASH 25U
#define SISX_IF 26U
#define SISX_ELSE_IF 27U
#define SISX_INSTALL_BLOCK 28U
#define SISX_DATA 30U
#define SISX_DATA_UNIT 31U
#define SISX_FILE_DATA 32U
#define SISX_CONTROLLER_CHECKSUM 34U
#define SISX_DATA_CHECKSUM 35U
#define SISX_DATA_INDEX 40U
#define SISX_OP_NULL 8U

#define SIS_MAX_ENTRIES 65536U
#define SIS_MAX_FILE_DATA 65536U
#define SIS_MAX_UNITS 65536U
#define SIS_MAX_NAME_BYTES 2048U
#define SIS_MAX_CONTROLLER (16U * 1024U * 1024U)
#define SIS_MAX_DEPTH 32U
#define SIS_STEP_LIMIT 4000000UL
#define SIS_COPY_CHUNK 65536U

#define SIS_METHOD_STORE 0U
#define SIS_METHOD_ZLIB 8U

typedef struct sis_entry_s {
    char *name;
    int64_t offset;   /**< Payload offset from base, -1 when absent. */
    int64_t size;     /**< Payload bytes in the package. */
    int64_t unpacked; /**< Bytes the member decodes to. */
    uint32_t method;
    bool safe;
} sis_entry;

typedef struct sis_ctx_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t extent;
    uint32_t variant;
    bool unicode;
    unsigned long steps;
    sis_entry *entries;
    size_t count;
    size_t capacity;
    /* SISX data index: file-data k of the package, grouped by unit. */
    int64_t *fd_offset;
    int64_t *fd_length;
    size_t fd_count;
    size_t fd_capacity;
    uint32_t *unit_first;
    uint32_t *unit_count;
    size_t units;
    size_t unit_capacity;
} sis_ctx;

typedef struct sis_stream_s {
    sis_ctx ctx;
    size_t index;
} sis_stream;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint32_t sis_le16(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static bool sis_step(sis_ctx *ctx)
{
    return ++ctx->steps <= SIS_STEP_LIMIT;
}

static void sis_extend(sis_ctx *ctx, int64_t end)
{
    if (end > ctx->extent) ctx->extent = end > ctx->size ? ctx->size : end;
}

/* Read @p size bytes at @p offset relative to the package start. */
static bool sis_read(sis_ctx *ctx, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (offset < 0 || offset > ctx->size || (uint64_t)size > (uint64_t)(ctx->size - offset)) return false;
    if (size == 0U) return true;
    if (xx_io_seek64(ctx->device, ctx->base + offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(ctx->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool sis_open(sis_ctx *ctx, Abstractformat *format)
{
    int64_t total;
    xx_mem_zero(ctx, sizeof(*ctx));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    ctx->device = format->device;
    ctx->base = format->base_address;
    ctx->size = total - format->base_address;
    return true;
}

static void sis_ctx_free(sis_ctx *ctx)
{
    size_t index;
    if (!ctx) return;
    for (index = 0U; index < ctx->count; ++index)
        if (ctx->entries[index].name) xx_str_free(ctx->entries[index].name);
    if (ctx->entries) xx_mem_free(ctx->entries);
    if (ctx->fd_offset) xx_mem_free(ctx->fd_offset);
    if (ctx->fd_length) xx_mem_free(ctx->fd_length);
    if (ctx->unit_first) xx_mem_free(ctx->unit_first);
    if (ctx->unit_count) xx_mem_free(ctx->unit_count);
    ctx->entries = NULL;
    ctx->fd_offset = ctx->fd_length = NULL;
    ctx->unit_first = ctx->unit_count = NULL;
    ctx->count = ctx->capacity = ctx->fd_count = ctx->units = 0U;
}

/* Grow an array of @p element bytes to hold one more than @p used. */
static bool sis_grow(void **array, size_t *capacity, size_t used, size_t element, size_t cap)
{
    size_t wanted;
    void *grown;
    if (used < *capacity) return true;
    if (used >= cap) return false;
    wanted = *capacity ? *capacity * 2U : 16U;
    if (wanted > cap) wanted = cap;
    grown = xx_mem_realloc(*array, wanted * element);
    if (!grown) return false;
    *array = grown;
    *capacity = wanted;
    return true;
}

/* Two parallel arrays sharing one capacity. */
static bool sis_grow2(void **first, void **second, size_t *capacity, size_t used, size_t element, size_t cap)
{
    size_t wanted;
    void *grown;
    if (used < *capacity) return true;
    if (used >= cap) return false;
    wanted = *capacity ? *capacity * 2U : 16U;
    if (wanted > cap) wanted = cap;
    grown = xx_mem_realloc(*first, wanted * element);
    if (!grown) return false;
    *first = grown;
    grown = xx_mem_realloc(*second, wanted * element);
    if (!grown) return false;
    *second = grown;
    *capacity = wanted;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static char *sis_strdup_n(const char *text, size_t length)
{
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    if (length) xx_rt_memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

/* Package strings to UTF-8: UTF-16LE or Latin-1.  Unpaired surrogates
 * become U+FFFD. */
static char *sis_decode(const uint8_t *data, size_t size, bool utf16)
{
    size_t index, out = 0U;
    char *text = (char *)xx_mem_alloc(size * 3U / (utf16 ? 2U : 1U) + 4U);
    if (!text) return NULL;
    if (!utf16) {
        for (index = 0U; index < size; ++index) {
            uint32_t c = data[index];
            if (c < 0x80U) {
                text[out++] = (char)c;
            } else {
                text[out++] = (char)(0xC0U | (c >> 6U));
                text[out++] = (char)(0x80U | (c & 0x3FU));
            }
        }
    } else {
        for (index = 0U; index + 1U < size; index += 2U) {
            uint32_t c = sis_le16(data + index);
            if (c >= 0xD800U && c <= 0xDBFFU && index + 3U < size) {
                uint32_t low = sis_le16(data + index + 2U);
                if (low >= 0xDC00U && low <= 0xDFFFU) {
                    c = 0x10000U + ((c - 0xD800U) << 10U) + (low - 0xDC00U);
                    index += 2U;
                } else {
                    c = 0xFFFDU;
                }
            } else if (c >= 0xD800U && c <= 0xDFFFU) {
                c = 0xFFFDU;
            }
            if (c < 0x80U) {
                text[out++] = (char)c;
            } else if (c < 0x800U) {
                text[out++] = (char)(0xC0U | (c >> 6U));
                text[out++] = (char)(0x80U | (c & 0x3FU));
            } else if (c < 0x10000U) {
                text[out++] = (char)(0xE0U | (c >> 12U));
                text[out++] = (char)(0x80U | ((c >> 6U) & 0x3FU));
                text[out++] = (char)(0x80U | (c & 0x3FU));
            } else {
                text[out++] = (char)(0xF0U | (c >> 18U));
                text[out++] = (char)(0x80U | ((c >> 12U) & 0x3FU));
                text[out++] = (char)(0x80U | ((c >> 6U) & 0x3FU));
                text[out++] = (char)(0x80U | (c & 0x3FU));
            }
        }
    }
    text[out] = 0;
    return text;
}

/* Install path to member path: backslashes become '/', a drive ("c:",
 * "!:") and leading, doubled and trailing separators go away.  The result
 * may be empty. */
static char *sis_target_path(const char *target)
{
    size_t length = target ? xx_str_len(target) : 0U, in = 0U, out = 0U;
    char *path = (char *)xx_mem_alloc(length + 1U);
    if (!path) return NULL;
    if (length >= 2U && target[1] == ':') in = 2U;
    for (; in < length; ++in) {
        char c = target[in] == '\\' ? '/' : target[in];
        if (c == '/' && (out == 0U || path[out - 1U] == '/')) continue;
        path[out++] = c;
    }
    while (out > 0U && path[out - 1U] == '/') --out;
    path[out] = 0;
    return path;
}

static const char *sis_basename(const char *path)
{
    const char *base = path;
    for (; *path; ++path)
        if (*path == '/') base = path + 1;
    return base;
}

static char sis_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool sis_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || sis_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* One path component: printable, no Windows-reserved punctuation, not only
 * dots and spaces, not ending in a dot or space, not a device name. */
static bool sis_component_safe(const char *name, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\') return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (sis_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((sis_upper(name[0]) == 'C' && sis_upper(name[1]) == 'O' && sis_upper(name[2]) == 'M') ||
         (sis_upper(name[0]) == 'L' && sis_upper(name[1]) == 'P' && sis_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool sis_path_safe(const char *path)
{
    size_t start = 0U, index = 0U;
    if (!path || !path[0] || path[0] == '/') return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!sis_component_safe(path + start, index - start)) return false;
            if (path[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* Add a member; takes ownership of @p name. */
static bool sis_add(sis_ctx *ctx, char *name, int64_t offset, int64_t size, int64_t unpacked, uint32_t method)
{
    sis_entry *entry;
    if (!name) return false;
    if (!sis_grow((void **)&ctx->entries, &ctx->capacity, ctx->count, sizeof(sis_entry), SIS_MAX_ENTRIES)) {
        xx_str_free(name);
        return false;
    }
    entry = &ctx->entries[ctx->count++];
    entry->name = name;
    entry->offset = offset;
    entry->size = size;
    entry->unpacked = unpacked;
    entry->method = method;
    entry->safe = sis_path_safe(name);
    return true;
}

static uint32_t sis_name_hash(const char *name)
{
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)sis_upper(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool sis_same_name(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (sis_upper(*a) != sis_upper(*b)) return false;
    return *a == *b;
}

/* Lookup (and optionally insert) in an open-addressing table of entry
 * indexes + 1. */
static bool sis_table_has(const sis_ctx *ctx, const uint32_t *table, size_t mask, const char *name)
{
    size_t slot = sis_name_hash(name) & mask;
    while (table[slot]) {
        if (sis_same_name(ctx->entries[table[slot] - 1U].name, name)) return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

static void sis_table_put(const sis_ctx *ctx, uint32_t *table, size_t mask, size_t index)
{
    size_t slot = sis_name_hash(ctx->entries[index].name) & mask;
    while (table[slot]) slot = (slot + 1U) & mask;
    table[slot] = (uint32_t)(index + 1U);
}

/* Names that collide (ignoring ASCII case, as the target file systems do)
 * get "~N" before the extension of their base name. */
static bool sis_dedupe(sis_ctx *ctx)
{
    size_t slots = 16U, mask, index;
    uint32_t *table;
    if (ctx->count == 0U) return true;
    while (slots < ctx->count * 2U) slots <<= 1U;
    mask = slots - 1U;
    table = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    if (!table) return false;
    for (index = 0U; index < ctx->count; ++index) {
        sis_entry *entry = &ctx->entries[index];
        if (sis_table_has(ctx, table, mask, entry->name)) {
            const char *name = entry->name;
            const char *base = sis_basename(name);
            const char *dot = NULL, *scan;
            size_t head, number;
            char *fresh = NULL;
            for (scan = base; *scan; ++scan)
                if (*scan == '.' && scan != base) dot = scan;
            head = dot ? (size_t)(dot - name) : xx_str_len(name);
            for (number = 2U; number <= ctx->count + 2U; ++number) {
                char suffix[24];
                char *prefix;
                (void)xx_rt_snprintf(suffix, sizeof(suffix), "~%u", (unsigned)number);
                prefix = sis_strdup_n(name, head);
                if (!prefix) break;
                fresh = xx_str_concat3(prefix, suffix, dot ? dot : "");
                xx_str_free(prefix);
                if (!fresh) break;
                if (!sis_table_has(ctx, table, mask, fresh)) break;
                xx_str_free(fresh);
                fresh = NULL;
            }
            if (!fresh) {
                xx_mem_free(table);
                return false;
            }
            xx_str_free(entry->name);
            entry->name = fresh;
            entry->safe = entry->safe && sis_path_safe(fresh);
        }
        sis_table_put(ctx, table, mask, index);
    }
    xx_mem_free(table);
    return true;
}

/* ---------------------------------------------------------------------- */
/* EPOC SIS                                                                */

typedef struct sis_epoc_header_s {
    uint32_t variant;
    uint32_t langs;
    uint32_t files;
    uint32_t options;
    int64_t languages_ptr;
    int64_t files_ptr;
    int64_t component_ptr;
    int64_t header_size;
    bool unicode;
    bool compressed;
} sis_epoc_header;

static bool sis_epoc_parse_header(sis_ctx *ctx, sis_epoc_header *out)
{
    uint8_t head[SIS_EPOC_HEADER];
    uint32_t uid2;
    sis_epoc_header h;
    if (!sis_read(ctx, 0, head, sizeof(head))) return false;
    uid2 = xx_data_get_u32(head + 4, 4, 0, false);
    if (xx_data_get_u32(head + 8, 4, 0, false) != SIS_UID3_EPOC || (uid2 != SIS_UID2_EPOC && uid2 != SIS_UID2_EPOC6)) return false;
    xx_mem_zero(&h, sizeof(h));
    h.variant = uid2 == SIS_UID2_EPOC6 ? XX_SIS_VARIANT_EPOC6 : XX_SIS_VARIANT_EPOC;
    h.header_size = h.variant == XX_SIS_VARIANT_EPOC6 ? SIS_EPOC_HEADER6 : SIS_EPOC_HEADER;
    h.langs = sis_le16(head + 0x12);
    h.files = sis_le16(head + 0x14);
    h.options = sis_le16(head + 0x24);
    h.languages_ptr = (int64_t)xx_data_get_u32(head + 0x30, 4, 0, false);
    h.files_ptr = (int64_t)xx_data_get_u32(head + 0x34, 4, 0, false);
    h.component_ptr = (int64_t)xx_data_get_u32(head + 0x40, 4, 0, false);
    h.unicode = (h.options & SIS_EPOC_OPT_UNICODE) != 0U;
    h.compressed = h.variant == XX_SIS_VARIANT_EPOC6 && (h.options & SIS_EPOC_OPT_NOCOMPRESS) == 0U;
    /* Every package names at least one language, and the language table
     * and file records live inside the file. */
    if (h.langs == 0U || h.langs > SIS_EPOC_MAX_LANGS) return false;
    if (h.languages_ptr < (int64_t)SIS_EPOC_HEADER || h.languages_ptr + 2 * (int64_t)h.langs > ctx->size) return false;
    if (h.files != 0U && (h.files_ptr < (int64_t)SIS_EPOC_HEADER || h.files_ptr + 4 > ctx->size)) return false;
    if (h.files != 0U) {
        uint8_t type[4];
        if (!sis_read(ctx, h.files_ptr, type, 4U) || xx_data_get_u32(type, 4, 0, false) > 6U) return false;
    }
    *out = h;
    return true;
}

/* Language codes, from Deark's sis.c (MIT). */
static void sis_language_code(uint32_t language, char code[3])
{
    static const char codes[99 * 2 + 1] =
        "XXENFRGESPITSWDANOFIAMSFSGPOTUICRUHUDUBLAUBGASNZIFCSSKPLSLTCHKZH"
        "JATHAFSQAHARHYTLBEBNBGMYCAHRCEIESFETFACFGDKAELCGGUHEHIINGASZKNKK"
        "KMKOLOLVLTMKMSMLMRMOMNNNBPPAROSRSISOOSLSSHFSXXTATEBOTICTTKUKURXX"
        "VICYZU";
    if (language >= 99U) language = 0U;
    code[0] = codes[2U * language];
    code[1] = codes[2U * language + 1U];
    code[2] = 0;
}

static char *sis_epoc_string(sis_ctx *ctx, const sis_epoc_header *h, uint32_t length, uint32_t pointer)
{
    uint8_t *raw;
    char *text;
    if (length == 0U || length > SIS_MAX_NAME_BYTES || (int64_t)pointer + (int64_t)length > ctx->size) return sis_strdup_n("", 0U);
    raw = (uint8_t *)xx_mem_alloc(length);
    if (!raw) return NULL;
    if (!sis_read(ctx, (int64_t)pointer, raw, length)) {
        xx_mem_free(raw);
        return sis_strdup_n("", 0U);
    }
    sis_extend(ctx, (int64_t)pointer + (int64_t)length);
    text = sis_decode(raw, length, h->unicode);
    xx_mem_free(raw);
    return text;
}

/* Member name of fork @p fork of a file record. */
static char *sis_epoc_name(const char *source, const char *destination, bool language_set, const char *code, uint32_t record)
{
    char *dst = sis_target_path(destination);
    char *src = sis_target_path(source);
    char *result = NULL;
    const char *use = NULL;
    if (!dst || !src) goto done;
    if (dst[0]) {
        use = dst;
    } else if (src[0]) {
        use = sis_basename(src);
    }
    if (!use) {
        char fallback[32];
        (void)xx_rt_snprintf(fallback, sizeof(fallback), "file_%u", (unsigned)record);
        result = language_set ? xx_str_concat3(code, ".", fallback) : sis_strdup_n(fallback, xx_str_len(fallback));
        goto done;
    }
    if (language_set) {
        const char *base = sis_basename(use);
        char *dir = sis_strdup_n(use, (size_t)(base - use));
        char *prefixed = dir ? xx_str_concat3(dir, code, ".") : NULL;
        if (prefixed) result = xx_str_concat(prefixed, base);
        if (prefixed) xx_str_free(prefixed);
        if (dir) xx_str_free(dir);
    } else {
        result = sis_strdup_n(use, xx_str_len(use));
    }
done:
    if (dst) xx_mem_free(dst);
    if (src) xx_mem_free(src);
    return result;
}

static bool sis_epoc_parse(sis_ctx *ctx)
{
    sis_epoc_header h;
    uint8_t languages[2U * SIS_EPOC_MAX_LANGS];
    uint8_t record[28U + 12U * SIS_EPOC_MAX_LANGS + 8U];
    int64_t position;
    uint32_t index, fork;
    if (!sis_epoc_parse_header(ctx, &h)) return false;
    ctx->variant = h.variant;
    ctx->unicode = h.unicode;
    ctx->extent = h.header_size < ctx->size ? h.header_size : ctx->size;
    if (!sis_read(ctx, h.languages_ptr, languages, 2U * h.langs)) return false;
    sis_extend(ctx, h.languages_ptr + 2 * (int64_t)h.langs);
    position = h.files_ptr;
    for (index = 0U; index < h.files; ++index) {
        uint32_t type, file_type, forks, tail;
        size_t fixed;
        char *source, *destination;
        if (!sis_step(ctx) || !sis_read(ctx, position, record, 4U)) break;
        type = xx_data_get_u32(record, 4, 0, false);
        if (type == 3U || type == 4U) {
            uint32_t length;
            if (!sis_read(ctx, position + 4, record, 4U)) break;
            length = xx_data_get_u32(record, 4, 0, false);
            if ((int64_t)length > ctx->size - position - 8) break;
            position += 8 + (int64_t)length;
            sis_extend(ctx, position);
            continue;
        }
        if (type == 5U || type == 6U) {
            position += 4;
            sis_extend(ctx, position);
            continue;
        }
        if (type > 1U) break; /* options records and unknown types end it */
        forks = type == 1U ? h.langs : 1U;
        tail = h.variant == XX_SIS_VARIANT_EPOC6 ? 12U * forks + 8U : 8U * forks;
        fixed = 28U + tail;
        if (!sis_read(ctx, position, record, fixed)) break;
        sis_extend(ctx, position + (int64_t)fixed);
        file_type = xx_data_get_u32(record + 4, 4, 0, false);
        source = sis_epoc_string(ctx, &h, xx_data_get_u32(record + 12, 4, 0, false), xx_data_get_u32(record + 16, 4, 0, false));
        destination = sis_epoc_string(ctx, &h, xx_data_get_u32(record + 20, 4, 0, false), xx_data_get_u32(record + 24, 4, 0, false));
        if (!source || !destination) {
            if (source) xx_mem_free(source);
            if (destination) xx_mem_free(destination);
            return false;
        }
        /* Deark's selection: standard, text, component, run and open
         * files carry data; "file created during install" (4) does not. */
        if (file_type <= 5U && file_type != 4U) {
            for (fork = 0U; fork < forks; ++fork) {
                const uint8_t *lens = record + 28U;
                const uint8_t *ptrs = lens + 4U * forks;
                int64_t length = (int64_t)xx_data_get_u32(lens + 4U * fork, 4, 0, false);
                int64_t pointer = (int64_t)xx_data_get_u32(ptrs + 4U * fork, 4, 0, false);
                int64_t unpacked = length;
                uint32_t method = SIS_METHOD_STORE;
                char code[3];
                char *name;
                if (h.variant == XX_SIS_VARIANT_EPOC6) unpacked = (int64_t)xx_data_get_u32(ptrs + 4U * forks + 4U * fork, 4, 0, false);
                if (h.compressed) method = SIS_METHOD_ZLIB;
                else unpacked = length;
                sis_language_code(sis_le16(languages + 2U * fork), code);
                name = sis_epoc_name(source, destination, type == 1U, code, index);
                if (pointer + length > ctx->size) pointer = -1;
                else sis_extend(ctx, pointer + length);
                if (!name || !sis_add(ctx, name, pointer, length, unpacked, method)) {
                    /* Out of memory, or the member cap: keep what is
                     * listed so far. */
                    xx_mem_free(source);
                    xx_mem_free(destination);
                    return name != NULL;
                }
            }
        }
        xx_mem_free(source);
        xx_mem_free(destination);
        position += (int64_t)fixed;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* SISX fields                                                             */

typedef struct sisx_field_s {
    uint32_t type;
    uint64_t length;
    uint64_t data; /**< Offset of the field data. */
    uint64_t next; /**< Offset after data and padding, clamped to the end. */
} sisx_field;

/* Decode a field header held in @p head (at least 12 bytes readable, zero
 * padded past @p available).  @p typed: whether a type word leads. */
static bool sisx_field_decode(const uint8_t *head, size_t available, uint64_t at, uint64_t end, bool typed, sisx_field *out)
{
    size_t used = 0U;
    uint32_t low;
    uint64_t length, padded;
    if (typed) {
        if (available < 4U) return false;
        out->type = xx_data_get_u32(head, 4, 0, false);
        used = 4U;
    } else {
        out->type = 0U;
    }
    if (available < used + 4U) return false;
    low = xx_data_get_u32(head + used, 4, 0, false);
    used += 4U;
    if (low & 0x80000000U) {
        uint32_t high;
        if (available < used + 4U) return false;
        high = xx_data_get_u32(head + used, 4, 0, false);
        used += 4U;
        if (high >= 0x80000000U) return false;
        length = ((uint64_t)high << 31U) | (low & 0x7FFFFFFFU);
    } else {
        length = low;
    }
    if (at + used > end || length > end - at - used) return false;
    out->length = length;
    out->data = at + used;
    padded = out->data + length + ((4U - (length & 3U)) & 3U);
    out->next = padded > end ? end : padded;
    return true;
}

static bool sisx_dev_field(sis_ctx *ctx, int64_t at, int64_t end, bool typed, sisx_field *out)
{
    uint8_t head[12];
    size_t available;
    if (at < 0 || at >= end || end > ctx->size) return false;
    available = end - at < 12 ? (size_t)(end - at) : 12U;
    if (!sis_read(ctx, at, head, available)) return false;
    return sisx_field_decode(head, available, (uint64_t)at, (uint64_t)end, typed, out);
}

static bool sisx_mem_field(const uint8_t *buffer, size_t at, size_t end, bool typed, sisx_field *out)
{
    if (at >= end) return false;
    return sisx_field_decode(buffer + at, end - at, (uint64_t)at, (uint64_t)end, typed, out);
}

/* The element type of an in-memory SISArray field and its element range. */
static bool sisx_mem_array(const uint8_t *buffer, const sisx_field *array, uint32_t *element, size_t *start, size_t *end)
{
    if (array->type != SISX_ARRAY || array->length < 4U) return false;
    *element = xx_data_get_u32(buffer + array->data, 4, 0, false);
    *start = (size_t)array->data + 4U;
    *end = (size_t)(array->data + array->length);
    return true;
}

/* ---------------------------------------------------------------------- */
/* SISX data index                                                         */

static bool sisx_index_data(sis_ctx *ctx, const sisx_field *data)
{
    sisx_field array, unit, inner, element, compressed;
    uint8_t word[4];
    int64_t position, end;
    if (!sisx_dev_field(ctx, (int64_t)data->data, (int64_t)(data->data + data->length), true, &array) || array.type != SISX_ARRAY || array.length < 4U ||
        !sis_read(ctx, (int64_t)array.data, word, 4U) || xx_data_get_u32(word, 4, 0, false) != SISX_DATA_UNIT)
        return false;
    position = (int64_t)array.data + 4;
    end = (int64_t)(array.data + array.length);
    while (position < end) {
        int64_t inner_position, inner_end;
        if (!sis_step(ctx) || !sisx_dev_field(ctx, position, end, false, &unit) ||
            !sis_grow2((void **)&ctx->unit_first, (void **)&ctx->unit_count, &ctx->unit_capacity, ctx->units, sizeof(uint32_t), SIS_MAX_UNITS))
            break;
        ctx->unit_first[ctx->units] = (uint32_t)ctx->fd_count;
        ctx->unit_count[ctx->units] = 0U;
        ++ctx->units;
        position = (int64_t)unit.next;
        /* SISDataUnit: one SISArray<SISFileData>. */
        if (unit.length == 0U || !sisx_dev_field(ctx, (int64_t)unit.data, (int64_t)(unit.data + unit.length), true, &inner) || inner.type != SISX_ARRAY ||
            inner.length < 4U || !sis_read(ctx, (int64_t)inner.data, word, 4U) || xx_data_get_u32(word, 4, 0, false) != SISX_FILE_DATA)
            continue;
        inner_position = (int64_t)inner.data + 4;
        inner_end = (int64_t)(inner.data + inner.length);
        while (inner_position < inner_end) {
            size_t slot;
            if (!sis_step(ctx) || !sisx_dev_field(ctx, inner_position, inner_end, false, &element)) break;
            inner_position = (int64_t)element.next;
            if (!sis_grow2((void **)&ctx->fd_offset, (void **)&ctx->fd_length, &ctx->fd_capacity, ctx->fd_count, sizeof(int64_t), SIS_MAX_FILE_DATA)) return true;
            slot = ctx->fd_count++;
            ++ctx->unit_count[ctx->units - 1U];
            ctx->fd_offset[slot] = -1;
            ctx->fd_length[slot] = 0;
            /* SISFileData: one SISCompressed field. */
            if (element.length != 0U && sisx_dev_field(ctx, (int64_t)element.data, (int64_t)(element.data + element.length), true, &compressed) &&
                compressed.type == SISX_COMPRESSED && compressed.length >= 12U) {
                ctx->fd_offset[slot] = (int64_t)compressed.data;
                ctx->fd_length[slot] = (int64_t)compressed.length;
            }
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* SISX controller walk                                                    */

static void sisx_file(sis_ctx *ctx, const uint8_t *b, size_t start, size_t end, uint32_t unit)
{
    sisx_field target, field;
    size_t position;
    uint32_t operation, index;
    char *text, *name;
    int64_t offset = -1, size = 0, unpacked = 0;
    uint32_t method = SIS_METHOD_STORE;
    if (!sisx_mem_field(b, start, end, true, &target) || target.type != SISX_STRING || target.length > SIS_MAX_NAME_BYTES) return;
    position = (size_t)target.next;
    if (!sisx_mem_field(b, position, end, true, &field) || field.type != SISX_STRING) return;
    position = (size_t)field.next;
    if (!sisx_mem_field(b, position, end, true, &field)) return;
    if (field.type == SISX_CAPABILITIES) {
        position = (size_t)field.next;
        if (!sisx_mem_field(b, position, end, true, &field)) return;
    }
    if (field.type != SISX_HASH) return;
    position = (size_t)field.next;
    if (end - position < 28U) return;
    operation = xx_data_get_u32(b + position, 4, 0, false);
    index = xx_data_get_u32(b + position + 24U, 4, 0, false);
    if (operation & SISX_OP_NULL) return;
    if (unit < ctx->units && index < ctx->unit_count[unit]) {
        size_t slot = (size_t)ctx->unit_first[unit] + index;
        uint8_t head[12];
        if (slot < ctx->fd_count && ctx->fd_offset[slot] >= 0 && sis_read(ctx, ctx->fd_offset[slot], head, sizeof(head))) {
            uint32_t algorithm = xx_data_get_u32(head, 4, 0, false);
            uint64_t declared = xx_data_get_u64(head + 4, 8, 0, false);
            int64_t payload = ctx->fd_length[slot] - 12;
            if (algorithm == 0U && declared <= (uint64_t)payload) {
                offset = ctx->fd_offset[slot] + 12;
                size = (int64_t)declared;
                unpacked = (int64_t)declared;
            } else if (algorithm == 1U && declared <= (uint64_t)INT64_MAX) {
                offset = ctx->fd_offset[slot] + 12;
                size = payload;
                unpacked = (int64_t)declared;
                method = SIS_METHOD_ZLIB;
            }
        }
    }
    text = sis_decode(b + target.data, (size_t)target.length, true);
    if (!text) return;
    name = sis_target_path(text);
    xx_mem_free(text);
    if (!name) return;
    if (!name[0]) {
        char fallback[48];
        xx_mem_free(name);
        (void)xx_rt_snprintf(fallback, sizeof(fallback), "file_%u_%u", (unsigned)unit, (unsigned)index);
        name = sis_strdup_n(fallback, xx_str_len(fallback));
    }
    (void)sis_add(ctx, name, offset, size, unpacked, method);
}

static void sisx_block(sis_ctx *ctx, const uint8_t *b, size_t start, size_t end, uint32_t unit, unsigned depth);

static void sisx_controller(sis_ctx *ctx, const uint8_t *b, size_t start, size_t end, unsigned depth)
{
    sisx_field field;
    size_t position = start, block_start = 0U, block_end = 0U;
    uint32_t unit = 0U;
    bool have_block = false;
    if (depth > SIS_MAX_DEPTH) return;
    while (position < end && sis_step(ctx) && sisx_mem_field(b, position, end, true, &field)) {
        if (field.type == SISX_INSTALL_BLOCK && !have_block) {
            have_block = true;
            block_start = (size_t)field.data;
            block_end = (size_t)(field.data + field.length);
        } else if (field.type == SISX_DATA_INDEX && field.length >= 4U) {
            unit = xx_data_get_u32(b + field.data, 4, 0, false);
        }
        position = (size_t)field.next;
    }
    if (have_block) sisx_block(ctx, b, block_start, block_end, unit, depth + 1U);
}

static void sisx_if(sis_ctx *ctx, const uint8_t *b, size_t start, size_t end, uint32_t unit, unsigned depth)
{
    sisx_field field;
    size_t position = start;
    if (depth > SIS_MAX_DEPTH) return;
    while (position < end && sis_step(ctx) && sisx_mem_field(b, position, end, true, &field)) {
        uint32_t element;
        size_t first, last;
        if (field.type == SISX_INSTALL_BLOCK) {
            sisx_block(ctx, b, (size_t)field.data, (size_t)(field.data + field.length), unit, depth + 1U);
        } else if (sisx_mem_array(b, &field, &element, &first, &last) && element == SISX_ELSE_IF) {
            sisx_field item;
            while (first < last && sis_step(ctx) && sisx_mem_field(b, first, last, false, &item)) {
                sisx_if(ctx, b, (size_t)item.data, (size_t)(item.data + item.length), unit, depth + 1U);
                first = (size_t)item.next;
            }
        }
        position = (size_t)field.next;
    }
}

static void sisx_block(sis_ctx *ctx, const uint8_t *b, size_t start, size_t end, uint32_t unit, unsigned depth)
{
    sisx_field field;
    size_t position = start;
    if (depth > SIS_MAX_DEPTH) return;
    while (position < end && sis_step(ctx) && sisx_mem_field(b, position, end, true, &field)) {
        uint32_t element;
        size_t first, last;
        if (sisx_mem_array(b, &field, &element, &first, &last)) {
            sisx_field item;
            while (first < last && sis_step(ctx) && ctx->count < SIS_MAX_ENTRIES && sisx_mem_field(b, first, last, false, &item)) {
                size_t from = (size_t)item.data;
                size_t to = (size_t)(item.data + item.length);
                if (element == SISX_FILE_DESCRIPTION) sisx_file(ctx, b, from, to, unit);
                else if (element == SISX_CONTROLLER) sisx_controller(ctx, b, from, to, depth + 1U);
                else if (element == SISX_IF) sisx_if(ctx, b, from, to, unit, depth + 1U);
                first = (size_t)item.next;
            }
        }
        position = (size_t)field.next;
    }
}

/* ---------------------------------------------------------------------- */
/* SISX top level                                                          */

static bool sisx_header(sis_ctx *ctx, sisx_field *contents)
{
    uint8_t head[16];
    sisx_field first;
    if (!sis_read(ctx, 0, head, sizeof(head)) || xx_data_get_u32(head, 4, 0, false) != SIS_UID1_SISX || !sisx_dev_field(ctx, 16, ctx->size, true, contents) ||
        contents->type != SISX_CONTENTS || contents->length < 12U)
        return false;
    /* The first child is a checksum or the compressed controller. */
    if (!sisx_dev_field(ctx, (int64_t)contents->data, (int64_t)(contents->data + contents->length), true, &first)) return false;
    return first.type == SISX_CONTROLLER_CHECKSUM || first.type == SISX_DATA_CHECKSUM || first.type == SISX_COMPRESSED;
}

static uint8_t *sisx_load_controller(sis_ctx *ctx, const sisx_field *field, size_t *size_out)
{
    uint8_t head[12];
    uint32_t algorithm;
    uint64_t declared, stored;
    uint8_t *plain, *packed;
    size_t written = 0U;
    if (field->length < 12U || !sis_read(ctx, (int64_t)field->data, head, 12U)) return NULL;
    algorithm = xx_data_get_u32(head, 4, 0, false);
    declared = xx_data_get_u64(head + 4, 8, 0, false);
    stored = field->length - 12U;
    if (declared < 8U || declared > SIS_MAX_CONTROLLER) return NULL;
    if (algorithm == 0U) {
        if (stored < declared) return NULL;
        plain = (uint8_t *)xx_mem_alloc((size_t)declared);
        if (!plain) return NULL;
        if (!sis_read(ctx, (int64_t)field->data + 12, plain, (size_t)declared)) {
            xx_mem_free(plain);
            return NULL;
        }
        *size_out = (size_t)declared;
        return plain;
    }
    if (algorithm != 1U || stored < 2U || stored > SIS_MAX_CONTROLLER) return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)stored);
    plain = (uint8_t *)xx_mem_alloc((size_t)declared);
    if (!packed || !plain || !sis_read(ctx, (int64_t)field->data + 12, packed, (size_t)stored) ||
        !xx_zlib_stream_decode_memory(packed, (size_t)stored, plain, (size_t)declared, &written) || written != (size_t)declared) {
        if (packed) xx_mem_free(packed);
        if (plain) xx_mem_free(plain);
        return NULL;
    }
    xx_mem_free(packed);
    *size_out = (size_t)declared;
    return plain;
}

static bool sisx_parse(sis_ctx *ctx)
{
    sisx_field contents, field, controller;
    sisx_field compressed_controller = {0, 0, 0, 0}, data = {0, 0, 0, 0};
    bool have_controller = false, have_data = false;
    int64_t position, end;
    uint8_t *plain;
    size_t plain_size = 0U;
    if (!sisx_header(ctx, &contents)) return false;
    ctx->variant = XX_SIS_VARIANT_SISX;
    ctx->unicode = true;
    ctx->extent = (int64_t)contents.next;
    position = (int64_t)contents.data;
    end = (int64_t)(contents.data + contents.length);
    while (position < end && sis_step(ctx) && sisx_dev_field(ctx, position, end, true, &field)) {
        if (field.type == SISX_COMPRESSED && !have_controller) {
            compressed_controller = field;
            have_controller = true;
        } else if (field.type == SISX_DATA && !have_data) {
            data = field;
            have_data = true;
        }
        position = (int64_t)field.next;
    }
    if (!have_controller) return false;
    plain = sisx_load_controller(ctx, &compressed_controller, &plain_size);
    if (!plain) return false;
    if (!sisx_mem_field(plain, 0U, plain_size, true, &controller) || controller.type != SISX_CONTROLLER) {
        xx_mem_free(plain);
        return false;
    }
    if (have_data) (void)sisx_index_data(ctx, &data);
    sisx_controller(ctx, plain, (size_t)controller.data, (size_t)(controller.data + controller.length), 0U);
    xx_mem_free(plain);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Whole package                                                           */

static bool sis_parse(Abstractformat *format, sis_ctx *ctx)
{
    bool parsed;
    if (!sis_open(ctx, format)) return false;
    if (ctx->size < 16) return false;
    {
        uint8_t head[12];
        if (!sis_read(ctx, 0, head, sizeof(head))) return false;
        if (xx_data_get_u32(head, 4, 0, false) == SIS_UID1_SISX) parsed = sisx_parse(ctx);
        else parsed = sis_epoc_parse(ctx);
    }
    if (!parsed || !sis_dedupe(ctx)) {
        sis_ctx_free(ctx);
        return false;
    }
    return true;
}

static bool sis_quick_check(Abstractformat *format)
{
    sis_ctx ctx;
    uint8_t head[12];
    if (!sis_open(&ctx, format) || ctx.size < 16 || !sis_read(&ctx, 0, head, sizeof(head))) return false;
    if (xx_data_get_u32(head, 4, 0, false) == SIS_UID1_SISX) {
        sisx_field contents;
        return sisx_header(&ctx, &contents);
    } else {
        sis_epoc_header h;
        return sis_epoc_parse_header(&ctx, &h);
    }
}

/* ---------------------------------------------------------------------- */
/* Decoding                                                                */

typedef struct sis_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t total;
    bool overflow;
} sis_sink;

static ssize_t sis_sink_write(xx_io_device *self, const void *buffer, size_t size)
{
    sis_sink *sink = (sis_sink *)self->priv;
    size_t done = 0U;
    if ((uint64_t)size > sink->limit - sink->total) {
        sink->overflow = true;
        return -1;
    }
    while (sink->target && done < size) {
        ssize_t wrote = xx_io_write(sink->target, (const uint8_t *)buffer + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return -1;
        done += (size_t)wrote;
    }
    sink->total += size;
    return (ssize_t)size;
}

static ssize_t sis_sink_read(xx_io_device *self, void *buffer, size_t size)
{
    (void)self;
    (void)buffer;
    (void)size;
    return -1;
}

static int sis_sink_seek(xx_io_device *self, long offset, int whence)
{
    (void)self;
    (void)offset;
    (void)whence;
    return -1;
}

static int sis_sink_close(xx_io_device *self)
{
    (void)self;
    return 0;
}

static int64_t sis_sink_tell(xx_io_device *self)
{
    return (int64_t)((sis_sink *)self->priv)->total;
}

static int64_t sis_sink_size(xx_io_device *self)
{
    return (int64_t)((sis_sink *)self->priv)->total;
}

static bool sis_decode_entry(sis_ctx *ctx, const sis_entry *entry, xx_io_device *destination, xx_pd_struct *pd)
{
    sis_sink sink;
    if (entry->offset < 0 || entry->size < 0 || entry->unpacked < 0 || entry->offset > ctx->size || entry->size > ctx->size - entry->offset) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = sis_sink_write;
    sink.device.read = sis_sink_read;
    sink.device.seek = sis_sink_seek;
    sink.device.close = sis_sink_close;
    sink.device.tell = sis_sink_tell;
    sink.device.total_size = sis_sink_size;
    sink.device.priv = &sink;
    sink.target = destination;
    sink.limit = (uint64_t)entry->unpacked;
    if (entry->method == SIS_METHOD_STORE) {
        uint8_t *buffer;
        int64_t done = 0;
        bool ok = true;
        if (entry->unpacked != entry->size) return false;
        buffer = (uint8_t *)xx_mem_alloc(SIS_COPY_CHUNK);
        if (!buffer) return false;
        while (done < entry->size) {
            size_t chunk = entry->size - done > (int64_t)SIS_COPY_CHUNK ? SIS_COPY_CHUNK : (size_t)(entry->size - done);
            if ((pd && xx_pd_is_stopped(pd)) || !sis_read(ctx, entry->offset + done, buffer, chunk) || sis_sink_write(&sink.device, buffer, chunk) != (ssize_t)chunk) {
                ok = false;
                break;
            }
            done += (int64_t)chunk;
        }
        xx_mem_free(buffer);
        return ok && sink.total == (uint64_t)entry->unpacked;
    } else {
        uint8_t head[2];
        if (entry->size < 2 || !sis_read(ctx, entry->offset, head, 2U) || !xx_zlib_stream_header_is_valid(head, 2U)) return false;
        if (!xx_deflate_unpack_device(ctx->device, ctx->base + entry->offset + 2, entry->size - 2, &sink.device, false, pd)) return false;
        return !sink.overflow && sink.total == (uint64_t)entry->unpacked;
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

static void sis_stream_free(void *opaque)
{
    sis_stream *stream = (sis_stream *)opaque;
    if (!stream) return;
    sis_ctx_free(&stream->ctx);
    xx_mem_free(stream);
}

static bool sis_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *sis_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sis_set_record(xx_archive_record *record, const sis_ctx *ctx, const sis_entry *entry)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->data_offset = entry->offset >= 0 ? ctx->base + entry->offset : -1;
    record->compressed_size = entry->size;
    return xx_archive_record_set_original_name(record, entry->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)entry->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, entry->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_sis_init(xx_sis *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SIS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/vnd.symbian.install");
    xx_format_set_extension(&archive->format, "sis");
    archive->format.check_is_valid = xx_sis_check_is_valid;
    archive->format.handle_base_info = xx_sis_handle_base_info;
    archive->format.get_format_size = xx_sis_get_format_size;
    archive->format.get_number_of_archive_records = xx_sis_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_sis_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_sis_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_sis_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_sis_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_sis_free_archive_records_reading;
}

xx_sis *xx_sis_create(xx_io_device *device, int64_t base_address)
{
    xx_sis *archive = (xx_sis *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sis_init(archive, device, base_address);
    return archive;
}

void xx_sis_destroy(xx_sis *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sis_free(xx_sis *archive)
{
    if (!archive) return;
    xx_sis_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sis_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    (void)pd;
    return sis_quick_check(format);
}

bool xx_sis_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    sis_ctx ctx;
    xx_sis *archive;
    (void)pd;
    if (!format || !sis_parse(format, &ctx)) return false;
    archive = (xx_sis *)format;
    archive->variant = ctx.variant;
    archive->number_of_records = ctx.count;
    if (ctx.variant == XX_SIS_VARIANT_SISX) xx_format_set_extension(format, "sisx");
    format->number_of_archive_records = ctx.count;
    format->format_size = ctx.extent;
    format->is_valid = true;
    format->base_info_handled = true;
    sis_ctx_free(&ctx);
    return true;
}

int64_t xx_sis_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_sis_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_sis_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_sis_handle_base_info(format, pd)) ? ((xx_sis *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sis_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    sis_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (sis_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!sis_parse(format, &stream->ctx)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sis_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sis_stream_free;
    state->total_records = (int64_t)stream->ctx.count;
    if (!sis_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->ctx.count > 0U) {
        if (!sis_set_record(&state->current_record, &stream->ctx, &stream->ctx.entries[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_sis_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_sis_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sis_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (sis_stream *)state->internal_state) || stream->index + 1U >= stream->ctx.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!sis_set_record(&state->current_record, &stream->ctx, &stream->ctx.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sis_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sis_stream *stream;
    const sis_entry *entry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (sis_stream *)state->internal_state) || stream->index >= stream->ctx.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->ctx.entries[stream->index];
    path_option = sis_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return sis_decode_entry(&stream->ctx, entry, NULL, pd);
    if (!entry->safe || entry->offset < 0) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", entry->name)
                                                                                                  : xx_str_concat(base, entry->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = sis_decode_entry(&stream->ctx, entry, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sis_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

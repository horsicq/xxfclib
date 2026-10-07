/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent native C implementation of unencrypted recursive Malie LIB
 * and LIBU. Format evidence: GARbro ArcFormats/Malie/ArcLIB.cs/ArcLIBU.cs
 * (MIT, morkt),
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/Malie/ArcLIB.cs.
 * No upstream code is incorporated. Encrypted LIBP/LIBU directories require
 * separate title-specific decryptors and are never parsed as stored LIB.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/malie_lib/xx_malie_lib.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

#ifdef MALIE_LIB
#define ML_FILE_TYPE XX_FILE_TYPE_MALIE_LIB
#else
#define ML_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define ML_HEADER_SIZE 16U
#define ML_RECORD_SIZE 48U
#define ML_U_RECORD_SIZE 80U
#define ML_MAX_COUNT 0xFFFFFU
#define ML_MAX_PATH 4096U
#define ML_MAX_DEPTH 64U
#define ML_MAX_ACTIVE_INDEX (16U * 1024U * 1024U)
#define ML_MAX_EXPANDED (64U * 1024U * 1024U)

typedef struct ml_member {
    int64_t offset;
    int64_t header_offset;
    uint32_t size;
    uint32_t header_size;
    char *name;
    bool duplicate;
} ml_member;
typedef struct ml_layout {
    ml_member *members;
    uint32_t count;
    uint32_t capacity;
    uint32_t directories;
    size_t active_index;
    size_t expanded_names;
    int64_t format_size;
    size_t index;
} ml_layout;
typedef struct ml_name_key {
    const char *name;
    uint32_t index;
} ml_name_key;

static bool ml_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static bool ml_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || xx_io_seek64(device, at, XX_RT_SEEK_SET))
        return false;
    while (done < size) {
        ssize_t got;
        size_t take = size - done;
        if (ml_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void ml_layout_free(void *ptr) {
    ml_layout *layout = (ml_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i)
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
        xx_mem_free(layout->members);
    }
    xx_mem_free(layout);
}
static size_t ml_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool ml_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool ml_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *ml_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (ml_lead(c) && i < size && ml_trail(raw[i])) {
            at += ml_escape(result + at, c);
            at += ml_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += ml_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
/* LIBU names occupy 34 UTF-16LE code units. Invalid surrogate sequences are
 * rejected; '%' remains escaped so our collision aliases are unambiguous. */
static char *ml_unicode_name(const uint8_t *raw) {
    char *result = (char *)xx_mem_alloc(34U * 4U + 1U);
    size_t i = 0U, out = 0U;
    if (!result) return NULL;
    while (i < 34U) {
        uint32_t c = (uint32_t)raw[2U * i] | (uint32_t)raw[2U * i + 1U] << 8U;
        ++i;
        if (!c) break;
        if (c >= 0xd800U && c <= 0xdbffU) {
            uint32_t trail;
            if (i >= 34U) goto invalid;
            trail = (uint32_t)raw[2U * i] | (uint32_t)raw[2U * i + 1U] << 8U;
            if (trail < 0xdc00U || trail > 0xdfffU) goto invalid;
            ++i;
            c = 0x10000U + ((c - 0xd800U) << 10U) + trail - 0xdc00U;
        } else if (c >= 0xdc00U && c <= 0xdfffU) goto invalid;
        if (c == '%') { out += ml_escape(result + out, (uint8_t)c); continue; }
        if (c == '\\') c = '/';
        if (c < 0x80U) result[out++] = (char)c;
        else if (c < 0x800U) {
            result[out++] = (char)(0xc0U | (c >> 6U));
            result[out++] = (char)(0x80U | (c & 0x3fU));
        } else if (c < 0x10000U) {
            result[out++] = (char)(0xe0U | (c >> 12U));
            result[out++] = (char)(0x80U | ((c >> 6U) & 0x3fU));
            result[out++] = (char)(0x80U | (c & 0x3fU));
        } else {
            result[out++] = (char)(0xf0U | (c >> 18U));
            result[out++] = (char)(0x80U | ((c >> 12U) & 0x3fU));
            result[out++] = (char)(0x80U | ((c >> 6U) & 0x3fU));
            result[out++] = (char)(0x80U | (c & 0x3fU));
        }
    }
    result[out] = 0;
    return out ? result : (xx_mem_free(result), (char *)NULL);
invalid:
    xx_mem_free(result); return NULL;
}
static int ml_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int ml_compare_keys(const void *a, const void *b) {
    const ml_name_key *x = (const ml_name_key *)a;
    const ml_name_key *y = (const ml_name_key *)b;
    int order = ml_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void ml_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), component = 0U, dot = length, i;
    int amount = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') component = i + 1U;
    for (i = length; i > component + 1U; --i)
        if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (amount <= 0 || (size_t)amount >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)amount, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)amount);
}
static int ml_prefix_compare(const char *name, const char *prefix, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        unsigned char a = (unsigned char)name[i], b = (unsigned char)prefix[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
    }
    return name[length] ? 1 : 0;
}
/* A leaf named root cannot coexist with the implicit directory root/child.
 * Alias every such leaf before any output; raw '%' is escaped, so generated
 * aliases cannot collide with original archive names. */
static bool ml_mark_prefixes(ml_layout *layout, const ml_name_key *keys, xx_pd_struct *pd) {
    uint32_t i;
    for (i = 0U; i < layout->count; ++i) {
        const char *name = layout->members[i].name;
        size_t n;
        for (n = 0U; name[n]; ++n) if (name[n] == '/' && n) {
            uint32_t first = 0U, end = layout->count;
            if (ml_stopped(pd)) return false;
            while (first < end) {
                uint32_t middle = first + (end - first) / 2U;
                if (ml_prefix_compare(keys[middle].name, name, n) < 0) first = middle + 1U;
                else end = middle;
            }
            while (first < layout->count && !ml_prefix_compare(keys[first].name, name, n))
                layout->members[keys[first++].index].duplicate = true;
        }
    }
    return true;
}

static char *ml_path(const char *parent, const uint8_t *raw, size_t length) {
    char *component = ml_name(raw, length), *path;
    size_t prefix = xx_str_len(parent), name_length, size;
    if (!component) return NULL;
    name_length = xx_str_len(component);
    size = prefix + (prefix ? 1U : 0U) + name_length;
    if (size > ML_MAX_PATH) { xx_mem_free(component); return NULL; }
    path = (char *)xx_mem_alloc(size + 14U);
    if (path) {
        if (prefix) { xx_rt_memcpy(path, parent, prefix); path[prefix++] = '/'; }
        xx_rt_memcpy(path + prefix, component, name_length + 1U);
    }
    xx_mem_free(component);
    return path;
}
static char *ml_unicode_path(const char *parent, const uint8_t *raw) {
    char *component = ml_unicode_name(raw), *path;
    size_t prefix, name_length, size;
    if (!component) return NULL;
    prefix = xx_str_len(parent); name_length = xx_str_len(component);
    size = prefix + (prefix ? 1U : 0U) + name_length;
    if (size > ML_MAX_PATH) { xx_mem_free(component); return NULL; }
    path = (char *)xx_mem_alloc(size + 14U);
    if (path) {
        if (prefix) { xx_rt_memcpy(path, parent, prefix); path[prefix++] = '/'; }
        xx_rt_memcpy(path + prefix, component, name_length + 1U);
    }
    xx_mem_free(component);
    return path;
}
static bool ml_extensionless(const char *name) {
    const char *dot = NULL, *at;
    for (at = name; *at; ++at) {
        if (*at == '/') dot = NULL;
        else if (*at == '.') dot = at;
    }
    return !dot || !dot[1];
}
static bool ml_append(ml_layout *layout, char *name, int64_t header,
                       int64_t offset, uint32_t size, uint32_t header_size) {
    size_t allocation = xx_str_len(name) + 14U;
    ml_member *member;
    if (layout->count >= ML_MAX_COUNT || allocation > ML_MAX_EXPANDED - layout->expanded_names)
        return false;
    if (layout->count == layout->capacity) {
        uint32_t capacity = layout->capacity ? layout->capacity * 2U : 32U;
        ml_member *grown;
        if (capacity > ML_MAX_COUNT) capacity = ML_MAX_COUNT;
        if ((uint64_t)capacity * sizeof(*grown) > SIZE_MAX) return false;
        grown = (ml_member *)xx_mem_realloc(layout->members, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        xx_mem_zero(grown + layout->capacity, (size_t)(capacity - layout->capacity) * sizeof(*grown));
        layout->members = grown; layout->capacity = capacity;
    }
    member = &layout->members[layout->count++];
    member->name = name; member->header_offset = header;
    member->offset = offset; member->size = size; member->header_size = header_size;
    layout->expanded_names += allocation;
    return true;
}
/* Validate the whole parent's directory before following any child. Each
 * extent is relative to its immediate container and must be beyond that
 * container's complete index, preventing cycles and index/data overlap.
 */
/* 1 valid directory; 0 a bounded opaque resource; -1 I/O/resource failure.
 * An invalid immediate child index must not leave partial leaf state behind. */
static int ml_walk(Abstractformat *format, ml_layout *layout, uint64_t at,
    uint64_t size, const char *parent, unsigned depth, xx_pd_struct *pd) {
    uint8_t header[16], signature[4], *index = NULL;
    uint32_t count, i;
    size_t index_size;
    uint64_t index_end;
    int result = -1;
    if (ml_stopped(pd) || depth >= ML_MAX_DEPTH || layout->directories >= ML_MAX_COUNT) return -1;
    if (size < 16U) return 0;
    if (!ml_read(format->device, format->base_address + (int64_t)at, header, sizeof(header), pd)) return -1;
    if (xx_rt_memcmp(header, "LIB\0", 4U)) return 0;
    count = (uint32_t)header[8] | (uint32_t)header[9] << 8U;
    if (!count || count >= 0x8000U) return 0;
    index_size = (size_t)count * ML_RECORD_SIZE;
    index_end = ML_HEADER_SIZE + (uint64_t)index_size;
    if (index_end > size) return 0;
    if (index_size > ML_MAX_ACTIVE_INDEX - layout->active_index) return -1;
    index = (uint8_t *)xx_mem_alloc(index_size);
    if (!index) return -1;
    layout->active_index += index_size;
    if (!ml_read(format->device, format->base_address + (int64_t)at + ML_HEADER_SIZE, index, index_size, pd)) goto done;
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_RECORD_SIZE;
        uint32_t offset = xx_data_get_u32(entry + 40U, 4, 0, false), member_size = xx_data_get_u32(entry + 36U, 4, 0, false);
        if (ml_stopped(pd)) goto done;
        if (!entry[0] || (uint64_t)offset < index_end ||
            (uint64_t)offset > size || (uint64_t)member_size > size - offset) { result = 0; goto done; }
    }
    ++layout->directories;
    if ((int64_t)(at + index_end) > layout->format_size) layout->format_size = (int64_t)(at + index_end);
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_RECORD_SIZE;
        uint32_t offset = xx_data_get_u32(entry + 40U, 4, 0, false), member_size = xx_data_get_u32(entry + 36U, 4, 0, false);
        if ((int64_t)(at + offset + member_size) > layout->format_size)
            layout->format_size = (int64_t)(at + offset + member_size);
    }
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_RECORD_SIZE;
        uint32_t offset = xx_data_get_u32(entry + 40U, 4, 0, false), member_size = xx_data_get_u32(entry + 36U, 4, 0, false);
        size_t length = 0U;
        char *path;
        bool nested = false;
        if (ml_stopped(pd)) goto done;
        while (length < 36U && entry[length]) ++length;
        path = ml_path(parent, entry, length);
        if (!path) goto done;
        if (ml_extensionless(path) && member_size >= 4U) {
            if (!ml_read(format->device, format->base_address + (int64_t)(at + offset), signature, sizeof(signature), pd)) {
                xx_mem_free(path); goto done;
            }
            if (!xx_rt_memcmp(signature, "LIBP", 4U) || !xx_rt_memcmp(signature, "LIBU", 4U)) {
                xx_mem_free(path); goto done;
            }
            nested = !xx_rt_memcmp(signature, "LIB\0", 4U);
        }
        if (nested) {
            int valid = ml_walk(format, layout, at + offset, member_size, path, depth + 1U, pd);
            if (valid < 0) { xx_mem_free(path); goto done; }
            if (valid > 0) { xx_mem_free(path); continue; }
        }
        if (!ml_append(layout, path,
                format->base_address + (int64_t)at + ML_HEADER_SIZE + (int64_t)i * ML_RECORD_SIZE,
                format->base_address + (int64_t)(at + offset), member_size, ML_RECORD_SIZE)) {
            xx_mem_free(path); goto done;
        }
    }
    result = 1;
done:
    layout->active_index -= index_size;
    xx_mem_free(index);
    return result;
}
/* GARbro ArcLIBU.cs: 16-byte LIBU header, signed 32-bit count at +8, then
 * 80-byte entries (34 UTF-16LE code units, u32 length, i64 relative offset).
 * A complete immediate index is checked before following any nested item. */
static int ml_walk_u(Abstractformat *format, ml_layout *layout, uint64_t at,
    uint64_t size, const char *parent, unsigned depth, xx_pd_struct *pd) {
    uint8_t header[ML_HEADER_SIZE], signature[4], *index = NULL;
    uint32_t count, i;
    size_t index_size;
    uint64_t index_end;
    int result = -1;
    if (ml_stopped(pd) || depth >= ML_MAX_DEPTH || layout->directories >= ML_MAX_COUNT) return -1;
    if (size < ML_HEADER_SIZE) return 0;
    if (!ml_read(format->device, format->base_address + (int64_t)at, header, sizeof(header), pd)) return -1;
    if (xx_rt_memcmp(header, "LIBU", 4U)) return 0;
    count = xx_data_get_u32(header + 8U, 4, 0, false);
    if (!count || count > 32767U) return 0;
    index_size = (size_t)count * ML_U_RECORD_SIZE;
    index_end = ML_HEADER_SIZE + (uint64_t)index_size;
    if (index_end > size) return 0;
    if (index_size > ML_MAX_ACTIVE_INDEX - layout->active_index) return -1;
    index = (uint8_t *)xx_mem_alloc(index_size);
    if (!index) return -1;
    layout->active_index += index_size;
    if (!ml_read(format->device, format->base_address + (int64_t)at + ML_HEADER_SIZE,
                 index, index_size, pd)) goto done;
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_U_RECORD_SIZE;
        uint64_t offset = xx_data_get_u64(entry + 72U, 8, 0, false);
        uint32_t member_size = xx_data_get_u32(entry + 68U, 4, 0, false);
        char *name;
        if (ml_stopped(pd)) goto done;
        name = ml_unicode_name(entry);
        if (!name || offset < index_end || offset > size || member_size > size - offset) {
            if (name) xx_mem_free(name);
            result = 0; goto done;
        }
        xx_mem_free(name);
    }
    ++layout->directories;
    if ((int64_t)(at + index_end) > layout->format_size) layout->format_size = (int64_t)(at + index_end);
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_U_RECORD_SIZE;
        uint64_t offset = xx_data_get_u64(entry + 72U, 8, 0, false), member_size = xx_data_get_u32(entry + 68U, 4, 0, false);
        if ((int64_t)(at + offset + member_size) > layout->format_size)
            layout->format_size = (int64_t)(at + offset + member_size);
    }
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = index + (size_t)i * ML_U_RECORD_SIZE;
        uint64_t offset = xx_data_get_u64(entry + 72U, 8, 0, false);
        uint32_t member_size = xx_data_get_u32(entry + 68U, 4, 0, false);
        char *path;
        bool nested = false;
        if (ml_stopped(pd)) goto done;
        path = ml_unicode_path(parent, entry);
        if (!path) goto done;
        if (ml_extensionless(path) && member_size >= 4U) {
            if (!ml_read(format->device, format->base_address + (int64_t)(at + offset),
                         signature, sizeof(signature), pd)) { xx_mem_free(path); goto done; }
            nested = !xx_rt_memcmp(signature, "LIBU", 4U);
        }
        if (nested) {
            int valid = ml_walk_u(format, layout, at + offset, member_size, path, depth + 1U, pd);
            if (valid < 0) { xx_mem_free(path); goto done; }
            if (valid > 0) { xx_mem_free(path); continue; }
        }
        if (!ml_append(layout, path,
                format->base_address + (int64_t)at + ML_HEADER_SIZE + (int64_t)i * ML_U_RECORD_SIZE,
                format->base_address + (int64_t)(at + offset), member_size, ML_U_RECORD_SIZE)) {
            xx_mem_free(path); goto done;
        }
    }
    result = 1;
done:
    layout->active_index -= index_size;
    xx_mem_free(index);
    return result;
}
static ml_layout *ml_parse_inner(Abstractformat *format, xx_pd_struct *pd) {
    ml_layout *layout = NULL;
    ml_name_key *keys = NULL;
    int64_t total;
    uint8_t signature[4];
    int parsed;
    uint32_t i;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0 || ml_stopped(pd)) return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    layout = (ml_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout || !ml_read(format->device, format->base_address, signature, sizeof(signature), pd)) goto done;
    parsed = !xx_rt_memcmp(signature, "LIBU", 4U)
        ? ml_walk_u(format, layout, 0U, (uint64_t)(total - format->base_address), "", 0U, pd)
        : ml_walk(format, layout, 0U, (uint64_t)(total - format->base_address), "", 0U, pd);
    if (parsed != 1 ||
        !layout->count || (uint64_t)layout->count * sizeof(*keys) > SIZE_MAX) goto done;
    keys = (ml_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(*keys));
    if (!keys) goto done;
    for (i = 0U; i < layout->count; ++i) {
        if (ml_stopped(pd)) goto done;
        keys[i].name = layout->members[i].name; keys[i].index = i;
    }
    xx_rt_qsort(keys, layout->count, sizeof(*keys), ml_compare_keys);
    if (ml_stopped(pd)) goto done;
    for (i = 1U; i < layout->count; ++i)
        if (!ml_fold_compare(keys[i - 1U].name, keys[i].name))
            layout->members[keys[i].index].duplicate = true;
    if (!ml_mark_prefixes(layout, keys, pd)) goto done;
    for (i = 0U; i < layout->count; ++i) {
        if (ml_stopped(pd)) goto done;
        if (layout->members[i].duplicate) ml_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    if (keys) xx_mem_free(keys);
    if (!ok) { ml_layout_free(layout); layout = NULL; }
    return layout;
}
static ml_layout *ml_parse(Abstractformat *format, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    ml_layout *layout = ml_parse_inner(format, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) {
        ml_layout_free(layout); layout = NULL;
    }
    return layout;
}
static void ml_destroy_format(Abstractformat *format) {
    xx_malie_lib_destroy((xx_malie_lib *)format);
}
void xx_malie_lib_init(xx_malie_lib *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = ML_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "lib");
    xx_format_set_mime_type(&archive->format, "application/x-malie-lib-archive");
    archive->format.destroy = ml_destroy_format;
    archive->format.check_is_valid = xx_malie_lib_check_is_valid;
    archive->format.handle_base_info = xx_malie_lib_handle_base_info;
    archive->format.get_format_size = xx_malie_lib_get_format_size;
    archive->format.get_number_of_archive_records = xx_malie_lib_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_malie_lib_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_malie_lib_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_malie_lib_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_malie_lib_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_malie_lib_free_archive_records_reading;
}
xx_malie_lib *xx_malie_lib_create(xx_io_device *device, int64_t base) {
    xx_malie_lib *archive = (xx_malie_lib *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_malie_lib_init(archive, device, base);
    return archive;
}
void xx_malie_lib_destroy(xx_malie_lib *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_malie_lib_free(xx_malie_lib *archive) {
    if (!archive) return;
    xx_malie_lib_destroy(archive); xx_mem_free(archive);
}
bool xx_malie_lib_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ml_layout *layout = ml_parse(format, pd);
    bool valid = layout != NULL;
    ml_layout_free(layout);
    return valid;
}
bool xx_malie_lib_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ml_layout *layout;
    xx_malie_lib *archive;
    if (!format || ml_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = ml_parse(format, pd);
    if (!layout) return false;
    archive = (xx_malie_lib *)format;
    archive->number_of_records = layout->count;
    archive->number_of_directories = layout->directories;
    format->number_of_archive_records = layout->count;
    format->format_size = layout->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ml_layout_free(layout);
    return true;
}
int64_t xx_malie_lib_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_malie_lib_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_malie_lib_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_malie_lib_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool ml_set_record(Abstractformat *format, xx_archive_record_state *state) {
    ml_layout *layout = (ml_layout *)state->internal_state;
    const ml_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    (void)format;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
xx_archive_record_state *xx_malie_lib_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ml_layout *layout = ml_parse(format, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { ml_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = ml_layout_free;
    state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (ml_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = ml_set_record(format, state);
    if (state->has_record) return state;
fail:
    xx_archive_record_state_free(state);
    return NULL;
}
const xx_archive_record *xx_malie_lib_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
bool xx_malie_lib_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ml_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ml_layout *)state->internal_state)) return false;
    if (ml_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index;
    state->current_index = (int64_t)layout->index;
    state->has_record = ml_set_record(format, state);
    return state->has_record;
}
static const xx_var *ml_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}
bool xx_malie_lib_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    ml_layout *layout;
    const ml_member *member;
    const xx_var *limit;
    uint8_t *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    uint32_t done = 0U;
    int64_t cursor, total;
    bool ok = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record || !(layout = (ml_layout *)state->internal_state) ||
        layout->index >= layout->count || destination == format->device || ml_stopped(pd))
        return false;
    member = &layout->members[layout->index];
    total = xx_io_total_size(format->device);
    if (member->offset < 0 || member->offset > total ||
        (int64_t)member->size > total - member->offset) return false;
    limit = ml_option(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->size > xx_var_get_u64(limit)) return false;
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > 1048576U) capacity = 1048576U;
    if (capacity > member->size) capacity = member->size;
    limit = ml_option(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && xx_var_get_u64(limit) < capacity) capacity = (size_t)xx_var_get_u64(limit);
    cursor = xx_io_tell(format->device);
    if (!member->size) { ok = true; goto done; }
    if (!capacity || !(buffer = (uint8_t *)xx_mem_alloc(capacity))) goto done;
    while (done < member->size) {
        size_t take = member->size - done, wrote = 0U;
        if (take > capacity) take = capacity;
        if (!ml_read(format->device, member->offset + done, buffer, take, pd)) goto done;
        while (destination && wrote < take) {
            ssize_t amount;
            if (ml_stopped(pd)) goto done;
            amount = xx_io_write(destination, buffer + wrote, take - wrote);
            if (amount <= 0 || (size_t)amount > take - wrote) goto done;
            wrote += (size_t)amount;
        }
        done += (uint32_t)take;
    }
    ok = !ml_stopped(pd);
done:
    if (buffer) xx_mem_free(buffer);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
static bool ml_reserved(const char *component, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[9];
    size_t n = 0U, i;
    while (n < length && component[n] != '.') ++n;
    while (n && component[n - 1U] == ' ') --n;
    if (n >= sizeof(stem)) return false;
    for (i = 0U; i < n; ++i) {
        char c = component[i]; stem[i] = c >= 'a' && c <= 'z' ? (char)(c + 'A' - 'a') : c;
    }
    stem[n] = 0;
    if (n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (!xx_rt_memcmp(stem, "COM", 3U) || !xx_rt_memcmp(stem, "LPT", 3U))) return true;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!xx_str_cmp(stem, names[i])) return true;
    return false;
}
static bool ml_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || ml_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_malie_lib_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ml_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ml_layout *)state->internal_state) ||
        layout->index >= layout->count || ml_stopped(pd)) return false;
    path_option = ml_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_malie_lib_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!ml_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = ml_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    stage_path = (char *)xx_mem_alloc(xx_str_len(path) + 50U);
    if (!stage_path) goto done;
    for (attempt = 0U; attempt < 128U && !ml_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path, xx_str_len(path) + 50U,
            "%s.xxfc-malie_lib-%u-%u.tmp", path, (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_malie_lib_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !ml_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_malie_lib_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}

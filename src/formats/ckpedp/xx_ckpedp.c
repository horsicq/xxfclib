/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CKP and EDP share a 14-byte header and a variable-length index. Names are
 * inverted bytes (CKP) or inverted UTF-16LE units (EDP); payloads are stored.
 * Layout follows the local XArchive XCKPEDPBase reader, without Qt dependencies.
 */
#include "xx_ckpedp_internal.h"
#include "xx_ckpedp_casefold.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include "xxfclib/data/xx_data.h"

#define CE_HEADER_SIZE 14
#define CE_RECORD_FIXED_SIZE 22
#define CE_MAX_RECORDS 100000U
#define CE_MAX_NAME_UNITS 4096U
#define CE_MAX_RESOLVED_BYTES 32768U
#define CE_COPY_SIZE 65536U

typedef struct ce_member {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
} ce_member;

typedef struct ce_layout {
    ce_member *members;
    size_t count;
    size_t index;
    int64_t format_size;
} ce_layout;

typedef struct ce_range {
    int64_t start;
    int64_t end;
} ce_range;

typedef struct ce_path_slot {
    char *key;
    char *value;
    bool is_directory;
    uint32_t next_suffix;
} ce_path_slot;
typedef struct ce_path_map {
    ce_path_slot *slots;
    size_t capacity, count;
} ce_path_map;

static bool ce_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool ce_range_within(int64_t total, int64_t at, int64_t length) {
    return at >= 0 && length >= 0 && at <= total && length <= total - at;
}
static bool ce_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t length, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || ce_stopped(pd) ||
        xx_io_seek64(device, at, XX_RT_SEEK_SET) != 0) return false;
    while (done < length) {
        size_t take = length - done;
        ssize_t got;
        if (ce_stopped(pd)) return false;
        if (take > CE_COPY_SIZE) take = CE_COPY_SIZE;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static size_t ce_utf8_put(char *out, uint32_t codepoint) {
    if (codepoint < 0x80U) { out[0] = (char)codepoint; return 1U; }
    if (codepoint < 0x800U) {
        out[0] = (char)(0xc0U | (codepoint >> 6U));
        out[1] = (char)(0x80U | (codepoint & 0x3fU));
        return 2U;
    }
    if (codepoint < 0x10000U) {
        out[0] = (char)(0xe0U | (codepoint >> 12U));
        out[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        out[2] = (char)(0x80U | (codepoint & 0x3fU));
        return 3U;
    }
    out[0] = (char)(0xf0U | (codepoint >> 18U));
    out[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
    out[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
    out[3] = (char)(0x80U | (codepoint & 0x3fU));
    return 4U;
}
static bool ce_reserved(const char *part, size_t length) {
    char upper[5] = {0};
    size_t i, stem = 0U;
    while (stem < length && part[stem] != '.') ++stem;
    if (stem < 3U || stem > 4U) return false;
    for (i = 0U; i < stem; ++i) {
        unsigned char c = (unsigned char)part[i];
        upper[i] = (char)(c >= 'a' && c <= 'z' ? c - 32U : c);
    }
    if (stem == 3U)
        return !xx_rt_memcmp(upper, "CON", 3U) ||
               !xx_rt_memcmp(upper, "PRN", 3U) ||
               !xx_rt_memcmp(upper, "AUX", 3U) ||
               !xx_rt_memcmp(upper, "NUL", 3U);
    return ((!xx_rt_memcmp(upper, "COM", 3U) ||
             !xx_rt_memcmp(upper, "LPT", 3U)) &&
            upper[3] >= '1' && upper[3] <= '9');
}
static bool ce_utf8_next(const char **cursor, uint32_t *codepoint) {
    const uint8_t *bytes = (const uint8_t *)*cursor;
    uint32_t value;
    size_t width, i;
    if (!bytes[0]) { *codepoint = 0U; return true; }
    if (bytes[0] < 0x80U) { *codepoint = bytes[0]; ++*cursor; return true; }
    if (bytes[0] >= 0xc2U && bytes[0] <= 0xdfU) {
        width = 2U; value = bytes[0] & 0x1fU;
    } else if (bytes[0] >= 0xe0U && bytes[0] <= 0xefU) {
        width = 3U; value = bytes[0] & 0x0fU;
    } else if (bytes[0] >= 0xf0U && bytes[0] <= 0xf4U) {
        width = 4U; value = bytes[0] & 0x07U;
    } else return false;
    for (i = 1U; i < width; ++i) {
        if (!bytes[i] || (bytes[i] & 0xc0U) != 0x80U) return false;
        value = (value << 6U) | (bytes[i] & 0x3fU);
    }
    if (value < (width == 2U ? 0x80U : width == 3U ? 0x800U : 0x10000U) ||
        (value >= 0xd800U && value <= 0xdfffU) || value > 0x10ffffU)
        return false;
    *cursor += width;
    *codepoint = value;
    return true;
}
static bool ce_name_safe(const char *name) {
    const char *cursor, *component;
    size_t component_units = 0U, total_units = 0U, bytes;
    uint32_t codepoint;
    if (!name || !*name || *name == '/') return false;
    bytes = xx_str_len(name);
    if (bytes > CE_MAX_RESOLVED_BYTES) return false;
    cursor = component = name;
    for (;;) {
        const char *before = cursor;
        if (!ce_utf8_next(&cursor, &codepoint)) return false;
        if (codepoint == 0U || codepoint == '/') {
            size_t length = (size_t)(before - component);
            if (!length || component_units > 255U ||
                (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                before[-1] == '.' || before[-1] == ' ' ||
                ce_reserved(component, length)) return false;
            if (!codepoint) return total_units <= CE_MAX_RESOLVED_BYTES;
            component = cursor;
            component_units = 0U;
            ++total_units;
        } else {
            component_units += codepoint > 0xffffU ? 2U : 1U;
            total_units += codepoint > 0xffffU ? 2U : 1U;
            if (component_units > 255U ||
                total_units > CE_MAX_RESOLVED_BYTES) return false;
        }
    }
}
static char *ce_decode_name(const uint8_t *raw, uint16_t units, bool utf16) {
    char *name;
    size_t used = 0U;
    uint16_t i;
    if (!raw || !units || units > CE_MAX_NAME_UNITS) return NULL;
    name = (char *)xx_mem_alloc((size_t)units * 4U + 1U);
    if (!name) return NULL;
    for (i = 0U; i < units; ++i) {
        uint32_t c = utf16 ? (uint32_t)(~xx_data_get_u16(raw + (size_t)i * 2U, 2, 0, false) & 0xffffU)
                           : (uint32_t)(~raw[i] & 0xffU);
        if (c < 0x40U) c += 0x20U;
        if (utf16 && c >= 0xd800U && c <= 0xdbffU) {
            uint32_t low;
            if (++i >= units) goto invalid;
            low = (uint32_t)(~xx_data_get_u16(raw + (size_t)i * 2U, 2, 0, false) & 0xffffU);
            if (low < 0xdc00U || low > 0xdfffU) goto invalid;
            c = 0x10000U + ((c - 0xd800U) << 10U) + low - 0xdc00U;
        } else if (utf16 && c >= 0xdc00U && c <= 0xdfffU) {
            goto invalid;
        }
        if (c == '\\') c = '/';
        else if (c < 0x20U || c == 0x7fU || c == '<' || c == '>' ||
                 c == ':' || c == '"' || c == '|' || c == '?' || c == '*') c = '_';
        used += ce_utf8_put(name + used, c);
    }
    name[used] = 0;
    if (!ce_name_safe(name)) goto invalid;
    return name;
invalid:
    xx_mem_free(name);
    return NULL;
}
static int ce_compare_ranges(const void *left, const void *right) {
    const ce_range *a = (const ce_range *)left;
    const ce_range *b = (const ce_range *)right;
    return a->start < b->start ? -1 : a->start > b->start ? 1 : 0;
}
static char *ce_path_key(const char *name) {
    size_t length = xx_str_len(name), used = 0U;
    const char *cursor = name;
    char *key;
    if (length > CE_MAX_RESOLVED_BYTES || length > (SIZE_MAX - 1U) / 4U)
        return NULL;
    key = (char *)xx_mem_alloc(length * 4U + 1U);
    if (!key) return NULL;
    while (*cursor) {
        uint32_t codepoint;
        if (!ce_utf8_next(&cursor, &codepoint)) { xx_mem_free(key); return NULL; }
        if (codepoint == 0xdfU || codepoint == 0x1e9eU) {
            key[used++] = 'S'; key[used++] = 'S';
        } else if (codepoint == 0x130U) {
            key[used++] = 'I';
            used += ce_utf8_put(key + used, 0x307U);
        } else {
            used += ce_utf8_put(key + used, ce_fold(codepoint));
        }
    }
    key[used] = 0;
    return key;
}
static size_t ce_path_hash(const char *key) {
    size_t result = (size_t)UINT32_C(2166136261);
    while (*key) {
        result ^= (uint8_t)*key++;
        result *= (size_t)UINT32_C(16777619);
    }
    return result;
}
static ce_path_slot *ce_map_get(const ce_path_map *map, const char *key) {
    size_t index, mask;
    if (!map->capacity) return NULL;
    mask = map->capacity - 1U;
    index = ce_path_hash(key) & mask;
    for (;;) {
        ce_path_slot *slot = &map->slots[index];
        if (!slot->key) return NULL;
        if (xx_rt_strcmp(slot->key, key) == 0) return slot;
        index = (index + 1U) & mask;
    }
}
static bool ce_map_grow(ce_path_map *map) {
    size_t capacity = map->capacity ? map->capacity * 2U : 32U;
    ce_path_slot *slots;
    size_t i;
    if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(*slots))
        return false;
    slots = (ce_path_slot *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    for (i = 0U; i < map->capacity; ++i) {
        ce_path_slot old = map->slots[i];
        size_t index;
        if (!old.key) continue;
        index = ce_path_hash(old.key) & (capacity - 1U);
        while (slots[index].key) index = (index + 1U) & (capacity - 1U);
        slots[index] = old;
    }
    xx_mem_free(map->slots);
    map->slots = slots;
    map->capacity = capacity;
    return true;
}
static bool ce_map_add(ce_path_map *map, char *key, char *value,
                       bool is_directory) {
    size_t index;
    if (!map || !key ||
        ((map->count + 1U) * 4U >= map->capacity * 3U && !ce_map_grow(map)) ||
        ce_map_get(map, key)) return false;
    index = ce_path_hash(key) & (map->capacity - 1U);
    while (map->slots[index].key)
        index = (index + 1U) & (map->capacity - 1U);
    map->slots[index].key = key;
    map->slots[index].value = value;
    map->slots[index].is_directory = is_directory;
    map->slots[index].next_suffix = 2U;
    ++map->count;
    return true;
}
static void ce_map_free(ce_path_map *map) {
    size_t i;
    for (i = 0U; i < map->capacity; ++i) {
        xx_mem_free(map->slots[i].key);
        xx_mem_free(map->slots[i].value);
    }
    xx_mem_free(map->slots);
    xx_mem_zero(map, sizeof(*map));
}
static char *ce_join(const char *parent, const char *component) {
    return parent && *parent ? xx_str_concat3(parent, "/", component)
                             : xx_str_dup(component);
}
static char *ce_suffix(const char *component, uint32_t number) {
    const char *dot = NULL, *cursor;
    char suffix[24], *result;
    size_t head, tail, suffix_size;
    for (cursor = component; *cursor; ++cursor) if (*cursor == '.') dot = cursor;
    if (dot == component) dot = NULL;
    head = dot ? (size_t)(dot - component) : xx_str_len(component);
    tail = dot ? xx_str_len(dot) : 0U;
    (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u", number);
    suffix_size = xx_str_len(suffix);
    result = (char *)xx_mem_alloc(head + suffix_size + tail + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, component, head);
    xx_rt_memcpy(result + head, suffix, suffix_size);
    if (tail) xx_rt_memcpy(result + head + suffix_size, dot, tail);
    result[head + suffix_size + tail] = 0;
    return result;
}
static char *ce_resolve_path(const char *source, uint32_t max_suffix,
                             ce_path_map *used, ce_path_map *directories) {
    const char *cursor = source;
    char *parent = NULL;
    while (*cursor) {
        const char *end = cursor;
        char *component = NULL, *original = NULL, *original_key = NULL;
        char *chosen = NULL;
        bool leaf, prior_directory = false;
        uint32_t suffix = 1U;
        while (*end && *end != '/') ++end;
        leaf = *end == 0;
        component = (char *)xx_mem_alloc((size_t)(end - cursor) + 1U);
        if (!component) goto fail;
        xx_rt_memcpy(component, cursor, (size_t)(end - cursor));
        component[end - cursor] = 0;
        original = ce_join(parent, component);
        original_key = original ? ce_path_key(original) : NULL;
        if (!original_key) goto fail;
        if (!leaf) {
            ce_path_slot *prior = ce_map_get(directories, original_key);
            if (prior) {
                ce_path_slot *occupied;
                char *key = ce_path_key(prior->value);
                occupied = key ? ce_map_get(used, key) : NULL;
                xx_mem_free(key);
                if (!occupied || !occupied->is_directory) goto fail;
                chosen = xx_str_dup(prior->value);
                if (!chosen) goto fail;
                prior_directory = true;
            }
        }
        while (!chosen && suffix <= max_suffix) {
            char *candidate_component = suffix == 1U ? xx_str_dup(component)
                                                       : ce_suffix(component, suffix);
            char *candidate = candidate_component
                ? ce_join(parent, candidate_component) : NULL;
            char *key = candidate ? ce_path_key(candidate) : NULL;
            ce_path_slot *occupied = key ? ce_map_get(used, key) : NULL;
            xx_mem_free(candidate_component);
            if (!candidate || !key) {
                xx_mem_free(candidate); xx_mem_free(key); goto fail;
            }
            if (ce_name_safe(candidate) &&
                (!occupied || (!leaf && occupied->is_directory))) {
                if (!occupied && !ce_map_add(used, key, NULL, !leaf)) {
                    xx_mem_free(candidate); xx_mem_free(key); goto fail;
                }
                if (occupied) xx_mem_free(key);
                chosen = candidate;
                if (suffix > 1U) {
                    ce_path_slot *original_slot = ce_map_get(used, original_key);
                    if (original_slot && suffix < UINT32_MAX)
                        original_slot->next_suffix = suffix + 1U;
                }
            } else {
                xx_mem_free(candidate); xx_mem_free(key);
                if (suffix == 1U) {
                    ce_path_slot *original_slot = ce_map_get(used, original_key);
                    suffix = original_slot && original_slot->next_suffix > 2U
                        ? original_slot->next_suffix : 2U;
                } else ++suffix;
            }
        }
        if (!chosen) goto fail;
        if (!leaf && !prior_directory) {
            char *mapped = xx_str_dup(chosen);
            if (!mapped || !ce_map_add(directories, original_key, mapped, true)) {
                xx_mem_free(mapped); goto fail;
            }
            original_key = NULL;
        }
        xx_mem_free(parent);
        parent = chosen;
        xx_mem_free(component);
        xx_mem_free(original);
        xx_mem_free(original_key);
        if (leaf) return parent;
        cursor = end + 1;
        continue;
fail:
        xx_mem_free(component);
        xx_mem_free(original);
        xx_mem_free(original_key);
        xx_mem_free(chosen);
        xx_mem_free(parent);
        return NULL;
    }
    xx_mem_free(parent);
    return NULL;
}
static void ce_layout_free(void *pointer) {
    ce_layout *layout = (ce_layout *)pointer;
    size_t i;
    if (!layout) return;
    for (i = 0U; i < layout->count; ++i)
        if (layout->members[i].name) xx_mem_free(layout->members[i].name);
    xx_mem_free(layout->members);
    xx_mem_free(layout);
}
static ce_layout *ce_parse(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[CE_HEADER_SIZE], count_raw[2];
    ce_layout *layout = NULL;
    ce_range *ranges = NULL;
    ce_path_map used_paths = {0}, directories = {0};
    int64_t total, span, cursor, logical_end;
    uint32_t count, i, range_count = 0U;
    bool utf16, ok = false;
    if (!format || !format->device || format->base_address < 0 || ce_stopped(pd) ||
        (format->file_type != XX_FILE_TYPE_CKP && format->file_type != XX_FILE_TYPE_EDP))
        return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    span = total - format->base_address;
    if (span < CE_HEADER_SIZE ||
        !ce_read(format->device, format->base_address, header, sizeof(header), pd))
        return NULL;
    utf16 = format->file_type == XX_FILE_TYPE_EDP;
    if (xx_rt_memcmp(header, utf16 ? ".EDP" : ".CKP", 4U) ||
        header[4] != 0U || header[5] != 1U) return NULL;
    count = xx_data_get_u32(header + 6U, 4, 0, false);
    if (count > CE_MAX_RECORDS ||
        (uint64_t)count > (uint64_t)(span - CE_HEADER_SIZE) /
                          (CE_RECORD_FIXED_SIZE + (utf16 ? 2U : 1U)) ||
        (!count && span != CE_HEADER_SIZE)) return NULL;
    layout = (ce_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) return NULL;
    layout->count = count;
    if (count) {
        layout->members = (ce_member *)xx_mem_calloc(count, sizeof(*layout->members));
        ranges = (ce_range *)xx_mem_alloc((size_t)count * sizeof(*ranges));
        if (!layout->members || !ranges) goto done;
    }
    cursor = CE_HEADER_SIZE;
    for (i = 0U; i < count; ++i) {
        uint16_t units;
        size_t name_bytes, record_bytes;
        uint8_t *record;
        const uint8_t *tail;
        ce_member *member = &layout->members[i];
        if (ce_stopped(pd) || !ce_range_within(span, cursor, 2) ||
            !ce_read(format->device, format->base_address + cursor,
                     count_raw, sizeof(count_raw), pd)) goto done;
        units = xx_data_get_u16(count_raw, 2, 0, false);
        if (!units || units > CE_MAX_NAME_UNITS) goto done;
        name_bytes = (size_t)units * (utf16 ? 2U : 1U);
        record_bytes = CE_RECORD_FIXED_SIZE + name_bytes;
        if (!ce_range_within(span, cursor, (int64_t)record_bytes)) goto done;
        record = (uint8_t *)xx_mem_alloc(record_bytes);
        if (!record) goto done;
        if (!ce_read(format->device, format->base_address + cursor,
                     record, record_bytes, pd)) {
            xx_mem_free(record); goto done;
        }
        tail = record + 2U + name_bytes;
        member->name = ce_decode_name(record + 2U, units, utf16);
        if (member->name) {
            char *unique = ce_resolve_path(member->name, count + 1U,
                                           &used_paths, &directories);
            xx_mem_free(member->name);
            member->name = unique;
        }
        member->header_offset = format->base_address + cursor;
        member->header_size = (int64_t)record_bytes;
        member->data_offset = xx_data_get_u32(tail + 12U, 4, 0, false);
        member->data_size = xx_data_get_u32(tail + 16U, 4, 0, false);
        if (xx_data_get_u32(tail + 8U, 4, 0, false) != 0U) {
            xx_mem_free(record); goto done;
        }
        xx_mem_free(record);
        if (!member->name ||
            !ce_range_within(span, member->data_offset, member->data_size)) goto done;
        if (member->data_size) {
            ranges[range_count].start = member->data_offset;
            ranges[range_count].end = member->data_offset + member->data_size;
            ++range_count;
        }
        cursor += (int64_t)record_bytes;
    }
    logical_end = cursor;
    for (i = 0U; i < count; ++i) {
        if (layout->members[i].data_offset < cursor) goto done;
        if (layout->members[i].data_offset + layout->members[i].data_size > logical_end)
            logical_end = layout->members[i].data_offset + layout->members[i].data_size;
    }
    if (count) {
        xx_rt_qsort(ranges, range_count, sizeof(*ranges), ce_compare_ranges);
        for (i = 1U; i < range_count; ++i) {
            if (ranges[i].start < ranges[i - 1U].end) goto done;
        }
    }
    layout->format_size = logical_end;
    ok = !ce_stopped(pd);
done:
    xx_mem_free(ranges);
    ce_map_free(&used_paths);
    ce_map_free(&directories);
    if (!ok) { ce_layout_free(layout); layout = NULL; }
    return layout;
}
static ce_layout *ce_open(Abstractformat *format, xx_pd_struct *pd) {
    ce_layout *layout;
    int64_t saved;
    if (!format || !format->device) return NULL;
    saved = xx_io_tell(format->device);
    if (saved < 0) return NULL;
    layout = ce_parse(format, pd);
    if (xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) {
        ce_layout_free(layout); return NULL;
    }
    return layout;
}
bool xx_ckpedp_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ce_layout *layout = ce_open(format, pd);
    if (!layout) return false;
    ce_layout_free(layout);
    return true;
}
static bool ce_handle(Abstractformat *format, xx_pd_struct *pd) {
    ce_layout *layout = ce_open(format, pd);
    if (!layout) return false;
    format->format_size = layout->format_size;
    format->number_of_archive_records = layout->count;
    format->overlay_offset = format->base_address + layout->format_size;
    format->overlay_size = xx_io_total_size(format->device) - format->overlay_offset;
    format->is_valid = true;
    format->base_info_handled = true;
    ce_layout_free(layout);
    return true;
}
static int64_t ce_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled || ce_handle(format, pd))
        ? format->format_size : 0;
}
static uint64_t ce_count(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled || ce_handle(format, pd))
        ? format->number_of_archive_records : 0U;
}
static bool ce_set_record(xx_archive_record_state *state) {
    ce_layout *layout = (ce_layout *)state->internal_state;
    ce_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = state->format->base_address + member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
static xx_archive_record_state *ce_create_records(Abstractformat *format,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd) {
    ce_layout *layout = ce_open(format, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { ce_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = ce_layout_free;
    state->total_records = (int64_t)layout->count;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *source = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!source) continue;
        xx_meta_init(&copy, source->meta_id);
        if (!xx_var_copy(&copy.var, &source->var) ||
            !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = layout->count != 0U && ce_set_record(state);
    if (layout->count && !state->has_record) {
        xx_archive_record_state_free(state); return NULL;
    }
    return state;
}
static const xx_archive_record *ce_current(Abstractformat *format,
                                           xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
static bool ce_next(Abstractformat *format, xx_archive_record_state *state,
                    xx_pd_struct *pd) {
    ce_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        ce_stopped(pd)) return false;
    layout = (ce_layout *)state->internal_state;
    if (++layout->index >= layout->count) {
        state->has_record = false; return false;
    }
    ++state->current_index;
    state->has_record = ce_set_record(state);
    return state->has_record;
}
static bool ce_same_path(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *left == *right;
}
static xx_io_device *ce_stage(const char *target, char **stage_name) {
    char *parent = xx_str_dup(target);
    size_t i, cut = 0U;
    unsigned attempt;
    if (!parent) return NULL;
    *stage_name = NULL;
    for (i = 0U; parent[i]; ++i)
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40];
        char *candidate;
        xx_io_device *output;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_ckpedp.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        if (ce_same_path(candidate, target)) {
            xx_str_free(candidate);
            continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_name = candidate;
            xx_str_free(parent);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}
static bool ce_unpack(Abstractformat *format, xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    ce_layout *layout;
    ce_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned = NULL, *target = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    uint8_t *buffer = NULL;
    int64_t saved, at, left;
    bool ok = false, overwrite = false;
    if (!format || !state || state->format != format || !state->has_record ||
        ce_stopped(pd)) return false;
    layout = (ce_layout *)state->internal_state;
    member = &layout->members[layout->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                                XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->data_size > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                                XX_META_ID_OPT_UNPACK_PATH);
    if (option) {
        if (option->type == XX_VAR_TYPE_STRING ||
            option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
        else if (option->type == XX_VAR_TYPE_WSTRING ||
                 option->type == XX_VAR_TYPE_WSTRING_VIEW)
            base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        if (!base) goto done;
        target = *base ? xx_str_concat3(base, "/", member->name)
                       : xx_str_dup(member->name);
        if (!target || !xx_store_create_dirs_a(target, false)) goto done;
        option = xx_format_resolve_extra_parameter(format, &state->options,
                                                    XX_META_ID_OPT_OVERWRITE);
        overwrite = option && xx_var_get_bool(option);
        if ((!overwrite && xx_io_file_exists_a(target)) || ce_stopped(pd)) goto done;
        output = ce_stage(target, &stage);
        if (!output) goto done;
    }
    saved = xx_io_tell(format->device);
    if (saved < 0) goto done;
    buffer = (uint8_t *)xx_mem_alloc(CE_COPY_SIZE);
    if (!buffer) goto restore;
    at = format->base_address + member->data_offset;
    left = member->data_size;
    ok = true;
    while (left > 0 && ok) {
        size_t take = (uint64_t)left > CE_COPY_SIZE ? CE_COPY_SIZE : (size_t)left;
        size_t written = 0U;
        if (!ce_read(format->device, at, buffer, take, pd)) { ok = false; break; }
        while (output && written < take) {
            ssize_t sent = xx_io_write(output, buffer + written, take - written);
            if (sent <= 0 || (size_t)sent > take - written) { ok = false; break; }
            written += (size_t)sent;
        }
        at += (int64_t)take;
        left -= (int64_t)take;
    }
    if (ce_stopped(pd)) ok = false;
restore:
    if (xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) ok = false;
done:
    xx_mem_free(buffer);
    if (output && xx_io_close(output) != 0) ok = false;
    if (ok && stage) ok = xx_io_file_replace_a(stage, target, overwrite);
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    xx_str_free(target);
    xx_str_free(owned);
    return ok;
}
static void ce_free_records(Abstractformat *format,
                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
void xx_ckpedp_destroy(Abstractformat *format) {
    if (format) xx_format_cleanup_extra_parameters(format);
}
void xx_ckpedp_init(Abstractformat *format, xx_io_device *device,
                    int64_t base_address, xx_file_type_t type) {
    if (!format) return;
    xx_format_init(format, device, base_address);
    format->endian = XX_ENDIAN_LITTLE;
    format->file_type = type;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    xx_format_set_extension(format, type == XX_FILE_TYPE_EDP ? "edp" : "ckp");
    xx_format_set_mime_type(format, type == XX_FILE_TYPE_EDP
                           ? "application/x-edgedatapak" : "application/x-ckp");
    format->check_is_valid = xx_ckpedp_check_is_valid;
    format->handle_base_info = ce_handle;
    format->get_format_size = ce_size;
    format->get_number_of_archive_records = ce_count;
    format->create_archive_records_reading = ce_create_records;
    format->get_current_archive_record = ce_current;
    format->archive_record_move_to_next = ce_next;
    format->unpack_current_archive_record = ce_unpack;
    format->free_archive_records_reading = ce_free_records;
    format->destroy = xx_ckpedp_destroy;
}

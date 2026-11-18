/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* The walk follows libyara/modules/dex.c of YARA 3.11, including its
 * quirks: a string value is the `utf16_size` bytes after the first byte of
 * its string_data_item, and a member that cannot be resolved stops the
 * current member list but not the class. Reads are bounds checked here. */

#include <xxfclib/formats/executable/dex/xx_dex_legacy.h>

#include <xxfclib/rt/xx_rt.h>

#include <string.h>

#define XX_DEX_LEGACY_HEADER_SIZE 0x70

typedef struct xx_dex_legacy_state {
    const uint8_t *data;
    size_t size;
    /* string_ids[i]: -1 when YARA 3.11 leaves the entry undefined */
    int64_t *string_offset;
    uint32_t *string_value_size;
    /* Memo of xx_dex_legacy_string_unicode per string: 0 not yet known,
       1 ASCII, 2 has a byte above 0x7F. Member names repeat (<init>, run). */
    uint8_t *string_unicode;
    uint32_t string_count;
    uint32_t field_ids_size, field_ids_offset;
    uint32_t method_ids_size, method_ids_offset;
} xx_dex_legacy_state;

static uint32_t xx_dex_legacy_u32(const xx_dex_legacy_state *dex, size_t offset)
{
    const uint8_t *p = dex->data + offset;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* fits_in_dex(dex, dex->data + offset, size) */
static bool xx_dex_legacy_fits(const xx_dex_legacy_state *dex, uint64_t offset, uint64_t size)
{
    return size <= dex->size && offset <= dex->size - size;
}

static uint8_t xx_dex_legacy_byte(const xx_dex_legacy_state *dex, size_t offset)
{
    return offset < dex->size ? dex->data[offset] : 0;
}

/* read_uleb128() of YARA 3.11: at most five bytes, no range checks. */
static uint32_t xx_dex_legacy_uleb128(const xx_dex_legacy_state *dex, size_t offset, uint32_t *size)
{
    uint32_t result = xx_dex_legacy_byte(dex, offset++);
    ++*size;
    if (result > 0x7f) {
        uint32_t cur = xx_dex_legacy_byte(dex, offset++);
        ++*size;
        result = (result & 0x7f) | ((cur & 0x7f) << 7);
        if (cur > 0x7f) {
            cur = xx_dex_legacy_byte(dex, offset++);
            ++*size;
            result |= (cur & 0x7f) << 14;
            if (cur > 0x7f) {
                cur = xx_dex_legacy_byte(dex, offset++);
                ++*size;
                result |= (cur & 0x7f) << 21;
                if (cur > 0x7f) {
                    cur = xx_dex_legacy_byte(dex, offset++);
                    ++*size;
                    result |= cur << 28;
                }
            }
        }
    }
    return result;
}

/* dex_get_integer() / dex_get_string() treat an index above this as
 * undefined ("a reasonably large limit to table indexes"). */
#define XX_DEX_LEGACY_MAX_INDEX 0x80000u

/* string_ids[index].value has a byte outside 0x00-0x7F. */
static bool xx_dex_legacy_string_unicode(const xx_dex_legacy_state *dex, uint32_t index, bool *defined)
{
    *defined = false;
    if (index > XX_DEX_LEGACY_MAX_INDEX || index >= dex->string_count || dex->string_offset[index] < 0) return false;
    *defined = true;
    if (dex->string_unicode[index]) return dex->string_unicode[index] == 2;
    size_t start = (size_t)dex->string_offset[index] + 1, length = dex->string_value_size[index];
    size_t end = start < dex->size ? (length < dex->size - start ? start + length : dex->size) : start;
    const uint8_t *p = dex->data + start, *stop = dex->data + end;
    bool unicode = false;
    for (; stop - p >= 8 && !unicode; p += 8) {
        uint64_t word;
        memcpy(&word, p, sizeof(word));
        unicode = (word & UINT64_C(0x8080808080808080)) != 0;
    }
    for (; p < stop && !unicode; ++p) unicode = (*p & 0x80) != 0;
    dex->string_unicode[index] = unicode ? 2 : 1;
    return unicode;
}

/* load_encoded_field() / load_encoded_method(): the bytes consumed, 0 when
 * the member cannot be loaded. *name_unicode tells whether the member's
 * name was set and has a non-ASCII byte. */
static uint32_t xx_dex_legacy_member(const xx_dex_legacy_state *dex, size_t start, uint32_t *previous, bool method,
                                     bool *name_unicode)
{
    uint32_t used = 0, diff;
    bool defined;
    *name_unicode = false;
    if (!xx_dex_legacy_fits(dex, start, method ? 12 : 8)) return 0;
    diff = xx_dex_legacy_uleb128(dex, start, &used);
    (void)xx_dex_legacy_uleb128(dex, start + used, &used);              /* access_flags */
    if (method) (void)xx_dex_legacy_uleb128(dex, start + used, &used); /* code_off */
    *previous += diff;
    /* field_ids[idx] / method_ids[idx] exist for idx < size, and the getter
       gives up above XX_DEX_LEGACY_MAX_INDEX. */
    uint32_t count = method ? dex->method_ids_size : dex->field_ids_size;
    if (*previous >= count || *previous > XX_DEX_LEGACY_MAX_INDEX) return 0;
    size_t item = (size_t)(method ? dex->method_ids_offset : dex->field_ids_offset) + (size_t)*previous * 8;
    uint32_t name_idx = xx_dex_legacy_u32(dex, item + 4);
    *name_unicode = xx_dex_legacy_string_unicode(dex, name_idx, &defined) && defined;
    return used;
}

bool xx_dex_legacy_analyze(const uint8_t *data, size_t size, xx_dex_legacy *result)
{
    static const char *const magics[] = {"dex\n035", "dex\n036", "dex\n037", "dex\n038"};
    xx_dex_legacy_state dex;
    bool ok = true;
    memset(result, 0, sizeof(*result));
    if (size < XX_DEX_LEGACY_HEADER_SIZE) return true;
    for (size_t i = 0; i < sizeof(magics) / sizeof(magics[0]) && !result->parsed; ++i)
        result->parsed = memcmp(data, magics[i], 8) == 0; /* includes the NUL */
    if (!result->parsed) return true;

    memset(&dex, 0, sizeof(dex));
    dex.data = data;
    dex.size = size;
    uint32_t string_ids_size = xx_dex_legacy_u32(&dex, 0x38), string_ids_offset = xx_dex_legacy_u32(&dex, 0x3C);
    uint32_t type_ids_size = xx_dex_legacy_u32(&dex, 0x40), type_ids_offset = xx_dex_legacy_u32(&dex, 0x44);
    uint32_t proto_ids_size = xx_dex_legacy_u32(&dex, 0x48), proto_ids_offset = xx_dex_legacy_u32(&dex, 0x4C);
    uint32_t map_offset = xx_dex_legacy_u32(&dex, 0x34);
    uint32_t class_defs_size = xx_dex_legacy_u32(&dex, 0x60), class_defs_offset = xx_dex_legacy_u32(&dex, 0x64);
    dex.field_ids_size = xx_dex_legacy_u32(&dex, 0x50);
    dex.field_ids_offset = xx_dex_legacy_u32(&dex, 0x54);
    dex.method_ids_size = xx_dex_legacy_u32(&dex, 0x58);
    dex.method_ids_offset = xx_dex_legacy_u32(&dex, 0x5C);

    /* String IDs */
    if (!xx_dex_legacy_fits(&dex, string_ids_offset, (uint64_t)string_ids_size * 4)) return true;
    /* Twelve bytes and a flag are kept for every string ID. Where size_t
       cannot count them (a 32-bit build and a table of 2 GiB or more) they
       cannot be allocated either. */
    size_t string_bytes = (size_t)string_ids_size * sizeof(int64_t);
    if (string_bytes / sizeof(int64_t) != string_ids_size) return false;
    dex.string_count = string_ids_size;
    dex.string_offset = (int64_t *)xx_rt_malloc((string_ids_size ? string_ids_size : 1) * sizeof(int64_t));
    dex.string_value_size = (uint32_t *)xx_rt_malloc((string_ids_size ? string_ids_size : 1) * sizeof(uint32_t));
    dex.string_unicode = (uint8_t *)xx_rt_calloc(string_ids_size ? string_ids_size : 1, 1);
    if (!dex.string_offset || !dex.string_value_size || !dex.string_unicode) {
        ok = false;
        goto done;
    }
    int64_t previous_offset = -1;
    for (uint32_t i = 0; i < string_ids_size; ++i) {
        uint32_t offset = xx_dex_legacy_u32(&dex, (size_t)string_ids_offset + (size_t)i * 4), unused = 0, value;
        dex.string_offset[i] = -1;
        dex.string_value_size[i] = 0;
        if (!xx_dex_legacy_fits(&dex, offset, 4)) {
            previous_offset = -1;
            continue;
        }
        value = xx_dex_legacy_uleb128(&dex, offset, &unused);
        if (!xx_dex_legacy_fits(&dex, offset, value)) {
            previous_offset = -1;
            continue;
        }
        dex.string_offset[i] = offset;
        dex.string_value_size[i] = value;
        if (previous_offset >= 0 && (int64_t)offset < previous_offset) result->unsorted_string_pool = true;
        previous_offset = offset;
    }

    /* The remaining ID tables only gate the class data walk. */
    if (!xx_dex_legacy_fits(&dex, type_ids_offset, (uint64_t)type_ids_size * 4) ||
        !xx_dex_legacy_fits(&dex, proto_ids_offset, (uint64_t)proto_ids_size * 12) ||
        !xx_dex_legacy_fits(&dex, dex.field_ids_offset, (uint64_t)dex.field_ids_size * 8) ||
        !xx_dex_legacy_fits(&dex, dex.method_ids_offset, (uint64_t)dex.method_ids_size * 8))
        goto done;
    if (map_offset != 0 && xx_dex_legacy_fits(&dex, map_offset, 4) &&
        !xx_dex_legacy_fits(&dex, map_offset, 4 + (uint64_t)xx_dex_legacy_u32(&dex, map_offset) * 12))
        goto done;
    if (!xx_dex_legacy_fits(&dex, class_defs_offset, (uint64_t)class_defs_size * 32)) goto done;

    /* Class data: dex.field[] and dex.method[] in YARA 3.11's order. */
    uint64_t field_index = 0, method_index = 0;
    for (uint32_t i = 0; i < class_defs_size; ++i) {
        uint32_t class_data = xx_dex_legacy_u32(&dex, (size_t)class_defs_offset + (size_t)i * 32 + 24);
        if (!class_data) continue;
        if (!xx_dex_legacy_fits(&dex, class_data, 16)) break;
        uint32_t used = 0;
        uint32_t lists[4];
        for (int k = 0; k < 4; ++k) lists[k] = xx_dex_legacy_uleb128(&dex, (size_t)class_data + used, &used);
        for (int k = 0; k < 4; ++k) {
            bool method = k >= 2;
            uint32_t previous = 0;
            for (uint32_t j = 0; j < lists[k]; ++j) {
                bool unicode;
                uint32_t consumed = xx_dex_legacy_member(&dex, (size_t)class_data + used, &previous, method, &unicode);
                if (!consumed) break;
                used += consumed;
                if (method) {
                    if (unicode && method_index <= dex.method_ids_size) ++result->unicode_method_names;
                    ++method_index;
                } else {
                    if (unicode && field_index <= dex.field_ids_size) ++result->unicode_field_names;
                    ++field_index;
                }
            }
        }
    }
done:
    xx_rt_free(dex.string_offset);
    xx_rt_free(dex.string_value_size);
    xx_rt_free(dex.string_unicode);
    return ok;
}

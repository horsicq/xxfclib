/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/executable/dex/xx_dex.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

typedef struct {
    char version[4];
    uint32_t hash;
} dex_analysis;

void xx_dex_cleanup_analysis(xx_dex *dex)
{
    dex_analysis *a;
    if (!dex || !(a = (dex_analysis *)dex->analysis)) return;
    xx_mem_free(a);
    dex->analysis = NULL;
}

bool xx_dex_analyze(xx_dex *dex, xx_pd_struct *pd)
{
    dex_analysis *a = NULL;
    int64_t saved;
    uint32_t map, i;
    bool be, ok = false;
    if (!dex || !dex->format.device || dex->format.base_address < 0 || xx_pd_is_stopped(pd)) return false;
    if (dex->analysis) return true;
    saved = xx_io_tell(dex->format.device);
    if (saved < 0) return false;
    if ((!dex->format.base_info_handled && !xx_dex_handle_base_info(&dex->format, pd)) || !xx_dex_validate_tables(dex)) goto done;
    a = (dex_analysis *)xx_mem_calloc(1, sizeof(*a));
    if (!a) goto done;
    for (i = 0; i < 3; ++i) a->version[i] = (char)dex->header.magic[4 + i];
    be = dex->is_big_endian;
    map = dex->header.map_off;
    if (map) {
        uint8_t raw[4];
        uint32_t count;
        uint64_t data_end = (uint64_t)dex->header.data_off + dex->header.data_size;
        int64_t map_abs = dex->format.base_address + (int64_t)map;
        if ((map & 3U) || map < dex->header.data_off || (uint64_t)map + 4 > data_end ||
            !xx_io_read_at(dex->format.device, map_abs, raw, sizeof(raw))) goto done;
        count = xx_data_get_u32(raw, sizeof(raw), 0, be);
        if (!count || count > 65536 || (uint64_t)count * 12 > data_end - map - 4) goto done;
        for (i = 0; i < count && !xx_pd_is_stopped(pd); ++i) {
            uint16_t value;
            if (!xx_io_read_at(dex->format.device, map_abs + 4 + (int64_t)i * 12, raw, 2)) goto done;
            value = xx_data_get_u16(raw, 2, 0, be);
            uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
            a->hash = xx_crc32_calc(a->hash, bytes, 2);
        }
    }
    ok = !xx_pd_is_stopped(pd);
done:
    if (xx_io_seek64(dex->format.device, saved, SEEK_SET) != 0) ok = false;
    if (ok) dex->analysis = a;
    else xx_mem_free(a);
    return ok;
}

const char *xx_dex_get_version(const xx_dex *dex)
{
    return dex && dex->analysis ? ((dex_analysis *)dex->analysis)->version : "";
}

uint32_t xx_dex_get_map_hash(const xx_dex *dex)
{
    return dex && dex->analysis ? ((dex_analysis *)dex->analysis)->hash : 0;
}

char *xx_dex_map_hash_hex(const xx_dex *dex)
{
    xx_buf_t out;
    xx_buf_init(&out);
    xx_buf_appendf(&out, "%08x", (unsigned)xx_dex_get_map_hash(dex));
    return xx_buf_detach(&out, NULL);
}

static int dex_compare_string_at(const xx_dex *dex, uint32_t string_idx, const char *target)
{
    xx_dex_string_id id;
    if (!xx_dex_read_string_id(dex, string_idx, &id)) return 1;

    int64_t str_abs = dex->format.base_address + (int64_t)id.string_data_off;
    int64_t total = xx_io_total_size(dex->format.device);
    uint8_t buf[256];
    size_t to_read = sizeof(buf);
    if (total >= 0) {
        if (str_abs >= total) return 1;
        if ((uint64_t)to_read > (uint64_t)(total - str_abs)) {
            to_read = (size_t)(total - str_abs);
        }
    }
    if (to_read < 2 || !xx_io_read_at(dex->format.device, str_abs, buf, to_read)) return 1;

    /* Skip ULEB128 utf16_size */
    size_t uleb_len = 0;
    while (uleb_len < to_read && uleb_len < 5) {
        if (!(buf[uleb_len++] & 0x80U)) break;
    }
    if (uleb_len >= to_read) return 1;

    const uint8_t *s = buf + uleb_len;
    size_t avail = to_read - uleb_len;
    const uint8_t *t = (const uint8_t *)target;

    for (;;) {
        if (!avail) {
            str_abs += (int64_t)(s - buf);
            to_read = sizeof(buf);
            if (total >= 0) {
                if (str_abs >= total) return 1;
                if ((uint64_t)to_read > (uint64_t)(total - str_abs)) {
                    to_read = (size_t)(total - str_abs);
                }
            }
            if (!to_read || !xx_io_read_at(dex->format.device, str_abs, buf, to_read)) return 1;
            s = buf;
            avail = to_read;
        }
        uint8_t c1 = *s++;
        uint8_t c2 = *t++;
        --avail;
        if (c1 != c2) return (int)c1 - (int)c2;
        if (!c1) return 0;
    }
}

static bool dex_find_string(const xx_dex *dex, const char *value, uint32_t *found_index)
{
    uint32_t count = dex->header.string_ids_size;
    if (!count || !value) return false;
    uint32_t low = 0, high = count - 1;
    while (low <= high) {
        uint32_t mid = low + (high - low) / 2;
        int cmp = dex_compare_string_at(dex, mid, value);
        if (cmp == 0) {
            if (found_index) *found_index = mid;
            return true;
        }
        if (cmp < 0) {
            low = mid + 1;
        } else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }
    /* Fallback for small pools or unsorted fixtures */
    if (count <= 64) {
        for (uint32_t i = 0; i < count; ++i) {
            if (dex_compare_string_at(dex, i, value) == 0) {
                if (found_index) *found_index = i;
                return true;
            }
        }
    }
    return false;
}

bool xx_dex_string_present(const xx_dex *dex, const char *value)
{
    if (!dex || !value || !dex->analysis) return false;
    return dex_find_string(dex, value, NULL);
}

bool xx_dex_item_string_present(const xx_dex *dex, const char *value)
{
    if (!dex || !value || !dex->analysis) return false;
    uint32_t str_idx = 0;
    if (!dex_find_string(dex, value, &str_idx)) return false;

    uint32_t types_count = dex->header.type_ids_size;
    if (!types_count) return false;

    uint32_t low = 0, high = types_count - 1;
    while (low <= high) {
        uint32_t mid = low + (high - low) / 2;
        xx_dex_type_id item;
        if (!xx_dex_read_type_id(dex, mid, &item)) return false;
        if (item.descriptor_idx == str_idx) return true;
        if (str_idx > item.descriptor_idx) low = mid + 1;
        else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }
    /* Fallback for small counts or unsorted type pools */
    if (types_count <= 64) {
        for (uint32_t i = 0; i < types_count; ++i) {
            xx_dex_type_id item;
            if (xx_dex_read_type_id(dex, i, &item) && item.descriptor_idx == str_idx) return true;
        }
    }
    return false;
}

const char *xx_dex_android_version(const xx_dex *dex)
{
    static const char *versions[] = {"035", "037", "038", "039", "040"};
    static const char *android[] = {"4.0.1-4.0.2", "7.0", "8.0", "9.0", "10.0"};
    size_t i;
    const char *version = xx_dex_get_version(dex);
    for (i = 0; i < 5; ++i)
        if (!xx_rt_strcmp(version, versions[i])) return android[i];
    return version;
}

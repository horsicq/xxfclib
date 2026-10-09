/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/dex/xx_dex.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#define DEX_STRING_BUDGET ((size_t)64 * 1024 * 1024)
#define DEX_ENTRY_LIMIT 1048576U
typedef struct { char version[4]; uint32_t hash; xx_list_s strings, types; } dex_analysis;
static int64_t dex_read_at(xx_io_device *view, int64_t offset, void *out, size_t size) {
    size_t done = 0;
    if (xx_io_seek64(view, offset, SEEK_SET) != 0) return -1;
    while (done < size) {
        ssize_t n = xx_io_read(view, (uint8_t *)out + done, size - done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return (int64_t)done;
}
static void dex_string_free(void *p) { xx_str_free(*(char **)p); }
void xx_dex_cleanup_analysis(xx_dex *dex) {
    dex_analysis *a;
    if (!dex || !(a = (dex_analysis *)dex->analysis)) return;
    xx_list_cleanup(&a->strings); xx_list_cleanup(&a->types); xx_mem_free(a); dex->analysis = NULL;
}
bool xx_dex_analyze(xx_dex *dex, xx_pd_struct *pd) {
    dex_analysis *a = NULL; xx_io_device *view = NULL; int64_t saved;
    uint32_t strings, types, map, i; bool be, ok = false;
    size_t budget = DEX_STRING_BUDGET;
    if (!dex || !dex->format.device || dex->format.base_address < 0 || xx_pd_is_stopped(pd)) return false;
    if (dex->analysis) return true;
    saved = xx_io_tell(dex->format.device);
    if (saved < 0) return false;
    if ((!dex->format.base_info_handled && !xx_dex_handle_base_info(&dex->format, pd)) ||
        !xx_dex_validate_tables(dex)) goto done;
    view = xx_io_sub_open_ro(dex->format.device, dex->format.base_address, dex->header.file_size);
    if (!view) goto done;
    a = (dex_analysis *)xx_mem_calloc(1, sizeof(*a)); if (!a) goto done;
    xx_list_init(&a->strings, sizeof(char *), dex_string_free); xx_list_init(&a->types, sizeof(uint32_t), NULL);
    for (i = 0; i < 3; ++i) a->version[i] = (char)dex->header.magic[4 + i];
    be = dex->is_big_endian;
    strings = dex->header.string_ids_size; types = dex->header.type_ids_size;
    map = dex->header.map_off;
    if (map) {
        uint8_t raw[4]; uint32_t count;
        uint64_t data_end = (uint64_t)dex->header.data_off + dex->header.data_size;
        if ((map & 3U) || map < dex->header.data_off || (uint64_t)map + 4 > data_end ||
            dex_read_at(view, map, raw, sizeof(raw)) != sizeof(raw)) goto done;
        count = xx_data_get_u32(raw, sizeof(raw), 0, be);
        if (!count || count > 65536 || (uint64_t)count * 12 > data_end - map - 4) goto done;
        for (i = 0; i < count && !xx_pd_is_stopped(pd); ++i) {
            uint16_t value;
            if (dex_read_at(view, (int64_t)map + 4 + (int64_t)i * 12, raw, 2) != 2) goto done;
            value = xx_data_get_u16(raw, 2, 0, be);
            uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
            a->hash = xx_crc32_calc(a->hash, bytes, 2);
        }
    }
    if (strings > DEX_ENTRY_LIMIT || types > DEX_ENTRY_LIMIT) goto done;
    for (i = 0; i < strings && !xx_pd_is_stopped(pd); ++i) {
        xx_dex_string_info info; char *text; size_t bytes;
        if (!xx_dex_read_string_info(dex, i, budget, &info, pd)) goto done;
        bytes = info.byte_size + 1;
        text = (char *)xx_mem_alloc(bytes); if (!text) goto done;
        if (dex_read_at(view, info.data_off, text, bytes) != (int64_t)bytes ||
            !xx_list_append(&a->strings, &text)) { xx_str_free(text); goto done; }
        budget -= bytes;
    }
    for (i = 0; i < types && !xx_pd_is_stopped(pd); ++i) {
        xx_dex_type_id item; uint32_t index;
        if (!xx_dex_read_type_id(dex, i, &item)) goto done;
        index = item.descriptor_idx;
        if (index >= a->strings.count || !xx_list_append(&a->types, &index)) goto done;
    }
    ok = !xx_pd_is_stopped(pd);
done:
    if (view) xx_io_close(view);
    if (xx_io_seek64(dex->format.device, saved, SEEK_SET) != 0) ok = false;
    if (ok) dex->analysis = a;
    else if (a) { xx_list_cleanup(&a->strings); xx_list_cleanup(&a->types); xx_mem_free(a); }
    return ok;
}
const char *xx_dex_get_version(const xx_dex *dex) { return dex && dex->analysis ? ((dex_analysis *)dex->analysis)->version : ""; }
uint32_t xx_dex_get_map_hash(const xx_dex *dex) { return dex && dex->analysis ? ((dex_analysis *)dex->analysis)->hash : 0; }
char *xx_dex_map_hash_hex(const xx_dex *dex) { xx_buf_t out; xx_buf_init(&out); xx_buf_appendf(&out, "%08x", (unsigned)xx_dex_get_map_hash(dex)); return xx_buf_detach(&out, NULL); }
bool xx_dex_string_present(const xx_dex *dex, const char *value) {
    const dex_analysis *a = dex ? (dex_analysis *)dex->analysis : NULL; size_t i;
    for (i = 0; a && value && i < a->strings.count; ++i) if (!xx_rt_strcmp(*(char **)xx_list_at(&a->strings, i), value)) return true;
    return false;
}
bool xx_dex_item_string_present(const xx_dex *dex, const char *value) {
    const dex_analysis *a = dex ? (dex_analysis *)dex->analysis : NULL; size_t i;
    for (i = 0; a && value && i < a->types.count; ++i) {
        uint32_t index = *(uint32_t *)xx_list_at(&a->types, i);
        if (!xx_rt_strcmp(*(char **)xx_list_at(&a->strings, index), value)) return true;
    }
    return false;
}
const char *xx_dex_android_version(const xx_dex *dex) {
    static const char *versions[] = { "035", "037", "038", "039", "040" };
    static const char *android[] = { "4.0.1-4.0.2", "7.0", "8.0", "9.0", "10.0" }; size_t i;
    const char *version = xx_dex_get_version(dex);
    for (i = 0; i < 5; ++i) if (!xx_rt_strcmp(version, versions[i])) return android[i];
    return version;
}

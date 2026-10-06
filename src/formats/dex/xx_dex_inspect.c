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
static char *dex_string(xx_io_device *view, uint32_t offset, size_t budget, xx_pd_struct *pd) {
    int64_t size = xx_io_total_size(view), position = offset; uint32_t units = 0;
    unsigned i; uint8_t byte; xx_buf_t out; size_t maximum;
    if (!offset || position >= size) return xx_str_create("");
    for (i = 0; i < 5; ++i) {
        if (dex_read_at(view, position++, &byte, 1) != 1 || (i == 4 && (byte & 0xF0))) return xx_str_create("");
        units |= (uint32_t)(byte & 0x7F) << (i * 7);
        if (!(byte & 0x80)) break;
    }
    maximum = units > (budget - 1) / 3 ? budget : (size_t)units * 3 + 1;
    if ((uint64_t)maximum > (uint64_t)(size - position)) maximum = (size_t)(size - position);
    xx_buf_init(&out);
    while (maximum && !xx_pd_is_stopped(pd)) {
        uint8_t chunk[512]; size_t want = maximum < sizeof(chunk) ? maximum : sizeof(chunk), k;
        if (dex_read_at(view, position, chunk, want) != (int64_t)want) break;
        for (k = 0; k < want && chunk[k]; ++k) {}
        if (!xx_buf_append(&out, chunk, k)) break;
        if (k < want) return xx_buf_detach(&out, NULL);
        maximum -= want; position += (int64_t)want;
    }
    /* Failed strings end analysis: charging an empty result would let many
     * duplicate offsets rescan a large unterminated string indefinitely. */
    xx_buf_free(&out); return NULL;
}
bool xx_dex_analyze(xx_dex *dex, xx_pd_struct *pd) {
    dex_analysis *a = NULL; xx_io_device *view = NULL; int64_t total, saved;
    uint8_t header[112]; uint32_t strings, string_offset, types, type_offset, map, i; bool be, ok = false;
    size_t budget = DEX_STRING_BUDGET;
    if (!dex || !dex->format.device || dex->format.base_address < 0 || xx_pd_is_stopped(pd)) return false;
    if (dex->analysis) return true;
    total = xx_io_total_size(dex->format.device); saved = xx_io_tell(dex->format.device);
    if (total < dex->format.base_address || total - dex->format.base_address < 112) return false;
    view = xx_io_sub_open_ro(dex->format.device, dex->format.base_address, total - dex->format.base_address);
    if (!view || dex_read_at(view, 0, header, sizeof(header)) != sizeof(header) || xx_rt_memcmp(header, "dex\n", 4)) goto done;
    a = (dex_analysis *)xx_mem_calloc(1, sizeof(*a)); if (!a) goto done;
    xx_list_init(&a->strings, sizeof(char *), dex_string_free); xx_list_init(&a->types, sizeof(uint32_t), NULL);
    for (i = 0; i < 3; ++i) a->version[i] = header[4 + i] ? (char)header[4 + i] : '0';
    be = xx_io_get_u32(view, 0x28, false) == 0x78563412U;
    strings = xx_io_get_u32(view, 0x38, be); string_offset = xx_io_get_u32(view, 0x3C, be);
    types = xx_io_get_u32(view, 0x40, be); type_offset = xx_io_get_u32(view, 0x44, be);
    map = xx_io_get_u32(view, 0x34, be); total = xx_io_total_size(view);
    if (map && map <= total - 4) {
        uint32_t count = xx_io_get_u32(view, map, be);
        if (count > (uint64_t)(total - map - 4) / 12) count = (uint32_t)((total - map - 4) / 12);
        if (count > 65536) count = 65536;
        for (i = 0; i < count && !xx_pd_is_stopped(pd); ++i) {
            uint16_t value = xx_io_get_u16(view, (int64_t)map + 4 + (int64_t)i * 12, be);
            uint8_t bytes[2] = { (uint8_t)value, (uint8_t)(value >> 8) };
            a->hash = xx_crc32_calc(a->hash, bytes, 2);
        }
    }
    if (strings > DEX_ENTRY_LIMIT || types > DEX_ENTRY_LIMIT) goto done;
    for (i = 0; i < strings && !xx_pd_is_stopped(pd); ++i) {
        int64_t at = (int64_t)string_offset + (int64_t)i * 4; char *text; size_t bytes;
        if (at > total - 4) break;
        if (budget < 2) goto done;
        text = dex_string(view, xx_io_get_u32(view, at, be), budget, pd); if (!text) goto done;
        bytes = xx_rt_strlen(text) + 1;
        if (bytes > budget || !xx_list_append(&a->strings, &text)) { xx_str_free(text); goto done; }
        budget -= bytes;
    }
    for (i = 0; i < types && !xx_pd_is_stopped(pd); ++i) {
        int64_t at = (int64_t)type_offset + (int64_t)i * 4; uint32_t index;
        if (at > total - 4) break;
        index = xx_io_get_u32(view, at, be);
        if (index < a->strings.count && !xx_list_append(&a->types, &index)) goto done;
    }
    ok = !xx_pd_is_stopped(pd);
done:
    if (view) xx_io_close(view);
    if (saved >= 0) (void)xx_io_seek64(dex->format.device, saved, SEEK_SET);
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

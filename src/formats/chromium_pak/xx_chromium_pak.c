/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Format independently implemented from Chromium's tools/grit/grit/format/data_pack.py.
 */
#include "xxfclib/formats/chromium_pak/xx_chromium_pak.h"
#include "../ue2_indexed.h"
#ifdef CHROMIUM_PAK
#define UE2_CHROMIUM_TYPE XX_FILE_TYPE_CHROMIUM_PAK
#else
#define UE2_CHROMIUM_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static ue2_index *chromium_parse(Abstractformat *f, uint32_t *version_out, uint32_t *encoding_out, xx_pd_struct *pd) {
    uint8_t h[12], *table = NULL, used[8192];
    uint32_t version, encoding, count, aliases, header, first, end, i, previous_id = 0;
    uint64_t directory_size;
    ue2_index *index = NULL;
    int64_t total = f && f->device ? xx_io_total_size(f->device) : -1;
    if (!f || f->base_address < 0 || !ue2_range(total, f->base_address, 9) ||
        !ue2_read(f, f->base_address, h, 9)) return NULL;
    version = ue2_u32(h);
    if (version == 4) { count = ue2_u32(h + 4); encoding = h[8]; aliases = 0; header = 9; }
    else if (version == 5 && ue2_read(f, f->base_address, h, 12)) {
        encoding = h[4]; count = ue2_u16(h + 8); aliases = ue2_u16(h + 10); header = 12;
        if (h[5] || h[6] || h[7]) return NULL;
    } else return NULL;
    if (encoding > 2 || count > 65535U || count + aliases > 65535U) return NULL;
    directory_size = ((uint64_t)count + 1) * 6 + (uint64_t)aliases * 4;
    if (!ue2_range(total, f->base_address + header, (int64_t)directory_size)) return NULL;
    table = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    index = (ue2_index *)xx_mem_calloc(1, sizeof(*index));
    if (!table || !index || !ue2_read(f, f->base_address + header, table, (size_t)directory_size)) goto fail;
    xx_mem_zero(used, sizeof(used));
    first = ue2_u32(table + 2); end = ue2_u32(table + (size_t)count * 6 + 2);
    if (first != header + directory_size || ue2_u16(table + (size_t)count * 6) != 0 ||
        end < first || !ue2_range(total, f->base_address, end)) goto fail;
    for (i = 0; i < count + aliases; ++i) {
        uint32_t id, offset, next;
        char name[48];
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (i < count) {
            id = ue2_u16(table + (size_t)i * 6); offset = ue2_u32(table + (size_t)i * 6 + 2);
            next = ue2_u32(table + ((size_t)i + 1) * 6 + 2);
            if (!id || (i && id <= previous_id)) goto fail;
            previous_id = id;
        } else {
            const uint8_t *a = table + ((size_t)count + 1) * 6 + (size_t)(i - count) * 4;
            uint32_t slot = ue2_u16(a + 2);
            id = ue2_u16(a);
            if (!id || slot >= count) goto fail;
            offset = ue2_u32(table + (size_t)slot * 6 + 2);
            next = ue2_u32(table + ((size_t)slot + 1) * 6 + 2);
        }
        if ((used[id >> 3] & (1U << (id & 7))) || offset < first || next < offset || next > end) goto fail;
        used[id >> 3] |= (uint8_t)(1U << (id & 7));
        xx_rt_snprintf(name, sizeof(name), "%u.%s", id, encoding ? "txt" : "bin");
        if (!ue2_add(index, name, f->base_address + offset, (int64_t)next - offset, id)) goto fail;
    }
    index->size = end;
    if (version_out) *version_out = version;
    if (encoding_out) *encoding_out = encoding;
    xx_mem_free(table); return index;
fail:
    xx_mem_free(table); ue2_index_free(index); return NULL;
}
static bool chromium_valid(Abstractformat *f, xx_pd_struct *pd) {
    ue2_index *index = chromium_parse(f, NULL, NULL, pd);
    bool valid = index != NULL; ue2_index_free(index); return valid;
}
static bool chromium_info(Abstractformat *f, xx_pd_struct *pd) {
    xx_chromium_pak *a = (xx_chromium_pak *)f;
    return ue2_accept(f, chromium_parse(f, &a->version, &a->encoding, pd));
}
void xx_chromium_pak_init(xx_chromium_pak *a, xx_io_device *device, int64_t base) {
    if (!a) { return; } xx_mem_zero(a, sizeof(*a));
    ue2_init_format(&a->format, device, base, UE2_CHROMIUM_TYPE, "pak", "application/x-chromium-pak");
    a->format.check_is_valid = chromium_valid; a->format.handle_base_info = chromium_info;
}
xx_chromium_pak *xx_chromium_pak_create(xx_io_device *device, int64_t base) {
    xx_chromium_pak *a = (xx_chromium_pak *)xx_mem_alloc(sizeof(*a));
    if (a) { xx_chromium_pak_init(a, device, base); } return a;
}
void xx_chromium_pak_destroy(xx_chromium_pak *a) { if (a) ue2_destroy_format(&a->format); }
void xx_chromium_pak_free(xx_chromium_pak *a) { if (a) { xx_chromium_pak_destroy(a); xx_mem_free(a); } }

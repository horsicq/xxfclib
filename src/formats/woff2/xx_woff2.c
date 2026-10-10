/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.w3.org/TR/WOFF2/
 * WOFF 2.0 single fonts with null-transformed tables, Brotli metadata/private data. Transformed glyf/hmtx and font collections rejected; no rendering.
 */
#include "xxfclib/formats/woff2/xx_woff2.h"
#include "../xx_payload_members.h"
#include "../sfnt/xx_font_table_impl.h"

static bool base128(Abstractformat *f, int64_t *at, uint32_t *out)
{
    uint32_t n = 0;
    unsigned i;
    uint8_t b;
    for (i = 0; i < 5; ++i) {
        if (!pm_read(f, (*at)++, &b, 1) || (!i && b == 0x80) || (n & 0xfe000000U)) return false;
        n = (n << 7) | (b & 127);
        if (!(b & 128)) {
            *out = n;
            return true;
        }
    }
    return false;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const char tags[][5] = {"cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT",
                                   "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH",
                                   "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar", "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar",
                                   "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop", "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill"};
    uint8_t h[48], b, tag[4], *input = NULL, *decoded = NULL;
    uint32_t count, total, packed, i, j, size, lengths[4096], tagids[4096];
    uint64_t expanded = 0, sfntsize;
    int64_t at = 48, end;
    size_t written = 0, position = 0;
    bool ok = false;
    const xx_var *budget = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (!pm_read(f, 0, h, 48) || xx_rt_memcmp(h, "wOF2", 4) || !font_flavor(xx_data_get_u32(h + 4, 4, 0, true)) || xx_data_get_u16(h + 14, 2, 0, true)) return false;
    total = xx_data_get_u32(h + 8, 4, 0, true);
    count = xx_data_get_u16(h + 12, 2, 0, true);
    packed = xx_data_get_u32(h + 20, 4, 0, true);
    if (!count || count > 4096 || !packed || total > pm_available(f) || total < 48 || packed > 64U * 1024U * 1024U) return false;
    sfntsize = 12 + (uint64_t)count * 16;
    for (i = 0; i < count; ++i) {
        unsigned index, transform;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at++, &b, 1)) return false;
        index = b & 63;
        transform = b >> 6;
        if (index == 63) {
            if (!pm_read(f, at, tag, 4) || !font_tag(tag)) return false;
            at += 4;
        } else xx_rt_memcpy(tag, tags[index], 4);
        tagids[i] = xx_data_get_u32(tag, 4, 0, true);
        for (j = 0; j < i; ++j)
            if (tagids[j] == tagids[i]) return false;
        /* Null transform differs for glyf/loca; transformed font tables need reconstruction. */
        if (transform != ((tagids[i] == 0x676c7966 || tagids[i] == 0x6c6f6361) ? 3U : 0U) || !base128(f, &at, &size)) return false;
        lengths[i] = size;
        expanded += size;
        sfntsize += ((uint64_t)size + 3) & ~UINT64_C(3);
    }
    if (sfntsize != xx_data_get_u32(h + 16, 4, 0, true) || expanded * 2 + packed > 64U * 1024U * 1024U || at > total || packed > (uint64_t)(total - at) ||
        (budget && expanded * 2 + packed > xx_var_get_u64(budget)))
        return false;
    input = (uint8_t *)xx_mem_alloc(packed);
    decoded = (uint8_t *)xx_mem_alloc(expanded ? (size_t)expanded : 1);
    if (!input || !decoded || !pm_read(f, at, input, packed) || !xx_brotli_decompress_memory(input, packed, decoded, (size_t)expanded, &written) || written != expanded)
        goto done;
    end = at + packed;
    for (i = 0; i < count; ++i) {
        pm_member *m;
        char name[64];
        uint32_t t = tagids[i];
        xx_rt_snprintf(name, sizeof(name), "table-%08x.bin", t);
        if ((pd && xx_pd_is_stopped(pd)) || !pm_add(f, s, name, at, 0)) goto done;
        m = &s->items[s->count - 1];
        m->memory = (uint8_t *)xx_mem_alloc(lengths[i] ? lengths[i] : 1);
        if (!m->memory) goto done;
        xx_rt_memcpy(m->memory, decoded + position, lengths[i]);
        position += lengths[i];
        m->size = lengths[i];
        m->packed_size = 0;
    }
    xx_mem_free(input);
    input = NULL;
    xx_mem_free(decoded);
    decoded = NULL;
    for (i = 0; i < 2; ++i) {
        uint32_t off = xx_data_get_u32(h + (i ? 40 : 28), 4, 0, true), len = xx_data_get_u32(h + (i ? 44 : 32), 4, 0, true),
                 orig = i ? len : xx_data_get_u32(h + 36, 4, 0, true);
        if (!off && !len && !orig) continue;
        if (!off || !len || !orig || off % 4 || off < end || off - end > 3 || off > total || len > total - off ||
            !font_range(f, s, off, len, orig, i ? "private.bin" : "metadata.xml", i ? 0 : 2, 0, false, pd))
            goto done;
        while (end < off) {
            uint8_t pad;
            if (!pm_read(f, end++, &pad, 1) || pad) goto done;
        }
        end = (int64_t)off + len;
    }
    if (end > total || total - end > 3) goto done;
    while (end < total) {
        uint8_t pad;
        if (!pm_read(f, end++, &pad, 1) || pad) goto done;
    }
    s->size = total;
    ok = true;
done:
    if (input) xx_mem_free(input);
    if (decoded) xx_mem_free(decoded);
    return ok;
}

void xx_woff2_init(xx_woff2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_WOFF2, "woff2");
    }
}
xx_woff2 *xx_woff2_create(xx_io_device *d, int64_t b)
{
    xx_woff2 *r = (xx_woff2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_woff2_init(r, d, b);
    return r;
}
void xx_woff2_destroy(xx_woff2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_woff2_free(xx_woff2 *r)
{
    if (r) {
        xx_woff2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_woff2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_woff2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

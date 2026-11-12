/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Demonstar GLB 2.0 (Mountain King Studios): the Raptor GLB file table in
 * its plain form behind a magic. Layout verified on Game.glb files:
 *   char magic[8] = "GLB2.0\0\0"; u32 count;
 *   count x { u32 flags; u32 offset; u32 size; char name[16] }   (28 bytes)
 * An all-zero entry terminates the table (it is counted); offsets are
 * absolute, ascending and start at the end of the table. Only flags 0,
 * stored, occurs in Demonstar; the encrypted tables of Raptor (flags 1,
 * key "32768GLB") are refused rather than guessed. */
#include "xxfclib/formats/game/demonstar_glb/xx_demonstar_glb.h"
#include "../../xx_payload_members.h"
#define DG_MAX_COUNT 65536U
#define DG_TOTAL INT64_C(2147483648)
static uint32_t dg_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* 1..16 printable bytes without path or wildcard characters, zero padded. */
static bool dg_name(const uint8_t *p, char name[17])
{
    size_t n = 0, i;
    while (n < 16U && p[n]) ++n;
    if (!n) return false;
    for (i = n; i < 16U; ++i)
        if (p[i]) return false;
    for (i = 0; i < n; ++i) {
        unsigned c = p[i];
        if (c < 0x21U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        name[i] = (char)c;
    }
    name[n] = 0;
    return true;
}
static bool dg_zero(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[28];
    int64_t total = pm_available(f);
    uint32_t count, i;
    uint64_t previous_end;
    char section[17];
    if (total < 12 + 28 || total > DG_TOTAL || !pm_read(f, 0, h, 12) || xx_rt_memcmp(h, "GLB2.0\0\0", 8U)) return false;
    count = dg_u32(h + 8);
    if (!count || count > DG_MAX_COUNT || (uint64_t)count * 28U > (uint64_t)(total - 12)) return false;
    previous_end = 12U + (uint64_t)count * 28U;
    section[0] = 0;
    for (i = 0; i < count; ++i) {
        char name[17], label[40];
        uint32_t flags, offset, size;
        pm_member *m;
        size_t n;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 12 + (int64_t)i * 28, h, sizeof(h))) return false;
        if (dg_zero(h, sizeof(h))) {
            /* The terminator: nothing may follow it in the table. */
            if (i + 1U != count) return false;
            break;
        }
        flags = dg_u32(h);
        offset = dg_u32(h + 4);
        size = dg_u32(h + 8);
        if (!size) {
            /* Section markers: zero-size entries named START<section>: and
             * END<section>:, with the current position or zero as offset.
             * Members between them go into a folder of that name. */
            for (n = 0; n < 16U && h[12 + n]; ++n)
                if (h[12 + n] < 0x21U || h[12 + n] > 0x7eU) return false;
            if (n > 6U && h[12 + n - 1U] == ':' && !xx_rt_memcmp(h + 12, "START", 5U)) {
                xx_rt_memcpy(section, h + 17, n - 6U);
                section[n - 6U] = 0;
            } else if (n > 4U && h[12 + n - 1U] == ':' && !xx_rt_memcmp(h + 12, "END", 3U)) {
                section[0] = 0;
            }
            continue;
        }
        if (flags || !dg_name(h + 12, name) || offset < previous_end || size > (uint64_t)total - offset) return false;
        previous_end = (uint64_t)offset + size;
        if (section[0]) xx_rt_snprintf(label, sizeof(label), "%s/%s", section, name);
        else xx_rt_memcpy(label, name, xx_rt_strlen(name) + 1U);
        if (!pm_add(f, s, label, offset, size)) return false;
        m = &s->items[s->count - 1U];
        xx_rt_memcpy(m->name, label, xx_rt_strlen(label) + 1U);
    }
    if (!s->count) return false;
    /* Names repeat inside a section (palettes, shared sprites): keep every
     * member as its own output file. */
    if (s->count <= 8192U) {
        size_t j, k;
        for (j = 1; j < s->count; ++j) {
            for (k = 0; k < j; ++k)
                if (!xx_rt_strcmp(s->items[j].name, s->items[k].name)) break;
            if (k < j) {
                size_t len = xx_rt_strlen(s->items[j].name);
                xx_rt_snprintf(s->items[j].name + len, sizeof(s->items[j].name) - len, "~%u", (unsigned)j);
            }
        }
    }
    s->size = total;
    return !(pd && xx_pd_is_stopped(pd));
}
Abstractformat *xx_demonstar_glb_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_DEMONSTAR_GLB, "glb");
    return f;
}
void xx_demonstar_glb_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

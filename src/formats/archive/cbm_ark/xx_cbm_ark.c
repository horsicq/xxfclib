/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Commodore 64 ARK archives (ARKive): a stored directory of PETSCII names
 * followed by the files in 254-byte blocks. Layout, from the archives:
 *   u8  count                       directory entries
 *   count x 29 bytes:
 *     u8   type      CBM file type: 0x80 DEL, 0x81 SEQ, 0x82 PRG, 0x83 USR,
 *                    0x84 REL, bit 6 = locked
 *     u8   last      index of the last byte used in the final block, as a
 *                    CBM sector stores it (bytes = last - 1)
 *     char name[16]  PETSCII, padded with 0xA0
 *     u8   rel[9]    REL side-sector data, zero otherwise
 *     u16  blocks    254-byte blocks of the file
 *   padding to the next 254-byte boundary, then every file's blocks in
 *   directory order; the archive may be padded to a 256-byte sector.
 * The format has no magic: validity is the block arithmetic of the whole
 * directory against the file size. */
#include "xxfclib/formats/archive/cbm_ark/xx_cbm_ark.h"
#include "../../xx_payload_members.h"
#define AK_BLOCK 254U
#define AK_ENTRY 29U
#define AK_TOTAL INT64_C(16777216)
static const char *ak_extension(unsigned type)
{
    switch (type & 7U) {
        case 1U: return ".seq";
        case 2U: return ".prg";
        case 3U: return ".usr";
        case 4U: return ".rel";
        default: return ".del";
    }
}
/* PETSCII directory name to a file name: letters as they are, shifted
 * letters to upper case, graphics and controls to '_'. */
static bool ak_name(const uint8_t *p, unsigned type, char name[32])
{
    size_t n = 0, i;
    const char *ext;
    while (n < 16U && p[n] != 0xa0U) ++n;
    if (!n) return false;
    for (i = n; i < 16U; ++i)
        if (p[i] != 0xa0U) return false;
    for (i = 0; i < n; ++i) {
        unsigned c = p[i];
        if (c >= 0xc1U && c <= 0xdaU) c -= 0x80U;
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        name[i] = (char)c;
    }
    ext = ak_extension(type);
    xx_rt_memcpy(name + n, ext, xx_rt_strlen(ext) + 1U);
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t e[AK_ENTRY];
    int64_t total = pm_available(f), data;
    unsigned count, i;
    uint64_t used = 0;
    if (total < 1 + AK_ENTRY || total > AK_TOTAL || !pm_read(f, 0, e, 1)) return false;
    count = e[0];
    if (!count || 1 + (int64_t)count * AK_ENTRY > total) return false;
    data = (1 + (int64_t)count * AK_ENTRY + AK_BLOCK - 1) / AK_BLOCK * AK_BLOCK;
    if (data > total) return false;
    for (i = 0; i < count; ++i) {
        char name[32];
        unsigned type, last, blocks;
        int64_t size;
        pm_member *m;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 1 + (int64_t)i * AK_ENTRY, e, sizeof(e))) return false;
        type = e[0];
        last = e[1];
        blocks = e[27] | ((unsigned)e[28] << 8);
        if ((type & 0x80U) == 0U || (type & 7U) > 4U || (type & 0x38U) || !last || !blocks || !ak_name(e + 2, type, name)) return false;
        size = (int64_t)(blocks - 1U) * AK_BLOCK + (int64_t)(last - 1U);
        if (used + (uint64_t)blocks * AK_BLOCK > (uint64_t)(total - data)) return false;
        if (!pm_add(f, s, name, data + (int64_t)used, size)) return false;
        m = &s->items[s->count - 1U];
        xx_rt_memcpy(m->name, name, xx_rt_strlen(name) + 1U);
        used += (uint64_t)blocks * AK_BLOCK;
    }
    /* Nothing but sector padding may follow the last block. */
    if ((uint64_t)(total - data) - used >= 256U) return false;
    s->size = total;
    return !(pd && xx_pd_is_stopped(pd));
}
Abstractformat *xx_cbm_ark_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_CBM_ARK, "ark");
    return f;
}
void xx_cbm_ark_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

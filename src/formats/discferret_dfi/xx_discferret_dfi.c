/* SPDX-License-Identifier: MIT. Original DiscFerret sample-block framing.
 * Facts: official HxC dfi_format.h embedded format-originator description.
 * The raw delta/index encoding is retained, including legal terminal carry.
 */
#include "xxfclib/formats/discferret_dfi/xx_discferret_dfi.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    hx_blob b;
    uint64_t at = 4;
    uint32_t count = 0;
    bool v2, ok = false;
    char name[96], info[256];
    if (!hx_load(f, &b, pd)) {
        return false;
    }
    v2 = hx_tag(&b, 0, "DFE2", 4);
    HX_NEED(v2 || hx_tag(&b, 0, "DFER", 4));
    HX_NEED(hx_emit(f, s, &b, "descriptor.dfi", 0, 4));
    while (at < b.n) {
        uint32_t cyl, head, sector, z, k;
        uint64_t carry = 0;
        HX_NEED(count < 1024 && hx_span(&b, at, 10));
        cyl = xx_data_get_u16(b.p + at, 2, 0, true);
        head = xx_data_get_u16(b.p + at + 2, 2, 0, true);
        sector = xx_data_get_u16(b.p + at + 4, 2, 0, true);
        z = xx_data_get_u32(b.p + at + 6, 4, 0, true);
        HX_NEED(cyl <= 255 && head <= 15 && z <= 8U * 1024U * 1024U && hx_span(&b, at + 10, z) && hx_work(&b, z));
        for (k = 0; k < z; ++k) {
            uint8_t c = b.p[at + 10 + k], low = c & 127U;
            if (!(k & 4095U)) HX_NEED(hx_poll(&b));
            if (v2) {
                if (low == 127U) carry += 127U;
                else if (c & 128U) carry += low;
                else carry = 0;
            } else {
                if (!low) carry += 127U;
                else carry = 0;
            }
            HX_NEED(carry <= UINT32_MAX);
        }
        xx_rt_snprintf(name, sizeof(name), "capture-%04u-C%03u-H%u-S%u.descriptor.dfi", count, cyl, head, sector);
        HX_NEED(hx_emit(f, s, &b, name, at, 10));
        xx_rt_snprintf(name, sizeof(name), "capture-%04u-C%03u-H%u-S%u.%s-flux", count, cyl, head, sector, v2 ? "dfe2" : "dfer");
        HX_NEED(hx_emit(f, s, &b, name, at + 10, z));
        ++count;
        at += 10U + z;
    }
    HX_NEED(count);
    xx_rt_snprintf(info, sizeof(info),
                   "Format: DiscFerret %s\nCaptures: %u\nRepresentation: original transition/index delta bytes with per-capture cylinder/head/sector "
                   "descriptors\nIntegrity: sample-block extents; no container checksum\n",
                   v2 ? "DFE2" : "DFER", count);
    HX_NEED(hx_text(f, s, &b, info));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
HX_API(discferret_dfi, XX_FILE_TYPE_DISCFERRET_DFI, "dfi")

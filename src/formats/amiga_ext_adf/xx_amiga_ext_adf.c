/* SPDX-License-Identifier: MIT. UAE extended ADF framing, independently written.
 * Layout: WinUAE disk.cpp ADF_EXT2 header documentation and HxC EXTADF loader.
 * Raw MFM/revolution bytes are preserved; no sector recovery is claimed.
 */
#include "xxfclib/formats/amiga_ext_adf/xx_amiga_ext_adf.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    hx_blob b;
    uint32_t tracks, i;
    uint64_t at;
    bool ok = false;
    char name[96], info[256];
    if (!hx_load(f, &b, pd)) return false;
    HX_NEED(hx_tag(&b, 0, "UAE-1ADF", 8) && hx_span(&b, 0, 12));
    tracks = xx_data_get_u16(b.p + 10, 2, 0, true);
    HX_NEED(tracks && tracks <= 340 && xx_data_get_u16(b.p + 8, 2, 0, true) == 0 && hx_span(&b, 12, (uint64_t)tracks * 12));
    at = 12 + (uint64_t)tracks * 12;
    HX_NEED(hx_emit(f, s, &b, "descriptor.ext-adf", 0, at));
    for (i = 0; i < tracks; ++i) {
        const uint8_t *q = b.p + 12 + i * 12;
        uint32_t z = xx_data_get_u32(q + 4, 4, 0, true), bits = xx_data_get_u32(q + 8, 4, 0, true), type = q[3];
        HX_NEED(hx_poll(&b) && xx_data_get_u16(q, 2, 0, true) == 0 && type <= 1U && z <= 1048576U && !(z & 1U) && hx_span(&b, at, z));
        HX_NEED(bits <= (uint64_t)z * 8U);
        if (type == 0) HX_NEED(q[2] == 0 && !(bits & 4095U) && bits / 8U <= z);
        if (z) {
            xx_rt_snprintf(name, sizeof(name), "track-C%03u-H%u.%s", i / 2U, i % 2U, type ? (bits ? "mfm-bitcells" : "unformatted-allocation") : "sectors");
            HX_NEED(hx_emit(f, s, &b, name, at, z));
        }
        at += z;
    }
    HX_NEED(at == b.n);
    xx_rt_snprintf(info, sizeof(info),
                   "Format: UAE extended ADF\nTrack sides: %u\nRepresentation: original allocated sector or MFM track bytes; descriptor retains bit lengths and "
                   "revolution counts\nIntegrity: container lengths; no track checksum\n",
                   tracks);
    HX_NEED(hx_text(f, s, &b, info));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
HX_API(amiga_ext_adf, XX_FILE_TYPE_AMIGA_EXT_ADF, "adf")

/* SPDX-License-Identifier: MIT. Original SAB Diskette Utility geometry parser.
 * Layout facts: HxC sdu_format.h/sdu_loader.c. Sector image follows 46-byte header.
 */
#include "xxfclib/formats/sdu/xx_sdu.h"
#include "../hxc_afi/xx_hxc_tracks.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    hx_blob b;
    uint32_t tracks, sides, sectors, size, i;
    uint64_t n;
    bool ok = false;
    char info[256];
    if (!hx_load(f, &b, pd)) return false;
    HX_NEED(hx_tag(&b, 0, "SAB Diskette Utility\0", 21) && hx_span(&b, 0, 46));
    HX_NEED(b.p[21] >= '0' && b.p[21] <= '9' && b.p[22] == '.' && b.p[23] >= '0' && b.p[23] <= '9' && b.p[24] >= '0' && b.p[24] <= '9' && !b.p[25]);
    tracks = xx_data_get_u16(b.p + 30, 2, 0, false);
    sides = xx_data_get_u16(b.p + 32, 2, 0, false);
    sectors = xx_data_get_u16(b.p + 34, 2, 0, false);
    size = xx_data_get_u16(b.p + 42, 2, 0, false);
    HX_NEED(tracks && tracks <= 170 && sides && sides <= 2 && sectors && sectors <= 64 && size >= 128 && size <= 8192 && !(size & (size - 1U)) &&
            xx_data_get_u16(b.p + 44, 2, 0, false) == size * sectors);
    for (i = 0; i < 3; ++i) HX_NEED(xx_data_get_u16(b.p + 36 + i * 2, 2, 0, false) <= xx_data_get_u16(b.p + 30 + i * 2, 2, 0, false));
    n = (uint64_t)tracks * sides * sectors * size;
    HX_NEED(b.n == 46 + n && hx_emit(f, s, &b, "descriptor.sdu", 0, 46) && hx_emit(f, s, &b, "sector-image.img", 46, n));
    xx_rt_snprintf(info, sizeof(info),
                   "Format: SAB Diskette Utility\nCylinders: %u\nSides: %u\nSectors per track: %u\nSector bytes: %u\nRepresentation: complete stored sector "
                   "image\nIntegrity: geometry and extent; no payload checksum\n",
                   tracks, sides, sectors, size);
    HX_NEED(hx_text(f, s, &b, info));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
HX_API(sdu, XX_FILE_TYPE_SDU, "sdu")

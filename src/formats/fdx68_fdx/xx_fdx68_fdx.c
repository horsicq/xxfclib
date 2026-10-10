/* SPDX-License-Identifier: MIT. Original parser from primary documented layout facts. */
#include "xxfclib/formats/fdx68_fdx/xx_fdx68_fdx.h"
#include "../disk_additions/xx_disk_additions.h"

/* FDX68 revision3: LE256-byte disk header and fixed-sized track blocks. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[256], t[16];
    uint32_t c, heads, block, type, cyl, head;
    uint64_t at;
    char name[64];
    if (!da_read(f, 0, h, 256, pd) || xx_rt_memcmp(h, "FDX", 3) || h[3] != 3U) {
        return false;
    }
    type = xx_data_get_u32(h + 68, 4, 0, false);
    c = xx_data_get_u32(h + 72, 4, 0, false);
    heads = xx_data_get_u32(h + 76, 4, 0, false);
    block = xx_data_get_u32(h + 100, 4, 0, false);
    if ((type > 2U && type != 9U) || !c || c > 256U || !heads || heads > 2U || block <= 16U || block > 1048576U || xx_data_get_u32(h + 88, 4, 0, false) > 1U ||
        !xx_data_get_u32(h + 80, 4, 0, false) || !xx_data_get_u32(h + 84, 4, 0, false) || (uint64_t)pm_available(f) != 256U + (uint64_t)c * heads * block)
        return false;
    if (!da_add(f, s, "descriptor.fdx", 0, 256)) {
        return false;
    }
    at = 256;
    for (cyl = 0; cyl < c; ++cyl)
        for (head = 0; head < heads; ++head) {
            uint32_t bits, index;
            uint64_t bytes;
            if (!da_read(f, at, t, 16, pd) || xx_data_get_u32(t, 4, 0, false) != cyl || xx_data_get_u32(t + 4, 4, 0, false) != head) return false;
            index = xx_data_get_u32(t + 8, 4, 0, false);
            bits = xx_data_get_u32(t + 12, 4, 0, false);
            bytes = ((uint64_t)bits + 7U) / 8U;
            if (!bits || bytes > block - 16U || index >= bits) return false;
            xx_rt_snprintf(name, sizeof(name), "c%03u-h%u-%s.bin", cyl, head, type == 9U ? "sampled-raw" : "encoded-cells");
            if (!da_add(f, s, name, at + 16U, bytes)) return false;
            xx_rt_snprintf(name, sizeof(name), "c%03u-h%u-original-track.fdx", cyl, head);
            if (!da_add(f, s, name, at, block)) return false;
            at += block;
        }
    s->size = (int64_t)at;
    return true;
}
DA_API(fdx68_fdx, XX_FILE_TYPE_FDX68_FDX, "fdx")

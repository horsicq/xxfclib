/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/t98_hdd/xx_t98_hdd.h"
#include "../disk_additions/xx_disk_additions.h"

/* T98: LE cylinder count and252 zero bytes, 8 heads x33 sectors x256. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[256];
    uint32_t c, i;
    uint64_t n;
    if (!da_read(f, 0, h, 256, pd)) return false;
    c = xx_data_get_u32(h, 4, 0, false);
    if (!c || c > 1048576U) return false;
    for (i = 4; i < 256U; ++i)
        if (h[i]) return false;
    n = (uint64_t)c * 8U * 33U * 256U;
    if ((uint64_t)pm_available(f) != 256U + n || !da_add(f, s, "descriptor.thd", 0, 256) || !da_add(f, s, "sector-image.img", 256, n)) return false;
    s->size = (int64_t)(256U + n);
    return true;
}
DA_API(t98_hdd, XX_FILE_TYPE_T98_HDD, "thd")

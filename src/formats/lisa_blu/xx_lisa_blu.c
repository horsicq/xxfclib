/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/lisa_blu/xx_lisa_blu.h"
#include "../disk_additions/xx_disk_additions.h"

/* Primary field reference: Aaru v5.4.2 Aaru.Images/BLU/Structs.cs, Read.cs. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[23];
    uint64_t blocks, total;
    uint32_t bytes, i;
    if (!da_read(f, 0, h, sizeof(h), pd)) {
        return false;
    }
    for (i = 0; i < 13U; ++i)
        if (h[i] < 32U || h[i] > 126U) return false;
    blocks = (uint32_t)h[18] << 16 | (uint32_t)h[19] << 8 | h[20];
    bytes = xx_data_get_u16(h + 21, 2, 0, true);
    if (!blocks || (bytes & 0xfe00U) != 512U) return false;
    total = (blocks + 1U) * bytes;
    if (total != (uint64_t)pm_available(f) || !da_add(f, s, "descriptor.blu", 0, bytes) || !da_one(f, s, "sector-image.img", bytes, 512, blocks, bytes, -1, NULL))
        return false;
    if (bytes > 512U && !da_one(f, s, "sector-tags.bin", bytes + 512U, bytes - 512U, blocks, bytes, -1, NULL)) {
        return false;
    }
    s->size = (int64_t)total;
    return da_poll(pd);
}
DA_API(lisa_blu, XX_FILE_TYPE_LISA_BLU, "blu")

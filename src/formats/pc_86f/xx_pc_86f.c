/* SPDX-License-Identifier: MIT. Original parser from primary layout facts. */
#include "xxfclib/formats/pc_86f/xx_pc_86f.h"
#include "../disk_additions/xx_disk_additions.h"

/* 86Box official v2.12: encoded16-bit words, surface mask, total/extra cells.
 * Preserves original track bytes and its index/flags; no FM/MFM FS recovery. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[2056], t[10];
    uint32_t flags, entries, i, j, at, used = 0;
    uint64_t end = 0, n = (uint64_t)pm_available(f);
    hx_range ranges[512];
    char name[64];
    if (!da_read(f, 0, h, 8, pd) || xx_rt_memcmp(h, "86BF", 4) || h[4] != 12U || h[5] != 2U) {
        return false;
    }
    flags = xx_data_get_u16(h + 6, 2, 0, false);
    if (flags & 0xe000U) return false;
    entries = (flags & 8U) ? 512U : 256U;
    if (!da_read(f, 8, h + 8, entries * 4U, pd) || !xx_data_get_u32(h + 8, 4, 0, false) || !da_add(f, s, "descriptor.86f", 0, 8U + entries * 4U)) return false;
    for (i = 0; i < entries; ++i) {
        uint64_t bits, bytes, size;
        uint32_t index, header = (flags & 128U) ? 10U : 6U;
        int64_t extra = 0;
        uint32_t speed = (flags >> 5) & 3U, hole = (flags >> 1) & 3U;
        static const uint32_t words[3][7] = {
            {12500, 12625, 12687, 12750, 12376, 12315, 12254}, {25000, 25250, 25375, 25500, 24752, 24630, 24509}, {50000, 50500, 50750, 51000, 49504, 49261, 49019}};
        at = xx_data_get_u32(h + 8U + i * 4U, 4, 0, false);
        if (!at) continue;
        if (at < 8U + entries * 4U || !da_read(f, at, t, header, pd) || (xx_data_get_u16(t, 2, 0, false) & 0xff00U)) return false;
        if (flags & 128U) {
            extra = (int32_t)xx_data_get_u32(t + 2, 4, 0, false);
        }
        index = xx_data_get_u32(t + header - 4U, 4, 0, false);
        if ((flags & 0x1080U) == 0x1080U && !speed) {
            if (extra <= 0) return false;
            bits = (uint64_t)extra;
        } else {
            uint32_t mode = speed ? ((flags & 4096U) ? speed + 3U : speed) : 0U;
            int64_t total = (int64_t)words[hole < 2U ? 0U : hole - 1U][mode] * 16 + extra;
            if (extra < -1024 || extra > 1024 || total <= 0) return false;
            bits = (uint64_t)total;
        }
        if (bits > 2097152U || index >= bits) {
            return false;
        }
        bytes = ((bits + 15U) / 16U) * 2U;
        size = header + bytes * ((flags & 1U) ? 2U : 1U);
        if (at > n || size > n - at) return false;
        for (j = 0; j < used; ++j)
            if (at != ranges[j].at && at < ranges[j].at + ranges[j].n && ranges[j].at < at + size) return false;
        ranges[used].at = at;
        ranges[used++].n = size;
        if (at + size > end) end = at + size;
        xx_rt_snprintf(name, sizeof(name), "track-%03u-h%u-encoded-cells.bin", (flags & 8U) ? i / 2U : i, (flags & 8U) ? i & 1U : 0U);
        if (!da_add(f, s, name, at + header, bytes)) return false;
        xx_rt_snprintf(name, sizeof(name), "track-%03u-original.86f", i);
        if (!da_add(f, s, name, at, size)) return false;
        if (flags & 1U) {
            xx_rt_snprintf(name, sizeof(name), "track-%03u-surface-mask.bin", i);
            if (!da_add(f, s, name, at + header + bytes, bytes)) return false;
        }
    }
    if (!used || end != n) {
        return false;
    }
    s->size = (int64_t)n;
    return da_poll(pd);
}
DA_API(pc_86f, XX_FILE_TYPE_PC_86F, "86f")

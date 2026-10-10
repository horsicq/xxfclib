/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/copytape/xx_copytape.h"
#include "../disk_additions/xx_disk_additions.h"

/* CopyTape ASCII framing: decimal six-digit block length, filemarks and EOT. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[16], tail;
    uint64_t at = 0, n = (uint64_t)pm_available(f);
    unsigned file = 0, block = 0;
    bool end = false;
    char name[64];
    while (at < n) {
        uint32_t len = 0, i;
        if (!da_read(f, at, h, 9, pd)) return false;
        if (!xx_rt_memcmp(h, "CPTP:EOT\n", 9)) {
            at += 9;
            end = true;
            break;
        }
        if (!xx_rt_memcmp(h, "CPTP:MRK\n", 9)) {
            xx_rt_snprintf(name, sizeof(name), "file-%04u-filemark.cptp", file++);
            if (!da_add(f, s, name, at, 9)) return false;
            at += 9;
            block = 0;
            continue;
        }
        if (xx_rt_memcmp(h, "CPTP:BLK ", 9) || !da_read(f, at, h, 16, pd) || h[15] != '\n') return false;
        for (i = 9; i < 15U; ++i) {
            if (h[i] < '0' || h[i] > '9') return false;
            len = len * 10U + h[i] - '0';
        }
        if (!len || at > n || len + 17U > n - at || !da_read(f, at + 16U + len, &tail, 1, pd) || tail != '\n') return false;
        xx_rt_snprintf(name, sizeof(name), "file-%04u-block-%06u.bin", file, block++);
        if (!da_add(f, s, name, at + 16U, len)) return false;
        at += 17U + len;
        if (s->count > 100000U) return false;
    }
    if (!end || at != n) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}
DA_API(copytape, XX_FILE_TYPE_COPYTAPE, "cptp")

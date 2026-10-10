/* SPDX-License-Identifier: MIT. Original parser from primary layout facts. */
#include "xxfclib/formats/hxc_logic_analyzer/xx_hxc_logic_analyzer.h"
#include "../disk_additions/xx_disk_additions.h"

/* Explicit sampled8-bit signal import: falling data edges become LE32 sample
 * deltas; falling index edges become LE32 absolute sample positions. */
static bool la_scan(Abstractformat *f, xx_pd_struct *pd, uint8_t *pulse, uint8_t *index, uint32_t *np, uint32_t *ni)
{
    xx_disk_additions_info *r = (xx_disk_additions_info *)f;
    uint8_t buffer[16384], old = 0;
    uint64_t at = 0, n = (uint64_t)pm_available(f);
    uint32_t pc = 0, ic = 0, last = 0;
    unsigned d = 1U << r->data_bit, x = 1U << r->index_bit;
    while (at < n) {
        size_t count = n - at > sizeof(buffer) ? sizeof(buffer) : (size_t)(n - at), i;
        if (!da_read(f, at, buffer, count, pd)) return false;
        for (i = 0; i < count; ++i) {
            uint32_t pos = (uint32_t)(at + i);
            uint8_t now = buffer[i];
            if ((old & d) && !(now & d)) {
                if (pulse) xx_data_set_u32(pulse + pc * 4U, 4, 0, pos - last, false);
                last = pos;
                ++pc;
            }
            if ((old & x) && !(now & x)) {
                if (index) xx_data_set_u32(index + ic * 4U, 4, 0, pos, false);
                ++ic;
            }
            old = now;
        }
        at += count;
    }
    *np = pc;
    *ni = ic;
    return da_poll(pd);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_disk_additions_info *r = (xx_disk_additions_info *)f;
    uint64_t n = (uint64_t)pm_available(f);
    uint32_t pc = 0, ic = 0, a, b;
    uint8_t *pulse = NULL, *index = NULL;
    hx_blob blob;
    bool ok = false;
    char info[256];
    xx_mem_zero(&blob, sizeof(blob));
    blob.pd = pd;
    if (!n || n > 64U * 1024U * 1024U || r->sample_hz < 1000U || r->sample_hz > 1000000000U || r->data_bit > 7U || r->index_bit > 7U || r->data_bit == r->index_bit ||
        !hx_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, 16384U))
        return false;
    if (!la_scan(f, pd, NULL, NULL, &pc, &ic) || (uint64_t)(pc + ic) * 4U > 64U * 1024U * 1024U || !hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, (uint64_t)pc * 4U) ||
        !hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, (uint64_t)ic * 4U) ||
        !hx_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, (uint64_t)(pc + ic) * 4U + 65536U + 8U * sizeof(pm_member) + sizeof(pm_stream) + sizeof(info)))
        return false;
    pulse = hx_alloc(f, &blob, (size_t)pc * 4U);
    if (!pulse) goto done;
    index = (uint8_t *)xx_mem_alloc(ic ? (size_t)ic * 4U : 1U);
    if (!index) goto done;
    if (!la_scan(f, pd, pulse, index, &a, &b) || a != pc || b != ic || !da_add(f, s, "original-samples.logicbin8bits", 0, n) ||
        !hx_owned(f, s, &blob, "flux-deltas-samples.le32", pulse, (size_t)pc * 4U)) {
        goto done;
    }
    pulse = NULL;
    if (!hx_owned(f, s, &blob, "index-positions-samples.le32", index, (size_t)ic * 4U)) {
        goto done;
    }
    index = NULL;
    xx_rt_snprintf(info, sizeof(info),
                   "Format: configured8-bit logic capture\nSample frequency: %u Hz\nData signal bit: %u\nIndex signal bit: %u\nData pulses: %u\nIndex edges: %u\nUnits: "
                   "sample ticks, LE32\n",
                   r->sample_hz, r->data_bit, r->index_bit, pc, ic);
    if (!hx_text(f, s, &blob, info)) goto done;
    s->size = (int64_t)n;
    ok = true;
done:
    if (pulse) xx_mem_free(pulse);
    if (index) xx_mem_free(index);
    return ok;
}
DA_API(hxc_logic_analyzer, XX_FILE_TYPE_HXC_LOGIC_ANALYZER, "logicbin8bits")
bool xx_hxc_logic_analyzer_set_signals(xx_hxc_logic_analyzer *r, uint32_t hz, unsigned data, unsigned index)
{
    if (!r || hz < 1000U || hz > 1000000000U || data > 7U || index > 7U || data == index) return false;
    r->sample_hz = hz;
    r->data_bit = data;
    r->index_bit = index;
    r->format.is_valid = false;
    r->format.base_info_handled = false;
    return true;
}

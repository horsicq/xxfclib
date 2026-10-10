/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/WbfsBlob.cpp
 * One-disc WBFS volumes with 512-byte host sectors and 0.5-16MiB WBFS clusters, up to4096 allocated disc blocks. Checks declared physical length, first slot, logical
 * block map, unique physical clusters and Wii header copy. Exports stored disc-cluster components; sparse-disc reconstruction, split .wbf files, game filesystem and
 * decryption unsupported.
 */
#include "xxfclib/formats/nintendo_wbfs/xx_nintendo_wbfs.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

/* The final 20 bytes of the disc-info header copy may be a WBFS-specific
 * "MD5#" record rather than bytes from the first physical disc block. */
#define WBFS_MD5_RECORD_OFFSET 236U
#define WBFS_MD5_RECORD_SIZE 20U

static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[512], copy[256], disc[256], e[2];
    uint32_t shift, i, nused = 0, seen[4096];
    uint64_t cluster, total, blocks, info_end;
    char label[48];
    if (!pm_read(f, 0, h, 512) || xx_rt_memcmp(h, "WBFS", 4) || h[8] != 9 || h[9] < 19 || h[9] > 24 || h[10] || h[11] || h[12] != 1) return false;
    for (i = 13; i < 512; ++i) {
        if (h[i]) return false;
    }
    shift = h[9];
    cluster = 1ULL << shift;
    total = (uint64_t)xx_data_get_u32(h + 4, 4, 0, true) * 512;
    blocks = (143432ULL * 2 * 32768 + cluster - 1) / cluster;
    info_end = (512 + 256 + blocks * 2 + 511) & ~511ULL;
    if (total < 2 * cluster || total % cluster || total > (uint64_t)pm_available(f) || info_end > cluster || !pm_read(f, 512, copy, 256) ||
        xx_data_get_u32(copy + 24, 4, 0, true) != 0x5d1c9ea3U)
        return false;
    for (i = 0; i < blocks; ++i) {
        uint32_t physical, j;
        if (stop(pd) || !pm_read(f, 768 + (int64_t)i * 2, e, 2)) return false;
        physical = xx_data_get_u16(e, 2, 0, true);
        if (!physical) continue;
        if (nused >= 4096 || physical >= total / cluster) {
            return false;
        }
        for (j = 0; j < nused; ++j)
            if (seen[j] == physical) return false;
        seen[nused++] = physical;
        if (!i) {
            if (!pm_read(f, (int64_t)((uint64_t)physical * cluster), disc, sizeof(disc)) || xx_rt_memcmp(copy, disc, WBFS_MD5_RECORD_OFFSET) ||
                (xx_rt_memcmp(copy + WBFS_MD5_RECORD_OFFSET, disc + WBFS_MD5_RECORD_OFFSET, WBFS_MD5_RECORD_SIZE) &&
                 xx_rt_memcmp(copy + WBFS_MD5_RECORD_OFFSET, "MD5#", 4U)))
                return false;
        }
        xx_rt_snprintf(label, sizeof(label), "disc-block-%u.bin", i);
        if (!emit(f, s, label, (uint64_t)physical * cluster, cluster, total)) return false;
    }
    if (!nused || !pm_read(f, 768, e, 2) || !xx_data_get_u16(e, 2, 0, true)) {
        return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_nintendo_wbfs_init(xx_nintendo_wbfs *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_WBFS, "wbfs");
    }
}
xx_nintendo_wbfs *xx_nintendo_wbfs_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_wbfs *r = (xx_nintendo_wbfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_wbfs_init(r, d, b);
    return r;
}
void xx_nintendo_wbfs_destroy(xx_nintendo_wbfs *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_wbfs_free(xx_nintendo_wbfs *r)
{
    if (r) {
        xx_nintendo_wbfs_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_wbfs_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_wbfs_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

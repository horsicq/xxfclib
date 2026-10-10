/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://llvm.org/docs/PDB/MsfFile.html */
#include "xxfclib/formats/microsoft_msf/xx_microsoft_msf.h"
#include "../common/xx_container_codec_helpers.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint8_t *dir = NULL, *owned = NULL, *used = NULL;
    uint64_t at, j, k, dn, db, map, blocks, count, size, need, total = 0;
    uint32_t bs, fpm;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(blob_span(&b, 0, 56) && !xx_rt_memcmp(b.p,
                                                    "Microsoft C/C++ MSF 7.00\r\n\x1a"
                                                    "DS\0\0\0",
                                                    32));
    bs = xx_data_get_u32(b.p + 32, 4, 0, false);
    fpm = xx_data_get_u32(b.p + 36, 4, 0, false);
    blocks = xx_data_get_u32(b.p + 40, 4, 0, false);
    dn = xx_data_get_u32(b.p + 44, 4, 0, false);
    map = xx_data_get_u32(b.p + 52, 4, 0, false);
    BLOB_NEED(bs == 4096 && (fpm == 1 || fpm == 2) && blocks >= 4 && blocks * bs == b.n && dn >= 4 && dn <= 4194304 && map < blocks &&
              !xx_data_get_u32(b.p + 48, 4, 0, false));
    db = (dn + bs - 1) / bs;
    BLOB_NEED(db * 4 <= bs);
    dir = (uint8_t *)xx_mem_alloc((size_t)dn);
    used = (uint8_t *)xx_mem_alloc((size_t)blocks);
    BLOB_NEED(dir && used);
    xx_mem_zero(used, (size_t)blocks);
    used[0] = 1;
    for (j = 0; j < blocks; j += bs) {
        if (j + 1 < blocks) used[(size_t)(j + 1)] = 1;
        if (j + 2 < blocks) used[(size_t)(j + 2)] = 1;
    }
    BLOB_NEED(!used[(size_t)map]);
    used[(size_t)map] = 1;
    for (j = 0; j < db; ++j) {
        uint64_t id = xx_data_get_u32(b.p + (size_t)(map * bs + j * 4), 4, 0, false), n = dn - j * bs;
        BLOB_NEED(id < blocks && !used[(size_t)id]);
        used[(size_t)id] = 1;
        if (n > bs) n = bs;
        xx_rt_memcpy(dir + (size_t)(j * bs), b.p + (size_t)(id * bs), (size_t)n);
    }
    count = xx_data_get_u32(dir, 4, 0, false);
    BLOB_NEED(count <= 4093 && count * 4 + 4 <= dn);
    at = count * 4 + 4;
    BLOB_NEED(blob_add(f, s, &b, "superblock", 0, 56));
    for (j = 0; j < count; ++j) {
        char name[40];
        size = xx_data_get_u32(dir + 4 + (size_t)(j * 4), 4, 0, false);
        if (size == 0xffffffffU) continue;
        BLOB_NEED(size <= 67108864 - total);
        total += size;
        need = (size + bs - 1) / bs;
        BLOB_NEED(record_span(at, need * 4, dn));
        if (!j) {
            at += need * 4;
            total -= size;
            continue;
        }
        owned = (uint8_t *)xx_mem_alloc((size_t)(size ? size : 1));
        BLOB_NEED(owned);
        for (k = 0; k < need; ++k) {
            uint64_t id = xx_data_get_u32(dir + (size_t)at, 4, 0, false), n = size - k * bs;
            at += 4;
            BLOB_NEED(!binary_stop(pd) && id < blocks && !used[(size_t)id]);
            used[(size_t)id] = 1;
            if (n > bs) n = bs;
            xx_rt_memcpy(owned + (size_t)(k * bs), b.p + (size_t)(id * bs), (size_t)n);
        }
        xx_rt_snprintf(name, sizeof(name), "stream-%04u", (unsigned)j);
        BLOB_NEED(container_codec_mem(f, s, name, &owned, size));
    }
    BLOB_NEED(at == dn);
    /* Each bitmap page represents bs*8 blocks, although pages are spaced bs blocks. */
    for (j = 0; j < blocks; ++j)
        if (used[(size_t)j]) {
            uint64_t page = (j / (bs * 8U)) * bs + fpm, bit = j % (bs * 8U);
            BLOB_NEED(!binary_stop(pd) && page < blocks && !(b.p[(size_t)(page * bs + bit / 8)] & (1U << (bit & 7))));
        }
    BLOB_NEED(container_codec_mem(f, s, "directory", &dir, dn));
    s->size = (int64_t)b.n;
    ok = true;
done:
    if (dir) xx_mem_free(dir);
    if (owned) xx_mem_free(owned);
    if (used) xx_mem_free(used);
    xx_mem_free(b.p);
    return ok;
}
void xx_microsoft_msf_init(xx_microsoft_msf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MICROSOFT_MSF, "pdb");
    }
}
xx_microsoft_msf *xx_microsoft_msf_create(xx_io_device *d, int64_t b)
{
    xx_microsoft_msf *r = (xx_microsoft_msf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_microsoft_msf_init(r, d, b);
    return r;
}
void xx_microsoft_msf_destroy(xx_microsoft_msf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_microsoft_msf_free(xx_microsoft_msf *r)
{
    if (r) {
        xx_microsoft_msf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_microsoft_msf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_microsoft_msf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

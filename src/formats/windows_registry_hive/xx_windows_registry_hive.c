/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/libyal/libregf/blob/main/documentation/Windows%20NT%20Registry%20File%20(REGF)%20format.asciidoc */
#include "xxfclib/formats/windows_registry_hive/xx_windows_registry_hive.h"
#include "../common/xx_container_codec_helpers.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at, end, cell, n, root;
    uint32_t crc = 0;
    bool found = false, ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(blob_span(&b, 0, 4096) && !xx_rt_memcmp(b.p, "regf", 4));
    BLOB_NEED(xx_data_get_u32(b.p + 4, 4, 0, false) == xx_data_get_u32(b.p + 8, 4, 0, false) && xx_data_get_u32(b.p + 20, 4, 0, false) == 1 &&
              xx_data_get_u32(b.p + 24, 4, 0, false) >= 2 && xx_data_get_u32(b.p + 24, 4, 0, false) <= 6 && !xx_data_get_u32(b.p + 28, 4, 0, false) &&
              xx_data_get_u32(b.p + 32, 4, 0, false) == 1 && xx_data_get_u32(b.p + 44, 4, 0, false) == 1);
    for (at = 0; at < 508; at += 4) {
        crc ^= xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
    }
    if (!crc) crc = 1;
    else if (crc == 0xffffffffU) crc = 0xfffffffeU;
    BLOB_NEED(crc == xx_data_get_u32(b.p + 508, 4, 0, false));
    end = (uint64_t)xx_data_get_u32(b.p + 40, 4, 0, false) + 4096;
    root = (uint64_t)xx_data_get_u32(b.p + 36, 4, 0, false) + 4096;
    BLOB_NEED(end == b.n && end > 4096 && !(end & 4095));
    BLOB_NEED(blob_add(f, s, &b, "regf-header", 0, 4096));
    for (at = 4096; at < end;) {
        uint64_t bin;
        BLOB_NEED(blob_span(&b, at, 32) && !xx_rt_memcmp(b.p + (size_t)at, "hbin", 4) && xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, false) == at - 4096);
        bin = xx_data_get_u32(b.p + (size_t)at + 8, 4, 0, false);
        BLOB_NEED(bin >= 4096 && !(bin & 4095) && blob_span(&b, at, bin));
        BLOB_NEED(blob_add(f, s, &b, "hbin-header", at, 32));
        for (cell = at + 32; cell < at + bin; cell += n) {
            uint32_t raw;
            BLOB_NEED(blob_span(&b, cell, 4));
            raw = xx_data_get_u32(b.p + (size_t)cell, 4, 0, false);
            n = raw & 0x80000000U ? (uint64_t)(0U - raw) : raw;
            BLOB_NEED(n >= 8 && !(n & 7) && record_span(cell, n, at + bin) && blob_span(&b, cell, n));
            if (raw & 0x80000000U) {
                if (cell == root) {
                    BLOB_NEED(n >= 80 && !xx_rt_memcmp(b.p + (size_t)cell + 4, "nk", 2));
                    found = true;
                }
                BLOB_NEED(blob_add(f, s, &b, "allocated-cell", cell, n));
            } else BLOB_NEED(cell != root);
        }
        BLOB_NEED(cell == at + bin);
        at += bin;
    }
    BLOB_NEED(found);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}
void xx_windows_registry_hive_init(xx_windows_registry_hive *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_WINDOWS_REGISTRY_HIVE, "hive");
    }
}
xx_windows_registry_hive *xx_windows_registry_hive_create(xx_io_device *d, int64_t b)
{
    xx_windows_registry_hive *r = (xx_windows_registry_hive *)xx_mem_alloc(sizeof(*r));
    if (r) xx_windows_registry_hive_init(r, d, b);
    return r;
}
void xx_windows_registry_hive_destroy(xx_windows_registry_hive *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_windows_registry_hive_free(xx_windows_registry_hive *r)
{
    if (r) {
        xx_windows_registry_hive_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_windows_registry_hive_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_windows_registry_hive_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

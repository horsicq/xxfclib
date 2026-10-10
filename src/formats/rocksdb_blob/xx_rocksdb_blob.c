/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/facebook/rocksdb/blob/main/db/blob/blob_log_format.h */
#include "xxfclib/formats/rocksdb_blob/xx_rocksdb_blob.h"
#include "../common/xx_container_wire_helpers.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 30, end, key, value, exp, count = 0, lo = UINT64_MAX, hi = 0, hlo, hhi, flo, fhi;
    bool ttl, ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 94 && xx_data_get_u32(b.p, 4, 0, false) == 2395959 && xx_data_get_u32(b.p + 4, 4, 0, false) == 1 && b.p[12] <= 1 && !b.p[13]);
    ttl = b.p[12] != 0;
    hlo = xx_data_get_u64(b.p + 14, 8, 0, false);
    hhi = xx_data_get_u64(b.p + 22, 8, 0, false);
    end = b.n - 32;
    BLOB_NEED((ttl ? hlo <= hhi : !hlo && !hhi) && xx_data_get_u32(b.p + (size_t)end, 4, 0, false) == 2395959 &&
              container_wire_crc(&b, end, 28, xx_data_get_u32(b.p + (size_t)end + 28, 4, 0, false)) && blob_add(f, s, &b, "blob-header", 0, 30));
    while (at < end) {
        BLOB_NEED(record_span(at, 32, end) && blob_span(&b, at, 32));
        key = xx_data_get_u64(b.p + (size_t)at, 8, 0, false);
        value = xx_data_get_u64(b.p + (size_t)at + 8, 8, 0, false);
        exp = xx_data_get_u64(b.p + (size_t)at + 16, 8, 0, false);
        BLOB_NEED(key <= 16777216 && value <= 33554432 && key <= end - at - 32 && value <= end - at - 32 - key &&
                  container_wire_crc(&b, at, 24, xx_data_get_u32(b.p + (size_t)at + 24, 4, 0, false)) &&
                  container_wire_crc(&b, at + 32, key + value, xx_data_get_u32(b.p + (size_t)at + 28, 4, 0, false)) && (ttl ? exp >= hlo && exp <= hhi : !exp));
        if (exp < lo) lo = exp;
        if (exp > hi) hi = exp;
        BLOB_NEED(++count <= 1024 && blob_add(f, s, &b, "blob-record-header", at, 32) && blob_add(f, s, &b, "key", at + 32, key) &&
                  blob_add(f, s, &b, "value", at + 32 + key, value));
        at += 32 + key + value;
    }
    flo = xx_data_get_u64(b.p + (size_t)end + 12, 8, 0, false);
    fhi = xx_data_get_u64(b.p + (size_t)end + 20, 8, 0, false);
    BLOB_NEED(count && count == xx_data_get_u64(b.p + (size_t)end + 4, 8, 0, false) && flo == lo && fhi == hi && blob_add(f, s, &b, "blob-footer", end, 32));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_rocksdb_blob_init(xx_rocksdb_blob *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ROCKSDB_BLOB, "blob");
    }
}
xx_rocksdb_blob *xx_rocksdb_blob_create(xx_io_device *d, int64_t b)
{
    xx_rocksdb_blob *r = (xx_rocksdb_blob *)xx_mem_alloc(sizeof(*r));
    if (r) xx_rocksdb_blob_init(r, d, b);
    return r;
}
void xx_rocksdb_blob_destroy(xx_rocksdb_blob *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_rocksdb_blob_free(xx_rocksdb_blob *r)
{
    if (r) {
        xx_rocksdb_blob_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_rocksdb_blob_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_rocksdb_blob_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

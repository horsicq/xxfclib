/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://kafka.apache.org/43/implementation/message-format/ */
#include "xxfclib/formats/kafka_record_batch/xx_kafka_record_batch.h"
#include "../common/xx_serialized_value_helpers.h"

static bool ka_bytes(Abstractformat *f, pm_stream *s, memory_blob *b, uint64_t *at, uint64_t end, const char *name, bool nullable, bool utf)
{
    int64_t n;
    if (!serialized_zig(b, at, end, &n) || n < (nullable ? -1 : 0) || n > 67108864) return false;
    if (n == -1) return true;
    if (!record_span(*at, (uint64_t)n, end) || !blob_span(b, *at, (uint64_t)n) || (utf && !serialized_utf(b, *at, (uint64_t)n)) ||
        !blob_add(f, s, b, name, *at, (uint64_t)n))
        return false;
    *at += (uint64_t)n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 0, end, i, j, recend, start, prev = 0;
    int64_t n, ts, off, headers;
    uint32_t count, last;
    uint16_t attr;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    while (at < b.n) {
        BLOB_NEED(blob_span(&b, at, 61) && b.p[(size_t)at + 16] == 2 && !(xx_data_get_u64(b.p + (size_t)at, 8, 0, true) >> 63));
        n = xx_data_get_u32(b.p + (size_t)at + 8, 4, 0, true);
        BLOB_NEED(n >= 49 && blob_span(&b, at, (uint64_t)n + 12));
        end = at + (uint64_t)n + 12;
        attr = xx_data_get_u16(b.p + (size_t)at + 21, 2, 0, true);
        last = xx_data_get_u32(b.p + (size_t)at + 23, 4, 0, true);
        count = xx_data_get_u32(b.p + (size_t)at + 57, 4, 0, true);
        BLOB_NEED(!(attr & 0xffe7U) && count && count <= 2048 && !(last & 0x80000000U) &&
                  serialized_crc(&b, at + 21, end - at - 21, xx_data_get_u32(b.p + (size_t)at + 17, 4, 0, true), true) && blob_add(f, s, &b, "batch-header", at, 61));
        at += 61;
        for (i = 0; i < count; ++i) {
            BLOB_NEED(serialized_zig(&b, &at, end, &n) && n >= 7 && record_span(at, (uint64_t)n, end));
            recend = at + (uint64_t)n;
            start = at;
            BLOB_NEED(blob_span(&b, at, 1) && !b.p[(size_t)at++] && serialized_zig(&b, &at, recend, &ts) && serialized_zig(&b, &at, recend, &off) && off >= 0 &&
                      (uint64_t)off <= last && (!i || (uint64_t)off > prev));
            prev = (uint64_t)off;
            BLOB_NEED(blob_add(f, s, &b, "record-prefix", start, at - start) && ka_bytes(f, s, &b, &at, recend, "key", true, false) &&
                      ka_bytes(f, s, &b, &at, recend, "value", true, false) && serialized_zig(&b, &at, recend, &headers) && headers >= 0 && headers <= 256);
            for (j = 0; j < (uint64_t)headers; ++j)
                BLOB_NEED(ka_bytes(f, s, &b, &at, recend, "header-key", false, true) && ka_bytes(f, s, &b, &at, recend, "header-value", true, false));
            BLOB_NEED(at == recend);
        }
        BLOB_NEED(at == end);
    }
    BLOB_NEED(s->count);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_kafka_record_batch_init(xx_kafka_record_batch *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_KAFKA_RECORD_BATCH, "batch");
    }
}
xx_kafka_record_batch *xx_kafka_record_batch_create(xx_io_device *d, int64_t b)
{
    xx_kafka_record_batch *r = (xx_kafka_record_batch *)xx_mem_alloc(sizeof(*r));
    if (r) xx_kafka_record_batch_init(r, d, b);
    return r;
}
void xx_kafka_record_batch_destroy(xx_kafka_record_batch *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_kafka_record_batch_free(xx_kafka_record_batch *r)
{
    if (r) {
        xx_kafka_record_batch_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_kafka_record_batch_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_kafka_record_batch_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

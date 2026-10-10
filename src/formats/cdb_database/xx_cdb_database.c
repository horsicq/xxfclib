/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://cr.yp.to/cdb/cdb-0.75.tar.gz */
#include "xxfclib/formats/cdb_database/xx_cdb_database.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

typedef struct cdbrec {
    uint32_t offset, hash;
    unsigned found;
} cdbrec;
static uint32_t keyhash(memory_blob *b, uint64_t p, uint64_t n)
{
    uint32_t h = 5381;
    for (uint64_t i = 0; i < n; ++i) {
        if (!(i & 65535U) && binary_stop(b->pd)) return 0;
        h = (h * 33) ^ b->p[(size_t)(p + i)];
    }
    return h;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    cdbrec *records = NULL;
    uint64_t dataend = 0, at;
    unsigned count = 0, work = 0;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 2048);
    dataend = xx_data_get_u32(b.p, 4, 0, false);
    BLOB_NEED(dataend >= 2048 && dataend <= b.n);
    records = (cdbrec *)xx_mem_alloc(2048 * sizeof(cdbrec));
    BLOB_NEED(records);
    at = 2048;
    while (at < dataend) {
        uint64_t start = at;
        BLOB_NEED(count < 2048 && protocol_take(&b, &at, dataend, 8));
        uint64_t key = xx_data_get_u32(b.p + (size_t)start, 4, 0, false), value = xx_data_get_u32(b.p + (size_t)start + 4, 4, 0, false);
        BLOB_NEED(protocol_take(&b, &at, dataend, key) && protocol_take(&b, &at, dataend, value));
        records[count].offset = (uint32_t)start;
        records[count].hash = keyhash(&b, start + 8, key);
        records[count++].found = 0;
        BLOB_NEED(blob_add(f, s, &b, "key", start + 8, key) && blob_add(f, s, &b, "value", start + 8 + key, value));
    }
    BLOB_NEED(count > 0 && at == dataend);
    at = dataend;
    for (unsigned bucket = 0; bucket < 256; ++bucket) {
        uint64_t pos = xx_data_get_u32(b.p + bucket * 8, 4, 0, false), slots = xx_data_get_u32(b.p + bucket * 8 + 4, 4, 0, false);
        unsigned keys = 0;
        for (unsigned i = 0; i < count; ++i)
            if ((records[i].hash & 255) == bucket) ++keys;
        BLOB_NEED(pos == at && slots == (uint64_t)keys * 2 && blob_span(&b, pos, slots * 8));
        for (uint64_t slot = 0; slot < slots; ++slot) {
            uint32_t h = xx_data_get_u32(b.p + (size_t)(pos + slot * 8), 4, 0, false), off = xx_data_get_u32(b.p + (size_t)(pos + slot * 8 + 4), 4, 0, false);
            if (!off) {
                BLOB_NEED(!h);
                continue;
            }
            BLOB_NEED((h & 255) == bucket);
            unsigned i = 0;
            while (i < count && records[i].offset != off) ++i;
            BLOB_NEED(i < count && records[i].hash == h && ++records[i].found == 1);
            uint64_t probe = (h >> 8) % slots;
            while (probe != slot) {
                BLOB_NEED(++work <= 1048576 && xx_data_get_u32(b.p + (size_t)(pos + probe * 8 + 4), 4, 0, false) != 0);
                probe = (probe + 1) % slots;
            }
        }
        at += slots * 8;
    }
    BLOB_NEED(at == b.n);
    for (unsigned i = 0; i < count; ++i) BLOB_NEED(records[i].found == 1);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(records);
    xx_mem_free(b.p);
    return ok;
}

void xx_cdb_database_init(xx_cdb_database *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CDB_DATABASE, "bin");
    }
}
xx_cdb_database *xx_cdb_database_create(xx_io_device *d, int64_t b)
{
    xx_cdb_database *r = (xx_cdb_database *)xx_mem_alloc(sizeof(*r));
    if (r) xx_cdb_database_init(r, d, b);
    return r;
}
void xx_cdb_database_destroy(xx_cdb_database *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_cdb_database_free(xx_cdb_database *r)
{
    if (r) {
        xx_cdb_database_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_cdb_database_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_cdb_database_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

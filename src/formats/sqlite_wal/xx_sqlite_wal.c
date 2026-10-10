/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.sqlite.org/fileformat.html#walformat
 * Checksummed WAL frames; exports page images without replaying transactions.
 */
#include "xxfclib/formats/sqlite_wal/xx_sqlite_wal.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static void wal_checksum(const uint8_t *p, size_t n, bool be, uint32_t *a, uint32_t *b)
{
    size_t i;
    for (i = 0; i < n; i += 8) {
        *a += (be ? xx_data_get_u32(p + i, 4, 0, true) : xx_data_get_u32(p + i, 4, 0, false)) + *b;
        *b += (be ? xx_data_get_u32(p + i + 4, 4, 0, true) : xx_data_get_u32(p + i + 4, 4, 0, false)) + *a;
    }
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32], e[24], buffer[8192];
    uint32_t page, a = 0, b = 0, magic;
    int64_t at = 32, left = pm_available(f);
    bool be;
    if (!pm_read(f, 0, h, 32)) return false;
    magic = xx_data_get_u32(h, 4, 0, true);
    be = magic == 0x377f0683U;
    if ((magic != 0x377f0682U && !be) || xx_data_get_u32(h + 4, 4, 0, true) != 3007000U) return false;
    page = xx_data_get_u32(h + 8, 4, 0, true);
    if (page < 512 || page > 65536 || (page & (page - 1))) return false;
    wal_checksum(h, 24, be, &a, &b);
    if (a != xx_data_get_u32(h + 24, 4, 0, true) || b != xx_data_get_u32(h + 28, 4, 0, true)) return false;
    while (at < left) {
        uint32_t number, offset = 0;
        char name[56];
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, e, 24) || page > (uint64_t)(left - at - 24)) return false;
        number = xx_data_get_u32(e, 4, 0, true);
        if (!number || number > 0xfffffffeU || xx_data_get_u32(e + 4, 4, 0, true) > 0xfffffffeU || xx_rt_memcmp(e + 8, h + 16, 8)) return false;
        wal_checksum(e, 8, be, &a, &b);
        while (offset < page) {
            size_t n = page - offset > sizeof(buffer) ? sizeof(buffer) : page - offset;
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at + 24 + offset, buffer, n)) return false;
            wal_checksum(buffer, n, be, &a, &b);
            offset += (uint32_t)n;
        }
        if (a != xx_data_get_u32(e + 16, 4, 0, true) || b != xx_data_get_u32(e + 20, 4, 0, true)) return false;
        xx_rt_snprintf(name, sizeof(name), "frame-%u-page-%u.bin", (unsigned)s->count, (unsigned)number);
        if (!pm_add(f, s, name, at + 24, page)) return false;
        at += 24 + page;
    }
    s->size = at;
    return s->count != 0;
}

void xx_sqlite_wal_init(xx_sqlite_wal *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SQLITE_WAL, "wal");
    }
}
xx_sqlite_wal *xx_sqlite_wal_create(xx_io_device *d, int64_t b)
{
    xx_sqlite_wal *r = (xx_sqlite_wal *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sqlite_wal_init(r, d, b);
    return r;
}
void xx_sqlite_wal_destroy(xx_sqlite_wal *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sqlite_wal_free(xx_sqlite_wal *r)
{
    if (r) {
        xx_sqlite_wal_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sqlite_wal_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sqlite_wal_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

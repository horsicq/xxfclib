/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/coreprime/kbot-io/blob/main/formats/hpi/v1/reader.go
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/totalannihilation_hpi/xx_totalannihilation_hpi.h"
#include "../common/xx_carrier_helpers.h"

static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20];
    uint32_t dirend, start, queue[1024], used = 0, count = 1;
    int64_t limit = pm_available(f), end;
    if (!pm_read(f, 0, h, 20) || xx_rt_memcmp(h, "HAPI", 4) || xx_data_get_u32(h + 4, 4, 0, false) != 0x10000 || xx_data_get_u32(h + 12, 4, 0, false)) return false;
    dirend = xx_data_get_u32(h + 8, 4, 0, false);
    start = xx_data_get_u32(h + 16, 4, 0, false);
    if (start < 20 || dirend < start + 8U || dirend > 16777216 || dirend > (uint64_t)limit) return false;
    queue[0] = start;
    end = dirend;
    while (used < count) {
        uint32_t at = queue[used++], n, table, i;
        if (at < start || !carrier_range(dirend, at, 8) || !pm_read(f, at, h, 8)) return false;
        n = xx_data_get_u32(h, 4, 0, false);
        table = xx_data_get_u32(h + 4, 4, 0, false);
        if (n > 65536 || table < start || !carrier_range(dirend, table, (uint64_t)n * 9)) return false;
        for (i = 0; i < n; ++i) {
            uint32_t name, record;
            int64_t p;
            char text[257], label[48];
            if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * 9, h, 9) || h[8] > 1) {
                return false;
            }
            name = xx_data_get_u32(h, 4, 0, false);
            record = xx_data_get_u32(h + 4, 4, 0, false);
            p = name;
            if (name < start || !carrier_string(f, &p, dirend, text, sizeof(text)) || !text[0]) return false;
            if (h[8]) {
                unsigned j;
                if (count == 1024 || record < start || !carrier_range(dirend, record, 8)) return false;
                for (j = 0; j < count; ++j)
                    if (queue[j] == record) return false;
                queue[count++] = record;
            } else {
                uint32_t data, bytes;
                if (record < start || !carrier_range(dirend, record, 9) || !pm_read(f, record, h, 9) || h[8]) return false;
                data = xx_data_get_u32(h, 4, 0, false);
                bytes = xx_data_get_u32(h + 4, 4, 0, false);
                if (data < dirend || !carrier_range(limit, data, bytes)) {
                    return false;
                }
                xx_rt_snprintf(label, sizeof(label), "file-%u.bin", (unsigned)s->count);
                if (!pm_add(f, s, label, data, bytes)) return false;
                if ((int64_t)data + bytes > end) end = (int64_t)data + bytes;
            }
        }
    }
    s->size = end;
    return s->count > 0;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_totalannihilation_hpi_init(xx_totalannihilation_hpi *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TOTALANNIHILATION_HPI, "hpi");
    }
}
xx_totalannihilation_hpi *xx_totalannihilation_hpi_create(xx_io_device *d, int64_t b)
{
    xx_totalannihilation_hpi *r = (xx_totalannihilation_hpi *)xx_mem_alloc(sizeof(*r));
    if (r) xx_totalannihilation_hpi_init(r, d, b);
    return r;
}
void xx_totalannihilation_hpi_destroy(xx_totalannihilation_hpi *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_totalannihilation_hpi_free(xx_totalannihilation_hpi *r)
{
    if (r) {
        xx_totalannihilation_hpi_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_totalannihilation_hpi_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_totalannihilation_hpi_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

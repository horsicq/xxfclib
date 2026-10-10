/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.etsi.org/deliver/etsi_TS/129200_129299/129244/17.06.00_60/ts_129244v170600p.pdf */
#include "xxfclib/formats/pfcp_message/xx_pfcp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 8;
    unsigned mask = 0, count = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 21 && b.n <= 65539 && b.p[0] == 32 && b.p[1] == 5 && xx_data_get_u16(b.p + 2, 2, 0, true) == b.n - 4 && !b.p[7] &&
              blob_add(f, s, &b, "pfcp-header", 0, 8));
    while (at < b.n) {
        uint64_t start = at;
        BLOB_NEED(++count <= 2 && protocol_take(&b, &at, b.n, 4));
        unsigned type = xx_data_get_u16(b.p + (size_t)start, 2, 0, true);
        uint64_t n = xx_data_get_u16(b.p + (size_t)start + 2, 2, 0, true), p = at;
        BLOB_NEED(protocol_take(&b, &at, b.n, n));
        if (type == 60) {
            BLOB_NEED(!(mask & 1) && n >= 2 && b.p[(size_t)p] <= 2);
            mask |= 1;
            unsigned kind = b.p[(size_t)p];
            if (kind == 0) BLOB_NEED(n == 5);
            else if (kind == 1) BLOB_NEED(n == 17);
            else {
                uint64_t q = p + 1;
                unsigned labels = 0;
                while (q < at) {
                    unsigned z = b.p[(size_t)q++];
                    BLOB_NEED(++labels <= 127 && z && z <= 63 && protocol_take(&b, &q, at, z) && packet_ascii(&b, q - z, z, false));
                }
                BLOB_NEED(q == at && n <= 256);
            }
            BLOB_NEED(blob_add(f, s, &b, "node-id", p, n));
        } else if (type == 96) {
            BLOB_NEED(!(mask & 2) && n == 4);
            mask |= 2;
            BLOB_NEED(blob_add(f, s, &b, "recovery-timestamp", p, n));
        } else goto done;
    }
    BLOB_NEED(mask == 3);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_pfcp_message_init(xx_pfcp_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PFCP_MESSAGE, "bin");
    }
}
xx_pfcp_message *xx_pfcp_message_create(xx_io_device *d, int64_t b)
{
    xx_pfcp_message *r = (xx_pfcp_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_pfcp_message_init(r, d, b);
    return r;
}
void xx_pfcp_message_destroy(xx_pfcp_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pfcp_message_free(xx_pfcp_message *r)
{
    if (r) {
        xx_pfcp_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_pfcp_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_pfcp_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

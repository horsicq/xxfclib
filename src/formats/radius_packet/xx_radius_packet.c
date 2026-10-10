/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2865.html */
#include "xxfclib/formats/radius_packet/xx_radius_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 20, start, n;
    unsigned attrs = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 22 && b.n <= 4096 && xx_data_get_u16(b.p + 2, 2, 0, true) == b.n && ((b.p[0] >= 1 && b.p[0] <= 5) || b.p[0] == 11) &&
              blob_add(f, s, &b, "radius-header-authenticator", 0, 20));
    while (at < b.n) {
        start = at;
        BLOB_NEED(++attrs <= 256 && protocol_take(&b, &at, b.n, 2));
        unsigned type = b.p[(size_t)start];
        n = b.p[(size_t)start + 1];
        BLOB_NEED(n >= 2 && protocol_take(&b, &at, b.n, n - 2));
        uint64_t v = start + 2;
        switch (type) {
            case 1:
            case 11:
            case 18:
            case 32:
            case 33: BLOB_NEED(n > 2 && serialized_utf(&b, v, n - 2)); break;
            case 2: BLOB_NEED(n >= 18 && n <= 130 && !((n - 2) & 15)); break;
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 12:
            case 13:
            case 15:
            case 16:
            case 27:
            case 28:
            case 29:
            case 40:
            case 41:
            case 42:
            case 43:
            case 46:
            case 47:
            case 48:
            case 49:
            case 61: BLOB_NEED(n == 6); break;
            case 24:
            case 25:
            case 30:
            case 31:
            case 44: BLOB_NEED(n > 2); break;
            case 26:
                BLOB_NEED(n >= 8);
                {
                    uint64_t p = v + 4;
                    while (p < at) {
                        BLOB_NEED(at - p >= 2 && b.p[(size_t)p + 1] >= 2 && protocol_take(&b, &p, at, b.p[(size_t)p + 1]));
                    }
                    BLOB_NEED(p == at);
                }
                break;
            case 80: BLOB_NEED(n == 18); break;
            default: BLOB_NEED(false);
        }
        BLOB_NEED(blob_add(f, s, &b, "attribute", start, n));
    }
    BLOB_NEED(attrs > 0);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_radius_packet_init(xx_radius_packet *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RADIUS_PACKET, "bin");
    }
}
xx_radius_packet *xx_radius_packet_create(xx_io_device *d, int64_t b)
{
    xx_radius_packet *r = (xx_radius_packet *)xx_mem_alloc(sizeof(*r));
    if (r) xx_radius_packet_init(r, d, b);
    return r;
}
void xx_radius_packet_destroy(xx_radius_packet *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_radius_packet_free(xx_radius_packet *r)
{
    if (r) {
        xx_radius_packet_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_radius_packet_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_radius_packet_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

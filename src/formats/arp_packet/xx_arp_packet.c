/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc826.html */
#include "xxfclib/formats/arp_packet/xx_arp_packet.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n == 28 && xx_data_get_u16(b.p, 2, 0, true) == 1 && xx_data_get_u16(b.p + 2, 2, 0, true) == 0x0800 && b.p[4] == 6 && b.p[5] == 4 &&
              (xx_data_get_u16(b.p + 6, 2, 0, true) == 1 || xx_data_get_u16(b.p + 6, 2, 0, true) == 2) && !(b.p[8] & 1) && !blob_zero(&b, 8, 6));
    BLOB_NEED(blob_add(f, s, &b, "arp-header", 0, 8) && blob_add(f, s, &b, "sender-mac-ip", 8, 10) && blob_add(f, s, &b, "target-mac-ip", 18, 10));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_arp_packet_init(xx_arp_packet *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ARP_PACKET, "bin");
    }
}
xx_arp_packet *xx_arp_packet_create(xx_io_device *d, int64_t b)
{
    xx_arp_packet *r = (xx_arp_packet *)xx_mem_alloc(sizeof(*r));
    if (r) xx_arp_packet_init(r, d, b);
    return r;
}
void xx_arp_packet_destroy(xx_arp_packet *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_arp_packet_free(xx_arp_packet *r)
{
    if (r) {
        xx_arp_packet_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_arp_packet_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_arp_packet_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

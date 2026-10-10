/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2637.html */
#include "xxfclib/formats/pptp_message/xx_pptp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t host, vendor;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n == 156 && xx_data_get_u16(b.p, 2, 0, true) == 156 && xx_data_get_u16(b.p + 2, 2, 0, true) == 1 && xx_data_get_u32(b.p + 4, 4, 0, true) == 0x1a2b3c4d &&
              xx_data_get_u16(b.p + 8, 2, 0, true) == 1 && !xx_data_get_u16(b.p + 10, 2, 0, true) && xx_data_get_u16(b.p + 12, 2, 0, true) == 0x0100 &&
              !xx_data_get_u16(b.p + 14, 2, 0, true));
    BLOB_NEED(xx_data_get_u32(b.p + 16, 4, 0, true) > 0 && xx_data_get_u32(b.p + 16, 4, 0, true) <= 3 && xx_data_get_u32(b.p + 20, 4, 0, true) > 0 &&
              xx_data_get_u32(b.p + 20, 4, 0, true) <= 3 && packet_zstr(&b, 28, 64, &host) && packet_zstr(&b, 92, 64, &vendor));
    BLOB_NEED(blob_add(f, s, &b, "pptp-control-header", 0, 12) && blob_add(f, s, &b, "start-fields", 12, 16) && blob_add(f, s, &b, "host-name", 28, host) &&
              blob_add(f, s, &b, "vendor-string", 92, vendor));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_pptp_message_init(xx_pptp_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PPTP_MESSAGE, "bin");
    }
}
xx_pptp_message *xx_pptp_message_create(xx_io_device *d, int64_t b)
{
    xx_pptp_message *r = (xx_pptp_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_pptp_message_init(r, d, b);
    return r;
}
void xx_pptp_message_destroy(xx_pptp_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pptp_message_free(xx_pptp_message *r)
{
    if (r) {
        xx_pptp_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_pptp_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_pptp_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

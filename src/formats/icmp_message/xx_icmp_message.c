/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc792.html */
#include "xxfclib/formats/icmp_message/xx_icmp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 9 && b.n <= 65535 && (b.p[0] == 0 || b.p[0] == 8) && !b.p[1] && packet_checksum(&b, 0, b.n, 0));
    BLOB_NEED(blob_add(f, s, &b, "icmp-echo-header", 0, 8) && blob_add(f, s, &b, "echo-data", 8, b.n - 8));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_icmp_message_init(xx_icmp_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ICMP_MESSAGE, "bin");
    }
}
xx_icmp_message *xx_icmp_message_create(xx_io_device *d, int64_t b)
{
    xx_icmp_message *r = (xx_icmp_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_icmp_message_init(r, d, b);
    return r;
}
void xx_icmp_message_destroy(xx_icmp_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_icmp_message_free(xx_icmp_message *r)
{
    if (r) {
        xx_icmp_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_icmp_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_icmp_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

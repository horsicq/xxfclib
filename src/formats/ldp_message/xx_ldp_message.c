/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc5036.html */
#include "xxfclib/formats/ldp_message/xx_ldp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 10;
    unsigned count = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 26 && b.n <= 4096 && xx_data_get_u16(b.p, 2, 0, true) == 1 && xx_data_get_u16(b.p + 2, 2, 0, true) == b.n - 4 && network_ip4(b.p + 4, false) &&
              !xx_data_get_u16(b.p + 8, 2, 0, true) && blob_add(f, s, &b, "ldp-pdu-header", 0, 10));
    while (at < b.n) {
        uint64_t start = at;
        BLOB_NEED(++count <= 128 && protocol_take(&b, &at, b.n, 8) && xx_data_get_u16(b.p + (size_t)start, 2, 0, true) == 0x0100);
        uint64_t n = xx_data_get_u16(b.p + (size_t)start + 2, 2, 0, true), end = start + 4 + n;
        BLOB_NEED(n >= 12 && end <= b.n && blob_add(f, s, &b, "hello-message-header", start, 8));
        unsigned mask = 0, tlvs = 0;
        while (at < end) {
            uint64_t p = at;
            BLOB_NEED(++tlvs <= 3 && protocol_take(&b, &at, end, 4));
            unsigned type = xx_data_get_u16(b.p + (size_t)p, 2, 0, true), z = xx_data_get_u16(b.p + (size_t)p + 2, 2, 0, true),
                     bit = type == 0x400   ? 1
                           : type == 0x401 ? 2
                           : type == 0x402 ? 4
                                           : 0;
            BLOB_NEED((tlvs != 1 || type == 0x400) && bit && !(mask & bit) && z == 4 && protocol_take(&b, &at, end, z));
            mask |= bit;
            if (type == 0x400) BLOB_NEED(!(xx_data_get_u16(b.p + (size_t)p + 6, 2, 0, true) & 0x3fff));
            if (type == 0x401) BLOB_NEED(network_ip4(b.p + (size_t)p + 4, false));
            BLOB_NEED(blob_add(f, s, &b, "hello-tlv-fields", p, 4) && blob_add(f, s, &b, "hello-tlv-value", p + 4, z));
        }
        BLOB_NEED(mask & 1);
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_ldp_message_init(xx_ldp_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LDP_MESSAGE, "bin");
    }
}
xx_ldp_message *xx_ldp_message_create(xx_io_device *d, int64_t b)
{
    xx_ldp_message *r = (xx_ldp_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ldp_message_init(r, d, b);
    return r;
}
void xx_ldp_message_destroy(xx_ldp_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ldp_message_free(xx_ldp_message *r)
{
    if (r) {
        xx_ldp_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ldp_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ldp_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

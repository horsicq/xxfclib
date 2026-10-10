/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc2131.html */
#include "xxfclib/formats/dhcp_message/xx_dhcp_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 240, start, n;
    bool mt = false, end = false, ok = false;
    uint8_t seen[256] = {0};
    unsigned opts = 0;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 300 && b.n <= 65535 && (b.p[0] == 1 || b.p[0] == 2) && b.p[1] == 1 && b.p[2] == 6 && b.p[3] <= 16 &&
              !(xx_data_get_u16(b.p + 10, 2, 0, true) & 0x7fff) && xx_data_get_u32(b.p + 236, 4, 0, true) == 0x63825363 && container_wire_zero(&b, 34, 10) &&
              blob_ascii(b.p + 44, 64, true) && blob_ascii(b.p + 108, 128, true));
    BLOB_NEED(blob_add(f, s, &b, "bootp-header", 0, 240));
    while (at < b.n) {
        start = at;
        BLOB_NEED(protocol_take(&b, &at, b.n, 1));
        uint8_t type = b.p[(size_t)start];
        if (!type) continue;
        if (type == 255) {
            end = true;
            BLOB_NEED(container_wire_zero(&b, at, b.n - at) && blob_add(f, s, &b, "end-padding", start, b.n - start));
            break;
        }
        BLOB_NEED(++opts <= 256 && !seen[type] && type != 52 && protocol_take(&b, &at, b.n, 1));
        seen[type] = 1;
        n = b.p[(size_t)at - 1];
        uint64_t value = at;
        BLOB_NEED(n && protocol_take(&b, &at, b.n, n));
        switch (type) {
            case 53:
                BLOB_NEED(n == 1 && b.p[(size_t)value] >= 1 && b.p[(size_t)value] <= 8);
                mt = true;
                break;
            case 1:
            case 28:
            case 50:
            case 51:
            case 54:
            case 58:
            case 59: BLOB_NEED(n == 4); break;
            case 3:
            case 6: BLOB_NEED(!(n & 3)); break;
            case 12:
            case 15:
            case 56:
            case 60: BLOB_NEED(serialized_utf(&b, value, n)); break;
            case 55: break;
            case 61: BLOB_NEED(n >= 2); break;
            case 57: BLOB_NEED(n == 2 && xx_data_get_u16(b.p + (size_t)value, 2, 0, true) >= 576); break;
            default: BLOB_NEED(false);
        }
        BLOB_NEED(blob_add(f, s, &b, "option", start, at - start));
    }
    BLOB_NEED(mt && end);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_dhcp_message_init(xx_dhcp_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DHCP_MESSAGE, "bin");
    }
}
xx_dhcp_message *xx_dhcp_message_create(xx_io_device *d, int64_t b)
{
    xx_dhcp_message *r = (xx_dhcp_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dhcp_message_init(r, d, b);
    return r;
}
void xx_dhcp_message_destroy(xx_dhcp_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dhcp_message_free(xx_dhcp_message *r)
{
    if (r) {
        xx_dhcp_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dhcp_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dhcp_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

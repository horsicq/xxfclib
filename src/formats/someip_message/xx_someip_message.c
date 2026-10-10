/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.autosar.org/fileadmin/standards/R24-11/FO/AUTOSAR_FO_PRS_SOMEIPProtocol.pdf */
#include "xxfclib/formats/someip_message/xx_someip_message.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_fields.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 0;
    unsigned count = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 17);
    while (at < b.n) {
        uint64_t start = at;
        BLOB_NEED(++count <= 1024 && protocol_take(&b, &at, b.n, 16));
        uint64_t n = xx_data_get_u32(b.p + (size_t)start + 4, 4, 0, true);
        unsigned type = b.p[(size_t)start + 14], ret = b.p[(size_t)start + 15];
        BLOB_NEED(n >= 9 && n <= 1048584 && protocol_take(&b, &at, b.n, n - 8));
        unsigned service = xx_data_get_u16(b.p + (size_t)start, 2, 0, true), method = xx_data_get_u16(b.p + (size_t)start + 2, 2, 0, true);
        BLOB_NEED(service && service != 65535 && method && method != 65535 && b.p[(size_t)start + 12] == 1 && b.p[(size_t)start + 13] > 0 &&
                  (type <= 2 || type == 128 || type == 129) && ret <= 10);
        if (type <= 2) BLOB_NEED(!ret);
        if (type == 0 || type == 1 || type == 128 || type == 129) BLOB_NEED(!(method & 0x8000));
        else BLOB_NEED(method & 0x8000);
        BLOB_NEED(blob_add(f, s, &b, "someip-header", start, 16) && blob_add(f, s, &b, "encoded-application-payload", start + 16, n - 8));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_someip_message_init(xx_someip_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SOMEIP_MESSAGE, "bin");
    }
}
xx_someip_message *xx_someip_message_create(xx_io_device *d, int64_t b)
{
    xx_someip_message *r = (xx_someip_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_someip_message_init(r, d, b);
    return r;
}
void xx_someip_message_destroy(xx_someip_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_someip_message_free(xx_someip_message *r)
{
    if (r) {
        xx_someip_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_someip_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_someip_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

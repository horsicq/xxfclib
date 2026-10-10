/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bzip 0.15/0.21 stream, with whole-file CRC verification.
 */
#include "xxfclib/formats/bzip1/xx_bzip1.h"
#include "../xx_payload_members.h"
bool xx_bzip1_decode(const uint8_t *, size_t, uint8_t **, size_t *, xx_pd_struct *);
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[4], *packed = NULL, *plain = NULL;
    size_t plain_size = 0;
    int64_t size = pm_available(f);
    bool ok = false;
    if (size < 16 || size > 134217728 || !pm_read(f, 0, header, 4) || header[0] != 'B' || header[1] != 'Z' || header[2] != '0' || header[3] < '1' || header[3] > '9')
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed || !pm_read(f, 0, packed, (size_t)size) || !xx_bzip1_decode(packed, (size_t)size, &plain, &plain_size, pd)) goto done;
    if (!pm_add(f, s, "payload", 0, size)) goto done;
    s->items[0].size = (int64_t)plain_size;
    s->items[0].memory = plain;
    plain = NULL;
    s->size = size;
    ok = true;
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}
void xx_bzip1_init(xx_bzip1 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_BZIP1, "bz");
    }
}
xx_bzip1 *xx_bzip1_create(xx_io_device *d, int64_t b)
{
    xx_bzip1 *r = (xx_bzip1 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_bzip1_init(r, d, b);
    return r;
}
void xx_bzip1_destroy(xx_bzip1 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_bzip1_free(xx_bzip1 *r)
{
    if (r) {
        xx_bzip1_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_bzip1_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_bzip1_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

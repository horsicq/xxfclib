/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/obspy/obspy/blob/master/obspy/io/seg2/seg2.py */
#include "xxfclib/formats/seismic_seg2/xx_seismic_seg2.h"
#include "../common/xx_memory_blob.h"

static bool freeform(memory_blob *b, uint64_t at, uint64_t end, bool be, const uint8_t *h, bool need_interval)
{
    bool interval = false;
    unsigned strings = 0;
    while (at < end) {
        uint16_t n;
        uint64_t text;
        unsigned term = h[8];
        if (!record_span(at, 2, end) || ++strings > 512) {
            return false;
        }
        n = xx_data_get_u16(b->p + (size_t)at, 2, 0, be);
        if (!n) return (!need_interval || interval) && blob_zero(b, at, end - at);
        if (n < 2 + term || n > 1024 || !record_span(at, n, end)) {
            return false;
        }
        text = at + 2;
        if (xx_rt_memcmp(b->p + (size_t)(at + n - term), h + 9, term) || !blob_ascii(b->p + (size_t)text, n - 2 - term, false)) return false;
        if (n > 18 + term && !xx_rt_memcmp(b->p + (size_t)text, "SAMPLE_INTERVAL ", 16)) {
            char value[1024];
            size_t len = n - 18 - term;
            if (interval) return false;
            xx_rt_memcpy(value, b->p + (size_t)text + 16, len);
            value[len] = 0;
            if (!scientific_number_positive_float(value)) return false;
            interval = true;
        }
        at += n;
    }
    return !need_interval || interval;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32];
    memory_blob b = {0};
    bool be, ok = false;
    uint32_t count, pointers, i;
    uint64_t at, next;
    if (!pm_read(f, 0, h, 32)) {
        return false;
    }
    if (xx_data_get_u16(h, 2, 0, false) == 0x3a55) be = false;
    else if (xx_data_get_u16(h, 2, 0, true) == 0x3a55) be = true;
    else return false;
    count = xx_data_get_u16(h + 6, 2, 0, be);
    pointers = xx_data_get_u16(h + 4, 2, 0, be);
    BLOB_NEED(xx_data_get_u16(h + 2, 2, 0, be) == 1 && count && count <= 1024 && pointers == count * 4 && (h[8] == 1 || h[8] == 2) && (h[11] == 1 || h[11] == 2) &&
              blob_load(f, &b, pd) && blob_span(&b, 32, pointers));
    at = 32 + pointers;
    next = xx_data_get_u32(b.p + 32, 4, 0, be);
    BLOB_NEED(next >= at && blob_span(&b, at, next - at) && freeform(&b, at, next, be, h, false) && blob_add(f, s, &b, "header", 0, next));
    for (i = 0; i < count; ++i) {
        uint32_t header, bytes, samples, width, code;
        uint64_t end = i + 1 < count ? xx_data_get_u32(b.p + 36 + i * 4, 4, 0, be) : b.n;
        const uint8_t *p;
        BLOB_NEED(xx_data_get_u32(b.p + 32 + i * 4, 4, 0, be) == next && blob_span(&b, next, 32) && end > next && end <= b.n);
        p = b.p + (size_t)next;
        header = xx_data_get_u16(p + 2, 2, 0, be);
        bytes = xx_data_get_u32(p + 4, 4, 0, be);
        samples = xx_data_get_u32(p + 8, 4, 0, be);
        code = p[12];
        width = code == 1 ? 2 : code == 2 || code == 4 ? 4 : code == 5 ? 8 : 0;
        BLOB_NEED(xx_data_get_u16(p, 2, 0, be) == 0x4422 && header >= 34 && samples && width && bytes == (uint64_t)samples * width &&
                  (uint64_t)header + bytes == end - next && freeform(&b, next + 32, next + header, be, h, true));
        if (code >= 4) {
            BLOB_NEED(blob_floats(&b, next + header, bytes, width, be));
        }
        BLOB_NEED(blob_add(f, s, &b, "trace-header", next, header) && blob_add(f, s, &b, "samples", next + header, bytes));
        next = end;
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_seismic_seg2_init(xx_seismic_seg2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SEISMIC_SEG2, "seismic_seg2");
    }
}
xx_seismic_seg2 *xx_seismic_seg2_create(xx_io_device *d, int64_t b)
{
    xx_seismic_seg2 *r = (xx_seismic_seg2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_seismic_seg2_init(r, d, b);
    return r;
}
void xx_seismic_seg2_destroy(xx_seismic_seg2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_seismic_seg2_free(xx_seismic_seg2 *r)
{
    if (r) {
        xx_seismic_seg2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_seismic_seg2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_seismic_seg2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

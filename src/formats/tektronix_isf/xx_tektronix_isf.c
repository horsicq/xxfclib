/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.tek.com/en/support/faqs/what-format-isf-file */
#include "xxfclib/formats/tektronix_isf/xx_tektronix_isf.h"
#include "../common/xx_memory_blob.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[8];
    memory_blob b = {0};
    char text[32769], v[1024];
    bool ok = false, quote = false;
    uint64_t at = 0, n, width, bits, count;
    unsigned i, digits;
    if (!pm_read(f, 0, h, 8) || xx_rt_memcmp(h, ":WFMPRE:", 8)) {
        return false;
    }
    BLOB_NEED(blob_load(f, &b, pd));
    while (at < b.n && at < 32768) {
        uint8_t c = b.p[(size_t)at];
        if (c == '\"') quote = !quote;
        if (!quote && c == ':' && blob_span(&b, at, 7) && !xx_rt_memcmp(b.p + (size_t)at, ":CURVE", 6)) break;
        BLOB_NEED(c >= 32 && c <= 126);
        text[(size_t)at++] = (char)c;
    }
    BLOB_NEED(!quote && at < 32768 && at > 8 && text[(size_t)at - 1] == ';');
    text[(size_t)at] = 0;
    BLOB_NEED(blob_field(text + 8, "BYT_NR", v, sizeof(v)) && scientific_number_uint(v, &width) && (width == 1 || width == 2));
    BLOB_NEED(blob_field(text + 8, "BIT_NR", v, sizeof(v)) && scientific_number_uint(v, &bits) && bits == width * 8);
    BLOB_NEED(blob_field(text + 8, "NR_PT", v, sizeof(v)) && scientific_number_uint(v, &count) && count && count <= 16000000 &&
              blob_field(text + 8, "ENCDG", v, sizeof(v)) && !xx_rt_strcmp(v, "BIN"));
    BLOB_NEED(blob_field(text + 8, "BN_FMT", v, sizeof(v)) && (!xx_rt_strcmp(v, "RI") || !xx_rt_strcmp(v, "RP")) && blob_field(text + 8, "BYT_OR", v, sizeof(v)) &&
              (!xx_rt_strcmp(v, "MSB") || !xx_rt_strcmp(v, "LSB")) && blob_field(text + 8, "PT_FMT", v, sizeof(v)) && !xx_rt_strcmp(v, "Y"));
    {
        static const char *keys[] = {"XINCR", "XZERO", "PT_OFF", "YMULT", "YZERO", "YOFF"};
        for (i = 0; i < 6; ++i)
            BLOB_NEED(blob_field(text + 8, keys[i], v, sizeof(v)) && (i == 0 || i == 3 ? scientific_number_positive_float(v) : scientific_number_float_token(v)));
    }
    at += 6;
    while (at < b.n && b.p[(size_t)at] == ' ') ++at;
    BLOB_NEED(blob_span(&b, at, 2) && b.p[(size_t)at++] == '#');
    digits = b.p[(size_t)at++] - '0';
    BLOB_NEED(digits >= 1 && digits <= 9 && blob_span(&b, at, digits));
    n = 0;
    for (i = 0; i < digits; ++i) {
        uint8_t c = b.p[(size_t)at++];
        BLOB_NEED(c >= '0' && c <= '9');
        n = n * 10 + c - '0';
    }
    BLOB_NEED(n == count * width && blob_span(&b, at, n));
    BLOB_NEED(at + n == b.n || (at + n + 1 == b.n && b.p[(size_t)(at + n)] == '\n') ||
              (at + n + 2 == b.n && b.p[(size_t)(at + n)] == '\r' && b.p[(size_t)(at + n + 1)] == '\n'));
    BLOB_NEED(blob_add(f, s, &b, "preamble", 0, at) && blob_add(f, s, &b, "samples", at, n));
    s->size = (int64_t)(at + n);
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_tektronix_isf_init(xx_tektronix_isf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TEKTRONIX_ISF, "tektronix_isf");
    }
}
xx_tektronix_isf *xx_tektronix_isf_create(xx_io_device *d, int64_t b)
{
    xx_tektronix_isf *r = (xx_tektronix_isf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tektronix_isf_init(r, d, b);
    return r;
}
void xx_tektronix_isf_destroy(xx_tektronix_isf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tektronix_isf_free(xx_tektronix_isf *r)
{
    if (r) {
        xx_tektronix_isf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tektronix_isf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tektronix_isf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

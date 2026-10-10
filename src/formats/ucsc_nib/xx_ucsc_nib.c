/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/FAQ/FAQformat.html */
#include "xxfclib/formats/ucsc_nib/xx_ucsc_nib.h"
#include "../common/xx_sequence_alignment.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    sequence_sequence q = {0};
    uint64_t total = 0, i;
    uint32_t length;
    bool be, ok = false;
    static const uint8_t bases[] = "TCAGN";
    q.decoded = &total;
    BLOB_NEED(blob_load(f, &b, pd) && b.n >= 9);
    if (xx_data_get_u32(b.p, 4, 0, true) == 0x6be93d3aU) be = true;
    else if (xx_data_get_u32(b.p, 4, 0, false) == 0x6be93d3aU) be = false;
    else goto done;
    length = xx_data_get_u32(b.p + 4, 4, 0, be);
    BLOB_NEED(length && length <= SEQUENCE_MAX_SEQUENCE && b.n == 8 + ((uint64_t)length + 1) / 2 && blob_add(f, s, &b, "nib-header", 0, 8));
    for (i = 0; i < length; ++i) {
        unsigned nib = b.p[(size_t)(8 + i / 2)], base;
        uint8_t ch;
        nib = (i & 1) ? (nib & 15) : (nib >> 4);
        base = nib & 7;
        BLOB_NEED(base <= 4);
        ch = bases[base];
        if (nib & 8) ch = (uint8_t)(ch + 32);
        BLOB_NEED(sequence_push(&q, ch, pd));
    }
    BLOB_NEED(!(length & 1) || !(b.p[(size_t)b.n - 1] & 15));
    BLOB_NEED(sequence_export(f, s, &q, "nucleotides", &total));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(q.p);
    xx_mem_free(b.p);
    return ok;
}

void xx_ucsc_nib_init(xx_ucsc_nib *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UCSC_NIB, "ucsc_nib");
    }
}
xx_ucsc_nib *xx_ucsc_nib_create(xx_io_device *d, int64_t b)
{
    xx_ucsc_nib *r = (xx_ucsc_nib *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ucsc_nib_init(r, d, b);
    return r;
}
void xx_ucsc_nib_destroy(xx_ucsc_nib *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ucsc_nib_free(xx_ucsc_nib *r)
{
    if (r) {
        xx_ucsc_nib_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ucsc_nib_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ucsc_nib_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

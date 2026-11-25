/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Independent Sun MO parser from the published msgfmt.h file layout.
 * No upstream implementation code is copied. */
#ifndef XX_SUN_MO_H
#define XX_SUN_MO_H
static bool doc_sun_header(const uint8_t *p, size_t prefix, int64_t total, bool *big, uint32_t *messages)
{
    unsigned pass, found = 0;
    if (prefix < 20 || total < 38) return false;
    for (pass = 0; pass < 2; ++pass) {
        bool be = pass != 0;
        uint32_t mid = xx_data_get_u32(p, 4, 0, be), count = xx_data_get_u32(p + 4, 4, 0, be);
        uint32_t a = xx_data_get_u32(p + 8, 4, 0, be), b = xx_data_get_u32(p + 12, 4, 0, be);
        uint32_t table = xx_data_get_u32(p + 16, 4, 0, be);
        if (!count || count > 1000000U || mid != (count - 1U) / 2U || table != (uint64_t)count * 16U || !a || !b || (uint64_t)20 + table + a + b != (uint64_t)total)
            continue;
        if (++found != 1) return false;
        *big = be;
        *messages = count;
    }
    return found == 1;
}
/* Intervals rather than untrusted child links drive this walk: recursion is
 * logarithmic, and every canonical node is checked exactly once. */
static bool doc_sun_tree(const uint8_t *p, bool be, uint32_t lo, uint32_t hi, xx_pd_struct *pd)
{
    uint32_t mid = lo + (hi - lo) / 2U;
    uint32_t left = lo < mid ? lo + (mid - lo - 1U) / 2U : UINT32_C(0xffffff9d);
    uint32_t right = mid < hi ? mid + 1U + (hi - mid - 1U) / 2U : UINT32_C(0xffffff9d);
    const uint8_t *row = p + 20U + (size_t)mid * 16U;
    if ((pd && xx_pd_is_stopped(pd)) || xx_data_get_u32(row, 4, 0, be) != left || xx_data_get_u32(row + 4, 4, 0, be) != right) return false;
    return (lo == mid || doc_sun_tree(p, be, lo, mid - 1U, pd)) && (mid == hi || doc_sun_tree(p, be, mid + 1U, hi, pd));
}
static bool doc_sun_mo(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    uint32_t count, ai, bi, table, i;
    size_t apos = 0, bpos = 0, previous_length = 0;
    const uint8_t *a, *b, *previous = NULL;
    bool be, ok = false;
    doc_bytes out = {0};
    if (!doc_sun_header(p, n, (int64_t)n, &be, &count) || !doc_sun_tree(p, be, 0, count - 1U, pd)) return false;
    ai = xx_data_get_u32(p + 8, 4, 0, be);
    bi = xx_data_get_u32(p + 12, 4, 0, be);
    table = xx_data_get_u32(p + 16, 4, 0, be);
    a = p + 20U + table;
    b = a + ai;
    out.limit = doc_limit(f) / 2U;
    for (i = 0; i < count; ++i) {
        const uint8_t *row = p + 20U + (size_t)i * 16U, *id, *translation;
        size_t al = 0, bl = 0;
        int order;
        if ((pd && xx_pd_is_stopped(pd)) || apos >= ai || bpos >= bi || xx_data_get_u32(row + 8, 4, 0, be) != apos || xx_data_get_u32(row + 12, 4, 0, be) != bpos)
            goto done;
        id = a + apos;
        translation = b + bpos;
        while (al < ai - apos && id[al]) ++al;
        while (bl < bi - bpos && translation[bl]) ++bl;
        if (al == ai - apos || bl == bi - bpos) goto done;
        if (previous) {
            order = xx_rt_memcmp(previous, id, previous_length < al ? previous_length : al);
            if (order < 0 || (!order && previous_length <= al)) goto done;
        }
        if (!doc_text(&out, "msgid ") || !doc_escape_po(&out, id, al) || !doc_text(&out, "msgstr ") || !doc_escape_po(&out, translation, bl) || !doc_text(&out, "\n"))
            goto done;
        previous = id;
        previous_length = al;
        apos += al + 1U;
        bpos += bl + 1U;
    }
    if (apos != ai || bpos != bi) goto done;
    ok = doc_member(f, s, "messages.po", &out);
done:
    xx_mem_free(out.p);
    return ok;
}
#endif

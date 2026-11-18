/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* PowerPacker LIBRARY: a file that is nothing but a run of self-contained
 * "PP20" streams, written back to back with no directory, no names and no
 * count.  Picture collections of Amiga programs use it: every member is one
 * crunched IFF file.  A member the cruncher could not shrink is stored as it
 * is, so a raw IFF "FORM" chunk (its own length in the header) may sit
 * between two streams.
 *
 *   item    := stream | form
 *   stream  := "PP20" table[4] bits... u24 length, u8 skip   (size % 4 == 0)
 *   form    := "FORM" u32be n, n bytes (a pad byte only when one is there)
 *
 * A stream has no length field: its trailer is found from its END.  The end
 * is the next place, on a longword boundary from the stream start, where a
 * new item begins (or the end of the file) AND where the stream so delimited
 * decrunches completely, using every source byte.  That second condition is
 * what makes the reading strict:
 *
 *   - the file starts with a stream, and the items cover it to the last byte;
 *   - every stream passes the single-stream header/trailer checks and the
 *     bounds-checked decoder, with no unused source byte;
 *   - there are at least two streams.
 *
 * So a single crunched file is never a library (the caller tries the
 * single-stream reading first anyway), and a file that merely contains the
 * bytes "PP20" is rejected at the first item that does not decode.
 *
 * Members are named by stream index (000.bin, 001.bin, ...).  A stored chunk
 * takes the index of the stream before it: 142_stored.iff.
 *
 * Confirmed on a 208-stream picture file (plus two stored chunks), every
 * member a size-consistent FORM ILBM.
 */
#ifndef XX_PP20_LIBRARY_H
#define XX_PP20_LIBRARY_H

#define PP20_LIBRARY_MAX_ITEMS 65536U
#define PP20_LIBRARY_MIN_STREAM 16U
/* A crunched stream may contain the four magic bytes by chance; a few false
 * boundaries are skipped, a file full of them is not a library. */
#define PP20_LIBRARY_BOUNDARY_TRIES 4U

static bool pp20_library_stream_at(const pp20_image *s, size_t at)
{
    return at <= s->size && s->size - at >= PP20_LIBRARY_MIN_STREAM && memcmp(s->data + at, "PP20", 4U) == 0 && xx_pp20_valid_table(s->data + at + 4U);
}

/* A raw IFF chunk at `at`; `*end` receives the offset of the next item. */
static bool pp20_library_form_at(const pp20_image *s, size_t at, size_t *end)
{
    const uint8_t *p;
    uint32_t length;
    size_t next;
    unsigned index;
    if (at > s->size || s->size - at < 12U) return false;
    p = s->data + at;
    if (memcmp(p, "FORM", 4U) != 0) return false;
    for (index = 8U; index < 12U; ++index) {
        if (!((p[index] >= 'A' && p[index] <= 'Z') || (p[index] >= '0' && p[index] <= '9') || p[index] == ' ')) return false;
    }
    length = ((uint32_t)p[4] << 24U) | ((uint32_t)p[5] << 16U) | ((uint32_t)p[6] << 8U) | (uint32_t)p[7];
    if (length < 4U || (size_t)length > s->size - at - 8U) return false;
    next = at + 8U + (size_t)length;
    if (end) *end = next;
    return true;
}

static bool pp20_library_item_at(const pp20_image *s, size_t at)
{
    return at == s->size || pp20_library_stream_at(s, at) || pp20_library_form_at(s, at, NULL);
}

static pp20_image_item *pp20_library_add(pp20_image *s, size_t *capacity)
{
    if (s->count == *capacity) {
        size_t wanted = *capacity ? *capacity * 2U : 64U;
        pp20_image_item *items;
        if (wanted > PP20_LIBRARY_MAX_ITEMS) wanted = PP20_LIBRARY_MAX_ITEMS;
        if (s->count >= wanted) return NULL;
        items = (pp20_image_item *)xx_mem_realloc(s->items, wanted * sizeof(*items));
        if (!items) return NULL;
        s->items = items;
        *capacity = wanted;
    }
    xx_mem_zero(&s->items[s->count], sizeof(s->items[0]));
    return &s->items[s->count++];
}

/* Finds where the stream that starts at `at` ends and proves it by decoding. */
static bool pp20_library_stream(pp20_image *s, size_t at, pp20_image_item *m, xx_pd_struct *pd)
{
    size_t end;
    unsigned tries = 0U;
    for (end = at + PP20_LIBRARY_MIN_STREAM; end <= s->size; end += 4U) {
        uint8_t *plain = NULL;
        if (!pp20_library_item_at(s, end)) continue;
        if (xx_pd_is_stopped(pd)) return false;
        m->start = at;
        m->size = end - at;
        if (xx_pp20_parse_buffer(s->data + at, m->size, &m->context) && m->context.variant == XX_PP20_VARIANT_PP20 &&
            s->raw <= UINT64_C(256) * 1024U * 1024U - m->context.uncompressed_size && pp20_image_decode(s, (size_t)(m - s->items), &plain, pd)) {
            xx_mem_free(plain);
            return true;
        }
        if (++tries >= PP20_LIBRARY_BOUNDARY_TRIES) return false;
    }
    return false;
}

static pp20_image *pp20_library_open(Abstractformat *f, xx_pd_struct *pd)
{
    pp20_image *s = NULL;
    int64_t n, saved;
    size_t at = 0U, capacity = 0U;
    uint32_t streams = 0U;
    bool ok = false;
    if (!f || !f->device || f->base_address < 0 || xx_pd_is_stopped(pd)) return NULL;
    saved = xx_io_tell(f->device);
    n = xx_io_total_size(f->device) - f->base_address;
    if (saved < 0 || n < (int64_t)(2U * PP20_LIBRARY_MIN_STREAM) || n > (int64_t)PP20_IMAGE_LIMIT) return NULL;
    s = (pp20_image *)xx_mem_calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->library = true;
    s->size = (size_t)n;
    s->data = (uint8_t *)xx_mem_alloc(s->size);
    if (!s->data || !xx_pp20_read_exact_at(f->device, f->base_address, s->data, s->size) || !pp20_library_stream_at(s, 0U)) goto done;
    while (at < s->size) {
        pp20_image_item *m;
        size_t end = 0U;
        if (xx_pd_is_stopped(pd)) goto done;
        m = pp20_library_add(s, &capacity);
        if (!m) goto done;
        if (pp20_library_stream_at(s, at)) {
            if (!pp20_library_stream(s, at, m, pd)) goto done;
            m->index = streams++;
        } else if (streams != 0U && pp20_library_form_at(s, at, &end)) {
            /* The chunk is stored without a pad byte when the next item
             * follows directly; take the pad only if that is what fits. */
            if (!pp20_library_item_at(s, end) && (end & 1U) != 0U && end < s->size && s->data[end] == 0U && pp20_library_item_at(s, end + 1U)) ++end;
            if (!pp20_library_item_at(s, end)) goto done;
            m->start = at;
            m->size = end - at;
            m->stored = true;
            m->index = streams - 1U;
            m->context.uncompressed_size = (uint64_t)m->size;
            m->context.stream_size = (int64_t)m->size;
            if (s->raw > UINT64_C(256) * 1024U * 1024U - m->context.uncompressed_size) goto done;
        } else {
            goto done;
        }
        s->raw += m->context.uncompressed_size;
        at = m->start + m->size;
    }
    ok = at == s->size && streams >= 2U;
done:
    (void)xx_io_seek64(f->device, saved, SEEK_SET);
    if (!ok) {
        pp20_image_free(s);
        s = NULL;
    }
    return s;
}
#endif

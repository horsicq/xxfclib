/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/wpg.c
 * WordPerfect Graphics WPGv1 bitmap subset: complete version/header/counted start/palette/bitmap/end records, palette bounds and complete checked byte/row-copy RLE. Original records and decoded packed bitmaps exported; v2, embedded PostScript, vector/unknown records declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/wordperfect_wpg/xx_wordperfect_wpg.h"
#include "../common/xx_component_binary.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[16];
    return n >= 20 && pm_read(f, 0, b, 16) && component_tag(b, "\xffWPC", 4) &&
           xx_data_get_u32(b + 4, 4, 0, false) == 16 && b[8] == 1 && b[9] == 0x16 && b[10] == 1 && !b[11] &&
           !xx_data_get_u16(b + 12, 2, 0, false) && !xx_data_get_u16(b + 14, 2, 0, false);
}
static bool image_document_wpg_length(component_binary_cursor *q, uint32_t *z) {
    const uint8_t *v;
    uint32_t u;
    if (!component_binary_take(q, 1, &v))
        return false;
    u = v[0];
    if (u == 255) {
        if (!component_binary_take(q, 2, &v))
            return false;
        u = xx_data_get_u16(v, 2, 0, false);
        if (u & 0x8000) {
            uint32_t high = u & 0x7fff;
            if (!component_binary_take(q, 2, &v))
                return false;
            u = (high << 16) | xx_data_get_u16(v, 2, 0, false);
        }
    }
    *z = u;
    return true;
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_binary_cursor q = {b, 16, n, pd};
    bool started = false, ended = false;
    unsigned records = 0, images = 0, palette = 0;
    uint32_t limit = 0;
    uint64_t decodedTotal = 0;
    uint8_t defined[256];
    char label[48];
    xx_mem_zero(defined, sizeof(defined));
    if (n < 20 || !component_tag(b, "\xffWPC", 4) || xx_data_get_u32(b + 4, 4, 0, false) != 16 || b[8] != 1 ||
        b[9] != 0x16 || b[10] != 1 || b[11] || xx_data_get_u32(b + 12, 4, 0, false) ||
        !component_emit(f, s, "descriptor.wpg", 0, 16, n))
        return false;
    while (q.p < n && !ended) {
        const uint8_t *tag, *v;
        uint64_t start = q.p, end;
        uint32_t z;
        if (++records > 1024 || !component_binary_take(&q, 1, &tag) || !image_document_wpg_length(&q, &z) ||
            !component_span(q.p, z, n))
            return false;
        end = q.p + z;
        v = b + q.p;
        if (tag[0] == 0x0f) {
            if (started || records != 1 || z != 6 || xx_data_get_u16(v, 2, 0, false) != 1 ||
                !xx_data_get_u16(v + 2, 2, 0, false) || !xx_data_get_u16(v + 4, 2, 0, false))
                return false;
            started = true;
        } else if (tag[0] == 0x0e) {
            uint32_t first, count, j;
            if (!started || images || palette++ || z < 4)
                return false;
            first = xx_data_get_u16(v, 2, 0, false);
            count = xx_data_get_u16(v + 2, 2, 0, false);
            if (first >= count || count > 256 || z != 4 + (count - first) * 3)
                return false;
            for (j = first; j < count; ++j)
                defined[j] = 1;
            limit = count;
        } else if (tag[0] == 0x0b) {
            uint32_t w, h, depth, stride, x = 0;
            uint64_t p = q.p + 10, total;
            uint8_t *decoded;
            if (!started || z < 11 || ++images > 256)
                return false;
            w = xx_data_get_u16(v, 2, 0, false);
            h = xx_data_get_u16(v + 2, 2, 0, false);
            depth = xx_data_get_u16(v + 4, 2, 0, false);
            if (!w || !h || w > 8192 || h > 8192 ||
                (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 24))
                return false;
            stride = (w * depth + 7) / 8;
            total = (uint64_t)stride * h;
            if (total > 33554432 - decodedTotal)
                return false;
            decodedTotal += total;
            decoded = (uint8_t *)xx_mem_alloc((size_t)total);
            if (!decoded)
                return false;
            while (p < end && x < total) {
                uint32_t run;
                uint8_t c = b[p++];
                if (xx_component_parser_stopped(pd)) {
                    xx_mem_free(decoded);
                    return false;
                }
                run = c & 127;
                if (c & 128) {
                    uint8_t value;
                    if (p == end) {
                        xx_mem_free(decoded);
                        return false;
                    }
                    value = b[p++];
                    if (!run) {
                        run = value;
                        value = 255;
                    }
                    if (!run || run > total - x) {
                        xx_mem_free(decoded);
                        return false;
                    }
                    xx_rt_memset(decoded + x, value, run);
                    x += run;
                } else if (run) {
                    if (!component_span(p, run, end) || run > total - x) {
                        xx_mem_free(decoded);
                        return false;
                    }
                    xx_rt_memcpy(decoded + x, b + p, run);
                    p += run;
                    x += run;
                } else {
                    uint32_t j;
                    if (p == end || (run = b[p++]) == 0 || x < stride || x % stride ||
                        (uint64_t)run * stride > total - x) {
                        xx_mem_free(decoded);
                        return false;
                    }
                    for (j = 0; j < run; ++j) {
                        xx_rt_memcpy(decoded + x, decoded + x - stride, stride);
                        x += stride;
                    }
                }
            }
            if (p != end || x != total) {
                xx_mem_free(decoded);
                return false;
            }
            if (palette && depth <= 8) {
                uint32_t y, j, mask = (1U << depth) - 1;
                for (y = 0; y < h; ++y) {
                    if (xx_component_parser_stopped(pd)) {
                        xx_mem_free(decoded);
                        return false;
                    }
                    for (j = 0; j < w; ++j) {
                        uint32_t index =
                            (decoded[(uint64_t)y * stride + (j * depth) / 8] >> (8 - depth - (j * depth) % 8)) & mask;
                        if (index >= limit || !defined[index]) {
                            xx_mem_free(decoded);
                            return false;
                        }
                    }
                }
            }
            xx_rt_snprintf(label, sizeof(label), "bitmap-%u.wpg", images - 1);
            if (!component_emit(f, s, label, start, end - start, n)) {
                xx_mem_free(decoded);
                return false;
            }
            xx_rt_snprintf(label, sizeof(label), "decoded-packed-bitmap-%u.bin", images - 1);
            if (!component_publish_memory_keep_on_failure(f, s, label, decoded, total)) {
                xx_mem_free(decoded);
                return false;
            }
            q.p = end;
            continue;
        } else {
            if (tag[0] == 0x10) {
                if (!started || !images || z || end != n)
                    return false;
                ended = true;
            } else
                return false;
        }
        xx_rt_snprintf(label, sizeof(label), "record-%u-type-%02x.wpg", records - 1, tag[0]);
        if (!component_emit(f, s, label, start, end - start, n))
            return false;
        q.p = end;
    }
    return ended && q.p == n;
}

void xx_wordperfect_wpg_init(xx_wordperfect_wpg *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_WORDPERFECT_WPG, "wpg");
    }
}
xx_wordperfect_wpg *xx_wordperfect_wpg_create(xx_io_device *d, int64_t at) {
    xx_wordperfect_wpg *r = (xx_wordperfect_wpg *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_wordperfect_wpg_init(r, d, at);
    return r;
}
void xx_wordperfect_wpg_destroy(xx_wordperfect_wpg *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_wordperfect_wpg_free(xx_wordperfect_wpg *r) {
    if (r) {
        xx_wordperfect_wpg_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_wordperfect_wpg_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_wordperfect_wpg_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }

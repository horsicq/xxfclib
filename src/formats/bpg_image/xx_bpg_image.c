/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://bellard.org/bpg/bpg_spec.txt
 * BPG still images with complete canonical variable-length dimensions, extension TLVs, reduced HEVC headers and bounded NAL framing/layer identities and PPS/SPS
 * identifiers. Entropy-coded HEVC slices remain opaque; declared zero picture length consumes physical EOF and cannot prove slice completeness. Original encoded
 * headers/picture data exported; animation and HEVC pixel decoding unsupported. Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/bpg_image/xx_bpg_image.h"
#include "../common/xx_component_binary.h"

static bool model_image_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool model_image_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(model_image, 67108864, )
static bool model_image_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[6];
    return n >= 12 && pm_read(f, 0, b, 6) && component_tag(b, "BPG\xfb", 4);
}
static bool bp_ue7(const uint8_t *b, uint64_t *p, uint64_t n, uint32_t *v)
{
    uint32_t u = 0;
    unsigned count = 0;
    if (*p >= n || b[*p] == 128) return false;
    for (;;) {
        uint8_t c;
        if (*p >= n || ++count > 5) return false;
        c = b[(*p)++];
        if (u > 0xffffffffU >> 7) return false;
        u = (u << 7) | (c & 127U);
        if (!(c & 128)) break;
    }
    *v = u;
    return true;
}
typedef struct bp_bits {
    const uint8_t *b;
    uint64_t p, end;
} bp_bits;
static bool bp_get(bp_bits *q, unsigned n, uint32_t *v)
{
    unsigned i;
    uint32_t u = 0;
    if (n > 32 || q->p > q->end || n > q->end - q->p) return false;
    for (i = 0; i < n; ++i) {
        u = (u << 1) | ((q->b[q->p / 8] >> (7 - (unsigned)(q->p & 7))) & 1);
        ++q->p;
    }
    *v = u;
    return true;
}
static bool bp_ue(bp_bits *q, uint32_t *v)
{
    unsigned zeros = 0;
    uint32_t u, x;
    for (;;) {
        if (!bp_get(q, 1, &u) || zeros > 20) return false;
        if (u) break;
        ++zeros;
    }
    if (!bp_get(q, zeros, &x)) return false;
    *v = ((1U << zeros) - 1) + x;
    return true;
}
static bool bp_header(const uint8_t *b, uint64_t *p, uint64_t end, unsigned depth)
{
    uint32_t z, mincb, diffcb, mintb, difftb, hier, u, pcm;
    bp_bits q;
    if (!bp_ue7(b, p, end, &z) || !z || z > 256 || !component_span(*p, z, end)) {
        return false;
    }
    q.b = b + *p;
    q.p = 0;
    q.end = (uint64_t)z * 8;
    if (!bp_ue(&q, &mincb) || !bp_ue(&q, &diffcb) || mincb > 3 || diffcb > 3 - mincb || !bp_ue(&q, &mintb) || !bp_ue(&q, &difftb) || mintb > 3 || difftb > 3 - mintb ||
        mintb + 2 > mincb + 3 || !bp_ue(&q, &hier) || hier > 4 || !bp_get(&q, 1, &u) || !bp_get(&q, 1, &pcm))
        return false;
    if (pcm) {
        uint32_t minpcm, diffpcm;
        if (!bp_get(&q, 4, &u) || u + 1 > depth || !bp_get(&q, 4, &u) || u + 1 > depth || !bp_ue(&q, &minpcm) || !bp_ue(&q, &diffpcm) || minpcm > 3 ||
            diffpcm > 3 - minpcm || !bp_get(&q, 1, &u))
            return false;
    }
    if (!bp_get(&q, 1, &u) || !bp_get(&q, 1, &u)) return false;
    if (u) {
        uint32_t range;
        if (!bp_get(&q, 1, &range) || !bp_get(&q, 7, &u) || u) return false;
        if (range && !bp_get(&q, 9, &u)) return false;
    }
    if (q.end - q.p > 7) {
        return false;
    }
    while (q.p < q.end) {
        if (!bp_get(&q, 1, &u) || u) return false;
    }
    *p += z;
    return true;
}
static bool model_image_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 6, extensions = 0, picture, header;
    uint32_t w, h, z, pixel = b[4] >> 5, space = b[5] >> 4, nals = 0, ps = 0, color = 0, alpha = 0;
    bool hasalpha = (b[4] & 16) || (b[5] & 4);
    char label[64];
    if (!component_tag(b, "BPG\xfb", 4) || pixel > 5 || (b[4] & 15) > 6 || space > 4 || (!pixel && space) || (b[5] & 1) || !bp_ue7(b, &p, n, &w) ||
        !bp_ue7(b, &p, n, &h) || !w || !h || w > 16384 || h > 16384 || (uint64_t)w * h > 16777216 || !bp_ue7(b, &p, n, &z))
        return false;
    if (b[5] & 8) {
        uint32_t bytes;
        uint64_t end;
        if (!bp_ue7(b, &p, n, &bytes) || bytes > 1048576 || !component_span(p, bytes, n)) return false;
        extensions = p;
        end = p + bytes;
        while (p < end) {
            uint32_t tag, len;
            uint64_t start = p;
            if (xx_component_parser_stopped(pd) || !bp_ue7(b, &p, end, &tag) || !bp_ue7(b, &p, end, &len) || tag == 5 || !component_span(p, len, end)) return false;
            p += len;
            xx_rt_snprintf(label, sizeof(label), "extension-%u.bpg", tag);
            if (!component_emit(f, s, label, start, p - start, n)) return false;
        }
    }
    picture = p;
    if (z && z != n - picture) return false;
    if (n - picture < 6 || !component_emit(f, s, "descriptor.bpg", 0, extensions ? extensions : picture, n)) return false;
    if (hasalpha) {
        header = p;
        if (!bp_header(b, &p, n, (b[4] & 15) + 8) || !component_emit(f, s, "alpha-hevc-header.bpg", header, p - header, n)) return false;
    }
    header = p;
    if (!bp_header(b, &p, n, (b[4] & 15) + 8) || !component_emit(f, s, "color-hevc-header.bpg", header, p - header, n)) return false;
    while (p < n) {
        uint64_t start = p, end;
        uint32_t type, layer;
        unsigned zeros = 0;
        if (xx_component_parser_stopped(pd) || ++nals > 2048 || !component_span(p, 3, n) || (b[p] & 128) || !(b[p + 1] & 7)) {
            return false;
        }
        type = (b[p] >> 1) & 63;
        layer = ((b[p] & 1) << 5) | (b[p + 1] >> 3);
        if ((b[p + 1] & 7) != 1 || layer > 1 || (layer && !hasalpha) || (type != 34 && type != 39 && type != 40 && type != 19 && type != 20)) {
            return false;
        }
        p += 2;
        end = p;
        while (end < n) {
            if ((end & 4095) == 0 && xx_component_parser_stopped(pd)) return false;
            if (b[end] == 0) {
                ++zeros;
                ++end;
                continue;
            }
            if (b[end] == 1 && zeros >= 2) {
                end -= zeros;
                break;
            }
            zeros = 0;
            ++end;
        }
        if (end == n) {
            while (end > p && b[end - 1] == 0) --end;
        }
        if (end <= p) return false;
        if (type == 34) {
            bp_bits q;
            uint32_t id, sps;
            q.b = b + p;
            q.p = 0;
            q.end = (end - p) * 8;
            if (!bp_ue(&q, &id) || !bp_ue(&q, &sps) || id || sps) return false;
            ps |= 1U << layer;
        } else if (type == 19 || type == 20) {
            if (!(ps & (1U << layer))) return false;
            if (layer) ++alpha;
            else ++color;
        }
        {
            uint64_t j;
            unsigned zero = 0;
            for (j = p; j < end; ++j) {
                uint8_t c = b[j];
                if (zero == 2) {
                    if (c < 3) return false;
                    if (c == 3) {
                        if (j + 1 == end || b[j + 1] > 3) return false;
                        zero = 0;
                        continue;
                    }
                }
                zero = c == 0 ? zero + 1 : 0;
            }
        }
        xx_rt_snprintf(label, sizeof(label), "nal-%u-type-%u-layer-%u.bpg", nals - 1, type, layer);
        if (!component_emit(f, s, label, start, end - start, n)) return false;
        p = end;
        if (p < n) {
            uint64_t prefix = p;
            while (p < n && b[p] == 0) ++p;
            if (p == n) {
                if (!component_emit(f, s, "nal-padding.bpg", prefix, p - prefix, n)) return false;
                break;
            }
            if (b[p] != 1 || p - prefix < 2 || p - prefix > 16) return false;
            ++p;
            if (!component_emit(f, s, "nal-separator.bpg", prefix, p - prefix, n)) return false;
        }
    }
    if (!color || hasalpha != (alpha != 0)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_bpg_image_init(xx_bpg_image *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_BPG_IMAGE, "bpg");
    }
}
xx_bpg_image *xx_bpg_image_create(xx_io_device *d, int64_t at)
{
    xx_bpg_image *r = (xx_bpg_image *)xx_mem_alloc(sizeof(*r));
    if (r) xx_bpg_image_init(r, d, at);
    return r;
}
void xx_bpg_image_destroy(xx_bpg_image *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_bpg_image_free(xx_bpg_image *r)
{
    if (r) {
        xx_bpg_image_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_bpg_image_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_bpg_image_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

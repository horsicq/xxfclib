/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/miff/
 * MIFF1.0 uncompressed DirectClass RGB/sRGB8/16/32-bit images: complete bounded key/value descriptor, finite color metadata and exact interleaved raster rows; concatenated images, indexed palettes, profiles and compression declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/imagemagick_miff/xx_imagemagick_miff.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[13];
    return n >= 32 && pm_read(f, 0, b, 13) && component_tag(b, "id=ImageMagick", 13);
}
static bool image_document_miff_name(const uint8_t *b, uint64_t p, uint64_t z, const char *name) {
    return z == xx_rt_strlen(name) && component_tag(b + p, name, (size_t)z);
}
static bool image_document_miff_uint(const uint8_t *b, uint64_t p, uint64_t z, uint32_t *value) {
    uint32_t v = 0;
    uint64_t i;
    if (!z || z > 10)
        return false;
    for (i = 0; i < z; ++i) {
        unsigned d = b[p + i] - '0';
        if (d > 9 || v > (33554432U - d) / 10)
            return false;
        v = v * 10 + d;
    }
    *value = v;
    return true;
}
static bool image_document_miff_floats(const uint8_t *b, uint64_t p, uint64_t z, unsigned count, bool positive,
                                       xx_pd_struct *pd) {
    component_lexer q = {b, p, p + z, pd, 0, false, true, false};
    unsigned i;
    double value;
    for (i = 0; i < count; ++i)
        if (!component_lexer_number_hash_cpp_comments(&q, &value) || (positive ? value <= 0 : value < 0) || value > 1)
            return false;
    return component_lexer_end_hash_cpp_comments(&q);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint64_t p = 0, at, kn, kz, vn, vz, stride;
    uint32_t seen = 0, w = 0, h = 0, depth = 8, alpha = 0, channels = 0, value, y;
    char label[48];
    static const char *const keys[] = {"id",
                                       "version",
                                       "class",
                                       "colors",
                                       "alpha-trait",
                                       "number-channels",
                                       "number-meta-channels",
                                       "channel-mask",
                                       "columns",
                                       "rows",
                                       "depth",
                                       "colorspace",
                                       "compression",
                                       "quality",
                                       "page",
                                       "rendering-intent",
                                       "gamma",
                                       "red-primary",
                                       "green-primary",
                                       "blue-primary",
                                       "white-point"};
    if (n < 32 || !component_tag(b, "id=ImageMagick", 13))
        return false;
    while (p < n && p < 16384) {
        unsigned key;
        while (p < n && (b[p] == 32 || b[p] == 9 || b[p] == 10 || b[p] == 13 || b[p] == 12))
            ++p;
        if (p < n && b[p] == ':' && component_span(p, 2, n) && b[p + 1] == 26) {
            p += 2;
            break;
        }
        kn = p;
        while (p < n && component_lexer_identifier_char(b[p])) {
            if (p - kn > 63)
                return false;
            ++p;
        }
        kz = p - kn;
        if (!kz || p == n || b[p++] != '=')
            return false;
        vn = p;
        while (p < n && b[p] != 32 && b[p] != 9 && b[p] != 10 && b[p] != 13 && b[p] != 12) {
            if (b[p] < 33 || b[p] > 126 || p - vn > 255)
                return false;
            ++p;
        }
        vz = p - vn;
        if (!vz)
            return false;
        for (key = 0; key < 21 && !image_document_miff_name(b, kn, kz, keys[key]); ++key) {
        }
        if (key == 21 || (seen & (1U << key)))
            return false;
        seen |= 1U << key;
        switch (key) {
        case 0:
            if (kn || !image_document_miff_name(b, vn, vz, "ImageMagick"))
                return false;
            break;
        case 1:
            if (!image_document_miff_name(b, vn, vz, "1.0"))
                return false;
            break;
        case 2:
            if (!image_document_miff_name(b, vn, vz, "DirectClass"))
                return false;
            break;
        case 3:
        case 6:
            if (!image_document_miff_uint(b, vn, vz, &value) || value)
                return false;
            break;
        case 4:
            if (image_document_miff_name(b, vn, vz, "Undefined"))
                alpha = 0;
            else if (image_document_miff_name(b, vn, vz, "Blend"))
                alpha = 1;
            else
                return false;
            break;
        case 5:
            if (!image_document_miff_uint(b, vn, vz, &channels) || (channels != 3 && channels != 4))
                return false;
            break;
        case 7: {
            uint64_t i;
            if (vz < 3 || vz > 18 || b[vn] != '0' || b[vn + 1] != 'x')
                return false;
            for (i = vn + 2; i < vn + vz; ++i)
                if (!((b[i] >= '0' && b[i] <= '9') || (b[i] >= 'a' && b[i] <= 'f') || (b[i] >= 'A' && b[i] <= 'F')))
                    return false;
        } break;
        case 8:
            if (!image_document_miff_uint(b, vn, vz, &w) || !w || w > 8192)
                return false;
            break;
        case 9:
            if (!image_document_miff_uint(b, vn, vz, &h) || !h || h > 4095)
                return false;
            break;
        case 10:
            if (!image_document_miff_uint(b, vn, vz, &depth) || (depth != 8 && depth != 16 && depth != 32))
                return false;
            break;
        case 11:
            if (!image_document_miff_name(b, vn, vz, "RGB") && !image_document_miff_name(b, vn, vz, "sRGB"))
                return false;
            break;
        case 12:
            if (!image_document_miff_name(b, vn, vz, "None"))
                return false;
            break;
        case 13:
            if (!image_document_miff_uint(b, vn, vz, &value) || value > 100)
                return false;
            break;
        case 14: {
            component_text_cursor q = {b, vn, vn + vz, 0, vn + vz, vn};
            int32_t width, height, x, yoff;
            uint64_t end;
            if (!component_text_integer_delimited(&q, &width)) /* geometry has explicit separators */ {
                end = vn;
                while (end < vn + vz && b[end] >= '0' && b[end] <= '9')
                    ++end;
                q.stop = end;
                if (!component_text_integer_delimited(&q, &width) || end == vn + vz || b[end] != 'x')
                    return false;
                q.t = end + 1;
                q.stop = vn + vz;
            } else
                return false;
            end = q.t;
            while (end < vn + vz && b[end] >= '0' && b[end] <= '9')
                ++end;
            q.stop = end;
            if (!component_text_integer_delimited(&q, &height) || end == vn + vz || (b[end] != '+' && b[end] != '-'))
                return false;
            q.t = end;
            q.stop = vn + vz;
            end = q.t + 1;
            while (end < vn + vz && b[end] >= '0' && b[end] <= '9')
                ++end;
            q.stop = end;
            if (!component_text_integer_delimited(&q, &x) || end == vn + vz)
                return false;
            q.t = end;
            q.stop = vn + vz;
            if (!component_text_integer_delimited(&q, &yoff) || q.t != q.stop || width < 1 || height < 1 || x < -8192 ||
                x > 8192 || yoff < -8192 || yoff > 8192)
                return false;
        } break;
        case 15:
            if (!image_document_miff_name(b, vn, vz, "Perceptual") &&
                !image_document_miff_name(b, vn, vz, "Saturation") &&
                !image_document_miff_name(b, vn, vz, "Relative") && !image_document_miff_name(b, vn, vz, "Absolute"))
                return false;
            break;
        case 16:
            if (!image_document_miff_floats(b, vn, vz, 1, true, pd))
                return false;
            break;
        default:
            if (!image_document_miff_floats(b, vn, vz, 2, false, pd))
                return false;
            break;
        }
        if (xx_component_parser_stopped(pd))
            return false;
    }
    at = p;
    if (at < 2 || at > 16384 || b[at - 2] != ':' || b[at - 1] != 26 ||
        (seen & ((1U << 0) | (1U << 8) | (1U << 9))) != ((1U << 0) | (1U << 8) | (1U << 9)) ||
        (channels && channels != 3 + alpha) || (uint64_t)w * h > 8388608)
        return false;
    stride = (uint64_t)w * (3 + alpha) * (depth / 8);
    if (n - at != stride * h || !component_emit(f, s, "descriptor.miff", 0, at, n))
        return false;
    for (y = 0; y < h; ++y) {
        xx_rt_snprintf(label, sizeof(label), "sample-row-%u.bin", y);
        if (xx_component_parser_stopped(pd) || !component_emit(f, s, label, at + (uint64_t)y * stride, stride, n))
            return false;
    }
    return true;
}

void xx_imagemagick_miff_init(xx_imagemagick_miff *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_IMAGEMAGICK_MIFF, "miff");
    }
}
xx_imagemagick_miff *xx_imagemagick_miff_create(xx_io_device *d, int64_t at) {
    xx_imagemagick_miff *r = (xx_imagemagick_miff *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_imagemagick_miff_init(r, d, at);
    return r;
}
void xx_imagemagick_miff_destroy(xx_imagemagick_miff *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_imagemagick_miff_free(xx_imagemagick_miff *r) {
    if (r) {
        xx_imagemagick_miff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_imagemagick_miff_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_imagemagick_miff_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }

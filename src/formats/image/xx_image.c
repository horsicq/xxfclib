/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Image core: the image object, properties, the encoder buffer, pixel
 * helpers, palette quantisation and the zlib wrappers over xxfclib.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XX_IMAGE_DEFAULT_MAX_PIXELS (1ULL << 28)

/* ---- options ------------------------------------------------------------ */

void xx_image_load_options_init(xx_image_load_options *o)
{
    if (!o) return;
    memset(o, 0, sizeof(*o));
}

void xx_image_save_options_init(xx_image_save_options *o)
{
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->level = -1;
}

uint64_t xx_image_codec_max_pixels(const xx_image_load_options *o)
{
    return (o && o->max_pixels) ? o->max_pixels : XX_IMAGE_DEFAULT_MAX_PIXELS;
}

xx_image_status xx_image_check_dimensions(uint32_t w, uint32_t h, const xx_image_load_options *o)
{
    if (!w || !h) return XX_IMAGE_ERR_CORRUPT;
    if ((uint64_t)w * h > xx_image_codec_max_pixels(o)) return XX_IMAGE_ERR_LIMIT;
    if ((uint64_t)w * h > (SIZE_MAX / 4)) return XX_IMAGE_ERR_LIMIT;
    return XX_IMAGE_OK;
}

/* ---- strings ------------------------------------------------------------ */

void xx_image_set_str(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || !cap) return;
    if (src)
        for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static char *xv_strdup(const char *s, size_t n)
{
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    if (n) memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* ---- image -------------------------------------------------------------- */

void xx_image_init(xx_image *img)
{
    if (!img) return;
    memset(img, 0, sizeof(*img));
}

static void xv_prop_free(xx_image_property *p)
{
    free(p->group);
    free(p->key);
    free(p->text);
    free(p->data);
}

void xx_image_free(xx_image *img)
{
    if (!img) return;
    for (size_t i = 0; i < img->frame_count; ++i) free(img->frames[i].rgba);
    free(img->frames);
    for (size_t i = 0; i < img->prop_count; ++i) xv_prop_free(&img->props[i]);
    free(img->props);
    free(img->source);
    xx_image_init(img);
}

xx_image_status xx_image_copy(xx_image *dst, const xx_image *src)
{
    if (!dst || !src || dst == src) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(dst);
    dst->width = src->width;
    dst->height = src->height;
    dst->background = src->background;
    dst->loop_count = src->loop_count;
    memcpy(dst->palette, src->palette, sizeof(dst->palette));
    dst->palette_count = src->palette_count;
    dst->info = src->info;
    for (size_t i = 0; i < src->frame_count; ++i) {
        const xx_image_frame *s = &src->frames[i];
        xx_image_frame *f = xx_image_add_frame(dst, s->width, s->height);
        if (!f) goto fail;
        f->x = s->x;
        f->y = s->y;
        f->delay_ms = s->delay_ms;
        f->dispose = s->dispose;
        memcpy(f->rgba, s->rgba, (size_t)s->width * s->height * 4);
    }
    for (size_t i = 0; i < src->prop_count; ++i) {
        const xx_image_property *p = &src->props[i];
        xx_image_status st;
        switch (p->type) {
        case XX_IMAGE_PROP_TEXT: st = xx_image_add_prop_text(dst, p->group, p->key, p->text); break;
        case XX_IMAGE_PROP_INT: st = xx_image_add_prop_int(dst, p->group, p->key, p->v.i); break;
        case XX_IMAGE_PROP_UINT: st = xx_image_add_prop_uint(dst, p->group, p->key, p->v.u); break;
        case XX_IMAGE_PROP_REAL: st = xx_image_add_prop_real(dst, p->group, p->key, p->v.r); break;
        case XX_IMAGE_PROP_BOOL: st = xx_image_add_prop_bool(dst, p->group, p->key, p->v.b); break;
        default: st = xx_image_add_prop_bytes(dst, p->group, p->key, p->data, p->size); break;
        }
        if (st != XX_IMAGE_OK) goto fail;
    }
    if (src->source && src->source_size) {
        dst->source = (uint8_t *)malloc(src->source_size);
        if (!dst->source) goto fail;
        memcpy(dst->source, src->source, src->source_size);
        dst->source_size = src->source_size;
    }
    return XX_IMAGE_OK;
fail:
    xx_image_free(dst);
    return XX_IMAGE_ERR_MEMORY;
}

xx_image_frame *xx_image_add_frame(xx_image *img, uint32_t w, uint32_t h)
{
    xx_image_frame *f;
    size_t bytes;
    if (!img || !w || !h || (uint64_t)w * h > SIZE_MAX / 4) return NULL;
    if (img->frame_count == img->frame_capacity) {
        size_t cap = img->frame_capacity ? img->frame_capacity * 2 : 4;
        xx_image_frame *n = (xx_image_frame *)realloc(img->frames, cap * sizeof(*n));
        if (!n) return NULL;
        img->frames = n;
        img->frame_capacity = cap;
    }
    bytes = (size_t)w * h * 4;
    f = &img->frames[img->frame_count];
    memset(f, 0, sizeof(*f));
    f->rgba = (uint8_t *)calloc(1, bytes);
    if (!f->rgba) return NULL;
    f->width = w;
    f->height = h;
    ++img->frame_count;
    if (!img->width || !img->height) {
        img->width = w;
        img->height = h;
    }
    return f;
}

xx_image_status xx_image_create(xx_image *img, uint32_t w, uint32_t h)
{
    if (!img) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(img);
    if (!xx_image_add_frame(img, w, h)) return XX_IMAGE_ERR_MEMORY;
    img->info.color_model = XX_IMAGE_COLOR_RGBA;
    img->info.channels = 4;
    img->info.bits_per_sample = 8;
    img->info.bits_per_pixel = 32;
    img->info.frame_count = 1;
    return XX_IMAGE_OK;
}

uint32_t xx_image_get_pixel(const xx_image *img, size_t frame, uint32_t x, uint32_t y)
{
    const xx_image_frame *f;
    if (!img || frame >= img->frame_count) return 0;
    f = &img->frames[frame];
    if (x >= f->width || y >= f->height) return 0;
    return xx_image_get_rgba(f->rgba + ((size_t)y * f->width + x) * 4);
}

/* ---- properties --------------------------------------------------------- */

static xx_image_property *xv_prop_new(xx_image *img, const char *group, const char *key, xx_image_prop_type type)
{
    xx_image_property *p;
    if (!img || !key) return NULL;
    if (img->prop_count == img->prop_capacity) {
        size_t cap = img->prop_capacity ? img->prop_capacity * 2 : 16;
        xx_image_property *n = (xx_image_property *)realloc(img->props, cap * sizeof(*n));
        if (!n) return NULL;
        img->props = n;
        img->prop_capacity = cap;
    }
    p = &img->props[img->prop_count];
    memset(p, 0, sizeof(*p));
    if (!group) group = "";
    p->group = xv_strdup(group, strlen(group));
    p->key = xv_strdup(key, strlen(key));
    if (!p->group || !p->key) {
        xv_prop_free(p);
        return NULL;
    }
    p->type = type;
    ++img->prop_count;
    return p;
}

xx_image_status xx_image_add_prop_textn(xx_image *img, const char *group, const char *key, const char *value, size_t n)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_TEXT);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    p->text = xv_strdup(value ? value : "", value ? n : 0);
    if (!p->text) {
        --img->prop_count;
        xv_prop_free(p);
        return XX_IMAGE_ERR_MEMORY;
    }
    return XX_IMAGE_OK;
}

xx_image_status xx_image_add_prop_text(xx_image *img, const char *group, const char *key, const char *value)
{
    return xx_image_add_prop_textn(img, group, key, value, value ? strlen(value) : 0);
}

xx_image_status xx_image_add_prop_int(xx_image *img, const char *group, const char *key, int64_t v)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_INT);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    p->v.i = v;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_add_prop_uint(xx_image *img, const char *group, const char *key, uint64_t v)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_UINT);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    p->v.u = v;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_add_prop_real(xx_image *img, const char *group, const char *key, double v)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_REAL);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    p->v.r = v;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_add_prop_bool(xx_image *img, const char *group, const char *key, bool v)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_BOOL);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    p->v.b = v;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_add_prop_bytes(xx_image *img, const char *group, const char *key, const void *data, size_t size)
{
    xx_image_property *p = xv_prop_new(img, group, key, XX_IMAGE_PROP_BYTES);
    if (!p) return XX_IMAGE_ERR_MEMORY;
    if (size) {
        p->data = (uint8_t *)malloc(size);
        if (!p->data) {
            --img->prop_count;
            xv_prop_free(p);
            return XX_IMAGE_ERR_MEMORY;
        }
        memcpy(p->data, data, size);
        p->size = size;
    }
    return XX_IMAGE_OK;
}

const xx_image_property *xx_image_find_prop(const xx_image *img, const char *group, const char *key)
{
    if (!img || !key) return NULL;
    for (size_t i = 0; i < img->prop_count; ++i) {
        const xx_image_property *p = &img->props[i];
        if (strcmp(p->key, key) == 0 && (!group || strcmp(p->group, group) == 0)) return p;
    }
    return NULL;
}

size_t xx_image_prop_format(const xx_image_property *p, char *buf, size_t size)
{
    int n;
    if (!p || !buf || !size) return 0;
    switch (p->type) {
    case XX_IMAGE_PROP_TEXT: n = snprintf(buf, size, "%s", p->text ? p->text : ""); break;
    case XX_IMAGE_PROP_INT: n = snprintf(buf, size, "%lld", (long long)p->v.i); break;
    case XX_IMAGE_PROP_UINT: n = snprintf(buf, size, "%llu", (unsigned long long)p->v.u); break;
    case XX_IMAGE_PROP_REAL: n = snprintf(buf, size, "%g", p->v.r); break;
    case XX_IMAGE_PROP_BOOL: n = snprintf(buf, size, "%s", p->v.b ? "true" : "false"); break;
    default: n = snprintf(buf, size, "<%llu bytes>", (unsigned long long)p->size); break;
    }
    if (n < 0) {
        buf[0] = 0;
        return 0;
    }
    return (size_t)n < size ? (size_t)n : size - 1;
}

/* ---- buffer ------------------------------------------------------------- */

bool xx_image_buf_reserve(xx_image_buffer *b, size_t cap)
{
    uint8_t *n;
    size_t want;
    if (!b || b->failed) return false;
    if (cap <= b->capacity) return true;
    want = b->capacity ? b->capacity : 256;
    while (want < cap) {
        if (want > SIZE_MAX / 2) {
            want = cap;
            break;
        }
        want *= 2;
    }
    n = (uint8_t *)realloc(b->data, want);
    if (!n) {
        b->failed = true;
        return false;
    }
    b->data = n;
    b->capacity = want;
    return true;
}

bool xx_image_buf_append(xx_image_buffer *b, const void *data, size_t size)
{
    if (!b || b->failed) return false;
    if (!size) return true;
    if (size > SIZE_MAX - b->size || !xx_image_buf_reserve(b, b->size + size)) {
        b->failed = true;
        return false;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
    return true;
}

bool xx_image_buf_u8(xx_image_buffer *b, uint8_t v) { return xx_image_buf_append(b, &v, 1); }
bool xx_image_buf_u16le(xx_image_buffer *b, uint16_t v)
{
    uint8_t d[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    return xx_image_buf_append(b, d, 2);
}
bool xx_image_buf_u16be(xx_image_buffer *b, uint16_t v)
{
    uint8_t d[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    return xx_image_buf_append(b, d, 2);
}
bool xx_image_buf_u32le(xx_image_buffer *b, uint32_t v)
{
    uint8_t d[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return xx_image_buf_append(b, d, 4);
}
bool xx_image_buf_u32be(xx_image_buffer *b, uint32_t v)
{
    uint8_t d[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return xx_image_buf_append(b, d, 4);
}
bool xx_image_buf_fill(xx_image_buffer *b, uint8_t v, size_t count)
{
    if (!b || b->failed) return false;
    if (count > SIZE_MAX - b->size || !xx_image_buf_reserve(b, b->size + count)) {
        b->failed = true;
        return false;
    }
    memset(b->data + b->size, v, count);
    b->size += count;
    return true;
}
bool xx_image_buf_printf(xx_image_buffer *b, const char *fmt, ...)
{
    char small[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n < 0) {
        if (b) b->failed = true;
        return false;
    }
    if ((size_t)n < sizeof(small)) return xx_image_buf_append(b, small, (size_t)n);
    {
        char *big = (char *)malloc((size_t)n + 1);
        bool ok;
        if (!big) {
            if (b) b->failed = true;
            return false;
        }
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        ok = xx_image_buf_append(b, big, (size_t)n);
        free(big);
        return ok;
    }
}
void xx_image_buf_put_u32le_at(xx_image_buffer *b, size_t off, uint32_t v)
{
    if (!b || b->failed || !xx_image_in_range(b->size, off, 4)) return;
    b->data[off] = (uint8_t)v;
    b->data[off + 1] = (uint8_t)(v >> 8);
    b->data[off + 2] = (uint8_t)(v >> 16);
    b->data[off + 3] = (uint8_t)(v >> 24);
}
void xx_image_buf_put_u32be_at(xx_image_buffer *b, size_t off, uint32_t v)
{
    if (!b || b->failed || !xx_image_in_range(b->size, off, 4)) return;
    b->data[off] = (uint8_t)(v >> 24);
    b->data[off + 1] = (uint8_t)(v >> 16);
    b->data[off + 2] = (uint8_t)(v >> 8);
    b->data[off + 3] = (uint8_t)v;
}
void xx_image_buf_free(xx_image_buffer *b)
{
    if (!b) return;
    free(b->data);
    memset(b, 0, sizeof(*b));
}

/* ---- pixels for encoders ------------------------------------------------- */

const xx_image_frame *xx_image_save_frame(const xx_image *img, const xx_image_save_options *o)
{
    size_t i;
    if (!img || !img->frame_count) return NULL;
    i = o ? o->frame : 0;
    if (i >= img->frame_count) i = img->frame_count - 1;
    return &img->frames[i];
}

bool xx_image_frame_has_alpha(const xx_image_frame *f)
{
    size_t n;
    if (!f) return false;
    n = (size_t)f->width * f->height;
    for (size_t i = 0; i < n; ++i)
        if (f->rgba[i * 4 + 3] != 255) return true;
    return false;
}

bool xx_image_frame_is_gray(const xx_image_frame *f)
{
    size_t n;
    if (!f) return false;
    n = (size_t)f->width * f->height;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *p = f->rgba + i * 4;
        if (p[0] != p[1] || p[1] != p[2]) return false;
    }
    return true;
}

/* Exact palette when it fits, median cut otherwise. */
typedef struct xv_box {
    size_t begin, end; /* indices into the colour list */
} xv_box;

static int xv_cmp_channel;
static int xv_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    int sh = 24 - 8 * xv_cmp_channel;
    int cx = (int)((x >> sh) & 255), cy = (int)((y >> sh) & 255);
    return cx - cy;
}

static int xv_cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t xv_nearest(const uint32_t *pal, uint32_t n, uint32_t c)
{
    uint32_t best = 0;
    uint64_t bd = UINT64_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t d = 0;
        for (int k = 0; k < 4; ++k) {
            int64_t e = (int64_t)((c >> (24 - 8 * k)) & 255) - (int64_t)((pal[i] >> (24 - 8 * k)) & 255);
            d += e * e;
        }
        if ((uint64_t)d < bd) {
            bd = (uint64_t)d;
            best = i;
            if (!d) break;
        }
    }
    return best;
}

uint32_t xx_image_quantize(const xx_image_frame *f, uint32_t max_colors, uint8_t *indices, uint32_t *palette)
{
    size_t n, uniq = 0;
    uint32_t *colors;
    uint32_t count;
    if (!f || !indices || !palette || max_colors < 2 || max_colors > 256) return 0;
    n = (size_t)f->width * f->height;
    colors = (uint32_t *)malloc(n * sizeof(*colors));
    if (!colors) return 0;
    for (size_t i = 0; i < n; ++i) colors[i] = xx_image_get_rgba(f->rgba + i * 4);
    qsort(colors, n, sizeof(*colors), xv_cmp_u32);
    for (size_t i = 0; i < n; ++i)
        if (!uniq || colors[uniq - 1] != colors[i]) colors[uniq++] = colors[i];
    if (uniq <= max_colors) {
        count = (uint32_t)uniq;
        memcpy(palette, colors, uniq * sizeof(*colors));
    } else {
        xv_box boxes[256];
        uint32_t nb = 1;
        boxes[0].begin = 0;
        boxes[0].end = uniq;
        while (nb < max_colors) {
            /* split the box with the widest channel range */
            uint32_t pick = UINT32_MAX;
            int best_range = 0, best_ch = 0;
            for (uint32_t b = 0; b < nb; ++b) {
                if (boxes[b].end - boxes[b].begin < 2) continue;
                for (int ch = 0; ch < 4; ++ch) {
                    int lo = 255, hi = 0, sh = 24 - 8 * ch;
                    for (size_t i = boxes[b].begin; i < boxes[b].end; ++i) {
                        int v = (int)((colors[i] >> sh) & 255);
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                    if (hi - lo > best_range) {
                        best_range = hi - lo;
                        best_ch = ch;
                        pick = b;
                    }
                }
            }
            if (pick == UINT32_MAX) break;
            xv_cmp_channel = best_ch;
            qsort(colors + boxes[pick].begin, boxes[pick].end - boxes[pick].begin, sizeof(*colors), xv_cmp);
            {
                size_t mid = boxes[pick].begin + (boxes[pick].end - boxes[pick].begin) / 2;
                boxes[nb].begin = mid;
                boxes[nb].end = boxes[pick].end;
                boxes[pick].end = mid;
                ++nb;
            }
        }
        for (uint32_t b = 0; b < nb; ++b) {
            uint64_t s[4] = {0, 0, 0, 0};
            size_t cnt = boxes[b].end - boxes[b].begin;
            for (size_t i = boxes[b].begin; i < boxes[b].end; ++i)
                for (int k = 0; k < 4; ++k) s[k] += (colors[i] >> (24 - 8 * k)) & 255;
            palette[b] = 0;
            for (int k = 0; k < 4; ++k) palette[b] |= (uint32_t)((s[k] + cnt / 2) / cnt) << (24 - 8 * k);
        }
        count = nb;
    }
    free(colors);
    /* map pixels; cache the last lookup, images have long runs */
    {
        uint32_t last_c = 0, last_i = 0;
        bool have = false;
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = xx_image_get_rgba(f->rgba + i * 4);
            if (!have || c != last_c) {
                last_i = xv_nearest(palette, count, c);
                last_c = c;
                have = true;
            }
            indices[i] = (uint8_t)last_i;
        }
    }
    return count;
}

/* ---- checksums and zlib ------------------------------------------------- */

uint32_t xx_image_crc32(uint32_t crc, const void *data, size_t size)
{
    static uint32_t table[256];
    static volatile int ready = 0;
    const uint8_t *p = (const uint8_t *)data;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1; /* idempotent: racing threads write identical values */
    }
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

uint32_t xx_image_adler32(uint32_t adler, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (size) {
        size_t n = size < 5552 ? size : 5552;
        size -= n;
        while (n--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

xx_image_status xx_image_raw_inflate(const uint8_t *data, size_t size, size_t expected, size_t limit, uint8_t **out, size_t *out_size)
{
    size_t cap;
    if (!out || !out_size || (!data && size)) return XX_IMAGE_ERR_ARGUMENT;
    *out = NULL;
    *out_size = 0;
    if (!limit) limit = SIZE_MAX / 2;
    cap = expected ? expected : (size < limit / 4 ? size * 4 + 1024 : limit);
    if (cap > limit) cap = limit;
    for (;;) {
        uint8_t *buf = (uint8_t *)malloc(cap ? cap : 1);
        size_t written = 0;
        bool ok;
        if (!buf) return XX_IMAGE_ERR_MEMORY;
        ok = xx_deflate_decompress_memory(data, size, buf, cap, &written, false);
        if (ok) {
            *out = buf;
            *out_size = written;
            return XX_IMAGE_OK;
        }
        free(buf);
        /* Failure with a full buffer means the output did not fit: retry
         * larger.  Any other failure is corrupt input. */
        if (written < cap) return XX_IMAGE_ERR_CORRUPT;
        if (cap >= limit) return XX_IMAGE_ERR_LIMIT;
        cap = cap > limit / 2 ? limit : cap * 2;
    }
}

xx_image_status xx_image_zlib_inflate(const uint8_t *data, size_t size, size_t expected, size_t limit, uint8_t **out, size_t *out_size)
{
    if (!data || size < 2) return XX_IMAGE_ERR_CORRUPT;
    if ((data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) return XX_IMAGE_ERR_CORRUPT;
    if (data[1] & 0x20) return XX_IMAGE_ERR_UNSUPPORTED; /* preset dictionary */
    return xx_image_raw_inflate(data + 2, size - 2, expected, limit, out, out_size);
}

xx_image_status xx_image_zlib_deflate(const uint8_t *data, size_t size, int level, xx_image_buffer *out)
{
    size_t cap, written = 0;
    uint32_t adler;
    if (!out || (!data && size)) return XX_IMAGE_ERR_ARGUMENT;
    if (level < 0 || level > 9) level = 6;
    if (size > (SIZE_MAX - 4096) / 2) return XX_IMAGE_ERR_LIMIT;
    cap = size + size / 8 + 4096;
    if (!xx_image_buf_u8(out, 0x78) || !xx_image_buf_u8(out, level >= 7 ? 0xDA : level >= 2 ? 0x9C : 0x01)) return XX_IMAGE_ERR_MEMORY;
    if (!xx_image_buf_reserve(out, out->size + cap + 4)) return XX_IMAGE_ERR_MEMORY;
    if (!xx_deflate_compress_memory(data, size, out->data + out->size, cap, &written, level, false)) return XX_IMAGE_ERR_MEMORY;
    out->size += written;
    adler = xx_image_adler32(1, data, size);
    return xx_image_buf_u32be(out, adler) ? XX_IMAGE_OK : XX_IMAGE_ERR_MEMORY;
}

/* ---- names -------------------------------------------------------------- */

const char *xx_image_status_string(xx_image_status s)
{
    static const char *const names[XX_IMAGE_ERR_COUNT] = {"ok", "invalid argument", "out of memory", "I/O error", "unknown format", "corrupt data",
                                                       "unsupported feature", "image too large", "no decoder for this format", "this format cannot be saved"};
    return ((unsigned)s < XX_IMAGE_ERR_COUNT) ? names[s] : "unknown error";
}



void xx_image_mem_free(void *p)
{
    free(p);
}

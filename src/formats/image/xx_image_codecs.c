/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Image codec registry: the codec table, detection, load / inspect / save, file
 * I/O (through xxfclib, so UTF-8 paths work everywhere) and the bridge to
 * xxfclib's abstract image interface for header properties.
 */

#include "xxfclib/formats/image/xx_image_codec.h"

#include "xxfclib/formats/image/xx_image_props.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"

#include <stdlib.h>
#include <string.h>

/* The codec table: one XX_IMAGE_CODEC(name) line per codec, defined as
 * `const xx_image_codec xx_image_codec_<name>` in src/codecs. */
#define XX_IMAGE_CODEC(n) extern const xx_image_codec xx_image_codec_##n;
#include "xx_image_codecs.inc"
#undef XX_IMAGE_CODEC

static const xx_image_codec *const g_codecs[] = {
#define XX_IMAGE_CODEC(n) &xx_image_codec_##n,
#include "xx_image_codecs.inc"
#undef XX_IMAGE_CODEC
};

#define XV_CODEC_COUNT (sizeof(g_codecs) / sizeof(g_codecs[0]))

size_t xx_image_codec_count(void)
{
    return XV_CODEC_COUNT;
}

const xx_image_codec *xx_image_codec_at(size_t i)
{
    return i < XV_CODEC_COUNT ? g_codecs[i] : NULL;
}

static char xv_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool xv_list_has(const char *list, const char *name)
{
    size_t n;
    if (!list || !name) return false;
    n = strlen(name);
    while (*list) {
        size_t k = 0;
        while (list[k] && list[k] != ';') ++k;
        if (k == n) {
            size_t i = 0;
            while (i < n && xv_lower(list[i]) == xv_lower(name[i])) ++i;
            if (i == n) return true;
        }
        list += k;
        if (*list == ';') ++list;
    }
    return false;
}

const xx_image_codec *xx_image_codec_find(const char *name)
{
    if (!name) return NULL;
    if (*name == '.') ++name;
    if (!*name) return NULL;
    for (size_t i = 0; i < XV_CODEC_COUNT; ++i)
        if (xv_list_has(g_codecs[i]->id, name)) return g_codecs[i];
    for (size_t i = 0; i < XV_CODEC_COUNT; ++i)
        if (xv_list_has(g_codecs[i]->extensions, name)) return g_codecs[i];
    return NULL;
}

const xx_image_codec *xx_image_codec_for_path(const char *path)
{
    const char *dot = NULL;
    if (!path) return NULL;
    for (const char *p = path; *p; ++p) {
        if (*p == '.') dot = p;
        else if (*p == '/' || *p == '\\') dot = NULL;
    }
    return dot ? xx_image_codec_find(dot + 1) : NULL;
}

const xx_image_codec *xx_image_codec_detect(const void *data, size_t size)
{
    const xx_image_codec *best = NULL;
    int best_score = 0;
    if (!data || !size) return NULL;
    for (size_t i = 0; i < XV_CODEC_COUNT; ++i) {
        const xx_image_codec *c = g_codecs[i];
        int s;
        if (!c->probe || !(c->flags & XX_IMAGE_CODEC_LOAD)) continue;
        s = c->probe((const uint8_t *)data, size);
        if (s > best_score) {
            best_score = s;
            best = c;
        }
    }
    return best;
}

const char *xx_image_codec_id(const xx_image_codec *c) { return c ? c->id : ""; }
const char *xx_image_codec_name(const xx_image_codec *c) { return c ? c->name : ""; }
const char *xx_image_codec_extensions(const xx_image_codec *c) { return c ? c->extensions : ""; }
const char *xx_image_codec_mime_type(const xx_image_codec *c) { return c && c->mime_type ? c->mime_type : ""; }
uint32_t xx_image_codec_flags(const xx_image_codec *c) { return c ? c->flags : 0; }
bool xx_image_codec_can_load(const xx_image_codec *c) { return c && (c->flags & XX_IMAGE_CODEC_LOAD) && c->load; }
bool xx_image_codec_can_save(const xx_image_codec *c) { return c && (c->flags & XX_IMAGE_CODEC_SAVE) && c->save; }

/* ---- xxfclib bridge ----------------------------------------------------- */

static void xv_apply_props(xx_image *img, const xx_image_props *p, bool only_empty, bool add_props)
{
    xx_image_info *in = &img->info;
    const char *ft = xx_format_file_type_to_string(p->file_type);
    if (ft && !in->file_type[0]) XX_IMAGE_SET_STR(in->file_type, ft);
#define XV_U(f)                     \
    if (!only_empty || !in->f) in->f = p->f
    XV_U(bits_per_pixel);
    XV_U(bits_per_sample);
    XV_U(channels);
    XV_U(mipmap_count);
    XV_U(layer_count);
    XV_U(palette_entries);
#undef XV_U
    if (!only_empty || !in->frame_count) in->frame_count = p->frame_count;
    if (!only_empty || in->color_model == XX_IMAGE_COLOR_UNKNOWN) in->color_model = (xx_image_color_model)p->color_model;
    if (!only_empty || !in->dpi_x) in->dpi_x = p->dpi_x;
    if (!only_empty || !in->dpi_y) in->dpi_y = p->dpi_y;
    if (!only_empty) {
        in->has_alpha = p->has_alpha;
        in->is_interlaced = p->is_interlaced;
        in->is_animated = p->is_animated;
        in->is_compressed = p->is_compressed;
        in->is_lossy = p->is_lossy;
    }
    if (!only_empty || !in->compression[0]) XX_IMAGE_SET_STR(in->compression, p->compression);
    if (!only_empty || !in->pixel_format[0]) XX_IMAGE_SET_STR(in->pixel_format, p->pixel_format);
    if (!only_empty || !in->color_space[0]) XX_IMAGE_SET_STR(in->color_space, p->color_space);
    if (!only_empty || !in->version[0]) XX_IMAGE_SET_STR(in->version, p->version);
    if (!img->width) img->width = p->width;
    if (!img->height) img->height = p->height;
    if (add_props) {
        const char *g = "xxfclib";
        xx_image_add_prop_text(img, g, "file type", ft ? ft : "");
        xx_image_add_prop_uint(img, g, "width", p->width);
        xx_image_add_prop_uint(img, g, "height", p->height);
        if (p->depth) xx_image_add_prop_uint(img, g, "depth", p->depth);
        if (p->bits_per_pixel) xx_image_add_prop_uint(img, g, "bits per pixel", p->bits_per_pixel);
        if (p->bits_per_sample) xx_image_add_prop_uint(img, g, "bits per sample", p->bits_per_sample);
        if (p->channels) xx_image_add_prop_uint(img, g, "channels", p->channels);
        xx_image_add_prop_text(img, g, "color model", xx_image_color_model_to_string(p->color_model));
        xx_image_add_prop_bool(img, g, "alpha", p->has_alpha);
        if (p->is_interlaced) xx_image_add_prop_bool(img, g, "interlaced", true);
        if (p->is_animated) xx_image_add_prop_bool(img, g, "animated", true);
        if (p->frame_count) xx_image_add_prop_uint(img, g, "frames", p->frame_count);
        if (p->mipmap_count) xx_image_add_prop_uint(img, g, "mipmaps", p->mipmap_count);
        if (p->layer_count) xx_image_add_prop_uint(img, g, "layers", p->layer_count);
        if (p->palette_entries) xx_image_add_prop_uint(img, g, "palette entries", p->palette_entries);
        if (p->dpi_x) xx_image_add_prop_real(img, g, "dpi x", p->dpi_x);
        if (p->dpi_y) xx_image_add_prop_real(img, g, "dpi y", p->dpi_y);
        if (p->compression[0]) xx_image_add_prop_text(img, g, "compression", p->compression);
        if (p->pixel_format[0]) xx_image_add_prop_text(img, g, "pixel format", p->pixel_format);
        if (p->color_space[0]) xx_image_add_prop_text(img, g, "color space", p->color_space);
        if (p->version[0]) xx_image_add_prop_text(img, g, "version", p->version);
        if (p->pixel_data_offset >= 0) xx_image_add_prop_int(img, g, "pixel data offset", p->pixel_data_offset);
        if (p->pixel_data_size >= 0) xx_image_add_prop_int(img, g, "pixel data size", p->pixel_data_size);
    }
}

/* Header properties from xxfclib; true when a reader matched. */
static bool xv_xxfclib_props(const void *data, size_t size, int file_type, xx_image *img, bool only_empty, bool add_props)
{
    xx_image_props p;
    bool ok = false;
    xx_image_props_init(&p);
    if (file_type) ok = xx_image_props_read_memory(data, size, (xx_file_type_t)file_type, &p);
    if (!ok) ok = xx_image_props_read_memory(data, size, XX_FILE_TYPE_UNKNOWN, &p);
    if (!ok) {
        /* Still name the type when xxfclib knows it but has no reader. */
        xx_io_device *dev = xx_io_mem_open_ro(data, size);
        if (dev) {
            xx_file_type_t t = xx_format_get_file_type_device(dev);
            const xx_image_interface *row = xx_image_interface_get(t);
            const char *ft = xx_format_file_type_to_string(t);
            /* only image formats count: other types are "unknown format" here */
            if (row && ft && !img->info.file_type[0]) XX_IMAGE_SET_STR(img->info.file_type, ft);
            if (row && !img->info.format_name[0]) XX_IMAGE_SET_STR(img->info.format_name, row->name);
            xx_io_close(dev);
        }
        return false;
    }
    {
        const xx_image_interface *row = xx_image_interface_get(p.file_type);
        if (row && !img->info.format_name[0]) XX_IMAGE_SET_STR(img->info.format_name, row->name);
    }
    xv_apply_props(img, &p, only_empty, add_props);
    return true;
}

/* ---- load / inspect ----------------------------------------------------- */

xx_image_status xx_image_load_memory(const void *data, size_t size, const xx_image_codec *codec, const xx_image_load_options *options, xx_image *img)
{
    xx_image_load_options defaults;
    xx_image_status st;
    if (!img) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(img);
    if (!data || !size) return XX_IMAGE_ERR_ARGUMENT;
    if (!options) {
        xx_image_load_options_init(&defaults);
        options = &defaults;
    }
    if (!codec) codec = xx_image_codec_detect(data, size);
    if (!codec || !xx_image_codec_can_load(codec)) {
        /* Not decodable: describe it if xxfclib knows the format. */
        bool known = xv_xxfclib_props(data, size, codec ? codec->file_type : 0, img, false, !options->skip_metadata);
        if (codec) {
            XX_IMAGE_SET_STR(img->info.codec, codec->id);
            if (!img->info.format_name[0]) XX_IMAGE_SET_STR(img->info.format_name, codec->name);
        }
        return (known || codec || img->info.file_type[0]) ? XX_IMAGE_ERR_NO_DECODER : XX_IMAGE_ERR_FORMAT;
    }
    XX_IMAGE_SET_STR(img->info.codec, codec->id);
    XX_IMAGE_SET_STR(img->info.format_name, codec->name);
    st = codec->load((const uint8_t *)data, size, options, img);
    if (st != XX_IMAGE_OK) return st;
    if (!img->frame_count) return XX_IMAGE_ERR_CORRUPT;
    if (!img->width || !img->height) {
        img->width = img->frames[0].width;
        img->height = img->frames[0].height;
    }
    if (!img->info.frame_count) img->info.frame_count = (uint32_t)img->frame_count;
    /* Fill what the codec left empty from xxfclib's header reader. */
    xv_xxfclib_props(data, size, codec->file_type, img, true, !options->skip_metadata);
    if (options->keep_source) {
        img->source = (uint8_t *)malloc(size);
        if (!img->source) return XX_IMAGE_ERR_MEMORY;
        memcpy(img->source, data, size);
        img->source_size = size;
    }
    return XX_IMAGE_OK;
}

xx_image_status xx_image_inspect_memory(const void *data, size_t size, xx_image *img)
{
    const xx_image_codec *codec;
    if (!img) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(img);
    if (!data || !size) return XX_IMAGE_ERR_ARGUMENT;
    codec = xx_image_codec_detect(data, size);
    if (codec) {
        XX_IMAGE_SET_STR(img->info.codec, codec->id);
        XX_IMAGE_SET_STR(img->info.format_name, codec->name);
    }
    if (xv_xxfclib_props(data, size, codec ? codec->file_type : 0, img, false, true)) return XX_IMAGE_OK;
    if (codec) {
        /* No header reader: decode once and keep only the description. */
        xx_image full;
        xx_image_load_options o;
        xx_image_load_options_init(&o);
        o.first_frame_only = true;
        if (xx_image_load_memory(data, size, codec, &o, &full) == XX_IMAGE_OK) {
            xx_image_free(img);
            *img = full;
            for (size_t i = 0; i < img->frame_count; ++i) free(img->frames[i].rgba);
            free(img->frames);
            img->frames = NULL;
            img->frame_count = img->frame_capacity = 0;
            return XX_IMAGE_OK;
        }
        xx_image_free(&full);
        return XX_IMAGE_ERR_CORRUPT;
    }
    return img->info.file_type[0] ? XX_IMAGE_OK : XX_IMAGE_ERR_FORMAT;
}

static xx_image_status xv_read_file(const char *path, uint8_t **data, size_t *size)
{
    xx_io_device *dev;
    int64_t n;
    *data = NULL;
    *size = 0;
    if (!path) return XX_IMAGE_ERR_ARGUMENT;
    dev = xx_io_file_open(path, "rb");
    if (!dev) return XX_IMAGE_ERR_IO;
    n = xx_io_size(dev);
    if (n <= 0 || (uint64_t)n > (uint64_t)SIZE_MAX / 2) {
        xx_io_close(dev);
        return n == 0 ? XX_IMAGE_ERR_FORMAT : XX_IMAGE_ERR_IO;
    }
    *data = (uint8_t *)malloc((size_t)n);
    if (!*data) {
        xx_io_close(dev);
        return XX_IMAGE_ERR_MEMORY;
    }
    if (!xx_io_read_at(dev, 0, *data, (size_t)n)) {
        free(*data);
        *data = NULL;
        xx_io_close(dev);
        return XX_IMAGE_ERR_IO;
    }
    xx_io_close(dev);
    *size = (size_t)n;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_load_file(const char *path, const xx_image_codec *codec, const xx_image_load_options *options, xx_image *img)
{
    uint8_t *data;
    size_t size;
    xx_image_status st;
    if (!img) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(img);
    st = xv_read_file(path, &data, &size);
    if (st != XX_IMAGE_OK) return st;
    st = xx_image_load_memory(data, size, codec, options, img);
    free(data);
    return st;
}

xx_image_status xx_image_inspect_file(const char *path, xx_image *img)
{
    uint8_t *data;
    size_t size;
    xx_image_status st;
    if (!img) return XX_IMAGE_ERR_ARGUMENT;
    xx_image_init(img);
    st = xv_read_file(path, &data, &size);
    if (st != XX_IMAGE_OK) return st;
    st = xx_image_inspect_memory(data, size, img);
    free(data);
    return st;
}

/* ---- save --------------------------------------------------------------- */

xx_image_status xx_image_save_memory(const xx_image *img, const xx_image_codec *codec, const xx_image_save_options *options, uint8_t **data, size_t *size)
{
    xx_image_save_options defaults;
    xx_image_buffer out;
    xx_image_status st;
    if (!data || !size) return XX_IMAGE_ERR_ARGUMENT;
    *data = NULL;
    *size = 0;
    if (!img || !codec) return XX_IMAGE_ERR_ARGUMENT;
    if (!xx_image_codec_can_save(codec)) return XX_IMAGE_ERR_NO_ENCODER;
    if (!img->frame_count) return XX_IMAGE_ERR_ARGUMENT;
    if (!options) {
        xx_image_save_options_init(&defaults);
        options = &defaults;
    }
    memset(&out, 0, sizeof(out));
    st = codec->save(img, options, &out);
    if (st == XX_IMAGE_OK && out.failed) st = XX_IMAGE_ERR_MEMORY;
    if (st != XX_IMAGE_OK) {
        xx_image_buf_free(&out);
        return st;
    }
    *data = out.data;
    *size = out.size;
    return XX_IMAGE_OK;
}

xx_image_status xx_image_save_file(const xx_image *img, const char *path, const xx_image_codec *codec, const xx_image_save_options *options)
{
    uint8_t *data;
    size_t size;
    xx_image_status st;
    xx_io_device *dev;
    bool ok;
    if (!path) return XX_IMAGE_ERR_ARGUMENT;
    if (!codec) codec = xx_image_codec_for_path(path);
    if (!codec) return XX_IMAGE_ERR_FORMAT;
    st = xx_image_save_memory(img, codec, options, &data, &size);
    if (st != XX_IMAGE_OK) return st;
    dev = xx_io_file_open(path, "wb");
    if (!dev) {
        free(data);
        return XX_IMAGE_ERR_IO;
    }
    ok = xx_io_write(dev, data, size) == (ssize_t)size;
    if (xx_io_close(dev) != 0) ok = false;
    free(data);
    return ok ? XX_IMAGE_OK : XX_IMAGE_ERR_IO;
}

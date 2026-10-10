/* SPDX-License-Identifier: MIT
 * Primary reference: https://doc.mapeditor.org/en/stable/reference/tmx-map-format/
 * Tiled TMX1.x finite orthogonal maps: complete UTF8 XML without DTD or external entities, embedded rectangular image tilesets with declared tilecount/columns, image geometry and sorted nonoverlapping GID ranges, unique layer IDs and exact CSV or uncompressed strict-base64 matrix counts/GIDs. Original XML and decoded LE32 tile arrays exported. External image paths retained as metadata only. Infinite/chunked maps, external TSX, compressed data, property/object/group/custom tile sections and other orientations declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/tiled_tmx/xx_tiled_tmx.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
#include "xxfclib/xml/xx_xml.h"
static bool graphics_text_quick(Abstractformat *f, uint64_t n) {
    uint8_t c;
    return n >= 8 && pm_read(f, 0, &c, 1) && (c == '<' || c == 9 || c == 10 || c == 13 || c == 32);
}
static bool tm_uint(const char *s, uint32_t max, uint32_t *v) {
    uint32_t n = 0;
    unsigned i = 0;
    if (!s || !*s)
        return false;
    while (s[i]) {
        unsigned c = (unsigned char)s[i++];
        if (c < '0' || c > '9' || n > (max - (c - '0')) / 10 || c - '0' > max)
            return false;
        n = n * 10 + c - '0';
    }
    *v = n;
    return true;
}
static bool tm_attr(xx_xml *x, const char *key, uint32_t max, uint32_t *v, bool required) {
    const char *s = xx_xml_attribute_value(x, key);
    return s ? tm_uint(s, max, v) : !required;
}
static bool tm_attrs(xx_xml *x, const char *allowed) {
    size_t i, j;
    for (i = 0; i < x->attribute_count; ++i) {
        const char *name = x->attributes[i].name, *a = allowed;
        bool found = false;
        size_t z = xx_rt_strlen(name);
        if (i >= 32 || xx_rt_strlen(x->attributes[i].value) > 4096)
            return false;
        for (j = 0; j < i; ++j)
            if (!xx_rt_strcmp(name, x->attributes[j].name))
                return false;
        while (*a) {
            const char *e = a;
            while (*e && *e != '|')
                ++e;
            if ((size_t)(e - a) == z && !xx_rt_memcmp(a, name, z))
                found = true;
            a = *e ? e + 1 : e;
        }
        if (!found)
            return false;
    }
    return true;
}
static bool tm_white(const char *s) {
    while (*s) {
        if (*s != 9 && *s != 10 && *s != 13 && *s != 32)
            return false;
        ++s;
    }
    return true;
}
static bool tm_realattr(xx_xml *x, const char *key, double low, double high) {
    const char *str = xx_xml_attribute_value(x, key);
    component_text_cursor q;
    double v;
    if (!str)
        return true;
    xx_mem_zero(&q, sizeof(q));
    q.b = (const uint8_t *)str;
    q.stop = xx_rt_strlen(str);
    return component_text_number_36_digits(&q, &v) && q.t == q.stop && v >= low && v <= high;
}
static bool tm_version(const char *s) {
    unsigned i = 2;
    if (!s || s[0] != '1' || s[1] != '.' || s[2] < '0' || s[2] > '9')
        return false;
    while (s[i] >= '0' && s[i] <= '9')
        if (++i > 8)
            return false;
    return !s[i];
}
static bool tm_declaration(const char *str) {
    component_text_cursor q;
    xx_mem_zero(&q, sizeof(q));
    q.b = (const uint8_t *)str;
    q.stop = xx_rt_strlen(str);
    if (!component_text_word(&q, "xml") ||
        (!component_text_word(&q, "version=\"1.0\"") && !component_text_word(&q, "version='1.0'")))
        return false;
    if (component_text_done(&q))
        return true;
    if (!component_text_word(&q, "encoding=\"UTF-8\"") && !component_text_word(&q, "encoding='UTF-8'"))
        return false;
    return component_text_done(&q);
}
static bool tm_matrix(const char *str, bool base64, uint8_t *out, uint32_t count, xx_pd_struct *pd) {
    uint64_t p = 0;
    uint32_t k = 0;
    if (!base64) {
        while (k < count) {
            uint32_t v = 0;
            unsigned digits = 0;
            while (str[p] == 9 || str[p] == 10 || str[p] == 13 || str[p] == 32)
                ++p;
            while (str[p] >= '0' && str[p] <= '9') {
                unsigned c = str[p++] - '0';
                if (++digits > 10 || v > (0xffffffffU - c) / 10)
                    return false;
                v = v * 10 + c;
            }
            if (!digits || xx_component_parser_stopped(pd))
                return false;
            out[k * 4] = (uint8_t)v;
            out[k * 4 + 1] = (uint8_t)(v >> 8);
            out[k * 4 + 2] = (uint8_t)(v >> 16);
            out[k * 4 + 3] = (uint8_t)(v >> 24);
            ++k;
            while (str[p] == 9 || str[p] == 10 || str[p] == 13 || str[p] == 32)
                ++p;
            if (k < count) {
                if (str[p++] != ',')
                    return false;
            }
        }
        while (str[p] == 9 || str[p] == 10 || str[p] == 13 || str[p] == 32)
            ++p;
        return !str[p];
    } else {
        unsigned group = 0, padding = 0;
        uint32_t bits = 0;
        bool ended = false;
        while (str[p]) {
            unsigned char c = (unsigned char)str[p++];
            unsigned v;
            if (c == 9 || c == 10 || c == 13 || c == 32)
                continue;
            if (ended || xx_component_parser_stopped(pd))
                return false;
            if (c == '=') {
                if (group < 2 || ++padding > 2)
                    return false;
                v = 0;
            } else {
                if (padding)
                    return false;
                v = c >= 'A' && c <= 'Z'   ? c - 'A'
                    : c >= 'a' && c <= 'z' ? c - 'a' + 26
                    : c >= '0' && c <= '9' ? c - '0' + 52
                    : c == '+'             ? 62
                    : c == '/'             ? 63
                                           : 64;
                if (v > 63)
                    return false;
            }
            bits = (bits << 6) | v;
            if (++group == 4) {
                unsigned bytes = 3 - padding, j;
                if ((padding == 1 && (bits & 255)) || (padding == 2 && (bits & 65535)))
                    return false;
                if (bytes > count * 4 - k)
                    return false;
                for (j = 0; j < bytes; ++j)
                    out[k++] = (uint8_t)(bits >> (16 - j * 8));
                if (padding)
                    ended = true;
                group = padding = bits = 0;
            }
        }
        return !group && k == count * 4;
    }
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    xx_xml x;
    uint32_t width = 0, height = 0, tw = 0, th = 0, layers = 0, sets = 0, total = 0, first[256], last[256], ids[1024],
             iw = 0, ih = 0, columns = 0, tiles = 0, tsw = 0, tsh = 0, lw = 0, lh = 0;
    unsigned state = 0;
    bool done = false, image = false, data = false, base64 = false, result = false, decl = false;
    uint8_t *pixels = NULL;
    if (!component_utf8(b, n, false, pd) || !component_emit(f, s, "document.tmx", 0, n, n)) {
        return false;
    }
    xx_xml_init(&x, b, (size_t)n);
#define TM(v)                                                                                                          \
    do {                                                                                                               \
        if (!(v))                                                                                                      \
            goto finish;                                                                                               \
    } while (0)
    while (xx_xml_next(&x)) {
        const char *name = x.name;
        uint32_t v = 0;
        TM(!xx_component_parser_stopped(pd) && x.depth <= 4);
        if (x.type == XX_XML_COMMENT)
            continue;
        if (x.type == XX_XML_PI) {
            TM(!state && !done && !decl && x.text && xx_rt_strlen(x.text) < 128 && tm_declaration(x.text));
            decl = true;
            continue;
        }
        if (x.type == XX_XML_TEXT) {
            TM(x.text);
            if (state == 4 && !tm_white(x.text)) {
                uint32_t i;
                TM(!data && (uint64_t)lw * lh <= 4194304 && total + (uint64_t)lw * lh * 4 <= 16777216);
                pixels = (uint8_t *)xx_mem_alloc((size_t)lw * lh * 4);
                TM(pixels && tm_matrix(x.text, base64, pixels, lw * lh, pd));
                for (i = 0; i < lw * lh; ++i) {
                    unsigned j;
                    uint32_t gid = xx_data_get_u32(pixels + i * 4, 4, 0, false);
                    TM(!(gid & 0x10000000U));
                    gid &= 0x0fffffffU;
                    if (!gid)
                        continue;
                    for (j = 0; j < sets; ++j)
                        if (gid >= first[j] && gid < last[j])
                            break;
                    TM(j < sets);
                }
                TM(pm_add(f, s, "tiles.le32", 0, 0));
                s->items[s->count - 1].memory = pixels;
                s->items[s->count - 1].size = (int64_t)lw * lh * 4;
                s->items[s->count - 1].packed_size = 0;
                pixels = NULL;
                total += lw * lh * 4;
                data = true;
            } else
                TM(tm_white(x.text));
            continue;
        }
        if (x.type == XX_XML_END) {
            TM(name);
            if (state == 4) {
                TM(!xx_rt_strcmp(name, "data") && data);
                state = 3;
            } else if (state == 3) {
                TM(!xx_rt_strcmp(name, "layer") && data);
                state = 1;
            } else if (state == 2) {
                TM(!xx_rt_strcmp(name, "tileset") && image);
                state = 1;
            } else if (state == 1) {
                TM(!xx_rt_strcmp(name, "map") && layers && sets);
                state = 0;
                done = true;
            } else
                goto finish;
            continue;
        }
        TM(x.type == XX_XML_START && name && !done);
        if (!state) {
            TM(!xx_rt_strcmp(name, "map") && !x.self_closing &&
               tm_attrs(
                   &x,
                   "version|tiledversion|orientation|renderorder|width|height|tilewidth|tileheight|infinite|nextlayerid|nextobjectid") &&
               tm_version(xx_xml_attribute_value(&x, "version")));
            TM(xx_xml_attribute_value(&x, "orientation") &&
               !xx_rt_strcmp(xx_xml_attribute_value(&x, "orientation"), "orthogonal") &&
               tm_attr(&x, "width", 65535, &width, true) && width && tm_attr(&x, "height", 65535, &height, true) &&
               height && tm_attr(&x, "tilewidth", 65535, &tw, true) && tw &&
               tm_attr(&x, "tileheight", 65535, &th, true) && th && tm_attr(&x, "infinite", 0, &v, false) &&
               tm_attr(&x, "nextlayerid", 0x7fffffff, &v, false) && tm_attr(&x, "nextobjectid", 0x7fffffff, &v, false));
            {
                const char *order = xx_xml_attribute_value(&x, "renderorder");
                TM(!order || !xx_rt_strcmp(order, "right-down") || !xx_rt_strcmp(order, "right-up") ||
                   !xx_rt_strcmp(order, "left-down") || !xx_rt_strcmp(order, "left-up"));
            }
            state = 1;
        } else if (state == 1 && !xx_rt_strcmp(name, "tileset")) {
            TM(!layers && sets < 256 && !x.self_closing &&
               tm_attrs(&x, "firstgid|name|tilewidth|tileheight|tilecount|columns|spacing|margin") &&
               tm_attr(&x, "firstgid", 0x0fffffff, &v, true) && v && (!sets || v >= last[sets - 1]));
            first[sets] = v;
            columns = tiles = 0;
            TM(tm_attr(&x, "tilewidth", 65535, &tsw, true) && tsw && tm_attr(&x, "tileheight", 65535, &tsh, true) &&
               tsh && tm_attr(&x, "tilecount", 4194304, &tiles, true) && tiles &&
               tm_attr(&x, "columns", 65535, &columns, true) && columns && tm_attr(&x, "spacing", 0, &v, false) &&
               tm_attr(&x, "margin", 0, &v, false));
            last[sets] = first[sets] + tiles;
            TM(last[sets] > first[sets] && last[sets] <= 0x10000000U);
            ++sets;
            image = false;
            state = 2;
        } else if (state == 2 && !xx_rt_strcmp(name, "image")) {
            const char *source = xx_xml_attribute_value(&x, "source"), *trans = xx_xml_attribute_value(&x, "trans");
            unsigned i;
            TM(!image && x.self_closing && tm_attrs(&x, "source|width|height|trans") && source && *source &&
               tm_attr(&x, "width", 65535, &iw, true) && iw && tm_attr(&x, "height", 65535, &ih, true) && ih &&
               columns == iw / tsw && (uint64_t)columns * (ih / tsh) >= tiles);
            if (trans) {
                TM(xx_rt_strlen(trans) == 6);
                for (i = 0; i < 6; ++i)
                    TM((trans[i] >= '0' && trans[i] <= '9') || (trans[i] >= 'a' && trans[i] <= 'f') ||
                       (trans[i] >= 'A' && trans[i] <= 'F'));
            }
            image = true;
        } else if (state == 1 && !xx_rt_strcmp(name, "layer")) {
            unsigned i;
            v = 0;
            TM(sets && layers < 1024 && !x.self_closing &&
               tm_attrs(&x, "id|name|width|height|opacity|visible|offsetx|offsety") &&
               tm_attr(&x, "id", 0x7fffffff, &v, true) && v);
            for (i = 0; i < layers; ++i)
                TM(ids[i] != v);
            ids[layers++] = v;
            TM(tm_attr(&x, "width", 65535, &lw, true) && lw == width && tm_attr(&x, "height", 65535, &lh, true) &&
               lh == height && tm_realattr(&x, "opacity", 0, 1) && tm_realattr(&x, "offsetx", -100000000, 100000000) &&
               tm_realattr(&x, "offsety", -100000000, 100000000));
            v = 1;
            TM(tm_attr(&x, "visible", 1, &v, false));
            state = 3;
            data = false;
        } else if (state == 3 && !xx_rt_strcmp(name, "data")) {
            const char *encoding = xx_xml_attribute_value(&x, "encoding");
            TM(!data && !x.self_closing && tm_attrs(&x, "encoding") && encoding);
            if (!xx_rt_strcmp(encoding, "csv"))
                base64 = false;
            else if (!xx_rt_strcmp(encoding, "base64"))
                base64 = true;
            else
                goto finish;
            state = 4;
        } else
            goto finish;
    }
    TM(done && !state && !xx_xml_failed(&x) && x.position == n);
    s->size = (int64_t)n;
    result = true;
finish:
    xx_mem_free(pixels);
    xx_xml_cleanup(&x);
    return result;
#undef TM
}

void xx_tiled_tmx_init(xx_tiled_tmx *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_TILED_TMX, "tmx");
    }
}
xx_tiled_tmx *xx_tiled_tmx_create(xx_io_device *d, int64_t at) {
    xx_tiled_tmx *r = (xx_tiled_tmx *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_tiled_tmx_init(r, d, at);
    return r;
}
void xx_tiled_tmx_destroy(xx_tiled_tmx *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tiled_tmx_free(xx_tiled_tmx *r) {
    if (r) {
        xx_tiled_tmx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tiled_tmx_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_tiled_tmx_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }

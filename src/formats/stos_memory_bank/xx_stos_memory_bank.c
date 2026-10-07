/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * STOS Basic memory bank (.MBK).  xx_stos_memory_bank.h carries the field
 * table.  Sprites and icons are rendered to BMP, every other bank is
 * published verbatim.
 *
 * The bank layout and the pixel semantics (sprite parameter blocks, palette
 * position, mask polarity, icon layout and colour choice, the 9/12-bit
 * Atari ST palette guess and its scaling) follow Deark's stos module,
 * deark-1.7.3/modules/mbk.c and fmtutil.c (fmtutil_read_atari_palette),
 * Copyright (C) 2016-2026 Jason Summers, MIT License.  The container walk,
 * the bounds checks and the BMP writer are this file's own.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stos_memory_bank/xx_stos_memory_bank.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator (and its alias macro) exists in xxfc_defs.h. */
#ifdef STOS_MEMORY_BANK
#define XX_STOS_MEMORY_BANK_FILE_TYPE XX_FILE_TYPE_STOS_MEMORY_BANK
#else
#define XX_STOS_MEMORY_BANK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define STOS_HEADER 18
#define STOS_SIG_SIZE 10
#define STOS_MAX_BANK 15U

#define STOS_ID_SPRITE 0x19861987U
#define STOS_ID_ICON 0x28091960U
#define STOS_ID_PKSCREEN 0x06071963U
#define STOS_ID_MUSIC 0x13490157U
#define STOS_ID_MAESTRO 0x4D414553U
#define STOS_ID_PALT 0x50414C54U /* "PALT" */

#define STOS_SPRITE_HEADER 22 /* id, 3 offsets, 3 counts */
#define STOS_PARAM_BLOCK 8
#define STOS_PALETTE_BLOCK 36 /* "PALT" + 16 words */
#define STOS_ICON_SIZE 84
#define STOS_ICON_BITS 10

#define STOS_KIND_SPRITE 1
#define STOS_KIND_ICON 2
#define STOS_KIND_RAW 3

#define BMP_FILE_HEADER 14
#define BMP_V4_HEADER 108

#define STOS_NAME_MAX 40

typedef struct stos_item_s {
    int kind;
    uint32_t bpp;         /**< Image planes (sprites: 4, 2, 1). */
    int64_t width;        /**< Pixels. */
    int64_t height;
    int64_t words;        /**< Width in 16-pixel words (sprites). */
    int64_t data_offset;  /**< Absolute: sprite mask, icon start, raw start. */
    int64_t data_size;    /**< Bytes of the file the member covers. */
    int64_t output_size;
    uint32_t fg_white_is_zero; /**< Icons: image bit set = black. */
    char name[STOS_NAME_MAX];
} stos_item;

typedef struct stos_context_s {
    int64_t start;        /**< Absolute offset of the format. */
    int64_t format_size;
    uint32_t bank_number;
    uint32_t bank_type;
    uint32_t bank_id;
    uint32_t pal[16];     /**< 0xRRGGBB. */
    size_t count;
    stos_item *items;
} stos_context;

typedef struct stos_stream_s {
    stos_context context;
    size_t index;
} stos_stream;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static uint32_t stos_be16(const uint8_t *b) {
    return ((uint32_t)b[0] << 8U) | (uint32_t)b[1];
}

static bool stos_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool stos_write(xx_io_device *device, const void *data, size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verification run */
    while (done < size) {
        ssize_t wrote = xx_io_write(device, (const uint8_t *)data + done,
                                    size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static void stos_context_free(stos_context *context) {
    if (!context) return;
    if (context->items) xx_mem_free(context->items);
    context->items = NULL;
    context->count = 0U;
}

/* ---------------------------------------------------------------------- */
/* Palette (ported from Deark fmtutil_read_atari_palette, MIT)             */

static uint32_t stos_scale7(uint32_t x) { return (x * 510U + 7U) / 14U; }

static void stos_read_palette(const uint8_t *words, uint32_t *pal) {
    bool bit3 = false, nibble3 = false, twelve;
    uint32_t i;
    for (i = 0U; i < 16U; ++i) {
        uint32_t n = stos_be16(words + 2U * i);
        if (n & 0xF000U) nibble3 = true;
        if (n & 0x0888U) bit3 = true;
    }
    twelve = bit3 && !nibble3;
    for (i = 0U; i < 16U; ++i) {
        uint32_t n = stos_be16(words + 2U * i), r, g, b;
        if (twelve) {
            r = ((n >> 7U) & 0x0EU) | ((n & 0x800U) ? 1U : 0U);
            g = ((n >> 3U) & 0x0EU) | ((n & 0x080U) ? 1U : 0U);
            b = ((n << 1U) & 0x0EU) | ((n & 0x008U) ? 1U : 0U);
            r *= 17U;
            g *= 17U;
            b *= 17U;
        } else {
            r = stos_scale7((n >> 8U) & 7U);
            g = stos_scale7((n >> 4U) & 7U);
            b = stos_scale7(n & 7U);
        }
        pal[i] = (r << 16U) | (g << 8U) | b;
    }
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */

static const char *stos_kind_name(uint32_t type, uint32_t id, bool has_id) {
    if (type == 0x81U && has_id) {
        switch (id) {
        case STOS_ID_SPRITE: return "sprites";
        case STOS_ID_ICON: return "icons";
        case STOS_ID_PKSCREEN: return "packed_screen";
        case STOS_ID_MUSIC: return "music";
        case STOS_ID_MAESTRO: return "maestro";
        default: return "data";
        }
    }
    switch (type) {
    case 0x01U: return "work";
    case 0x02U: return "screen";
    case 0x81U: return "data";
    case 0x82U: return "datascreen";
    case 0x84U: return "set";
    case 0x85U: return "packed_files";
    default: return "bank";
    }
}

static int64_t stos_bmp_size(int64_t w, int64_t h) {
    return BMP_FILE_HEADER + BMP_V4_HEADER + w * 4 * h;
}

/* Next member slot: NULL on a counting run (@p full false). */
static stos_item *stos_next_item(stos_context *c, bool full) {
    if (!full) {
        ++c->count;
        return NULL;
    }
    return &c->items[c->count++];
}

static const char *const stos_res_name[3] = {"low", "med", "high"};
static const uint32_t stos_res_bpp[3] = {4U, 2U, 1U};

/* Sprite bank at @p bank (absolute).  @p strict (bare bank): every declared
 * sprite and the palette must be present.  Returns false when no sprite
 * can be placed; *end receives the furthest byte the bank uses. */
static bool stos_parse_sprites(xx_io_device *dev, stos_context *c,
                               int64_t bank, int64_t limit, bool strict,
                               bool full, int64_t *end) {
    uint8_t h[STOS_SPRITE_HEADER];
    uint8_t *blocks = NULL;
    uint8_t palblk[STOS_PALETTE_BLOCK];
    uint32_t offs[3], counts[3], total = 0U, r;
    int64_t pal_pos, furthest = bank + STOS_SPRITE_HEADER;
    size_t first = c->count;
    if (limit - bank < STOS_SPRITE_HEADER ||
        !stos_read_at(dev, bank, h, sizeof(h)) ||
        xx_data_get_u32(h, 4, 0, true) != STOS_ID_SPRITE)
        return false;
    for (r = 0U; r < 3U; ++r) {
        offs[r] = xx_data_get_u32(h + 4U + 4U * r, 4, 0, true);
        counts[r] = stos_be16(h + 16U + 2U * r);
        total += counts[r];
    }
    if (total == 0U) return false;
    /* Palette default when "PALT" is missing: entry 0 white, rest black. */
    xx_mem_zero(c->pal, sizeof(c->pal));
    c->pal[0] = 0xFFFFFFU;
    pal_pos = bank + STOS_SPRITE_HEADER + (int64_t)total * STOS_PARAM_BLOCK;
    if (pal_pos <= limit - STOS_PALETTE_BLOCK &&
        stos_read_at(dev, pal_pos, palblk, sizeof(palblk)) &&
        xx_data_get_u32(palblk, 4, 0, true) == STOS_ID_PALT) {
        stos_read_palette(palblk + 4, c->pal);
        if (pal_pos + STOS_PALETTE_BLOCK > furthest)
            furthest = pal_pos + STOS_PALETTE_BLOCK;
    } else if (strict) {
        return false;
    }
    for (r = 0U; r < 3U; ++r) {
        int64_t block = bank + 4 + (int64_t)offs[r];
        uint32_t k;
        if (counts[r] == 0U) continue;
        if (block < bank + STOS_SPRITE_HEADER ||
            block > limit - (int64_t)counts[r] * STOS_PARAM_BLOCK) {
            if (strict) return false;
            continue;
        }
        if (block + (int64_t)counts[r] * STOS_PARAM_BLOCK > furthest)
            furthest = block + (int64_t)counts[r] * STOS_PARAM_BLOCK;
        /* One read per resolution: at most 65535 x 8 bytes. */
        blocks = (uint8_t *)xx_mem_alloc((size_t)counts[r] * STOS_PARAM_BLOCK);
        if (!blocks ||
            !stos_read_at(dev, block, blocks,
                          (size_t)counts[r] * STOS_PARAM_BLOCK)) {
            if (blocks) xx_mem_free(blocks);
            return false;
        }
        for (k = 0U; k < counts[r]; ++k) {
            const uint8_t *pb = blocks + (size_t)k * STOS_PARAM_BLOCK;
            int64_t words, height, mask_off, size;
            stos_item *it;
            words = pb[4];
            height = pb[5];
            mask_off = block + (int64_t)xx_data_get_u32(pb, 4, 0, true);
            size = words * 2 * height * (1 + (int64_t)stos_res_bpp[r]);
            if (words == 0 || height == 0) continue; /* Deark skips these */
            if (mask_off < bank || mask_off > limit || size > limit - mask_off) {
                if (strict) {
                    xx_mem_free(blocks);
                    return false;
                }
                continue;
            }
            if (c->count >= XX_STOS_MEMORY_BANK_MAX_RECORDS) {
                xx_mem_free(blocks);
                return false;
            }
            if (mask_off + size > furthest) furthest = mask_off + size;
            it = stos_next_item(c, full);
            if (full) {
                xx_mem_zero(it, sizeof(*it));
                it->kind = STOS_KIND_SPRITE;
                it->bpp = stos_res_bpp[r];
                it->words = words;
                it->width = words * 16;
                it->height = height;
                it->data_offset = mask_off;
                it->data_size = size;
                it->output_size = stos_bmp_size(it->width, it->height);
                xx_rt_snprintf(it->name, sizeof(it->name), "sprite_%s_%03u.bmp",
                               stos_res_name[r], (unsigned)k);
            }
        }
        xx_mem_free(blocks);
        blocks = NULL;
    }
    if (c->count == first) return false;
    *end = furthest;
    return true;
}

static bool stos_parse_icons(xx_io_device *dev, stos_context *c, int64_t bank,
                             int64_t limit, bool full, int64_t *end) {
    uint8_t h[6];
    uint8_t icon[STOS_ICON_BITS];
    uint32_t n, k;
    if (limit - bank < 6 || !stos_read_at(dev, bank, h, sizeof(h))) return false;
    n = stos_be16(h + 4);
    if (n == 0U || (int64_t)n * STOS_ICON_SIZE > limit - bank - 6) return false;
    for (k = 0U; k < n; ++k) {
        int64_t at = bank + 6 + (int64_t)k * STOS_ICON_SIZE;
        stos_item *it;
        uint32_t bg, fg;
        it = stos_next_item(c, full);
        if (full) {
            /* The colour words are only needed to plan the member. */
            if (!stos_read_at(dev, at, icon, sizeof(icon))) return false;
            bg = stos_be16(icon + 6);
            fg = stos_be16(icon + 8);
            xx_mem_zero(it, sizeof(*it));
            it->kind = STOS_KIND_ICON;
            it->bpp = 1U;
            it->width = 16;
            it->height = 16;
            it->data_offset = at;
            it->data_size = STOS_ICON_SIZE;
            it->output_size = stos_bmp_size(16, 16);
            it->fg_white_is_zero = (fg == 0U && bg != 0U) ? 0U : 1U;
            xx_rt_snprintf(it->name, sizeof(it->name), "icon_%03u.bmp",
                           (unsigned)k);
        }
    }
    *end = bank + 6 + (int64_t)n * STOS_ICON_SIZE;
    return true;
}

/* Parse the bank.  @p full also plans every member; without it only the
 * structure is checked and the members are counted. */
static bool stos_parse(Abstractformat *format, stos_context *out, bool full) {
    static const uint8_t sig[STOS_SIG_SIZE] = {'L', 'i', 'o', 'n', 'p',
                                               'o', 'u', 'b', 'n', 'k'};
    uint8_t head[STOS_HEADER + 4];
    stos_context c;
    xx_io_device *dev;
    int64_t total, limit, start, end = 0;
    bool container, decoded = false;
    size_t capacity = 1U;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    xx_mem_zero(out, sizeof(*out));
    dev = format->device;
    total = xx_io_total_size(dev);
    start = format->base_address;
    if (total < start) return false;
    limit = total;
    if (limit - start < STOS_SPRITE_HEADER) return false;
    if (!stos_read_at(dev, start, head, sizeof(head))) return false;
    xx_mem_zero(&c, sizeof(c));
    c.start = start;
    container = xx_rt_memcmp(head, sig, STOS_SIG_SIZE) == 0;
    if (container) {
        c.bank_number = xx_data_get_u32(head + 10, 4, 0, true);
        c.bank_type = head[14];
        if (c.bank_number == 0U || c.bank_number > STOS_MAX_BANK) return false;
        if (c.bank_type == 0x81U) c.bank_id = xx_data_get_u32(head + STOS_HEADER, 4, 0, true);
    } else {
        if (xx_data_get_u32(head, 4, 0, true) != STOS_ID_SPRITE) return false;
        c.bank_id = STOS_ID_SPRITE;
    }
    if (full) {
        /* Upper bound: the three u16 sprite counts, or one u16 icon count. */
        uint8_t h[STOS_SPRITE_HEADER];
        int64_t bank = container ? start + STOS_HEADER : start;
        capacity = 1U;
        if (c.bank_id == STOS_ID_SPRITE &&
            stos_read_at(dev, bank, h, sizeof(h)))
            capacity += (size_t)stos_be16(h + 16) + stos_be16(h + 18) +
                        stos_be16(h + 20);
        if (capacity > XX_STOS_MEMORY_BANK_MAX_RECORDS + 1U)
            capacity = XX_STOS_MEMORY_BANK_MAX_RECORDS + 1U;
        else if (c.bank_id == STOS_ID_ICON &&
                 stos_read_at(dev, bank, h, 6U))
            capacity += (size_t)stos_be16(h + 4);
        c.items = (stos_item *)xx_mem_calloc(capacity, sizeof(stos_item));
        if (!c.items) return false;
    }
    if (c.bank_id == STOS_ID_SPRITE) {
        decoded = stos_parse_sprites(dev, &c, container ? start + STOS_HEADER
                                                        : start,
                                     limit, !container, full, &end);
        if (!decoded && !container) {
            stos_context_free(&c);
            return false;
        }
    } else if (c.bank_id == STOS_ID_ICON) {
        decoded = stos_parse_icons(dev, &c, start + STOS_HEADER, limit, full,
                                   &end);
    }
    if (!decoded) {
        /* Everything after the header, verbatim. */
        stos_item *it;
        c.count = 0U;
        it = stos_next_item(&c, full);
        if (full) {
            xx_mem_zero(it, sizeof(*it));
            it->kind = STOS_KIND_RAW;
            it->data_offset = start + STOS_HEADER;
            it->data_size = limit - start - STOS_HEADER;
            it->output_size = it->data_size;
            xx_rt_snprintf(it->name, sizeof(it->name), "bank%02u_%s.bin",
                           (unsigned)c.bank_number,
                           stos_kind_name(c.bank_type, c.bank_id,
                                          limit - start >= STOS_HEADER + 4));
        }
        end = limit;
    }
    if (c.count == 0U || end <= start) {
        stos_context_free(&c);
        return false;
    }
    c.format_size = end - start;
    *out = c;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Rendering                                                               */

static void stos_bmp_header(uint8_t *head, const stos_item *it) {
    xx_mem_zero(head, BMP_FILE_HEADER + BMP_V4_HEADER);
    head[0] = 'B';
    head[1] = 'M';
    xx_data_set_u32(head + 2, 4, 0, (uint32_t)it->output_size, false);
    xx_data_set_u32(head + 10, 4, 0, BMP_FILE_HEADER + BMP_V4_HEADER, false);
    xx_data_set_u32(head + 14, 4, 0, BMP_V4_HEADER, false);
    xx_data_set_u32(head + 18, 4, 0, (uint32_t)it->width, false);
    xx_data_set_u32(head + 22, 4, 0, (uint32_t)it->height, false); /* bottom-up */
    xx_data_set_u16(head + 26, 2, 0, (uint16_t)1U, false);
    xx_data_set_u16(head + 28, 2, 0, (uint16_t)32U, false);
    xx_data_set_u32(head + 30, 4, 0, 3U, false); /* BI_BITFIELDS */
    xx_data_set_u32(head + 34, 4, 0, (uint32_t)(it->width * 4 * it->height), false);
    xx_data_set_u32(head + 38, 4, 0, 2835U, false);
    xx_data_set_u32(head + 42, 4, 0, 2835U, false);
    xx_data_set_u32(head + 54, 4, 0, 0x00FF0000U, false);
    xx_data_set_u32(head + 58, 4, 0, 0x0000FF00U, false);
    xx_data_set_u32(head + 62, 4, 0, 0x000000FFU, false);
    xx_data_set_u32(head + 66, 4, 0, 0xFF000000U, false);
    xx_data_set_u32(head + 70, 4, 0, 0x73524742U, false); /* 'sRGB' */
}

static uint32_t stos_bit(const uint8_t *row, int64_t word, int64_t x) {
    uint32_t w = stos_be16(row + word * 2);
    return (w >> (15U - (uint32_t)(x & 15))) & 1U;
}

/* The member's bytes are read once (a sprite is at most 255 x 255 words
 * of mask and four planes, 650 KB) and the BMP is built in memory (at most
 * 4080 x 255 x 4 bytes) and written in one call. */
static bool stos_render(Abstractformat *format, const stos_context *c,
                        const stos_item *it, xx_io_device *dst,
                        xx_pd_struct *pd) {
    uint8_t head[BMP_FILE_HEADER + BMP_V4_HEADER];
    uint8_t *data = NULL, *out = NULL;
    int64_t stride = it->width * 4, y;
    int64_t mask_span, img_span;
    bool result = false;
    if (it->kind == STOS_KIND_SPRITE) {
        mask_span = it->words * 2;
        img_span = it->words * 2 * (int64_t)it->bpp;
    } else {
        mask_span = 4; /* icon rows: mask word, image word */
        img_span = 4;
    }
    stos_bmp_header(head, it);
    data = (uint8_t *)xx_mem_alloc((size_t)it->data_size);
    out = (uint8_t *)xx_mem_alloc((size_t)(stride * it->height));
    if (!data || !out ||
        !stos_read_at(format->device, it->data_offset, data,
                      (size_t)it->data_size))
        goto done;
    for (y = 0; y < it->height; ++y) {
        const uint8_t *mask, *img;
        uint8_t *row = out + (it->height - 1 - y) * stride; /* bottom-up */
        int64_t x;
        if (pd && (y & 63) == 0 && xx_pd_is_stopped(pd)) goto done;
        if (it->kind == STOS_KIND_SPRITE) {
            mask = data + y * mask_span;
            img = data + mask_span * it->height + y * img_span;
        } else {
            mask = data + STOS_ICON_BITS + y * 4;
            img = mask + 2;
        }
        for (x = 0; x < it->width; ++x) {
            uint8_t *p = row + x * 4;
            uint32_t m = stos_bit(mask, x >> 4, x);
            if (it->kind == STOS_KIND_SPRITE) {
                uint32_t v = 0U, plane, rgb;
                for (plane = 0U; plane < it->bpp; ++plane)
                    v |= stos_bit(img, (x >> 4) * (int64_t)it->bpp + plane, x)
                         << plane;
                rgb = c->pal[v & 15U];
                p[0] = (uint8_t)(rgb & 0xFFU);
                p[1] = (uint8_t)((rgb >> 8U) & 0xFFU);
                p[2] = (uint8_t)((rgb >> 16U) & 0xFFU);
                p[3] = m ? 0U : 0xFFU; /* sprite mask: set = transparent */
            } else {
                uint32_t v = stos_bit(img, 0, x);
                uint8_t g = (uint8_t)((v ? 0xFFU : 0U) ^
                                      (it->fg_white_is_zero ? 0xFFU : 0U));
                p[0] = g;
                p[1] = g;
                p[2] = g;
                p[3] = m ? 0xFFU : 0U; /* icon mask: set = opaque */
            }
        }
    }
    result = stos_write(dst, head, sizeof(head)) &&
             stos_write(dst, out, (size_t)(stride * it->height));
done:
    if (data) xx_mem_free(data);
    if (out) xx_mem_free(out);
    return result;
}

static bool stos_copy_raw(Abstractformat *format, const stos_item *it,
                          xx_io_device *dst, xx_pd_struct *pd) {
    uint8_t *buffer;
    size_t capacity = xx_get_file_buffer_size();
    int64_t done = 0;
    bool result = true;
    if (capacity == 0U) capacity = 65536U;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < it->data_size) {
        size_t chunk = (it->data_size - done) < (int64_t)capacity
                           ? (size_t)(it->data_size - done) : capacity;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !stos_read_at(format->device, it->data_offset + done, buffer,
                          chunk) ||
            !stos_write(dst, buffer, chunk)) {
            result = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return result;
}

static bool stos_unpack_item(Abstractformat *format, const stos_context *c,
                             const stos_item *it, xx_io_device *dst,
                             xx_pd_struct *pd) {
    return it->kind == STOS_KIND_RAW ? stos_copy_raw(format, it, dst, pd)
                                     : stos_render(format, c, it, dst, pd);
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void stos_stream_free(void *opaque) {
    stos_stream *stream = (stos_stream *)opaque;
    if (!stream) return;
    stos_context_free(&stream->context);
    xx_mem_free(stream);
}

static bool stos_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *stos_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool stos_set_record(xx_archive_record *record, const stos_item *it) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = it->data_offset;
    record->header_size = 0;
    record->data_offset = it->data_offset;
    record->compressed_size = it->data_size;
    return xx_archive_record_set_original_name(record, it->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)it->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)it->output_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          it->kind == STOS_KIND_RAW ? 0U : 1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_stos_memory_bank_init(xx_stos_memory_bank *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_STOS_MEMORY_BANK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stos-memory-bank");
    xx_format_set_extension(&archive->format, "mbk");
    archive->format.check_is_valid = xx_stos_memory_bank_check_is_valid;
    archive->format.handle_base_info = xx_stos_memory_bank_handle_base_info;
    archive->format.get_format_size = xx_stos_memory_bank_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stos_memory_bank_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stos_memory_bank_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stos_memory_bank_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stos_memory_bank_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stos_memory_bank_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stos_memory_bank_free_archive_records_reading;
}

xx_stos_memory_bank *xx_stos_memory_bank_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_stos_memory_bank *archive =
        (xx_stos_memory_bank *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_stos_memory_bank_init(archive, device, base_address);
    return archive;
}

void xx_stos_memory_bank_destroy(xx_stos_memory_bank *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_stos_memory_bank_free(xx_stos_memory_bank *archive) {
    if (!archive) return;
    xx_stos_memory_bank_destroy(archive);
    xx_mem_free(archive);
}

bool xx_stos_memory_bank_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    stos_context context;
    (void)pd;
    if (!stos_parse(format, &context, false)) return false;
    stos_context_free(&context);
    return true;
}

bool xx_stos_memory_bank_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    stos_context context;
    xx_stos_memory_bank *archive;
    (void)pd;
    if (!format || !stos_parse(format, &context, false)) return false;
    archive = (xx_stos_memory_bank *)format;
    archive->number_of_records = (uint64_t)context.count;
    archive->bank_number = context.bank_number;
    archive->bank_type = context.bank_type;
    archive->bank_id = context.bank_id;
    format->number_of_archive_records = (uint64_t)context.count;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    stos_context_free(&context);
    return true;
}

int64_t xx_stos_memory_bank_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stos_memory_bank_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_stos_memory_bank_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stos_memory_bank_handle_base_info(format, pd))
               ? ((xx_stos_memory_bank *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_stos_memory_bank_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    stos_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (stos_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!stos_parse(format, &stream->context, true)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        stos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = stos_stream_free;
    state->total_records = (uint64_t)stream->context.count;
    if (!stos_copy_options(&state->options, options) ||
        !stos_set_record(&state->current_record, &stream->context.items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_stos_memory_bank_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_stos_memory_bank_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    stos_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (stos_stream *)state->internal_state) ||
        stream->index + 1U >= stream->context.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!stos_set_record(&state->current_record,
                         &stream->context.items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_stos_memory_bank_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    stos_stream *stream;
    const stos_item *it;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (stos_stream *)state->internal_state) ||
        stream->index >= stream->context.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    it = &stream->context.items[stream->index];
    path_option = stos_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return stos_unpack_item(format, &stream->context, it, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Member names are built from numbers and fixed words only. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", it->name)
               : xx_str_concat(base, it->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = stos_unpack_item(format, &stream->context, it, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_stos_memory_bank_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

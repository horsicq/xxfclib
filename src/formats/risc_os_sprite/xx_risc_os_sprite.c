/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RISC OS sprite file (filetype &FF9).  xx_risc_os_sprite.h carries the
 * field table.  Every sprite of the area is one member: sprites of a pixel
 * type decoded here are rendered to BMP, all others are published verbatim
 * as one-sprite sprite files.
 *
 * The pixel semantics (old screen-mode table, the default 2/4/16/256 colour
 * palettes, averaging of the two colour words of a palette entry, the three
 * mask kinds and the padding-bit handling) follow Deark's rosprite module,
 * deark-1.7.3/modules/rosprite.c, Copyright (C) 2016 Jason Summers, MIT
 * License; the tables below are ported from it.  The container walk, the
 * bounds checks and the BMP writer are this file's own.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/risc_os_sprite/xx_risc_os_sprite.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator (and its alias macro) exists in xxfc_defs.h. */
#ifdef RISC_OS_SPRITE
#define XX_RISC_OS_SPRITE_FILE_TYPE XX_FILE_TYPE_RISC_OS_SPRITE
#else
#define XX_RISC_OS_SPRITE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ROS_AREA_HEADER 12
#define ROS_SPRITE_HEADER 44
#define ROS_NAME_SIZE 12
/* The same limit Deark applies to an image side. */
#define ROS_MAX_DIMENSION 10000
/* Largest rendered image: 16M pixels, 64 MB of 32-bit BMP. */
#define ROS_MAX_PIXELS (INT64_C(16) * 1024 * 1024)
#define ROS_NAME_MAX 40 /* sanitised name, suffix and extension */

#define ROS_KIND_BMP 1
#define ROS_KIND_RAW 2

#define ROS_MASK_NONE 0
#define ROS_MASK_OLD 1  /* fgbpp bits per pixel, 0 = transparent */
#define ROS_MASK_NEW1 2 /* 1 bit per pixel, 0 = transparent */
#define ROS_MASK_NEW8 3 /* 8-bit alpha */

#define BMP_FILE_HEADER 14
#define BMP_INFO_HEADER 40
#define BMP_V4_HEADER 108

typedef struct ros_sprite_s {
    int64_t offset;      /**< Sprite start, absolute device offset. */
    int64_t size;        /**< Sprite size (its next-sprite offset). */
    int64_t output_size; /**< Size of the published member. */
    int kind;
    /* Decode parameters (kind == ROS_KIND_BMP only). */
    uint32_t fgbpp;
    uint32_t maskbpp;
    int mask_type;
    bool use_mask;
    int64_t width; /**< Output width (padding removed). */
    int64_t height;
    int64_t start_pad; /**< Leading padding pixels in each row. */
    int64_t rowspan;
    int64_t mask_rowspan;
    int64_t image_offset; /**< Absolute. */
    int64_t mask_offset;  /**< Absolute. */
    uint32_t xdpi, ydpi;
    uint32_t palette_count; /**< Custom palette entries (0 = default). */
    char name[ROS_NAME_MAX];
} ros_sprite;

typedef struct ros_context_s {
    int64_t area_size; /**< Bytes of the file the area covers. */
    uint32_t declared_count;
    size_t count;
    ros_sprite *sprites;
} ros_context;

typedef struct ros_stream_s {
    ros_context context;
    size_t index;
} ros_stream;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static bool ros_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool ros_write(xx_io_device *device, const void *data, size_t size)
{
    size_t done = 0U;
    if (!device) return true; /* verification run */
    while (done < size) {
        ssize_t wrote = xx_io_write(device, (const uint8_t *)data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Pixel tables (ported from Deark rosprite.c, MIT)                        */

typedef struct ros_old_mode_s {
    uint8_t mode, bpp, xdpi, ydpi;
} ros_old_mode;

static const ros_old_mode ros_old_modes[] = {{0, 1, 90, 45},  {1, 2, 45, 45},  {2, 4, 0, 0},    {4, 1, 45, 45},  {5, 2, 0, 0},    {8, 2, 90, 45},  {9, 4, 45, 45},
                                             {10, 8, 0, 0},   {11, 2, 0, 0},   {12, 4, 90, 45}, {13, 8, 45, 45}, {14, 4, 0, 0},   {15, 8, 90, 45}, {16, 4, 0, 0},
                                             {17, 4, 0, 0},   {18, 1, 90, 90}, {19, 2, 90, 90}, {20, 4, 90, 90}, {21, 8, 90, 90}, {22, 4, 0, 0},   {23, 1, 0, 0},
                                             {24, 8, 0, 0},   {25, 1, 0, 0},   {26, 2, 0, 0},   {27, 4, 90, 90}, {28, 8, 90, 90}, {29, 1, 0, 0},   {30, 2, 0, 0},
                                             {31, 4, 90, 90}, {32, 8, 90, 90}, {33, 1, 0, 0},   {34, 2, 0, 0},   {35, 4, 0, 0},   {36, 8, 90, 45}, {37, 1, 0, 0},
                                             {38, 2, 0, 0},   {39, 4, 0, 0},   {40, 8, 0, 0},   {41, 1, 0, 0},   {42, 2, 0, 0},   {43, 4, 0, 0},   {44, 1, 0, 0},
                                             {45, 2, 0, 0},   {46, 4, 0, 0},   {47, 8, 0, 0},   {48, 4, 0, 0},   {49, 8, 0, 0},   {107, 16, 0, 0}};

/* Colours are 0xRRGGBB. */
static const uint32_t ros_pal4[4] = {0xffffffU, 0xbbbbbbU, 0x777777U, 0x000000U};
static const uint32_t ros_pal16[16] = {0xffffffU, 0xddddddU, 0xbbbbbbU, 0x999999U, 0x777777U, 0x555555U, 0x333333U, 0x000000U,
                                       0x4499ffU, 0xeeee00U, 0x00cc00U, 0xdd0000U, 0xeeeebbU, 0x558800U, 0xffbb00U, 0x00bbffU};

static uint32_t ros_pal256(uint32_t k)
{
    uint32_t r = k % 8U + ((k % 32U) / 16U) * 8U;
    uint32_t g = k % 4U + ((k % 128U) / 32U) * 4U;
    uint32_t b = k % 4U + ((k % 16U) / 8U) * 4U + (k / 128U) * 8U;
    r = ((r << 4U) | r) & 0xFFU;
    g = ((g << 4U) | g) & 0xFFU;
    b = ((b << 4U) | b) & 0xFFU;
    return (r << 16U) | (g << 8U) | b;
}

/* 5-bit to 8-bit, rounded: floor(v * 255 / 31 + 0.5). */
static uint32_t ros_scale5(uint32_t v)
{
    return (v * 510U + 31U) / 62U;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static char ros_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ros_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || ros_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* True when @p name (no extension yet) is a Windows device name. */
static bool ros_is_device(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ros_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((ros_upper(name[0]) == 'C' && ros_upper(name[1]) == 'O' && ros_upper(name[2]) == 'M') ||
            (ros_upper(name[0]) == 'L' && ros_upper(name[1]) == 'P' && ros_upper(name[2]) == 'T'));
}

/* Member names are built here, never taken verbatim: the sprite name up to
 * its terminator with every byte that is not plain printable ASCII, and
 * every path or reserved character, replaced by '_'. */
static void ros_make_stem(const uint8_t *field, size_t index, char *out, size_t out_size)
{
    size_t length = 0U, i;
    bool meaningful = false;
    for (i = 0U; i < ROS_NAME_SIZE && length + 1U < out_size; ++i) {
        uint8_t c = field[i];
        if (c == 0U || c < 0x20U) break;
        if (c > 0x7EU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') c = '_';
        out[length++] = (char)c;
        if (c != '.' && c != ' ') meaningful = true;
    }
    /* Windows drops trailing dots and spaces. */
    while (length > 0U && (out[length - 1U] == '.' || out[length - 1U] == ' ')) --length;
    out[length] = 0;
    if (!meaningful || length == 0U) {
        xx_rt_snprintf(out, out_size, "sprite%u", (unsigned)index);
        return;
    }
    if (ros_is_device(out) && length + 2U < out_size) {
        xx_rt_memmove(out + 1, out, length + 1U);
        out[0] = '_';
    }
}

static bool ros_name_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (ros_upper(*a) != ros_upper(*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

/* ---------------------------------------------------------------------- */
/* Structure                                                               */

/* Structural checks of one 44-byte sprite header that sits at @p pos
 * (relative to the file) of an area of @p area_size bytes. */
static bool ros_header_ok(const uint8_t *h, int64_t pos, int64_t area_size)
{
    uint32_t size = xx_data_get_u32(h, 4, 0, false), image = xx_data_get_u32(h + 32, 4, 0, false), mask = xx_data_get_u32(h + 36, 4, 0, false);
    if (size < ROS_SPRITE_HEADER || (int64_t)size > area_size - pos) return false;
    if (h[4] <= 0x20U || h[4] > 0x7EU) return false;
    if (xx_data_get_u32(h + 24, 4, 0, false) > 31U || xx_data_get_u32(h + 28, 4, 0, false) > 31U) return false;
    if (image < ROS_SPRITE_HEADER || (image & 3U) != 0U || image > size) return false;
    if (mask != 0U && (mask < ROS_SPRITE_HEADER || (mask & 3U) != 0U || mask > size)) return false;
    return true;
}

bool xx_risc_os_sprite_prefilter(const uint8_t *magic, size_t magic_size, int64_t total_size)
{
    uint32_t count, first, free_offset, size, image;
    size_t sb, i;
    if (!magic || magic_size < 36U) return false;
    count = xx_data_get_u32(magic, 4, 0, false);
    first = xx_data_get_u32(magic + 4, 4, 0, false);
    free_offset = xx_data_get_u32(magic + 8, 4, 0, false);
    if (count == 0U || count > XX_RISC_OS_SPRITE_MAX_SPRITES) return false;
    if (first != 16U && first != 32U) return false;
    sb = (size_t)first - 4U;
    if (magic_size < sb + 36U) return false;
    if (free_offset < first + ROS_SPRITE_HEADER || (int64_t)free_offset - 4 > total_size) return false;
    size = xx_data_get_u32(magic + sb, 4, 0, false);
    if (size < ROS_SPRITE_HEADER || size > free_offset - 4U - sb) return false;
    if (magic[sb + 4] <= 0x20U || magic[sb + 4] > 0x7EU) return false;
    for (i = 1U; i < ROS_NAME_SIZE; ++i) {
        uint8_t c = magic[sb + 4 + i];
        if (c == 0U) break;
        if (c < 0x20U || c > 0x7EU) return false;
    }
    if (xx_data_get_u32(magic + sb + 16, 4, 0, false) > 0xFFFFU || xx_data_get_u32(magic + sb + 20, 4, 0, false) > 0xFFFFU ||
        xx_data_get_u32(magic + sb + 24, 4, 0, false) > 31U || xx_data_get_u32(magic + sb + 28, 4, 0, false) > 31U)
        return false;
    image = xx_data_get_u32(magic + sb + 32, 4, 0, false);
    if (image < ROS_SPRITE_HEADER || (image & 3U) != 0U || image > size) return false;
    if (magic_size >= sb + ROS_SPRITE_HEADER) {
        uint32_t mask = xx_data_get_u32(magic + sb + 36, 4, 0, false);
        uint32_t mode = xx_data_get_u32(magic + sb + 40, 4, 0, false);
        if (mask != 0U && (mask < ROS_SPRITE_HEADER || (mask & 3U) != 0U || mask > size)) return false;
        if (mode > 0xFFU && ((mode >> 27U) & 0xFU) == 0U) return false;
    }
    return true;
}

/* Work out how a sprite is rendered; false leaves it as a raw member. */
static bool ros_plan_image(const uint8_t *h, ros_sprite *s)
{
    uint32_t words = xx_data_get_u32(h + 16, 4, 0, false), rows = xx_data_get_u32(h + 20, 4, 0, false);
    uint32_t first_bit = xx_data_get_u32(h + 24, 4, 0, false), last_bit = xx_data_get_u32(h + 28, 4, 0, false);
    uint32_t image = xx_data_get_u32(h + 32, 4, 0, false), mask = xx_data_get_u32(h + 36, 4, 0, false);
    uint32_t mode = xx_data_get_u32(h + 40, 4, 0, false), type = (mode >> 27U) & 0xFU;
    int64_t width_words, pdwidth, end_pad, image_bytes, end;
    bool has_mask = mask != 0U && mask != image;
    size_t i;

    s->fgbpp = 0U;
    s->mask_type = ROS_MASK_NONE;
    s->use_mask = false;
    s->maskbpp = 0U;
    s->xdpi = s->ydpi = 0U;
    if (words > 0xFFFFU || rows > 0xFFFFU) return false;
    width_words = (int64_t)words + 1;
    s->height = (int64_t)rows + 1;
    if (type == 0U) {
        for (i = 0U; i < sizeof(ros_old_modes) / sizeof(ros_old_modes[0]); ++i)
            if (mode == ros_old_modes[i].mode) {
                s->fgbpp = ros_old_modes[i].bpp;
                s->xdpi = ros_old_modes[i].xdpi;
                s->ydpi = ros_old_modes[i].ydpi;
                break;
            }
        if (s->fgbpp == 0U) return false;
        if (has_mask) {
            s->mask_type = ROS_MASK_OLD;
            s->maskbpp = s->fgbpp;
            /* An old-format 16 bpp sprite has no usable mask. */
            s->use_mask = s->fgbpp <= 8U;
        }
    } else {
        if (type > 6U) return false; /* CMYK, 24 bpp, RISC OS 5 words */
        s->fgbpp = 1U << (type - 1U);
        s->xdpi = (mode >> 14U) & 0x1FFFU;
        s->ydpi = (mode >> 1U) & 0x1FFFU;
        first_bit = 0U; /* not meaningful for the new format */
        if (has_mask) {
            s->mask_type = (mode & 0x80000000U) ? ROS_MASK_NEW8 : ROS_MASK_NEW1;
            s->maskbpp = s->mask_type == ROS_MASK_NEW8 ? 8U : 1U;
            s->use_mask = true;
        }
    }
    pdwidth = (width_words * 32) / (int64_t)s->fgbpp;
    s->start_pad = (int64_t)first_bit / (int64_t)s->fgbpp;
    end_pad = (int64_t)(31U - last_bit) / (int64_t)s->fgbpp;
    s->width = pdwidth - s->start_pad - end_pad;
    if (s->width < 1 || s->width > ROS_MAX_DIMENSION || s->height > ROS_MAX_DIMENSION || s->width * s->height > ROS_MAX_PIXELS) return false;
    s->rowspan = width_words * 4;
    image_bytes = s->rowspan * s->height;
    end = (int64_t)image + image_bytes;
    if (end > s->size) return false;
    s->image_offset = s->offset + image;
    s->mask_offset = s->offset + mask;
    if (s->mask_type == ROS_MASK_OLD) {
        s->mask_rowspan = s->rowspan;
    } else if (s->mask_type != ROS_MASK_NONE) {
        s->mask_rowspan = (((int64_t)s->maskbpp * s->width + 31) / 32) * 4;
    }
    if (s->use_mask && (int64_t)mask + s->mask_rowspan * s->height > s->size) return false;
    s->palette_count = 0U;
    if (s->fgbpp <= 8U && image >= ROS_SPRITE_HEADER + 8U) {
        uint32_t n = (image - ROS_SPRITE_HEADER) / 8U;
        s->palette_count = n > 256U ? 256U : n;
    }
    {
        int64_t bpp = s->use_mask ? 4 : 3;
        int64_t stride = ((s->width * bpp) + 3) & ~(int64_t)3;
        int64_t header = BMP_FILE_HEADER + (s->use_mask ? BMP_V4_HEADER : BMP_INFO_HEADER);
        s->output_size = header + stride * s->height;
    }
    return true;
}

static void ros_context_free(ros_context *context)
{
    if (context && context->sprites) xx_mem_free(context->sprites);
    if (context) {
        context->sprites = NULL;
        context->count = 0U;
    }
}

/* Parse the area.  @p full also plans every sprite and names the members;
 * without it only the structure is checked. */
static bool ros_parse(Abstractformat *format, ros_context *out, bool full)
{
    uint8_t header[ROS_AREA_HEADER];
    uint8_t sh[ROS_SPRITE_HEADER];
    ros_context context;
    int64_t total, pos;
    uint32_t count, first, free_offset, index;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    xx_mem_zero(out, sizeof(*out));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < ROS_AREA_HEADER + ROS_SPRITE_HEADER || !ros_read_at(format->device, format->base_address, header, sizeof(header))) return false;
    count = xx_data_get_u32(header, 4, 0, false);
    first = xx_data_get_u32(header + 4, 4, 0, false);
    free_offset = xx_data_get_u32(header + 8, 4, 0, false);
    if (count == 0U || count > XX_RISC_OS_SPRITE_MAX_SPRITES) return false;
    if (first < 16U || (first & 3U) != 0U) return false;
    if (free_offset < first + ROS_SPRITE_HEADER) return false;
    xx_mem_zero(&context, sizeof(context));
    context.declared_count = count;
    context.area_size = (int64_t)free_offset - 4;
    if (context.area_size > total) return false;
    if (full) {
        context.sprites = (ros_sprite *)xx_mem_calloc((size_t)count, sizeof(ros_sprite));
        if (!context.sprites) return false;
    }
    pos = (int64_t)first - 4;
    for (index = 0U; index < count; ++index) {
        ros_sprite *s;
        if (pos > context.area_size - ROS_SPRITE_HEADER) break;
        if (!ros_read_at(format->device, format->base_address + pos, sh, sizeof(sh)) || !ros_header_ok(sh, pos, context.area_size)) break;
        if (full) {
            size_t j, suffix = 1U;
            char stem[ROS_NAME_MAX - 8];
            s = &context.sprites[context.count];
            s->offset = format->base_address + pos;
            s->size = (int64_t)xx_data_get_u32(sh, 4, 0, false);
            if (ros_plan_image(sh, s)) {
                s->kind = ROS_KIND_BMP;
            } else {
                s->kind = ROS_KIND_RAW;
                s->output_size = ROS_AREA_HEADER + s->size;
            }
            ros_make_stem(sh + 4, context.count, stem, sizeof(stem));
            for (;;) {
                bool clash = false;
                if (suffix == 1U) xx_rt_snprintf(s->name, sizeof(s->name), "%s%s", stem, s->kind == ROS_KIND_BMP ? ".bmp" : ",ff9");
                else xx_rt_snprintf(s->name, sizeof(s->name), "%s_%u%s", stem, (unsigned)suffix, s->kind == ROS_KIND_BMP ? ".bmp" : ",ff9");
                for (j = 0U; j < context.count; ++j)
                    if (ros_name_equal(context.sprites[j].name, s->name)) {
                        clash = true;
                        break;
                    }
                if (!clash) break;
                ++suffix; /* at most count + 1 rounds */
            }
        }
        ++context.count;
        pos += (int64_t)xx_data_get_u32(sh, 4, 0, false);
    }
    if (context.count == 0U) {
        ros_context_free(&context);
        return false;
    }
    *out = context;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Rendering                                                               */

static void ros_build_palette(Abstractformat *format, const ros_sprite *s, const uint8_t *custom, uint32_t *pal)
{
    uint32_t k;
    (void)format;
    for (k = 0U; k < 256U; ++k) {
        if (s->palette_count) {
            if (k < s->palette_count) {
                const uint8_t *e = custom + 8U * k;
                uint32_t c1 = ((uint32_t)e[1] << 16U) | ((uint32_t)e[2] << 8U) | e[3];
                uint32_t c2 = ((uint32_t)e[5] << 16U) | ((uint32_t)e[6] << 8U) | e[7];
                if (c1 == c2) {
                    pal[k] = c1;
                } else {
                    uint32_t r = (((c1 >> 16U) & 0xFFU) + ((c2 >> 16U) & 0xFFU)) / 2U;
                    uint32_t g = (((c1 >> 8U) & 0xFFU) + ((c2 >> 8U) & 0xFFU)) / 2U;
                    uint32_t b = ((c1 & 0xFFU) + (c2 & 0xFFU)) / 2U;
                    pal[k] = (r << 16U) | (g << 8U) | b;
                }
            } else {
                pal[k] = ros_pal256(k);
            }
        } else if (s->fgbpp == 4U && k < 16U) {
            pal[k] = ros_pal16[k];
        } else if (s->fgbpp == 2U && k < 4U) {
            pal[k] = ros_pal4[k];
        } else if (s->fgbpp == 1U && k < 2U) {
            pal[k] = k == 0U ? 0xFFFFFFU : 0x000000U;
        } else {
            pal[k] = ros_pal256(k);
        }
    }
}

static uint32_t ros_index(const uint8_t *row, int64_t i, uint32_t bpp)
{
    int64_t bit = i * (int64_t)bpp;
    return ((uint32_t)row[bit >> 3] >> (uint32_t)(bit & 7)) & ((1U << bpp) - 1U);
}

static uint32_t ros_ppm(uint32_t dpi)
{
    return (uint32_t)(((uint64_t)dpi * 10000U + 127U) / 254U);
}

static bool ros_render(Abstractformat *format, const ros_sprite *s, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t head[BMP_FILE_HEADER + BMP_V4_HEADER];
    uint32_t *pal = NULL;
    uint8_t *custom = NULL, *row = NULL, *mrow = NULL, *out = NULL;
    size_t head_size;
    int64_t bpp = s->use_mask ? 4 : 3;
    int64_t stride = ((s->width * bpp) + 3) & ~(int64_t)3;
    int64_t y;
    bool result = false;

    head_size = BMP_FILE_HEADER + (s->use_mask ? BMP_V4_HEADER : BMP_INFO_HEADER);
    xx_mem_zero(head, sizeof(head));
    head[0] = 'B';
    head[1] = 'M';
    xx_data_set_u32(head + 2, 4, 0, (uint32_t)s->output_size, false);
    xx_data_set_u32(head + 10, 4, 0, (uint32_t)head_size, false);
    xx_data_set_u32(head + 14, 4, 0, (uint32_t)(head_size - BMP_FILE_HEADER), false);
    xx_data_set_u32(head + 18, 4, 0, (uint32_t)s->width, false);
    xx_data_set_u32(head + 22, 4, 0, (uint32_t)s->height, false); /* bottom-up */
    xx_data_set_u16(head + 26, 2, 0, (uint16_t)1U, false);
    xx_data_set_u16(head + 28, 2, 0, (uint16_t)((uint32_t)(bpp * 8)), false);
    xx_data_set_u32(head + 30, 4, 0, s->use_mask ? 3U : 0U, false); /* BI_BITFIELDS / BI_RGB */
    xx_data_set_u32(head + 34, 4, 0, (uint32_t)(stride * s->height), false);
    xx_data_set_u32(head + 38, 4, 0, ros_ppm(s->xdpi), false);
    xx_data_set_u32(head + 42, 4, 0, ros_ppm(s->ydpi), false);
    if (s->use_mask) {
        xx_data_set_u32(head + 54, 4, 0, 0x00FF0000U, false);
        xx_data_set_u32(head + 58, 4, 0, 0x0000FF00U, false);
        xx_data_set_u32(head + 62, 4, 0, 0x000000FFU, false);
        xx_data_set_u32(head + 66, 4, 0, 0xFF000000U, false);
        xx_data_set_u32(head + 70, 4, 0, 0x73524742U, false); /* 'sRGB' */
    }

    pal = (uint32_t *)xx_mem_alloc(256U * sizeof(uint32_t));
    row = (uint8_t *)xx_mem_alloc((size_t)s->rowspan);
    out = (uint8_t *)xx_mem_alloc((size_t)stride);
    if (!pal || !row || !out) goto done;
    if (s->use_mask) {
        mrow = (uint8_t *)xx_mem_alloc((size_t)s->mask_rowspan);
        if (!mrow) goto done;
    }
    if (s->fgbpp <= 8U) {
        if (s->palette_count) {
            custom = (uint8_t *)xx_mem_alloc(8U * s->palette_count);
            if (!custom || !ros_read_at(format->device, s->offset + ROS_SPRITE_HEADER, custom, 8U * s->palette_count)) goto done;
        }
        ros_build_palette(format, s, custom, pal);
    }
    if (!ros_write(destination, head, head_size)) goto done;
    for (y = s->height - 1; y >= 0; --y) {
        int64_t x;
        if (pd && (y & 63) == 0 && xx_pd_is_stopped(pd)) goto done;
        if (!ros_read_at(format->device, s->image_offset + y * s->rowspan, row, (size_t)s->rowspan)) goto done;
        if (s->use_mask && !ros_read_at(format->device, s->mask_offset + y * s->mask_rowspan, mrow, (size_t)s->mask_rowspan)) goto done;
        xx_mem_zero(out, (size_t)stride);
        for (x = 0; x < s->width; ++x) {
            int64_t i = x + s->start_pad;
            uint32_t rgb;
            uint8_t *p = out + x * bpp;
            if (s->fgbpp == 32U) {
                const uint8_t *q = row + i * 4;
                rgb = ((uint32_t)q[0] << 16U) | ((uint32_t)q[1] << 8U) | q[2];
            } else if (s->fgbpp == 16U) {
                uint32_t v = (uint32_t)row[i * 2] | ((uint32_t)row[i * 2 + 1] << 8U);
                rgb = (ros_scale5(v & 0x1FU) << 16U) | (ros_scale5((v >> 5U) & 0x1FU) << 8U) | ros_scale5((v >> 10U) & 0x1FU);
            } else {
                rgb = pal[ros_index(row, i, s->fgbpp)];
            }
            p[0] = (uint8_t)(rgb & 0xFFU);
            p[1] = (uint8_t)((rgb >> 8U) & 0xFFU);
            p[2] = (uint8_t)((rgb >> 16U) & 0xFFU);
            if (s->use_mask) {
                /* Old masks line up with the padded image row, new ones
                 * start at the first visible pixel (no left padding). */
                uint32_t m = ros_index(mrow, s->mask_type == ROS_MASK_OLD ? i : x, s->maskbpp);
                p[3] = s->mask_type == ROS_MASK_NEW8 ? (uint8_t)m : (uint8_t)(m ? 0xFFU : 0U);
            }
        }
        if (!ros_write(destination, out, (size_t)stride)) goto done;
    }
    result = true;
done:
    if (pal) xx_mem_free(pal);
    if (custom) xx_mem_free(custom);
    if (row) xx_mem_free(row);
    if (mrow) xx_mem_free(mrow);
    if (out) xx_mem_free(out);
    return result;
}

/* A one-sprite sprite file: area header, then the sprite verbatim (its
 * internal offsets are sprite-relative, so they stay valid). */
static bool ros_copy_raw(Abstractformat *format, const ros_sprite *s, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t head[ROS_AREA_HEADER];
    uint8_t *buffer;
    size_t capacity = xx_get_file_buffer_size();
    int64_t done = 0;
    bool result = true;
    if (s->size > UINT32_MAX - 16) return false;
    xx_data_set_u32(head, 4, 0, 1U, false);
    xx_data_set_u32(head + 4, 4, 0, 16U, false);
    xx_data_set_u32(head + 8, 4, 0, (uint32_t)(16 + s->size), false);
    if (!ros_write(destination, head, sizeof(head))) return false;
    if (capacity == 0U) capacity = 65536U;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < s->size) {
        size_t chunk = (s->size - done) < (int64_t)capacity ? (size_t)(s->size - done) : capacity;
        if ((pd && xx_pd_is_stopped(pd)) || !ros_read_at(format->device, s->offset + done, buffer, chunk) || !ros_write(destination, buffer, chunk)) {
            result = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return result;
}

static bool ros_unpack_sprite(Abstractformat *format, const ros_sprite *s, xx_io_device *destination, xx_pd_struct *pd)
{
    return s->kind == ROS_KIND_BMP ? ros_render(format, s, destination, pd) : ros_copy_raw(format, s, destination, pd);
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void ros_stream_free(void *opaque)
{
    ros_stream *stream = (ros_stream *)opaque;
    if (!stream) return;
    ros_context_free(&stream->context);
    xx_mem_free(stream);
}

static bool ros_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *ros_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ros_set_record(xx_archive_record *record, const ros_sprite *s)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = s->offset;
    record->header_size = ROS_SPRITE_HEADER;
    record->data_offset = s->offset;
    record->compressed_size = s->size;
    return xx_archive_record_set_original_name(record, s->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)s->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)s->output_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, s->kind == ROS_KIND_BMP ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_risc_os_sprite_init(xx_risc_os_sprite *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RISC_OS_SPRITE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "image/x-acorn-sprite");
    xx_format_set_extension(&archive->format, "ff9");
    archive->format.check_is_valid = xx_risc_os_sprite_check_is_valid;
    archive->format.handle_base_info = xx_risc_os_sprite_handle_base_info;
    archive->format.get_format_size = xx_risc_os_sprite_get_format_size;
    archive->format.get_number_of_archive_records = xx_risc_os_sprite_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_risc_os_sprite_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_risc_os_sprite_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_risc_os_sprite_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_risc_os_sprite_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_risc_os_sprite_free_archive_records_reading;
}

xx_risc_os_sprite *xx_risc_os_sprite_create(xx_io_device *device, int64_t base_address)
{
    xx_risc_os_sprite *archive = (xx_risc_os_sprite *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_risc_os_sprite_init(archive, device, base_address);
    return archive;
}

void xx_risc_os_sprite_destroy(xx_risc_os_sprite *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_risc_os_sprite_free(xx_risc_os_sprite *archive)
{
    if (!archive) return;
    xx_risc_os_sprite_destroy(archive);
    xx_mem_free(archive);
}

bool xx_risc_os_sprite_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    ros_context context;
    (void)pd;
    if (!ros_parse(format, &context, false)) return false;
    ros_context_free(&context);
    return true;
}

bool xx_risc_os_sprite_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    ros_context context;
    xx_risc_os_sprite *archive;
    (void)pd;
    if (!format || !ros_parse(format, &context, false)) return false;
    archive = (xx_risc_os_sprite *)format;
    archive->number_of_records = (uint64_t)context.count;
    archive->declared_count = context.declared_count;
    format->number_of_archive_records = (uint64_t)context.count;
    format->format_size = context.area_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ros_context_free(&context);
    return true;
}

int64_t xx_risc_os_sprite_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_risc_os_sprite_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_risc_os_sprite_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_risc_os_sprite_handle_base_info(format, pd)) ? ((xx_risc_os_sprite *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_risc_os_sprite_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    ros_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (ros_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!ros_parse(format, &stream->context, true)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ros_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ros_stream_free;
    state->total_records = (uint64_t)stream->context.count;
    if (!ros_copy_options(&state->options, options) || !ros_set_record(&state->current_record, &stream->context.sprites[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_risc_os_sprite_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_risc_os_sprite_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ros_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (ros_stream *)state->internal_state) || stream->index + 1U >= stream->context.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!ros_set_record(&state->current_record, &stream->context.sprites[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_risc_os_sprite_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    ros_stream *stream;
    const ros_sprite *s;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (ros_stream *)state->internal_state) || stream->index >= stream->context.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = &stream->context.sprites[stream->index];
    path_option = ros_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ros_unpack_sprite(format, s, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", s->name) : xx_str_concat(base, s->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = ros_unpack_sprite(format, s, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_risc_os_sprite_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/mskn2/xx_mskn2.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "../xx_bounded_member_cursor.h"
static void mskn2_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool mskn2_add_bitmap(Abstractformat *f, pm_stream *s, const char *name, const uint8_t *p, uint32_t w, uint32_t h)
{
    uint64_t n = (uint64_t)w * h * 4U;
    uint8_t *bmp;
    char label[96];
    if (!w || !h || w > INT32_MAX || h > INT32_MAX || n > BDM_MEMORY_LIMIT - 54U) return false;
    bmp = (uint8_t *)xx_mem_alloc((size_t)n + 54U);
    if (!bmp) return false;
    xx_mem_zero(bmp, 54);
    bmp[0] = 'B';
    bmp[1] = 'M';
    mskn2_put32(bmp + 2, (uint32_t)n + 54U);
    mskn2_put32(bmp + 10, 54);
    mskn2_put32(bmp + 14, 40);
    mskn2_put32(bmp + 18, w);
    mskn2_put32(bmp + 22, 0U - h);
    bmp[26] = 1;
    bmp[28] = 32;
    mskn2_put32(bmp + 34, (uint32_t)n);
    xx_mem_copy(bmp + 54, p, (size_t)n);
    (void)xx_rt_snprintf(label, sizeof(label), "%s.bmp", name);
    if (!bdm_add(f, s, label, 0, 0)) {
        xx_mem_free(bmp);
        return false;
    }
    s->items[s->count - 1U].memory = bmp;
    s->items[s->count - 1U].size = (int64_t)n + 54;
    s->items[s->count - 1U].compression_method = 8;
    return true;
}

/* MSKN v2 has its own complete header and decoded member grammar. */
static bool mskn2_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[9];
    bdm_buffer b = {0};
    bdm_cursor c;
    uint32_t i, count = 0;
    int64_t used = 0, size = pm_available(f);
    bool result = false;
    if (size < 9 + 6 || !pm_read(f, 0, header, sizeof(header)) || xx_mem_compare(header, "_MCT\0KSLZ", sizeof(header)) ||
        !bdm_inflate(f, 9, size - 9, true, &b, &used, pd) || used != size - 9 || !bdm_room(f, s, b.capacity))
        goto done;
    c.data = b.data;
    c.size = b.size;
    c.at = 0;
    for (i = 0; i < 5; ++i)
        if (!bdm_string(&c, NULL, 0)) goto done;
    {
        const uint8_t *count_bytes;
        if (!bdm_take(&c, 2, &count_bytes)) goto done;
        count = bdm_u16(count_bytes) & 4095U;
    }
    for (i = 0; i < count; ++i) {
        char name[80];
        const uint8_t *p;
        uint32_t w, n;
        if ((pd && xx_pd_is_stopped(pd)) || !bdm_take(&c, 2, NULL) || !bdm_string(&c, name, sizeof(name))) goto done;
        {
            uint64_t bytes;
            if (!bdm_word(&c, &w) || !bdm_word(&c, &n)) goto done;
            bytes = (uint64_t)w * n * 4U;
            if (bytes > BDM_MEMORY_LIMIT || !bdm_room(f, s, (uint64_t)b.capacity + bytes + 54U + sizeof(pm_member) * (s->capacity ? s->capacity : 8U)) ||
                !bdm_take(&c, (size_t)bytes, &p) || !mskn2_add_bitmap(f, s, name, p, w, n))
                goto done;
        }
    }
    /* Preserve remaining decoded theme-object metadata alongside the images. */
    if (c.at < c.size && (!bdm_room(f, s, (uint64_t)b.capacity + c.size - c.at + sizeof(pm_member) * (s->capacity ? s->capacity : 8U)) ||
                          !bdm_add_memory(f, s, "theme-objects.bin", c.data + c.at, c.size - c.at)))
        goto done;
    if (!s->count) goto done;
    s->size = size;
    for (i = 0; i < s->count; ++i) s->items[i].packed_size = 0;
    s->items[0].packed_size = size - 9;
    result = true;
done:
    xx_mem_free(b.data);
    return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_MSKN2) return false;
    return mskn2_parse(format, members, pd);
}

Abstractformat *xx_mskn2_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_MSKN2, "mskn");
    return format;
}
void xx_mskn2_free(Abstractformat *format)
{
    if (format) {
        xx_format_destroy(format);
        xx_mem_free(format);
    }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_mskn2_detect(xx_io_device *device, int64_t base)
{
    uint8_t signature[9];
    int64_t saved, total;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 9) return result;
    saved = xx_io_tell(device);
    if (xx_io_read_at(device, base, signature, sizeof(signature)) && !xx_mem_compare(signature, "_MCT\0KSLZ", sizeof(signature))) result = XX_FILE_TYPE_MSKN2;
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *mskn2_open(xx_io_device *device)
{
    return xx_mskn2_create(device, 0);
}
static const xx_file_type_t mskn2_types[] = {XX_FILE_TYPE_MSKN2};
static const xx_format_search_desc mskn2_descriptor = {mskn2_types, 1, NULL, 0, mskn2_open, xx_mskn2_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(mskn2, mskn2_descriptor)

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zisofs/xx_zisofs.h"
#include "../xx_memory_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool zisofs_zisofs(Abstractformat *f, pm_stream *s, const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    static const uint8_t magic[8] = {0x37, 0xe4, 0x53, 0x96, 0xc9, 0xdb, 0xd6, 0x07};
    uint32_t size;
    size_t block, blocks, header, table, i, position = 0U;
    uint8_t *plain;
    if (n < 20U || memcmp(p, magic, 8U) != 0 || p[12] < 4U || p[13] > 29U || p[14] || p[15]) return false;
    size = mdm_u32(p + 8U);
    block = (size_t)1U << p[13];
    header = (size_t)p[12] * 4U;
    if (size > MDM_MEMORY_LIMIT - n || !block || header > n) return false;
    blocks = size / block + (size % block != 0U);
    table = (blocks + 1U) * 4U;
    if (table > n - header || mdm_u32(p + header) != header + table || mdm_u32(p + header + blocks * 4U) != n) return false;
    for (i = 0U; i < blocks; ++i) {
        size_t a = mdm_u32(p + header + i * 4U), b = mdm_u32(p + header + (i + 1U) * 4U);
        if (a > b || b > n || b - a > block + 65536U) return false;
    }
    plain = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!plain) return false;
    for (i = 0U; i < blocks; ++i) {
        size_t a = mdm_u32(p + header + i * 4U), b = mdm_u32(p + header + (i + 1U) * 4U);
        size_t want = size - position < block ? size - position : block;
        if (mdm_stopped(pd)) goto bad;
        if (a == b) memset(plain + position, 0, want);
        else if (!mdm_inflate(p + a, b - a, plain + position, want, true, pd)) goto bad;
        position += want;
    }
    if (!mdm_memory_member(f, s, "content.bin", (int64_t)(header + table), (int64_t)(n - header - table), plain, size, 8U, NULL)) goto bad;
    s->size = (int64_t)n;
    return true;
bad:
    xx_mem_free(plain);
    return false;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    uint8_t *input;
    size_t size;
    bool result;
    if (format->file_type != XX_FILE_TYPE_ZISOFS) return false;
    input = mdm_input(format, &size, pd);
    if (!input) return false;
    result = zisofs_zisofs(format, members, input, size, pd);
    xx_mem_free(input);
    return result && !mdm_stopped(pd);
}

static void zisofs_destroy(Abstractformat *format)
{
    if (format) xx_format_cleanup_extra_parameters(format);
}
Abstractformat *xx_zisofs_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) {
        pm_init(format, device, base, XX_FILE_TYPE_ZISOFS, "zisofs");
        format->destroy = zisofs_destroy;
    }
    return format;
}
void xx_zisofs_free(Abstractformat *format)
{
    if (format) {
        zisofs_destroy(format);
        xx_mem_free(format);
    }
}

xx_file_type_t xx_zisofs_detect(xx_io_device *device, int64_t base)
{
    uint8_t h[16] = {0};
    int64_t total, saved;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    Abstractformat *format;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, h, (uint64_t)(total - base) < sizeof(h) ? (size_t)(total - base) : sizeof(h))) goto done;
    if (!(mdm_u32(h) == 0x9653e437U && mdm_u32(h + 4U) == 0x07d6dbc9U)) goto done;
    format = xx_zisofs_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_ZISOFS;
        xx_zisofs_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *zisofs_open(xx_io_device *device)
{
    return xx_zisofs_create(device, 0);
}
static const xx_file_type_t zisofs_types[] = {XX_FILE_TYPE_ZISOFS};
static const xx_format_search_desc zisofs_descriptor = {zisofs_types, 1, NULL, 0, zisofs_open, xx_zisofs_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(zisofs, zisofs_descriptor)

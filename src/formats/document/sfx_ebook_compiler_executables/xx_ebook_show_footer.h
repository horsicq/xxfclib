/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_EBOOK_SHOW_FOOTER_PRIVATE_H
#define XX_EBOOK_SHOW_FOOTER_PRIVATE_H
#define EBK_VARIANT_SHOW_FOOTER 3U
#define EBK_SHOW_MAX_TABLE 65536U
#define EBK_SHOW_MAX_FILES 4096U
static bool ebk_show_contains(const uint8_t *bytes, size_t size, const char *text)
{
    size_t n = xx_rt_strlen(text), i;
    for (i = 0; i + n <= size; i++)
        if (!xx_rt_memcmp(bytes + i, text, n)) return true;
    return false;
}
/* The no-assets CMSHOW runtime has only its declared DOS image and footer. */
static bool ebk_show_empty(Abstractformat *f, ebk_info *info, xx_pd_struct *pd)
{
    uint8_t prefix[256], tail[10];
    int64_t total, size, image, tail_size;
    uint32_t pages, last, header;
    if (ebk_stopped(pd)) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address) return false;
    size = total - f->base_address;
    if (size < 263 || !ebk_read_at(f->device, f->base_address, prefix, sizeof(prefix)) || xx_rt_memcmp(prefix, "MZ", 2) || xx_rt_memcmp(prefix + 28, "LZ91", 4) ||
        !ebk_show_contains(prefix, sizeof(prefix), "SHOW.EXE - Runti") || !ebk_show_contains(prefix, sizeof(prefix), "ER MAGIC"))
        return false;
    pages = ebk_le16(prefix + 4);
    last = ebk_le16(prefix + 2);
    header = ebk_le16(prefix + 8) * 16U;
    if (!pages || last > 511U || header < 28U) return false;
    image = (int64_t)(pages - 1U) * 512 + (last ? last : 512U);
    if (image < header || image < (int64_t)sizeof(prefix) || image > size) return false;
    tail_size = size - image;
    if ((tail_size != 7 && tail_size != 10) || !ebk_read_at(f->device, f->base_address + image, tail, (size_t)tail_size)) return false;
    if (tail_size == 7) {
        if (xx_rt_memcmp(tail, "xxxxxxx", 7)) return false;
    } else {
        static const uint8_t footer[10] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 13, 10};
        if (xx_rt_memcmp(tail, footer, 10)) return false;
    }
    xx_rt_memset(info, 0, sizeof(*info));
    info->variant = EBK_VARIANT_SHOW_FOOTER;
    info->payload = image;
    info->first = image;
    info->members_end = image;
    info->names = -1;
    info->end = size;
    info->has_trailer = true;
    xx_rt_memcpy(info->tag, "Show", 5);
    return true;
}
static bool ebk_show_member(Abstractformat *f, ebk_member *m, const char *name, int64_t header, uint32_t header_size, int64_t at, int64_t end, xx_pd_struct *pd)
{
    uint8_t word[4];
    size_t length = xx_rt_strlen(name);
    bool framed = false;
    int64_t size = end - at;
    if (size <= 0 || size > UINT32_MAX || ebk_stopped(pd)) return false;
    if ((length >= 4U && ebk_upper(name[length - 4U]) == '.' &&
         ((ebk_upper(name[length - 3U]) == 'T' && ebk_upper(name[length - 2U]) == 'X' && ebk_upper(name[length - 1U]) == 'T') ||
          (ebk_upper(name[length - 3U]) == 'G' && ebk_upper(name[length - 2U]) == 'I' && ebk_upper(name[length - 1U]) == 'F'))) ||
        (length >= 2U && name[length - 2U] == '.' && ebk_upper(name[length - 1U]) == 'K'))
        framed = true;
    if (framed) {
        if (size < 4 || !ebk_read_at(f->device, f->base_address + at, word, 4) || xx_data_get_u32(word, 4, 0, false) != (uint64_t)size - 4U) return false;
        at += 4;
        size -= 4;
    }
    if (m) {
        m->name = xx_str_dup(name);
        if (!m->name) return false;
        m->header_offset = header;
        m->header_size = header_size;
        m->data_offset = at;
        m->packed_span = size;
        m->packed_size = (uint32_t)size;
        m->raw_size = (uint32_t)size;
    }
    return true;
}
static bool ebk_show_scan(Abstractformat *f, ebk_info *info, ebk_member *items, uint32_t capacity, xx_pd_struct *pd)
{
    uint8_t dos[28], footer[10], marker[7], *table = NULL;
    int64_t size, total, image, names, previous = 0, previous_header = 0;
    uint32_t bytes, count = 0, previous_hsize = 0;
    size_t pos = 0;
    char prior[96] = {0};
    bool ok = false;
    if (!f || !f->device || f->base_address < 0 || ebk_stopped(pd)) return false;
    if (ebk_show_empty(f, info, pd)) return true;
    total = xx_io_total_size(f->device);
    if (total < f->base_address) return false;
    size = total - f->base_address;
    if (size < 128 || !ebk_read_at(f->device, f->base_address, dos, sizeof(dos)) || xx_rt_memcmp(dos, "MZ", 2) || !ebk_read_at(f->device, total - 10, footer, 10) ||
        xx_rt_memcmp(footer + 4, "xxxxx\xfc", 6))
        return false;
    bytes = xx_data_get_u32(footer, 4, 0, false);
    if (!bytes || bytes > EBK_SHOW_MAX_TABLE || bytes > (uint64_t)size - 10U) return false;
    names = size - 10 - bytes;
    {
        uint32_t pages = ebk_le16(dos + 4), last = ebk_le16(dos + 2), header = ebk_le16(dos + 8) * 16U;
        if (!pages || last > 511U || header < 28U) return false;
        image = (int64_t)(pages - 1U) * 512 + (last ? last : 512U);
        if (image < header || image > names - 7) return false;
    }
    if (!ebk_read_at(f->device, f->base_address + image, marker, 7) || xx_rt_memcmp(marker, "xxxxxxx", 7)) return false;
    table = (uint8_t *)xx_mem_alloc(bytes);
    if (!table || !ebk_read_at(f->device, f->base_address + names, table, bytes)) goto done;
    while (pos < bytes) {
        size_t start = pos, n = table[pos++], i;
        char name[96];
        uint32_t at;
        if (!n || n >= sizeof(name) || n + 4U > (size_t)bytes - pos || count >= EBK_SHOW_MAX_FILES || (items && count >= capacity) || ebk_stopped(pd)) goto done;
        for (i = 0; i < n; i++) {
            uint8_t c = table[pos + i];
            if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\') goto done;
            name[i] = (char)c;
        }
        name[n] = 0;
        if (!ebk_safe_output_name(name)) goto done;
        pos += n;
        at = xx_data_get_u32(table + pos, 4, 0, false);
        pos += 4;
        if ((int64_t)at < image + 7 || (int64_t)at >= names || (!count && (int64_t)at != image + 7) || (count && (int64_t)at <= previous)) goto done;
        if (count && !ebk_show_member(f, items ? &items[count - 1U] : NULL, prior, previous_header, previous_hsize, previous, at, pd)) goto done;
        xx_rt_memcpy(prior, name, n + 1U);
        previous = at;
        previous_header = names + (int64_t)start;
        previous_hsize = (uint32_t)(pos - start);
        ++count;
    }
    if (!count || !ebk_show_member(f, items ? &items[count - 1U] : NULL, prior, previous_header, previous_hsize, previous, names, pd)) goto done;
    xx_rt_memset(info, 0, sizeof(*info));
    info->variant = EBK_VARIANT_SHOW_FOOTER;
    info->count = count;
    info->payload = image + 7;
    info->first = image + 7;
    info->members_end = names;
    info->names = names;
    info->names_size = bytes;
    info->end = size;
    info->has_trailer = true;
    xx_rt_memcpy(info->tag, "Show", 5);
    ok = true;
done:
    xx_mem_free(table);
    return ok;
}
static bool ebk_show_copy(Abstractformat *f, const ebk_member *m, xx_io_device *target, xx_pd_struct *pd)
{
    uint8_t buffer[4096];
    uint64_t done = 0;
    while (done < m->raw_size) {
        size_t n = (uint64_t)m->raw_size - done > sizeof(buffer) ? sizeof(buffer) : (size_t)((uint64_t)m->raw_size - done), sent = 0;
        if (ebk_stopped(pd) || !ebk_read_at(f->device, f->base_address + m->data_offset + (int64_t)done, buffer, n)) return false;
        while (target && sent < n) {
            ssize_t written = xx_io_write(target, buffer + sent, n - sent);
            if (written <= 0 || (size_t)written > n - sent) return false;
            sent += (size_t)written;
        }
        done += n;
    }
    return !ebk_stopped(pd);
}
#endif

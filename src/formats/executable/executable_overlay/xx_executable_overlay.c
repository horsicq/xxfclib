/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * A conservative, stored view of executable overlays. An executable carrier
 * is followed at its declared physical end by another executable image,
 * allowing only up to 511 zero bytes or one FF delimiter between images.
 * There is no signature search. DOS page counts, PE sections and auxiliary
 * file ranges, or NE segments/resources determine each image's byte extent.
 * After a validated appended image the remaining bytes are opaque members;
 * neither those bytes nor packed machine code are presented as decompressed.
 * The legacy DOS overlay-number-one, zero-page-count header is an explicit
 * opaque terminal overlay profile, not a claim of a bounded runnable image.
 *
 * Layout references: Microsoft PE/COFF specification; Microsoft MS-DOS
 * Encyclopedia, executable file header; Microsoft Windows NE executable
 * header/segment/resource tables. All reads remain within the source device.
 */
#include "xxfclib/formats/executable/executable_overlay/xx_executable_overlay.h"
#include "../../xx_payload_members.h"

#define XO_MAX_IMAGES 4096U
#define XO_MAX_NE_RESOURCES 10000U

typedef struct xo_image {
    uint64_t size;
    const char *label;
} xo_image;
typedef struct xo_section {
    uint32_t raw, size, rva;
} xo_section;

static uint16_t xo_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t xo_u32(const uint8_t *p)
{
    return (uint32_t)xo_u16(p) | (uint32_t)xo_u16(p + 2) << 16;
}
static bool xo_range(uint64_t limit, uint64_t at, uint64_t size)
{
    return at <= limit && size <= limit - at;
}
static bool xo_read(Abstractformat *f, uint64_t base, uint64_t limit, uint64_t at, void *p, size_t n)
{
    return xo_range(limit, at, n) && base <= (uint64_t)INT64_MAX && at <= (uint64_t)INT64_MAX - base && pm_read(f, (int64_t)(base + at), p, n);
}
static bool xo_extend(uint64_t limit, uint64_t at, uint64_t bytes, uint64_t *end)
{
    if (!xo_range(limit, at, bytes)) return false;
    if (at + bytes > *end) *end = at + bytes;
    return true;
}
static bool xo_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool xo_pe(Abstractformat *f, uint64_t base, uint64_t limit, uint32_t p, uint64_t dos_end, xo_image *image, xx_pd_struct *pd)
{
    uint8_t h[24], optional[112], section[40], directory[8], debug[28];
    uint16_t count, option_size, magic;
    uint32_t directories, headers;
    uint64_t op = (uint64_t)p + 24, table, end, dd, i;
    xo_section sections[96];
    if (!xo_read(f, base, limit, p, h, sizeof(h))) return false;
    count = xo_u16(h + 6);
    option_size = xo_u16(h + 20);
    table = op + option_size;
    if (!count || count > 96 || option_size < 96 || !xo_range(limit, op, option_size) || !xo_range(limit, table, (uint64_t)count * 40) ||
        !xo_read(f, base, limit, op, optional, 96)) return false;
    magic = xo_u16(optional);
    if (magic == 0x10b) {
        directories = xo_u32(optional + 92);
        dd = op + 96;
    } else if (magic == 0x20b) {
        if (option_size < 112 || !xo_read(f, base, limit, op, optional, 112)) return false;
        directories = xo_u32(optional + 108);
        dd = op + 112;
    } else return false;
    if (directories > 16 || (uint64_t)directories * 8 > op + option_size - dd) return false;
    headers = xo_u32(optional + 60);
    if (headers < table + (uint64_t)count * 40 || headers > limit) return false;
    /* Both the DOS load image and Windows image belong to this executable. */
    end = headers > dos_end ? headers : dos_end;
    for (i = 0; i < count; ++i) {
        uint32_t size, raw, reloc, lines;
        uint16_t reloc_count, line_count;
        if (xo_stopped(pd) || !xo_read(f, base, limit, table + i * 40, section, 40)) return false;
        size = xo_u32(section + 16);
        raw = xo_u32(section + 20);
        reloc = xo_u32(section + 24);
        lines = xo_u32(section + 28);
        reloc_count = xo_u16(section + 32);
        line_count = xo_u16(section + 34);
        if (size && (raw < headers || !xo_extend(limit, raw, size, &end))) return false;
        if (reloc_count && (!reloc || !xo_extend(limit, reloc, (uint64_t)reloc_count * 10, &end))) return false;
        if (line_count && (!lines || !xo_extend(limit, lines, (uint64_t)line_count * 6, &end))) return false;
        sections[i].raw = raw;
        sections[i].size = size;
        sections[i].rva = xo_u32(section + 12);
    }
    /* The certificate directory uses physical file offsets, unlike RVAs. */
    if (directories > 4) {
        uint32_t at, size;
        if (!xo_read(f, base, limit, dd + 32, directory, 8)) return false;
        at = xo_u32(directory); size = xo_u32(directory + 4);
        if ((at == 0) != (size == 0) || (size && (at < headers || size < 8 || !xo_extend(limit, at, size, &end)))) return false;
    }
    /* COFF symbols are followed by their length-prefixed string table. */
    {
        uint32_t at = xo_u32(h + 12), symbols = xo_u32(h + 16);
        if (symbols) {
            uint8_t length[4];
            uint64_t strings = (uint64_t)at + (uint64_t)symbols * 18;
            uint32_t size;
            if (!at || !xo_read(f, base, limit, strings, length, 4)) return false;
            size = xo_u32(length);
            if (size < 4 || !xo_extend(limit, strings, size, &end)) return false;
        } else if (at) return false;
    }
    /* Debug directory entries may reference payloads beyond the sections. */
    if (directories > 6) {
        uint32_t rva, bytes;
        bool found = false;
        if (!xo_read(f, base, limit, dd + 48, directory, 8)) return false;
        rva = xo_u32(directory); bytes = xo_u32(directory + 4);
        if ((rva == 0) != (bytes == 0) || bytes % 28 || bytes / 28 > 65536) return false;
        if (bytes) {
            uint64_t at = 0, j;
            for (i = 0; i < count; ++i) {
                if (rva >= sections[i].rva && (uint64_t)rva - sections[i].rva < sections[i].size &&
                    bytes <= sections[i].size - ((uint64_t)rva - sections[i].rva)) {
                    at = (uint64_t)sections[i].raw + rva - sections[i].rva;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
            for (j = 0; j < bytes; j += 28) {
                uint32_t data, size;
                if (xo_stopped(pd) || !xo_read(f, base, limit, at + j, debug, 28)) return false;
                data = xo_u32(debug + 24); size = xo_u32(debug + 16);
                if (size && (!data || !xo_extend(limit, data, size, &end))) return false;
            }
        }
    }
    image->size = end;
    image->label = "pe-image.exe";
    return true;
}

static bool xo_ne(Abstractformat *f, uint64_t base, uint64_t limit, uint32_t p, uint64_t dos_end, xo_image *image, xx_pd_struct *pd)
{
    uint8_t h[64], item[12], word[2];
    uint16_t count, shift, resources, resident;
    uint64_t table, end = dos_end, i;
    uint32_t resource_count = 0;
    bool resource_payload = false;
    if (!xo_read(f, base, limit, p, h, 64)) return false;
    count = xo_u16(h + 28); shift = xo_u16(h + 50);
    table = (uint64_t)p + xo_u16(h + 34);
    if (count > 1024 || shift > 16 || (count && xo_u16(h + 34) < 64) ||
        !xo_extend(limit, table, (uint64_t)count * 8, &end) || !xo_extend(limit, p, 64, &end) ||
        !xo_extend(limit, (uint64_t)p + xo_u16(h + 4), xo_u16(h + 6), &end)) return false;
    for (i = 0; i < count; ++i) {
        uint64_t at, size;
        uint16_t flags;
        if (xo_stopped(pd) || !xo_read(f, base, limit, table + i * 8, item, 8)) return false;
        at = (uint64_t)xo_u16(item) << shift;
        size = xo_u16(item + 2); if (!size) size = 65536;
        flags = xo_u16(item + 4);
        if (!at) continue; /* Uninitialized segment has no physical bytes. */
        if (!xo_extend(limit, at, size, &end)) return false;
        if (flags & 0x100) {
            uint64_t relocation_at = at + size;
            if (!xo_read(f, base, limit, relocation_at, word, 2) ||
                !xo_extend(limit, relocation_at, 2U + (uint64_t)xo_u16(word) * 8, &end)) return false;
        }
    }
    {
        uint32_t at = xo_u32(h + 44);
        uint16_t bytes = xo_u16(h + 32);
        if (bytes && (!at || !xo_extend(limit, at, bytes, &end))) return false;
    }
    resources = xo_u16(h + 36); resident = xo_u16(h + 38);
    if (resources && resources != resident) {
        uint64_t at = (uint64_t)p + resources, resource_end;
        uint32_t total = 0;
        uint16_t resource_shift;
        if (resources < 64 || resident <= resources || !xo_range(limit, p, resident) ||
            !xo_read(f, base, limit, at, word, 2)) return false;
        resource_end = (uint64_t)p + resident;
        resource_shift = xo_u16(word); at += 2;
        if (resource_shift > 16) return false;
        for (;;) {
            uint16_t n;
            if (xo_stopped(pd) || at > resource_end || resource_end - at < 2 || !xo_read(f, base, limit, at, word, 2)) return false;
            if (!xo_u16(word)) break;
            if (resource_end - at < 8 || !xo_read(f, base, limit, at, item, 8)) return false;
            n = xo_u16(item + 2); at += 8;
            if (!n || n > XO_MAX_NE_RESOURCES - total || (uint64_t)n * 12 > resource_end - at) return false;
            total += n;
            resource_count += n;
            for (i = 0; i < n; ++i) {
                uint64_t raw, size;
                if (xo_stopped(pd) || !xo_read(f, base, limit, at, item, 12)) return false;
                raw = (uint64_t)xo_u16(item) << resource_shift;
                size = (uint64_t)xo_u16(item + 2) << resource_shift;
                if (size && raw < (uint64_t)p + 64) return false;
                if (!xo_extend(limit, raw, size, &end)) return false;
                if (size) resource_payload = true;
                at += 12;
            }
        }
        if (!xo_extend(limit, (uint64_t)p + resources, resident - resources, &end)) return false;
    }
    if (!count && (!resource_count || !resource_payload)) return false;
    image->size = end;
    image->label = "ne-image.exe";
    return true;
}

static bool xo_image_at(Abstractformat *f, uint64_t base, uint64_t limit, xo_image *image, bool *opaque, xx_pd_struct *pd)
{
    uint8_t h[64], sig[4];
    uint16_t pages, last, reloc, reloc_at;
    uint64_t header, end;
    uint32_t extended = 0;
    *opaque = false;
    if (xo_stopped(pd) || !xo_read(f, base, limit, 0, h, 28) ||
        !((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M'))) return false;
    pages = xo_u16(h + 4); last = xo_u16(h + 2);
    header = (uint64_t)xo_u16(h + 8) * 16;
    reloc = xo_u16(h + 6); reloc_at = xo_u16(h + 24);
    if (!pages) {
        unsigned i;
        /* A legacy overlay record has an overlay number but no DOS load
         * extent. Preserve it as opaque data only, including its header. */
        if (limit < 32 || last || reloc || header != 32 || reloc_at != 28 || xo_u16(h + 26) != 1) return false;
        for (i = 10; i < 24; ++i) if (h[i]) return false;
        *opaque = true;
        return true;
    }
    end = ((uint64_t)pages - 1) * 512 + (last ? last : 512);
    if (last > 511 || header < 28 || header > end || end > limit || (reloc && (reloc_at < 28 || (uint64_t)reloc_at + (uint64_t)reloc * 4 > header))) return false;
    if (limit >= 64) {
        if (!xo_read(f, base, limit, 28, h + 28, 36)) return false;
        extended = xo_u32(h + 60);
    }
    if (extended >= 64 && xo_range(limit, extended, 4)) {
        if (!xo_read(f, base, limit, extended, sig, 4)) return false;
        if (!xx_rt_memcmp(sig, "PE\0\0", 4)) return xo_pe(f, base, limit, extended, end, image, pd);
        if (!xx_rt_memcmp(sig, "NE", 2)) return xo_ne(f, base, limit, extended, end, image, pd);
        /* Linear executables need a different physical page-map grammar;
         * their DOS stub does not delimit their executable image. */
        if (!xx_rt_memcmp(sig, "LE", 2) || !xx_rt_memcmp(sig, "LX", 2)) return false;
    }
    image->size = end;
    image->label = "dos-image.exe";
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint64_t at, limit;
    xo_image image;
    bool opaque;
    unsigned images = 0;
    if (available < 64) return false;
    limit = (uint64_t)available;
    if (!xo_image_at(f, 0, limit, &image, &opaque, pd) || opaque || !image.size || image.size >= limit) return false;
    at = image.size;
    while (at < limit) {
        uint8_t gap[513];
        size_t n = limit - at > sizeof(gap) ? sizeof(gap) : (size_t)(limit - at), padding = 0;
        uint64_t next;
        if (xo_stopped(pd) || !xo_read(f, 0, limit, at, gap, n)) return false;
        while (padding < n && padding < 511 && !gap[padding]) ++padding;
        if (!padding && gap[0] == 255) padding = 1;
        next = at + padding;
        if (next >= limit || !xo_image_at(f, next, limit - next, &image, &opaque, pd)) {
            if (!images) return false;
            if (!pm_add(f, s, "overlay.bin", (int64_t)at, (int64_t)(limit - at))) return false;
            at = limit;
            break;
        }
        if (padding && !pm_add(f, s, "padding.bin", (int64_t)at, (int64_t)padding)) return false;
        if (opaque) {
            if (!pm_add(f, s, "overlay.bin", (int64_t)next, (int64_t)(limit - next))) return false;
            ++images;
            at = limit;
            break;
        }
        if (!image.size || image.size > limit - next || ++images > XO_MAX_IMAGES ||
            !pm_add(f, s, image.label, (int64_t)next, (int64_t)image.size)) return false;
        at = next + image.size;
    }
    if (!images || xo_stopped(pd)) return false;
    s->size = available;
    return true;
}

static void xo_vtable_destroy(Abstractformat *f)
{
    xx_executable_overlay_destroy((xx_executable_overlay *)f);
}
void xx_executable_overlay_init(xx_executable_overlay *r, xx_io_device *d, int64_t base)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, base, XX_FILE_TYPE_EXECUTABLE_OVERLAY, "exe");
    r->format.handle_type = XX_FILE_TYPE_EXECUTABLE_OVERLAY;
    r->format.destroy = xo_vtable_destroy;
    xx_format_set_mime_type(&r->format, "application/x-executable-overlay");
}
xx_executable_overlay *xx_executable_overlay_create(xx_io_device *d, int64_t base)
{
    xx_executable_overlay *r = (xx_executable_overlay *)xx_mem_alloc(sizeof(*r));
    if (r) xx_executable_overlay_init(r, d, base);
    return r;
}
xx_executable_overlay *xx_executable_overlay_new(xx_io_device *d)
{
    return xx_executable_overlay_create(d, 0);
}
void xx_executable_overlay_destroy(xx_executable_overlay *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_executable_overlay_free(xx_executable_overlay *r)
{
    if (!r) return;
    xx_executable_overlay_destroy(r);
    xx_mem_free(r);
}
bool xx_executable_overlay_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_executable_overlay_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
bool xx_executable_overlay_unpack_current_archive_record_to_device(Abstractformat *f, xx_archive_record_state *st, xx_io_device *destination, xx_pd_struct *pd)
{
    pm_stream *stream;
    pm_member *member;
    uint8_t *buffer;
    size_t capacity = xx_get_file_buffer_size();
    int64_t cursor, offset, remaining;
    bool result = true;
    const xx_var *max_size;
    if (!f || !f->device || !st || st->format != f || !st->has_record || !st->internal_state || xo_stopped(pd) || !capacity) return false;
    stream = (pm_stream *)st->internal_state;
    if (stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    max_size = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (max_size && (uint64_t)member->size > xx_var_get_u64(max_size)) return false;
    offset = member->offset - f->base_address;
    remaining = member->size;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    cursor = xx_io_tell(f->device);
    while (remaining > 0) {
        size_t n = (uint64_t)remaining > capacity ? capacity : (size_t)remaining, written = 0;
        if (xo_stopped(pd) || !pm_read(f, offset, buffer, n)) { result = false; break; }
        while (destination && written < n) {
            ssize_t amount;
            if (xo_stopped(pd)) { result = false; break; }
            amount = xx_io_write(destination, buffer + written, n - written);
            if (amount <= 0 || (size_t)amount > n - written) { result = false; break; }
            written += (size_t)amount;
        }
        if (!result) break;
        offset += (int64_t)n;
        remaining -= (int64_t)n;
    }
    if (xo_stopped(pd)) result = false;
    if (xx_io_seek64(f->device, cursor, SEEK_SET) != 0) result = false;
    xx_mem_free(buffer);
    return result;
}

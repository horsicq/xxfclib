/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xx_format_fast_detect_rules.h"
#include <stdint.h>

#if !XFU_FORMAT_SUPPORT_FULL
#include "xxfclib/formats/xx_format_extractor.h"
/* Compression prefixes are scheduling hints only; the selected native
 * compound reader still verifies its stream and the TAR inside it. */
static int restricted_stream_signature(xx_file_type_t type, xx_io_device *device, int64_t base)
{
    static const struct { xx_file_type_t type; uint8_t bytes[9]; size_t length; } rules[] = {
        {XX_FILE_TYPE_GZ, {0x1f, 0x8b, 8}, 3},
        {XX_FILE_TYPE_BZ2, {'B', 'Z', 'h'}, 3},
        {XX_FILE_TYPE_XZ, {0xfd, '7', 'z', 'X', 'Z', 0}, 6},
        {XX_FILE_TYPE_LZIP, {'L', 'Z', 'I', 'P'}, 4},
        {XX_FILE_TYPE_LZOP, {0x89, 'L', 'Z', 'O', 0, 13, 10, 26, 10}, 9},
        {XX_FILE_TYPE_UNIX_COMPRESS, {0x1f, 0x9d}, 2},
        {XX_FILE_TYPE_LZ4, {4, 0x22, 0x4d, 0x18}, 4},
        {XX_FILE_TYPE_LZ4, {2, 0x21, 0x4c, 0x18}, 4},
        {XX_FILE_TYPE_ZSTD, {0x28, 0xb5, 0x2f, 0xfd}, 4}
    };
    uint8_t header[9];
    size_t i, j;
    bool declared = false;
    switch (type) {
        case XX_FILE_TYPE_TAR_GZ: type = XX_FILE_TYPE_GZ; break;
        case XX_FILE_TYPE_TAR_BZ2: type = XX_FILE_TYPE_BZ2; break;
        case XX_FILE_TYPE_TAR_XZ: type = XX_FILE_TYPE_XZ; break;
        case XX_FILE_TYPE_TAR_LZIP: type = XX_FILE_TYPE_LZIP; break;
        case XX_FILE_TYPE_TAR_LZOP: type = XX_FILE_TYPE_LZOP; break;
        case XX_FILE_TYPE_TAR_LZ4: type = XX_FILE_TYPE_LZ4; break;
        case XX_FILE_TYPE_TAR_ZSTD: type = XX_FILE_TYPE_ZSTD; break;
        case XX_FILE_TYPE_TAR_COMPRESS: type = XX_FILE_TYPE_UNIX_COMPRESS; break;
        default: break;
    }
    for (i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i) {
        if (rules[i].type != type) continue;
        declared = true;
        if (!xx_io_read_at(device, base, header, rules[i].length)) continue;
        for (j = 0; j < rules[i].length && header[j] == rules[i].bytes[j]; ++j) {}
        if (j == rules[i].length) return 1;
    }
    return declared ? 0 : -1;
}
/* Keep the ZIP profile's prefix rules identical to xx_zip_fast_detect.json,
 * without retaining the signature catalog for unrelated formats. */
int xx_format_fast_detect_rules(const xx_file_type_t *types, size_t type_count, xx_io_device *device, int64_t base_address, bool is_mapped)
{
    uint8_t header[4];
    size_t i;
    int64_t saved;
    bool zip = false, unknown = false;
    int result = 0;
    (void)is_mapped;
    if (!types || !type_count || !device || base_address < 0) return 0;
    saved = xx_io_tell(device);
    if (saved < 0) return 0;
    for (i = 0; i < type_count; ++i) {
#ifdef XFU_SUPPORT_FORMAT_ZIP
        if (types[i] == XX_FILE_TYPE_ZIP) zip = true;
        else
#endif
        if (xx_format_extractor_get(types[i])) {
            int signature = restricted_stream_signature(types[i], device, base_address);
            if (signature == 1) { result = 1; goto done; }
            if (signature < 0) unknown = true;
        }
    }
    if (!zip) goto done;
    if (xx_io_read_at(device, base_address, header, sizeof(header)) && header[0] == 'P' && header[1] == 'K') {
        if ((header[2] == 0x30 && header[3] == 0x30) || (header[2] == 7 && header[3] == 8)) {
            if (base_address > INT64_MAX - 4 || !xx_io_read_at(device, base_address + 4, header, sizeof(header))) goto done;
        }
        if (header[0] == 'P' && header[1] == 'K' &&
            ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) || (header[2] == 6 && header[3] == 6))) result = 1;
    }
done:
    if (xx_io_seek64(device, saved, SEEK_SET)) return 0;
    return result ? result : unknown ? -1 : 0;
}

size_t xx_format_fast_signature_types(xx_io_device *dev, int64_t base_address, xx_file_type_t *types, size_t capacity)
{
#ifdef XFU_SUPPORT_FORMAT_ZIP
    const xx_file_type_t type = XX_FILE_TYPE_ZIP;
    if (!types || !capacity || xx_format_fast_detect_rules(&type, 1U, dev, base_address, false) != 1) return 0;
    types[0] = type;
    return 1;
#else
    (void)dev; (void)base_address; (void)types; (void)capacity;
    return 0;
#endif
}
#else
enum fast_origin {
    FAST_BASE,
    FAST_END,
    FAST_PE_OVERLAY,
    FAST_NATIVE_PE_OVERLAY,
    FAST_NATIVE_PE_OVERLAY_U32,
    FAST_NATIVE_PE_ENTRY
};
typedef struct fast_atom {
    int64_t offset;
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t length;
    enum fast_origin origin;
    bool negate;
} fast_atom;
typedef struct fast_rule {
    int64_t min_size;
    int64_t max_size;
    size_t first, count;
} fast_rule;
typedef struct fast_type {
    xx_file_type_t type;
    size_t first, count;
} fast_type;

#include "xx_format_fast_detect_rules.inc"

typedef struct fast_context {
    xx_io_device *device;
    int64_t base, end, overlay, entry;
    int overlay_state; /* -2: not resolved; -1: requires structural fallback */
    int native_state, entry_state;
    bool mapped;
} fast_context;

static uint32_t fast_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned fast_u16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}
static bool fast_span(uint64_t at, uint64_t length, uint64_t total)
{
    return at <= total && length <= total - at;
}
static bool fast_read(fast_context *c, uint64_t at, void *out, size_t length)
{
    uint64_t size = (uint64_t)(c->end - c->base);
    return fast_span(at, length, size) && xx_io_read_at(c->device, c->base + (int64_t)at, out, length);
}

/* The native installer readers also accept their raw overlay without an EXE.
 * Read only DOS/PE/section/certificate headers. A more complex carrier falls
 * back to its reader rather than turning the signature path into a scan. */
static int fast_pe_overlay(fast_context *c)
{
    uint8_t dos[64], pe_header[24], header[8], section[40];
    uint64_t n = (uint64_t)(c->end - c->base), end, sections, cert, bytes, at;
    uint32_t pe;
    unsigned count, opt, i, dd, certificates = 0;
    if (n < 2 || !fast_read(c, 0, dos, 2)) return 0;
    if (dos[0] != 'M' || dos[1] != 'Z') {
        c->overlay = c->base;
        return 1;
    }
    if (n < 64 || !fast_read(c, 0, dos, sizeof(dos))) return 0;
    pe = fast_u32(dos + 60);
    if (!fast_read(c, pe, pe_header, sizeof(pe_header)) || pe_header[0] != 'P' || pe_header[1] != 'E' || pe_header[2] || pe_header[3]) return 0;
    count = fast_u16(pe_header + 6);
    opt = fast_u16(pe_header + 20);
    if (!count || opt < 64) return 0;
    if (count > 96 || opt > 4096) return -1;
    sections = (uint64_t)pe + 24 + opt;
    if (!fast_span((uint64_t)pe + 24, opt + (uint64_t)count * 40, n) || !fast_read(c, (uint64_t)pe + 84, header, 4)) return 0;
    end = fast_u32(header);
    for (i = 0; i < count; ++i) {
        uint64_t finish;
        if (!fast_read(c, sections + (uint64_t)i * 40, section, sizeof(section))) return 0;
        finish = (uint64_t)fast_u32(section + 16) + fast_u32(section + 20);
        if (finish > n) return 0;
        if (finish > end) end = finish;
    }
    if (!fast_read(c, (uint64_t)pe + 24, header, 2)) return 0;
    dd = fast_u16(header) == 0x10b ? 128 : fast_u16(header) == 0x20b ? 144 : 0;
    if (dd && opt >= dd + 8 && fast_read(c, (uint64_t)pe + 24 + dd, header, 8)) {
        cert = fast_u32(header);
        bytes = fast_u32(header + 4);
        if (bytes >= 8 && fast_span(cert, bytes, n) && (cert == end || (fast_span(end, bytes, n) && cert + bytes == n))) {
            bool prefix = cert != end;
            if (prefix) {
                uint8_t first[8], last[8];
                bool same = true;
                if (!fast_read(c, end, first, 8) || !fast_read(c, cert, last, 8)) return 0;
                for (i = 0; i < 8; ++i)
                    if (first[i] != last[i]) same = false;
                if (!same) {
                    if (end > n) return 0;
                    c->overlay = c->base + (int64_t)end;
                    return 1;
                }
            }
            at = cert;
            while (at < cert + bytes) {
                uint32_t size;
                if (++certificates > 32) return -1;
                if (!fast_read(c, at, header, 8)) return 0;
                size = fast_u32(header);
                if (size < 8 || !fast_span(at, size, cert + bytes) || fast_u16(header + 4) != 0x200 || fast_u16(header + 6) != 2) return 0;
                at = (at + size + 7) & ~UINT64_C(7);
            }
            if (at != cert + bytes) return 0;
            end = prefix ? end + bytes : at;
        }
    }
    if (end > n) return 0;
    c->overlay = c->base + (int64_t)end;
    return 1;
}

/* Native-PE signatures must not classify managed executables as installers.
 * A declared CLI RVA is conservatively excluded without parsing .NET metadata.
 * Raw installer overlays have no PE optional header and remain supported. */
static int fast_native_pe(fast_context *c)
{
    uint8_t dos[64], pe_header[24], header[8];
    uint64_t optional;
    unsigned opt, magic, dirs, cli;
    if (!fast_read(c, 0, dos, 2)) return 0;
    if (dos[0] != 'M' || dos[1] != 'Z') return 1;
    if (!fast_read(c, 0, dos, sizeof(dos)) || !fast_read(c, fast_u32(dos + 60), pe_header, sizeof(pe_header))) return 0;
    opt = fast_u16(pe_header + 20);
    optional = (uint64_t)fast_u32(dos + 60) + 24;
    if (!fast_read(c, optional, header, 2)) return 0;
    magic = fast_u16(header);
    if (magic != 0x10b && magic != 0x20b) return -1;
    dirs = magic == 0x10b ? 92 : 108;
    cli = magic == 0x10b ? 208 : 224;
    if (opt < dirs + 4) return 1;
    if (!fast_read(c, optional + dirs, header, 4)) return 0;
    if (fast_u32(header) <= 14 || opt < cli + 8) return 1;
    if (!fast_read(c, optional + cli, header, 8)) return 0;
    return fast_u32(header) == 0 ? 1 : 0;
}

/* Translate the entry RVA using backed section bytes only. No address is
 * searched, and a virtual-only section tail cannot become a file offset. */
static int fast_pe_entry(fast_context *c)
{
    uint8_t dos[64], pe_header[24], header[4], section[40];
    uint64_t optional, sections, n = (uint64_t)(c->end - c->base);
    uint32_t rva, headers;
    unsigned count, opt, i;
    if (!fast_read(c, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z' || !fast_read(c, fast_u32(dos + 60), pe_header, sizeof(pe_header))) return 0;
    count = fast_u16(pe_header + 6);
    opt = fast_u16(pe_header + 20);
    optional = (uint64_t)fast_u32(dos + 60) + 24;
    sections = optional + opt;
    if (!fast_read(c, optional + 16, header, 4)) return 0;
    rva = fast_u32(header);
    if (!fast_read(c, optional + 60, header, 4)) return 0;
    headers = fast_u32(header);
    if (rva < headers && rva < n) {
        c->entry = c->base + rva;
        return 1;
    }
    for (i = 0; i < count; ++i) {
        uint32_t address, bytes;
        uint64_t at;
        if (!fast_read(c, sections + (uint64_t)i * 40, section, sizeof(section))) return 0;
        address = fast_u32(section + 12);
        bytes = fast_u32(section + 16);
        if (rva < address || (uint64_t)rva - address >= bytes) continue;
        at = (uint64_t)fast_u32(section + 20) + ((uint64_t)rva - address);
        if (!fast_span(at, 1, n)) return 0;
        c->entry = c->base + (int64_t)at;
        return 1;
    }
    return 0;
}

static int fast_test(fast_context *c, const fast_atom *test)
{
    uint8_t buffer[256];
    int64_t origin, at;
    size_t done = 0;
    bool same = true;
    if (test->origin == FAST_PE_OVERLAY || test->origin == FAST_NATIVE_PE_OVERLAY || test->origin == FAST_NATIVE_PE_OVERLAY_U32 || test->origin == FAST_NATIVE_PE_ENTRY) {
        if (c->mapped) return -1;
        if (c->overlay_state == -2) c->overlay_state = fast_pe_overlay(c);
        if (c->overlay_state != 1) return c->overlay_state;
        if (test->origin != FAST_PE_OVERLAY) {
            if (c->native_state == -2) c->native_state = fast_native_pe(c);
            if (c->native_state != 1) return c->native_state;
        }
        origin = c->overlay;
        if (test->origin == FAST_NATIVE_PE_OVERLAY_U32) {
            uint8_t relative[4];
            uint32_t delta;
            if (!fast_read(c, (uint64_t)(origin - c->base), relative, sizeof(relative))) return 0;
            delta = fast_u32(relative);
            if ((uint64_t)delta > (uint64_t)(c->end - origin)) return 0;
            origin += (int64_t)delta;
        } else if (test->origin == FAST_NATIVE_PE_ENTRY) {
            if (c->entry_state == -2) c->entry_state = fast_pe_entry(c);
            if (c->entry_state != 1) return c->entry_state;
            origin = c->entry;
        }
    } else origin = test->origin == FAST_END ? c->end : c->base;
    if (test->offset == INT64_MIN || (test->offset > 0 && origin > INT64_MAX - test->offset) || (test->offset < 0 && origin < -(test->offset + 1) + 1)) return 0;
    at = origin + test->offset;
    if (at < c->base || at > c->end || (uint64_t)test->length > (uint64_t)(c->end - at)) return 0;
    while (done < test->length) {
        size_t i, amount = test->length - done;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (!xx_io_read_at(c->device, at + (int64_t)done, buffer, amount)) return 0;
        for (i = 0; i < amount; ++i)
            if (((buffer[i] ^ test->bytes[done + i]) & test->mask[done + i]) != 0) same = false;
        done += amount;
    }
    return (same != test->negate) ? 1 : 0;
}

static const fast_type *fast_lookup(xx_file_type_t type)
{
    size_t lo = 0, hi = fast_type_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (fast_types[mid].type == type) return &fast_types[mid];
        if (fast_types[mid].type < type) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

int xx_format_fast_detect_rules(const xx_file_type_t *types, size_t type_count, xx_io_device *device, int64_t base_address, bool is_mapped)
{
    fast_context c;
    int result = 0;
    bool unknown = false;
    int64_t saved;
    size_t t;
    if (!types || !type_count || !device || base_address < 0) return 0;
    saved = xx_io_tell(device);
    c.device = device;
    c.base = base_address;
    c.end = xx_io_total_size(device);
    c.mapped = is_mapped;
    c.overlay = 0;
    c.entry = 0;
    c.overlay_state = -2;
    c.native_state = -2;
    c.entry_state = -2;
    if (saved < 0 || c.end < 0 || base_address >= c.end) return 0;
    for (t = 0; t < type_count && result != 1; ++t) {
        const fast_type *entry = fast_lookup(types[t]);
        size_t r;
        if (!entry) {
            unknown = true;
            continue;
        }
        for (r = 0; r < entry->count; ++r) {
            const fast_rule *rule = &fast_rules[entry->first + r];
            bool match = true, uncertain = false;
            size_t a;
            if (c.end - c.base < rule->min_size || c.end - c.base > rule->max_size) continue;
            for (a = 0; a < rule->count; ++a) {
                int atom = fast_test(&c, &fast_atoms[rule->first + a]);
                if (atom == 0) {
                    match = false;
                    break;
                }
                if (atom < 0) uncertain = true;
            }
            if (match && !uncertain) {
                result = 1;
                break;
            }
            if (match && uncertain) unknown = true;
        }
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) return 0;
    return result == 1 ? 1 : unknown ? -1 : 0;
}

size_t xx_format_fast_signature_types(xx_io_device *dev, int64_t base_address, xx_file_type_t *types, size_t capacity)
{
    size_t index, count = 0U;
    if (!dev || !types || base_address < 0) return 0U;
    for (index = 0U; index < fast_type_count && count < capacity; ++index) {
        xx_file_type_t type = fast_types[index].type;
        if (xx_format_fast_detect_rules(&type, 1U, dev, base_address, false) == 1) types[count++] = type;
    }
    return count;
}

#endif /* restricted format profile */

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Wire layout: Nuitka 4.4 OnefileCompressor.py and OnefileBootstrap.c.
 */
#include "xxfclib/formats/executable/nuitka_onefile/xx_nuitka_onefile.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "../../common/xx_carrier_helpers.h"

#define NUITKA_TOC_LIMIT (UINT64_C(16) << 20)
#define NUITKA_DECODE_LIMIT (UINT64_C(256) << 20)
#define NUITKA_SCAN_LIMIT 65536U
#define NUITKA_MAX_ENTRIES 16384U

typedef struct nuitka_payload {
    int64_t table, body;
    uint64_t table_size, body_size;
    bool compressed;
} nuitka_payload;

typedef struct nuitka_body {
    nuitka_payload payload;
    uint8_t *bytes;
    uint64_t size;
    size_t references;
} nuitka_body;

typedef struct nuitka_member {
    nuitka_body *body;
    uint64_t offset, size;
    uint32_t crc;
    bool checked;
} nuitka_member;

static bool nuitka_reserved(const wchar_t *name, size_t size)
{
    char prefix[8] = {0};
    size_t n = 0;
    while (n < size && n < sizeof(prefix) - 1 && name[n] != L'.') {
        wchar_t c = name[n];
        if (c > 127) return false;
        prefix[n++] = (char)(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
    }
    while (n && prefix[n - 1] == ' ') prefix[--n] = 0;
    if (!xx_rt_strcmp(prefix, "con") || !xx_rt_strcmp(prefix, "prn") || !xx_rt_strcmp(prefix, "aux") ||
        !xx_rt_strcmp(prefix, "nul") || !xx_rt_strcmp(prefix, "conin$") || !xx_rt_strcmp(prefix, "conout$")) return true;
    return n == 4 && prefix[3] >= '0' && prefix[3] <= '9' &&
           (!xx_rt_memcmp(prefix, "com", 3) || !xx_rt_memcmp(prefix, "lpt", 3));
}

static bool nuitka_name(const uint8_t *toc, size_t end, size_t *position, wchar_t *name, size_t *length)
{
    size_t n = 0, segment = 0;
    while (*position + 2 <= end && n < 4096) {
        uint32_t c = xx_data_get_u16(toc + *position, 2, 0, false);
        *position += 2;
        if (!c || c == '\\' || c == '/') {
            if (n == segment || name[n - 1] == L'.' || name[n - 1] == L' ' || nuitka_reserved(name + segment, n - segment)) return false;
            if (!c) {
                name[n] = 0;
                *length = n;
                return true;
            }
            name[n++] = L'/';
            segment = n;
            continue;
        }
        if (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (*position + 2 > end || n >= 4095) return false;
            uint32_t low = xx_data_get_u16(toc + *position, 2, 0, false);
            if (low < 0xdc00 || low > 0xdfff) return false;
            *position += 2;
#if WCHAR_MAX <= 0xffff
            name[n++] = (wchar_t)c;
            c = low;
#else
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
#endif
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
        name[n++] = (wchar_t)c;
    }
    return false;
}

static bool nuitka_read_body(nuitka_body *body, Abstractformat *format, xx_pd_struct *pd)
{
    xx_io_device *memory;
    bool ok;
    if (!body->payload.compressed || body->bytes) return true;
    if (body->size > NUITKA_DECODE_LIMIT) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Nuitka compressed payload exceeds 256 MiB");
        return false;
    }
    body->bytes = (uint8_t *)xx_mem_alloc((size_t)body->size);
    if (!body->bytes) return false;
    memory = xx_io_mem_open(body->bytes, (size_t)body->size);
    ok = memory && xx_zstd_unpack_device_streaming(format->device, format->base_address + body->payload.body, (int64_t)body->payload.body_size, memory, body->size, 0, pd);
    if (memory) xx_io_close(memory);
    if (!ok) {
        xx_mem_free(body->bytes);
        body->bytes = NULL;
    }
    return ok;
}

static bool nuitka_bytes(Abstractformat *format, nuitka_member *member, uint64_t offset, void *output, size_t size)
{
    nuitka_body *body = member->body;
    uint64_t at = member->offset + offset;
    if (body->payload.compressed) {
        xx_rt_memcpy(output, body->bytes + (size_t)at, size);
        return true;
    }
    return pm_read(format, body->payload.body + (int64_t)at, output, size);
}

static bool nuitka_range(Abstractformat *format, pm_member *entry, uint64_t offset, void *output, size_t size, xx_pd_struct *pd)
{
    nuitka_member *member = (nuitka_member *)entry->context;
    if (carrier_stop(pd) || !nuitka_read_body(member->body, format, pd)) return false;
    if (member->crc && !member->checked) {
        uint8_t chunk[16384];
        uint64_t done = 0;
        uint32_t crc = 0;
        while (done < member->size) {
            size_t n = member->size - done > sizeof(chunk) ? sizeof(chunk) : (size_t)(member->size - done);
            if (carrier_stop(pd) || !nuitka_bytes(format, member, done, chunk, n)) return false;
            crc = xx_crc32_calc(crc, chunk, n);
            done += n;
        }
        if ((crc ? crc : 1) != member->crc) return false;
        member->checked = true;
    }
    return nuitka_bytes(format, member, offset, output, size);
}

static void nuitka_release_body(nuitka_body *body)
{
    if (--body->references) return;
    xx_mem_free(body->bytes);
    xx_mem_free(body);
}

static void nuitka_free_member(void *context)
{
    nuitka_member *member = (nuitka_member *)context;
    nuitka_release_body(member->body);
    xx_mem_free(member);
}

static bool nuitka_add_entry(Abstractformat *format, pm_stream *stream, nuitka_body *body, const wchar_t *name, uint64_t offset, uint64_t size, uint32_t crc)
{
    int64_t raw_size = body->payload.compressed ? 0 : (int64_t)size;
    int64_t raw_at = body->payload.body + (body->payload.compressed ? 0 : (int64_t)offset);
    pm_member *entry;
    if (!pm_add(format, stream, "member", raw_at, raw_size)) return false;
    entry = &stream->items[stream->count - 1];
    entry->display_name = xx_str_unicode_to_utf8(name);
    if (!entry->display_name) return false;
    entry->size = (int64_t)size;
    if (body->payload.compressed || crc) {
        nuitka_member *member = (nuitka_member *)xx_mem_alloc(sizeof(*member));
        if (!member) return false;
        *member = (nuitka_member){body, offset, size, crc, false};
        ++body->references;
        entry->context = member;
        entry->free_context = nuitka_free_member;
        entry->read_range = nuitka_range;
    }
    return true;
}

static bool nuitka_walk_toc(Abstractformat *format, const nuitka_payload *payload, const uint8_t *toc, bool checksums, pm_stream *stream, nuitka_body *body,
                            uint64_t *decoded_size, xx_pd_struct *pd)
{
    uint64_t total = 0;
    size_t at = 0, header_size = checksums ? 16 : 12;
    size_t limit = (size_t)payload->table_size;
    for (unsigned i = 0; i < NUITKA_MAX_ENTRIES && at < limit; ++i) {
        wchar_t name[4097];
        size_t length;
        uint64_t size;
        uint32_t flags, crc;
        if (carrier_stop(pd) || limit - at < header_size) return false;
        flags = xx_data_get_u32(toc + at, 4, 0, false);
        crc = checksums ? xx_data_get_u32(toc + at + 4, 4, 0, false) : 0;
        size = carrier_u64(toc + at + header_size - 8);
        at += header_size;
        if ((flags & ~5U) || size > (uint64_t)INT64_MAX - total ||
            !nuitka_name(toc, limit, &at, name, &length)) return false;
        if (!i && (size < 64 || length < 5 ||
                   (xx_rt_wcscmp(name + length - 4, L".dll") && xx_rt_wcscmp(name + length - 4, L".exe")))) return false;
        if (stream && !nuitka_add_entry(format, stream, body, name, total, size, crc)) return false;
        total += size;
        if (flags & 4U) {
            if (at != limit || (!payload->compressed && total != payload->body_size)) return false;
            *decoded_size = total;
            return true;
        }
    }
    return false;
}

static bool nuitka_try_payload(Abstractformat *format, int64_t at, int64_t end, const uint8_t header[16], pm_stream *stream, xx_pd_struct *pd)
{
    nuitka_payload payload;
    uint8_t magic[4], *toc;
    uint64_t decoded_size;
    bool valid, checksums = false;
    payload.table = at + 16;
    payload.table_size = carrier_u64(header);
    payload.body_size = carrier_u64(header + 8);
    if (payload.table_size < 22 || payload.table_size > NUITKA_TOC_LIMIT || payload.body_size < sizeof(magic) ||
        !carrier_range(end, (uint64_t)payload.table, payload.table_size)) return false;
    payload.body = payload.table + (int64_t)payload.table_size;
    if (!carrier_range(end, (uint64_t)payload.body, payload.body_size) || !pm_read(format, payload.body, magic, sizeof(magic))) return false;
    payload.compressed = !xx_rt_memcmp(magic, "\x28\xb5\x2f\xfd", 4);
    if (!payload.compressed && xx_rt_memcmp(magic, "MZ", 2)) return false;

    toc = (uint8_t *)xx_mem_alloc((size_t)payload.table_size);
    if (!toc) return false;
    valid = pm_read(format, payload.table, toc, (size_t)payload.table_size);
    if (valid && !nuitka_walk_toc(format, &payload, toc, false, NULL, NULL, &decoded_size, pd)) {
        checksums = true;
        valid = nuitka_walk_toc(format, &payload, toc, true, NULL, NULL, &decoded_size, pd);
    }
    if (valid && stream) {
        nuitka_body *body = (nuitka_body *)xx_mem_calloc(1, sizeof(*body));
        if (!body) valid = false;
        else {
            body->references = 1;
            body->payload = payload;
            body->size = decoded_size;
            valid = nuitka_walk_toc(format, &payload, toc, checksums, stream, body, &decoded_size, pd);
            nuitka_release_body(body);
        }
    }
    xx_mem_free(toc);
    return valid;
}

static bool nuitka_locate(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    static const uint8_t marker[] = "NUITKA_ONEFILE_PARENT";
    uint8_t dos[64], coff[24], section[40], header[16], *prefix;
    int64_t image_end, cab, cab_end, pe;
    int64_t available = pm_available(format), data_at = -1, data_end = -1;
    size_t prefix_size;
    unsigned count;
    bool found = false;
    if (!pm_read(format, 0, dos, sizeof(dos)) || xx_rt_memcmp(dos, "MZ", 2) || !carrier_pe(format, &image_end, &cab, &cab_end, pd)) return false;
    pe = xx_data_get_u32(dos + 60, 4, 0, false);
    if (!pm_read(format, pe, coff, sizeof(coff))) return false;
    count = xx_data_get_u16(coff + 6, 2, 0, false);
    pe += 24 + xx_data_get_u16(coff + 20, 2, 0, false);
    for (unsigned i = 0; i < count; ++i) {
        if (carrier_stop(pd) || !pm_read(format, pe + 40 * i, section, sizeof(section))) return false;
        if (xx_rt_memcmp(section, ".rdata\0", 8)) continue;
        data_at = xx_data_get_u32(section + 20, 4, 0, false);
        data_end = data_at + xx_data_get_u32(section + 16, 4, 0, false);
        break;
    }
    if (data_at < 0 || data_end <= data_at || data_end > available ||
        xx_io_find_bytes_buffer_optimize_ex(format->device, format->base_address + data_at, data_end - data_at,
                                            marker, sizeof(marker), NUITKA_SCAN_LIMIT, pd) < 0)
        return false;

    if (available - image_end >= 24 && pm_read(format, available - 8, header, 8)) {
        uint64_t size = carrier_u64(header);
        if (size >= 16 && size <= (uint64_t)(available - image_end - 8)) {
            int64_t offset = available - 8 - (int64_t)size;
            if (pm_read(format, offset, header, sizeof(header)) &&
                nuitka_try_payload(format, offset, available - 8, header, stream, pd)) return true;
            if (stream && stream->count) return false;
        }
    }

    prefix_size = data_end - data_at > NUITKA_SCAN_LIMIT ? NUITKA_SCAN_LIMIT : (size_t)(data_end - data_at);
    prefix = (uint8_t *)xx_mem_alloc(prefix_size);
    if (!prefix) return false;
    if (pm_read(format, data_at, prefix, prefix_size)) {
        for (size_t i = 0; i + sizeof(header) <= prefix_size && !carrier_stop(pd); i += 8) {
            if (nuitka_try_payload(format, data_at + (int64_t)i, data_end, prefix + i, stream, pd)) {
                found = true;
                break;
            }
            if (stream && stream->count) break;
        }
    }
    xx_mem_free(prefix);
    return found;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    if (!nuitka_locate(format, stream, pd)) return false;
    stream->size = pm_available(format);
    return true;
}

void xx_nuitka_onefile_init(xx_nuitka_onefile *reader, xx_io_device *device, int64_t base)
{
    pm_init(&reader->format, device, base, XX_FILE_TYPE_NUITKA_ONEFILE, "exe");
    reader->format.check_is_valid = xx_nuitka_onefile_check_is_valid;
}

xx_nuitka_onefile *xx_nuitka_onefile_create(xx_io_device *device, int64_t base)
{
    xx_nuitka_onefile *reader = (xx_nuitka_onefile *)xx_mem_calloc(1, sizeof(*reader));
    if (reader) xx_nuitka_onefile_init(reader, device, base);
    return reader;
}

void xx_nuitka_onefile_destroy(xx_nuitka_onefile *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_nuitka_onefile_free(xx_nuitka_onefile *reader)
{
    xx_nuitka_onefile_destroy(reader);
    xx_mem_free(reader);
}

bool xx_nuitka_onefile_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    if (!format || !format->device || carrier_stop(pd)) return false;
    int64_t cursor = xx_io_tell(format->device);
    bool found = nuitka_locate(format, NULL, pd);
    if (cursor >= 0) (void)xx_io_seek64(format->device, cursor, SEEK_SET);
    return found;
}

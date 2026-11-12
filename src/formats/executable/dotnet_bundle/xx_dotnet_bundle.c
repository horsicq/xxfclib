/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * .NET single-file bundle (.NET Core 3 to .NET 9+). Layout facts from the
 * dotnet/runtime sources (Microsoft.NET.HostModel/Bundle/Manifest.cs,
 * corehost/bundle/manifest.cpp): the apphost image holds a 40-byte
 * placeholder, the 8-byte little-endian offset of the manifest followed by
 * the 32-byte bundle signature (SHA-256 of ".net core bundle"). The manifest:
 *   uint32 major, uint32 minor, int32 count, string bundle_id
 *   (major >= 2) int64 deps_json_offset/size, runtimeconfig offset/size,
 *                uint64 flags
 *   count entries: int64 offset, int64 size, (major >= 6) int64 compressed,
 *                  uint8 type, string relative_path
 * Strings carry a 7-bit variable-length byte count and UTF-8 text. A nonzero
 * compressed size means the entry is a raw Deflate stream of that many bytes.
 * Entries are listed as members (one output leaf each); nothing runs.
 */
#include "xxfclib/formats/executable/dotnet_bundle/xx_dotnet_bundle.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"

#define DB_SEARCH_WINDOW (32 * 1024 * 1024)
#define DB_MAX_MANIFEST (8U * 1024U * 1024U)
#define DB_MAX_ENTRIES 65535U
#define DB_MAX_PATH 1024U

static const uint8_t db_signature[32] = {0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38, 0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
                                         0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18, 0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae};

/* A .NET length-prefixed string: 7-bit byte count, then UTF-8. */
static bool db_string(const uint8_t *p, size_t n, size_t *at, char *out, size_t capacity)
{
    size_t length = 0, i;
    unsigned shift = 0;
    for (i = 0; i < 5U; ++i) {
        uint8_t c;
        if (*at >= n) return false;
        c = p[(*at)++];
        length |= (size_t)(c & 0x7fU) << shift;
        shift += 7U;
        if (!(c & 0x80U)) break;
        if (i == 4U) return false;
    }
    if (length >= capacity || length > n - *at) return false;
    for (i = 0; i < length; ++i) {
        uint8_t c = p[*at + i];
        if (c < 0x20U && c != '\t') return false;
        out[i] = (char)c;
    }
    out[length] = 0;
    *at += length;
    return true;
}

/* A Deflate-compressed entry, decoded to the destination (or, when only
 * validated, to a temporary device) and checked against its declared size. */
static bool db_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    xx_io_device *sink = device ? device : xx_io_temp_open();
    int64_t before, after;
    bool ok;
    if (!sink) return false;
    if (m->packed_size <= 0 || m->size < 0) {
        if (!device) xx_io_close(sink);
        return false;
    }
    before = xx_io_total_size(sink);
    ok = xx_deflate_unpack_device(f->device, m->offset, m->packed_size, sink, false, pd);
    after = xx_io_total_size(sink);
    if (ok && before >= 0 && after >= 0 && after - before != m->size) ok = false;
    if (!device) xx_io_close(sink);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t head[2], offset_bytes[8], *manifest = NULL;
    int64_t total = pm_available(f), window, found, header, manifest_size;
    uint32_t major, minor, count, i;
    size_t at = 0;
    char text[DB_MAX_PATH];
    bool ok = false;
    if (total < 64 || !pm_read(f, 0, head, 2) || ((head[0] != 'M' || head[1] != 'Z') && (head[0] != 'Z' || head[1] != 'M'))) return false;
    window = total < DB_SEARCH_WINDOW ? total : DB_SEARCH_WINDOW;
    found = xx_io_find_bytes_buffer_optimize_ex(f->device, f->base_address + 8, window - 8, db_signature, sizeof(db_signature), xx_get_file_buffer_size(), pd);
    if (found < f->base_address + 8 || (pd && xx_pd_is_stopped(pd))) return false;
    if (!pm_read(f, found - f->base_address - 8, offset_bytes, 8)) return false;
    header = (int64_t)xx_data_get_u64(offset_bytes, 8, 0, false);
    if (header <= 0 || header >= total || header < found - f->base_address + 32) return false;
    manifest_size = total - header;
    if (manifest_size < 16 || manifest_size > (int64_t)DB_MAX_MANIFEST) return false;
    manifest = (uint8_t *)xx_mem_alloc((size_t)manifest_size);
    if (!manifest || !pm_read(f, header, manifest, (size_t)manifest_size)) goto done;
    major = xx_data_get_u32(manifest, 4, 0, false);
    minor = xx_data_get_u32(manifest + 4, 4, 0, false);
    count = xx_data_get_u32(manifest + 8, 4, 0, false);
    (void)minor;
    if (major < 1U || major > 64U || !count || count > DB_MAX_ENTRIES) goto done;
    at = 12U;
    if (!db_string(manifest, (size_t)manifest_size, &at, text, sizeof(text))) goto done;
    if (major >= 2U) {
        if ((size_t)manifest_size - at < 40U) goto done;
        at += 40U;
    }
    for (i = 0; i < count; ++i) {
        int64_t offset, size, packed = 0;
        uint8_t type;
        pm_member *m;
        if ((pd && xx_pd_is_stopped(pd)) || (size_t)manifest_size - at < 17U) goto done;
        offset = (int64_t)xx_data_get_u64(manifest + at, 8, 0, false);
        size = (int64_t)xx_data_get_u64(manifest + at + 8, 8, 0, false);
        at += 16U;
        if (major >= 6U) {
            if ((size_t)manifest_size - at < 9U) goto done;
            packed = (int64_t)xx_data_get_u64(manifest + at, 8, 0, false);
            at += 8U;
        }
        type = manifest[at++];
        if (!db_string(manifest, (size_t)manifest_size, &at, text, sizeof(text))) goto done;
        /* Entries precede the manifest; a compressed entry occupies its packed
         * bytes, a stored one its size. */
        if (type > 5U || offset < 0 || size < 0 || packed < 0 || offset >= header) goto done;
        if (packed ? packed > header - offset : size > header - offset) goto done;
        if (!pm_add(f, s, text, offset, packed ? packed : size)) goto done;
        m = &s->items[s->count - 1U];
        if (packed) {
            m->size = size;
            m->packed_size = packed;
            m->compression_method = 8U;
            m->read_all = db_read_all;
        }
    }
    s->size = total;
    ok = s->count > 0;
done:
    if (manifest) xx_mem_free(manifest);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

void xx_dotnet_bundle_init(xx_dotnet_bundle *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DOTNET_BUNDLE, "exe");
    }
}
xx_dotnet_bundle *xx_dotnet_bundle_create(xx_io_device *d, int64_t b)
{
    xx_dotnet_bundle *r = (xx_dotnet_bundle *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dotnet_bundle_init(r, d, b);
    return r;
}
void xx_dotnet_bundle_destroy(xx_dotnet_bundle *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dotnet_bundle_free(xx_dotnet_bundle *r)
{
    if (r) {
        xx_dotnet_bundle_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dotnet_bundle_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dotnet_bundle_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/0x0L/rs-utils/blob/master/bin/psarc.py
 * PSARC 1.4 unencrypted TOC, stored/zlib blocks and manifest paths. Encrypted
 * TOCs remain unsupported. TEST decodes in bounded memory and checks zlib
 * trailers; manifest paths are checked against the TOC's MD5 path digests.
 */
#include "xxfclib/formats/sony_psarc/xx_sony_psarc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include <string.h>

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
typedef struct ps_block {
    uint64_t at;
    uint32_t packed, size;
} ps_block;
typedef struct ps_member {
    uint32_t count;
    ps_block blocks[1];
} ps_member;
static void ps_free(void *p)
{
    xx_mem_free(p);
}
static bool ps_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool ps_write(xx_io_device *out, const uint8_t *bytes, size_t size)
{
    size_t done = 0;
    if (!out) return true;
    while (done < size) {
        ssize_t n = xx_io_write(out, bytes + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}
static bool ps_read(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    ps_member *ctx = (ps_member *)m->context;
    uint8_t packed[65536], plain[65536];
    uint64_t total = 0;
    uint32_t i;
    for (i = 0; i < ctx->count; ++i) {
        ps_block *b = &ctx->blocks[i];
        const uint8_t *decoded = packed;
        if (ps_stop(pd) || !b->packed || b->packed > sizeof(packed) || b->size > sizeof(plain) || !pm_read(f, (int64_t)b->at, packed, b->packed)) return false;
        if (b->packed != b->size) {
            xx_io_device *memory;
            size_t consumed = 0;
            bool ok;
            if (b->packed < 6 || !xx_zlib_stream_header_is_valid(packed, b->packed)) return false;
            memory = xx_io_mem_open(plain, b->size);
            if (!memory) return false;
            ok = xx_deflate_unpack_memory_to_device_ex(packed + 2, b->packed - 6, memory, &consumed, false, pd) && consumed == b->packed - 6 &&
                 xx_io_tell(memory) == b->size && xx_zlib_stream_trailer_matches(packed, b->packed, plain, b->size);
            xx_io_close(memory);
            if (!ok) return false;
            decoded = plain;
        }
        if (b->size > (uint64_t)m->size - total || !ps_write(out, decoded, b->size)) return false;
        total += b->size;
    }
    return total == (uint64_t)m->size && !ps_stop(pd);
}
static bool ps_name(char *name)
{
    size_t i, start = 0, n = xx_rt_strlen(name);
    while (n >= 2 && name[0] == '.' && (name[1] == '/' || name[1] == '\\')) {
        memmove(name, name + 2, n - 1);
        n -= 2;
    }
    if (!n || n > 4096 || name[0] == '/' || name[0] == '\\') return false;
    for (i = 0; i <= n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == '\\') name[i] = '/';
        if (c && (c < 32 || c == 127 || strchr(":<>\"|?*", c))) return false;
        if (!c || name[i] == '/') {
            size_t len = i - start;
            if (!len || (len == 1 && name[start] == '.') || (len == 2 && name[start] == '.' && name[start + 1] == '.') || name[i - 1] == '.' || name[i - 1] == ' ')
                return false;
            start = i + 1;
        }
    }
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32], e[30], b[2], *manifest = NULL;
    uint32_t toc, count, block, i, zcount, flags;
    uint64_t available = (uint64_t)pm_available(f);
    bool result = false;
    if (!pm_read(f, 0, h, 32) || xx_rt_memcmp(h, "PSAR", 4) || xx_data_get_u32(h + 4, 4, 0, true) != 0x10004 || xx_rt_memcmp(h + 8, "zlib", 4) ||
        xx_data_get_u32(h + 16, 4, 0, true) != 30 || xx_data_get_u32(h + 24, 4, 0, true) != 65536 || (xx_data_get_u32(h + 28, 4, 0, true) & ~3U))
        return false;
    toc = xx_data_get_u32(h + 12, 4, 0, true);
    count = xx_data_get_u32(h + 20, 4, 0, true);
    block = 65536;
    flags = r32(h + 28, true);
    if (!count || count > 65536 || toc < 32 + (uint64_t)count * 30 || toc > pm_available(f) || (toc - 32 - count * 30) & 1) return false;
    zcount = (toc - 32 - count * 30) / 2;
    if (zcount > 1000000U) return false;
    for (i = 0; i < count; ++i) {
        uint64_t size = 0, off = 0, j, left, pos, packed = 0;
        uint32_t index, blocks;
        char label[40];
        ps_member *ctx;
        size_t allocation;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 32 + (int64_t)i * 30, e, 30)) return false;
        index = xx_data_get_u32(e + 16, 4, 0, true);
        for (j = 0; j < 5; ++j) {
            size = (size << 8) | e[20 + j];
            off = (off << 8) | e[25 + j];
        }
        if (off < toc || off > available || index > zcount || (size + block - 1) / block > zcount - index) return false;
        blocks = (uint32_t)((size + block - 1) / block);
        allocation = sizeof(*ctx) + (blocks ? (size_t)(blocks - 1) * sizeof(ctx->blocks[0]) : 0);
        ctx = (ps_member *)xx_mem_alloc(allocation);
        if (!ctx) return false;
        ctx->count = blocks;
        left = size;
        pos = off;
        for (j = 0; left; ++j) {
            uint32_t actual, want = left > block ? block : (uint32_t)left;
            if (ps_stop(pd) || !pm_read(f, 32 + (int64_t)count * 30 + ((int64_t)index + j) * 2, b, 2)) {
                xx_mem_free(ctx);
                return false;
            }
            actual = xx_data_get_u16(b, 2, 0, true);
            if (!actual) actual = block;
            if (pos > available || actual > available - pos) {
                xx_mem_free(ctx);
                return false;
            }
            ctx->blocks[j].at = pos;
            ctx->blocks[j].packed = actual;
            ctx->blocks[j].size = want;
            pos += actual;
            packed += actual;
            left -= want;
        }
        xx_rt_snprintf(label, sizeof(label), i ? "file-%u.bin" : "manifest.txt", (unsigned)i);
        if (!pm_add(f, s, label, (int64_t)off, (int64_t)packed)) {
            xx_mem_free(ctx);
            return false;
        }
        {
            pm_member *m = &s->items[s->count - 1];
            m->size = (int64_t)size;
            m->context = ctx;
            m->free_context = ps_free;
            m->read_all = ps_read;
            m->compression_method = packed == size ? 0 : 8;
            if (!i) {
                m->display_name = xx_str_dup(label);
                if (!m->display_name) return false;
            }
        }
    }
    /* The unnamed first entry is the newline-separated path manifest. */
    if (s->items[0].size > 32U * 1024U * 1024U) return false;
    manifest = (uint8_t *)xx_mem_alloc((size_t)s->items[0].size + 1);
    if (!manifest) return false;
    {
        xx_io_device *memory = xx_io_mem_open(manifest, (size_t)s->items[0].size);
        bool ok = memory && ps_read(f, &s->items[0], memory, pd);
        xx_io_close(memory);
        if (!ok) goto done;
    }
    manifest[s->items[0].size] = 0;
    {
        size_t at = 0, end = (size_t)s->items[0].size;
        for (i = 1; i < count; ++i) {
            char name[4097], digest_name[4097];
            uint8_t digest[16];
            size_t start = at, len, k;
            if (ps_stop(pd) || at >= end) goto done;
            while (at < end && manifest[at] != '\n') {
                if (!manifest[at]) goto done;
                ++at;
            }
            len = at - start;
            if (len && manifest[start + len - 1] == '\r') --len;
            if (!len || len > 4096) goto done;
            memcpy(name, manifest + start, len);
            name[len] = 0;
            memcpy(digest_name, name, len + 1);
            if (flags & 1)
                for (k = 0; k < len; ++k)
                    if (digest_name[k] >= 'A' && digest_name[k] <= 'Z') digest_name[k] += 'a' - 'A';
            if (!pm_read(f, 32 + (int64_t)i * 30, e, 16) || !xx_md5_memory(digest_name, len, digest) || memcmp(e, digest, 16)) goto done;
            if (flags & 2) {
                if (name[0] != '/') goto done;
                memmove(name, name + 1, len);
            }
            if (!ps_name(name)) goto done;
            s->items[i].display_name = xx_str_dup(name);
            if (!s->items[i].display_name) goto done;
            if (at < end) ++at;
        }
        if (at != end) goto done;
    }
    s->size = (int64_t)available;
    result = true;
done:
    xx_mem_free(manifest);
    return result;
}

void xx_sony_psarc_init(xx_sony_psarc *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SONY_PSARC, "psarc");
    }
}
xx_sony_psarc *xx_sony_psarc_create(xx_io_device *d, int64_t b)
{
    xx_sony_psarc *r = (xx_sony_psarc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sony_psarc_init(r, d, b);
    return r;
}
void xx_sony_psarc_destroy(xx_sony_psarc *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sony_psarc_free(xx_sony_psarc *r)
{
    if (r) {
        xx_sony_psarc_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sony_psarc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sony_psarc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

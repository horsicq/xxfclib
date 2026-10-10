/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Private bounded RAM and Deflate member helpers; no container grammar. */
#ifndef XX_MEMORY_DEFLATE_MEMBERS_H
#define XX_MEMORY_DEFLATE_MEMBERS_H
#include "xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <string.h>
#define MDM_MEMORY_LIMIT ((size_t)256U * 1024U * 1024U)

static XXFC_MAYBE_UNUSED uint16_t mdm_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static XXFC_MAYBE_UNUSED uint32_t mdm_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static XXFC_MAYBE_UNUSED bool mdm_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

/* Exact-capacity sink: a malformed stream cannot allocate or emit more than
 * the declared logical member size. The Deflate engine monitors cancellation. */
typedef struct mdm_sink {
    uint8_t *data;
    size_t size, used;
} mdm_sink;
static XXFC_MAYBE_UNUSED ssize_t mdm_write(xx_io_device *d, const void *p, size_t n)
{
    mdm_sink *s = (mdm_sink *)d->priv;
    if (n > s->size - s->used) return -1;
    if (n) memcpy(s->data + s->used, p, n);
    s->used += n;
    return (ssize_t)n;
}
static XXFC_MAYBE_UNUSED bool mdm_inflate(const uint8_t *p, size_t n, uint8_t *out, size_t size, bool wrapped, xx_pd_struct *pd)
{
    xx_io_device device = {0};
    mdm_sink sink;
    size_t consumed = 0U;
    if (mdm_stopped(pd)) return false;
    if (wrapped) {
        if (n < 6U || !xx_zlib_stream_header_is_valid(p, n)) return false;
        p += 2U;
        n -= 6U;
    }
    sink.data = out;
    sink.size = size;
    sink.used = 0U;
    device.priv = &sink;
    device.write = mdm_write;
    if (!xx_deflate_unpack_memory_to_device_ex(p, n, &device, &consumed, false, pd) || consumed != n || sink.used != size || mdm_stopped(pd)) return false;
    if (wrapped) {
        uint32_t expected = ((uint32_t)p[n] << 24) | ((uint32_t)p[n + 1U] << 16) | ((uint32_t)p[n + 2U] << 8) | (uint32_t)p[n + 3U];
        if (xx_zlib_stream_adler32(out, size) != expected) return false;
    }
    return true;
}

/* Source filenames are not required for finite compressed streams. Where the
 * container supplies a filename, retain it after rejecting unsafe components. */
static XXFC_MAYBE_UNUSED char *mdm_name(const uint8_t *p, size_t n)
{
    char *name;
    size_t i, start = 0U;
    if (!n || n > 4096U || p[0] == '/' || p[0] == '\\') return NULL;
    name = (char *)xx_mem_alloc(n + 1U);
    if (!name) return NULL;
    for (i = 0; i <= n; ++i) {
        unsigned char c = i < n ? p[i] : 0U;
        if (i < n && (c < 32U || c == 127U || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (c == '/' || c == '\\' || i == n) {
            size_t len = i - start;
            if ((len == 1U && p[start] == '.') || (len == 2U && p[start] == '.' && p[start + 1U] == '.') || (!len && i < n) ||
                (len && (p[i - 1U] == '.' || p[i - 1U] == ' ')))
                goto bad;
            start = i + 1U;
        }
        name[i] = c == '\\' ? '/' : (char)c;
    }
    return name;
bad:
    xx_mem_free(name);
    return NULL;
}

static XXFC_MAYBE_UNUSED uint8_t *mdm_input(Abstractformat *f, size_t *n, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint8_t *p;
    if (available < 0 || (uint64_t)available > MDM_MEMORY_LIMIT || mdm_stopped(pd)) return NULL;
    *n = (size_t)available;
    p = (uint8_t *)xx_mem_alloc(*n ? *n : 1U);
    if (!p || !pm_read(f, 0, p, *n) || mdm_stopped(pd)) {
        xx_mem_free(p);
        return NULL;
    }
    return p;
}
static XXFC_MAYBE_UNUSED bool mdm_memory_member(Abstractformat *f, pm_stream *s, const char *label, int64_t at, int64_t packed, uint8_t *plain, size_t size,
                                                uint16_t method, char *name)
{
    pm_member *m;
    if (!pm_add(f, s, label, at, packed)) return false;
    m = &s->items[s->count - 1U];
    m->memory = plain;
    m->size = (int64_t)size;
    m->packed_size = packed;
    m->compression_method = method;
    m->display_name = name;
    return true;
}

#endif

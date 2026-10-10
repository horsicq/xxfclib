/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Field references: GARbro's Bruns, Unity/Utage, YuRis and RPGMaker readers;
 * repak's documented Unreal PAK reader; Fallout 2 Community Edition DAT2.
 * This implementation uses borrowed I/O, bounded RAM and never opens a
 * temporary file. Unsupported encryption/compression fails explicitly. */
#include "xxfclib/formats/ue2_games/xx_ue2_games.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define UG_LIMIT (64U * 1024U * 1024U)
#define UG_INDEX_LIMIT (16U * 1024U * 1024U)
#define UG_COUNT_LIMIT 65536U
static bool ug_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool ug_span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static unsigned ug_hex(char c)
{
    if (c >= '0' && c <= '9') return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A' + 10);
    return 16;
}
static const char *ug_password(Abstractformat *f)
{
    const xx_var *v = xx_format_find_extra_parameter(f, XX_META_ID_OPT_PASSWORD);
    return v ? xx_var_get_str(v) : NULL;
}
static size_t ug_limit(Abstractformat *f)
{
    const xx_var *v = xx_format_find_extra_parameter(f, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : UG_LIMIT;
    return n < UG_LIMIT ? (size_t)n : UG_LIMIT;
}
static bool ug_write(xx_io_device *d, const void *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0;
    if (ug_stop(pd)) return false;
    while (d && done < n) {
        ssize_t k = xx_io_write(d, (const uint8_t *)p + done, n - done);
        if (k <= 0 || (size_t)k > n - done || ug_stop(pd)) return false;
        done += (size_t)k;
    }
    return true;
}
/* Safe original path, UTF-8 bytes retained; no traversal or DOS device names. */
static bool ug_name(pm_member *m, const char *raw)
{
    size_t n = xx_rt_strlen(raw), i, start = 0;
    char *name;
    if (!n || n > 4096 || raw[0] == '/' || raw[0] == '\\') return false;
    name = xx_str_dup(raw);
    if (!name) return false;
    for (i = 0; i <= n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == '\\') name[i] = '/';
        if (c && c != '/' && c != '\\' && (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (!c || c == '/' || c == '\\') {
            size_t z = i - start, j, dot = z;
            char upper[5] = {0};
            if (!z || (z == 1 && name[start] == '.') || (z == 2 && name[start] == '.' && name[start + 1] == '.') || name[i - 1] == '.' || name[i - 1] == ' ') goto bad;
            for (j = 0; j < z; ++j)
                if (name[start + j] == '.') {
                    dot = j;
                    break;
                }
            if (dot <= 4) {
                for (j = 0; j < dot; ++j) {
                    char t = name[start + j];
                    upper[j] = (t >= 'a' && t <= 'z') ? (char)(t - 32) : t;
                }
                if (!xx_rt_strcmp(upper, "CON") || !xx_rt_strcmp(upper, "NUL") || !xx_rt_strcmp(upper, "AUX") || !xx_rt_strcmp(upper, "PRN") ||
                    ((!xx_rt_memcmp(upper, "COM", 3) || !xx_rt_memcmp(upper, "LPT", 3)) && upper[3] >= '1' && upper[3] <= '9'))
                    goto bad;
            }
            start = i + 1;
        }
    }
    m->display_name = name;
    return true;
bad:
    xx_str_free(name);
    return false;
}
static bool ug_add(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t packed, uint64_t size)
{
    pm_member *m;
    if (s->count >= UG_COUNT_LIMIT || at > INT64_MAX || packed > INT64_MAX || size > INT64_MAX || !pm_add(f, s, name, (int64_t)at, (int64_t)packed)) return false;
    m = &s->items[s->count - 1];
    m->size = (int64_t)size;
    if (!ug_name(m, name)) return false;
    return true;
}
static int ug_name_cmp(const void *a, const void *b)
{
    return xx_str_icmp(*(const char *const *)a, *(const char *const *)b);
}
static bool ug_names_unique(pm_stream *s)
{
    char **names;
    size_t i;
    bool ok = true;
    if (s->count < 2) return true;
    names = (char **)xx_mem_alloc(s->count * sizeof(*names));
    if (!names) return false;
    for (i = 0; i < s->count; ++i) names[i] = s->items[i].display_name;
    xx_rt_qsort(names, s->count, sizeof(*names), ug_name_cmp);
    for (i = 1; i < s->count; ++i)
        if (!xx_str_icmp(names[i - 1], names[i])) {
            ok = false;
            break;
        }
    xx_mem_free(names);
    return ok;
}
typedef struct ug_xor {
    uint8_t key[256];
    size_t length;
    uint64_t prefix;
    bool utage;
} ug_xor;
static void ug_xor_free(void *p)
{
    if (p) {
        xx_mem_zero(p, sizeof(ug_xor));
        xx_mem_free(p);
    }
}
static bool ug_xor_range(Abstractformat *f, pm_member *m, uint64_t at, void *p, size_t n, xx_pd_struct *pd)
{
    ug_xor *x = (ug_xor *)m->context;
    size_t i;
    if (ug_stop(pd) || !ug_span(at, n, (uint64_t)m->size) || !pm_read(f, m->offset - f->base_address + (int64_t)at, p, n)) return false;
    for (i = 0; i < n; ++i) {
        uint64_t pos = at + i;
        uint8_t k = x->key[pos % x->length];
        if (pos < x->prefix && (!x->utage || (((uint8_t *)p)[i] && ((uint8_t *)p)[i] != k))) ((uint8_t *)p)[i] ^= k;
    }
    return !ug_stop(pd);
}
static bool ug_xor_add(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t size, const uint8_t *key, size_t key_length, uint64_t prefix, bool utage,
                       const char *password)
{
    pm_member *m;
    ug_xor *x;
    if (!key_length || key_length > 256 || !ug_add(f, s, name, at, size, size)) return false;
    m = &s->items[s->count - 1];
    x = (ug_xor *)xx_mem_alloc(sizeof(*x));
    if (!x) return false;
    xx_mem_zero(x, sizeof(*x));
    xx_rt_memcpy(x->key, key, key_length);
    x->length = key_length;
    x->prefix = prefix;
    x->utage = utage;
    m->context = x;
    m->free_context = ug_xor_free;
    m->read_range = ug_xor_range;
    m->source_encrypted = true;
    if (password) {
        m->password = xx_str_dup(password);
        if (!m->password) return false;
    }
    return true;
}
static const char *ug_media(const uint8_t *p, size_t n)
{
    static const uint8_t png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n >= 16 && !xx_rt_memcmp(p, png, 8) && xx_data_get_u32(p + 8, 4, 0, true) == 13 && !xx_rt_memcmp(p + 12, "IHDR", 4)) return "payload.png";
    if (n >= 14 && p[0] == 'B' && p[1] == 'M' && xx_data_get_u32(p + 10, 4, 0, false) >= 14) return "payload.bmp";
    if (n >= 4 && p[0] == 255 && p[1] == 216 && p[2] == 255) return "payload.jpg";
    if (n >= 12 && !xx_rt_memcmp(p, "RIFF", 4) && !xx_rt_memcmp(p + 8, "WEBP", 4)) return "payload.webp";
    if (n >= 6 && !xx_rt_memcmp(p, "OggS", 4) && p[4] == 0) return "payload.ogg";
    if (n >= 12 && !xx_rt_memcmp(p + 4, "ftyp", 4) && xx_data_get_u32(p, 4, 0, true) >= 12) return "payload.m4a";
    return NULL;
}
typedef struct ug_buffer {
    uint8_t *p;
    size_t n, capacity, maximum;
    xx_pd_struct *pd;
} ug_buffer;
static ssize_t ug_buffer_write(xx_io_device *d, const void *p, size_t n)
{
    ug_buffer *b = (ug_buffer *)d->priv;
    size_t capacity;
    void *next;
    if (ug_stop(b->pd) || n > b->maximum - b->n) return -1;
    if (b->n + n > b->capacity) {
        capacity = b->capacity ? b->capacity : 4096;
        while (capacity < b->n + n) {
            if (capacity > b->maximum / 2) {
                capacity = b->maximum;
                break;
            }
            capacity *= 2;
        }
        next = xx_mem_realloc(b->p, capacity);
        if (!next) return -1;
        b->p = (uint8_t *)next;
        b->capacity = capacity;
    }
    if (n) {
        xx_rt_memcpy(b->p + b->n, p, n);
    }
    b->n += n;
    return (ssize_t)n;
}
static bool ug_zlib(const uint8_t *p, size_t n, size_t maximum, uint8_t **out, size_t *size, xx_pd_struct *pd)
{
    ug_buffer b;
    xx_io_device d;
    size_t used = 0;
    bool ok;
    xx_mem_zero(&b, sizeof(b));
    xx_mem_zero(&d, sizeof(d));
    b.maximum = maximum;
    b.pd = pd;
    d.priv = &b;
    d.write = ug_buffer_write;
    /* Supply the trailer as decoder look-ahead; consumed excludes look-ahead. */
    ok = n >= 6 && xx_zlib_stream_header_is_valid(p, n) && xx_deflate_unpack_memory_to_device_ex(p + 2, n - 2, &d, &used, false, pd) && used == n - 6 &&
         xx_zlib_stream_trailer_matches(p, n, b.p, b.n) && !ug_stop(pd);
    if (!ok) {
        xx_mem_free(b.p);
        return false;
    }
    *out = b.p;
    *size = b.n;
    return true;
}
static bool ug_memory(Abstractformat *f, pm_stream *s, const char *name, uint8_t *p, size_t n, uint64_t at, uint64_t packed, bool encrypted)
{
    pm_member *m;
    if (!ug_add(f, s, name, at, packed, n)) return false;
    m = &s->items[s->count - 1];
    m->memory = p;
    m->source_encrypted = encrypted;
    return true;
}
static bool ug_bruns(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[40], key[4];
    uint64_t n = (uint64_t)pm_available(f);
    unsigned i;
    const char *name;
    uint32_t k;
    if (n < 24 || !pm_read(f, 0, h, sizeof(h) < n ? sizeof(h) : (size_t)n)) return false;
    if (xx_data_get_u32(h, 4, 0, false) == 0xac9898b0U) {
        uint8_t ff = 255;
        for (i = 0; i < 32 && i < n; ++i) h[i] ^= 255;
        if (!ug_media(h, 32)) return false;
        return ug_xor_add(f, s, "payload.ogg", 0, n, &ff, 1, 0x800, false, NULL);
    }
    if (xx_rt_memcmp(h, "EENC", 4) && xx_rt_memcmp(h, "EENZ", 4)) {
        return false;
    }
    k = xx_data_get_u32(h + 4, 4, 0, false) ^ 0xdeadbeefU;
    xx_data_set_u32(key, 4, 0, k, false);
    for (i = 8; i < sizeof(h) && i < n; ++i) h[i] ^= key[(i - 8) % 4];
    if (h[3] == 'C') {
        name = ug_media(h + 8, (size_t)(n - 8 < 32 ? n - 8 : 32));
        if (!name) return false;
        return ug_xor_add(f, s, name, 8, n - 8, key, 4, UINT64_MAX, false, NULL);
    } else {
        uint8_t *p, *plain = NULL;
        size_t size = 0, limit = ug_limit(f);
        bool ok;
        if (n - 8 > limit) return false;
        p = (uint8_t *)xx_mem_alloc((size_t)n - 8);
        if (!p || !pm_read(f, 8, p, (size_t)n - 8)) {
            xx_mem_free(p);
            return false;
        }
        for (i = 0; i < n - 8; ++i) p[i] ^= (uint8_t)(k >> ((i & 3) * 8));
        ok = ug_zlib(p, (size_t)n - 8, limit, &plain, &size, pd);
        xx_mem_free(p);
        name = ok ? ug_media(plain, size) : NULL;
        if (!name) {
            xx_mem_free(plain);
            return false;
        }
        if (!ug_memory(f, s, name, plain, size, 8, n - 8, true)) {
            xx_mem_free(plain);
            return false;
        }
        s->items[s->count - 1].compression_method = 8;
        return true;
    }
}
static bool ug_rpgmv(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t header[] = {82, 80, 71, 77, 86, 0, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0};
    static const uint8_t png[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
    uint8_t h[49], key[16], plain[33];
    uint64_t n = (uint64_t)pm_available(f);
    const char *password = ug_password(f), *name;
    unsigned i;
    char hex[33];
    static const char digits[] = "0123456789abcdef";
    if (n < 49 || ug_stop(pd) || !pm_read(f, 0, h, 49) || xx_rt_memcmp(h, header, 16)) return false;
    if (password && password[0]) {
        if (xx_rt_strlen(password) != 32) return false;
        for (i = 0; i < 16; ++i) {
            unsigned a = ug_hex(password[2 * i]), b = ug_hex(password[2 * i + 1]);
            if (a > 15 || b > 15) return false;
            key[i] = (uint8_t)(a * 16 + b);
        }
    } else {
        for (i = 0; i < 16; ++i) key[i] = h[16 + i] ^ png[i]; /* PNG's entire first16bytes are invariant. */
    }
    xx_rt_memcpy(plain, h + 16, 33);
    for (i = 0; i < 16; ++i) plain[i] ^= key[i];
    name = ug_media(plain, 33);
    if (!name) return false;
    if (!xx_rt_strcmp(name, "payload.png")) {
        if (xx_data_get_u32(plain + 16, 4, 0, true) == 0 || xx_data_get_u32(plain + 20, 4, 0, true) == 0 || plain[26] > 1 || plain[27] > 1 || plain[28] > 1 ||
            xx_crc32_calc(0, plain + 12, 17) != xx_data_get_u32(plain + 29, 4, 0, true))
            return false;
    }
    for (i = 0; i < 16; ++i) {
        hex[2 * i] = digits[key[i] >> 4];
        hex[2 * i + 1] = digits[key[i] & 15];
    }
    hex[32] = 0;
    return ug_xor_add(f, s, name, 16, n - 16, key, 16, 16, false, hex);
}
static bool ug_utage(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    const char *password = ug_password(f), *name;
    uint8_t h[32];
    uint64_t n = (uint64_t)pm_available(f);
    size_t k, i;
    if (!password || !password[0]) {
        password = "InputOriginalKey";
    }
    k = xx_rt_strlen(password);
    if (!k || k > 256 || n < 32 || ug_stop(pd) || !pm_read(f, 0, h, 32)) return false;
    for (i = 0; i < 32; ++i) {
        if (h[i] && h[i] != (uint8_t)password[i % k]) h[i] ^= (uint8_t)password[i % k];
    }
    name = ug_media(h, 32);
    if (!name) return false;
    return ug_xor_add(f, s, name, 0, n, (const uint8_t *)password, k, UINT64_MAX, true, password);
}
static bool ug_ycg(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[56], *p = NULL, *a = NULL, *b = NULL, *bmp = NULL;
    uint64_t n = (uint64_t)pm_available(f), pixels;
    uint32_t w, height, packed1, packed2, size1, size2;
    size_t z1 = 0, z2 = 0, limit = ug_limit(f);
    bool ok = false;
    if (n < 56 || !pm_read(f, 0, h, 56) || xx_data_get_u32(h, 4, 0, false) != 0x00474359U || xx_data_get_u32(h + 16, 4, 0, false) != 1) {
        return false;
    }
    w = xx_data_get_u32(h + 4, 4, 0, false);
    height = xx_data_get_u32(h + 8, 4, 0, false);
    pixels = (uint64_t)w * height * 4;
    size1 = xx_data_get_u32(h + 32, 4, 0, false);
    packed1 = xx_data_get_u32(h + 36, 4, 0, false);
    size2 = xx_data_get_u32(h + 48, 4, 0, false);
    packed2 = xx_data_get_u32(h + 52, 4, 0, false);
    if (!w || !height || w > 65535 || height > 65535 || xx_data_get_u32(h + 12, 4, 0, false) != 32 || pixels > limit || pixels + 54 > UINT32_MAX ||
        (uint64_t)size1 + size2 != pixels || (uint64_t)packed1 + packed2 != n - 56 || packed1 < 6 || packed2 < 6 || n - 56 > limit)
        return false;
    p = (uint8_t *)xx_mem_alloc((size_t)n - 56);
    if (!p || !pm_read(f, 56, p, (size_t)n - 56) || !ug_zlib(p, packed1, size1, &a, &z1, pd) || z1 != size1 || !ug_zlib(p + packed1, packed2, size2, &b, &z2, pd) ||
        z2 != size2)
        goto done;
    bmp = (uint8_t *)xx_mem_alloc((size_t)pixels + 54);
    if (!bmp) goto done;
    xx_mem_zero(bmp, 54);
    bmp[0] = 'B';
    bmp[1] = 'M';
    xx_data_set_u32(bmp + 2, 4, 0, (uint32_t)pixels + 54, false);
    xx_data_set_u32(bmp + 10, 4, 0, 54, false);
    xx_data_set_u32(bmp + 14, 4, 0, 40, false);
    xx_data_set_u32(bmp + 18, 4, 0, w, false);
    xx_data_set_u32(bmp + 22, 4, 0, (uint32_t)(-(int32_t)height), false);
    bmp[26] = 1;
    bmp[28] = 32;
    xx_data_set_u32(bmp + 34, 4, 0, (uint32_t)pixels, false);
    if (z1) {
        xx_rt_memcpy(bmp + 54, a, z1);
    }
    if (z2) xx_rt_memcpy(bmp + 54 + z1, b, z2);
    if (!ug_memory(f, s, "image.bmp", bmp, (size_t)pixels + 54, 56, n - 56, false)) {
        goto done;
    }
    bmp = NULL;
    s->items[s->count - 1].compression_method = 8;
    ok = true;
done:
    xx_mem_free(p);
    xx_mem_free(a);
    xx_mem_free(b);
    xx_mem_free(bmp);
    return ok;
}
typedef struct ug_compressed {
    uint64_t packed;
    bool sha;
    uint8_t hash[20];
} ug_compressed;
static bool ug_compressed_read(Abstractformat *f, pm_member *m, xx_io_device *d, xx_pd_struct *pd)
{
    ug_compressed *c = (ug_compressed *)m->context;
    uint8_t *p = NULL, *out = NULL, hash[20];
    size_t size = 0, limit = ug_limit(f);
    bool ok = false;
    if (c->packed > limit || (uint64_t)m->size > limit || ug_stop(pd)) {
        return false;
    }
    p = (uint8_t *)xx_mem_alloc(c->packed ? (size_t)c->packed : 1);
    if (!p || !pm_read(f, m->offset - f->base_address, p, (size_t)c->packed)) goto done;
    if (c->sha && (!xx_sha1_memory(p, (size_t)c->packed, hash) || xx_rt_memcmp(hash, c->hash, 20))) goto done;
    if (m->compression_method == 8) {
        if (!ug_zlib(p, (size_t)c->packed, (size_t)m->size, &out, &size, pd) || size != (size_t)m->size) goto done;
    } else {
        out = p;
        p = NULL;
        size = (size_t)c->packed;
        if (size != (size_t)m->size) goto done;
    }
    ok = ug_write(d, out, size, pd);
done:
    xx_mem_free(p);
    xx_mem_free(out);
    return ok;
}
static bool ug_compressed_add(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t packed, uint64_t size, bool zlib, const uint8_t *hash)
{
    pm_member *m;
    ug_compressed *c;
    if (!ug_add(f, s, name, at, packed, size)) return false;
    m = &s->items[s->count - 1];
    c = (ug_compressed *)xx_mem_alloc(sizeof(*c));
    if (!c) return false;
    xx_mem_zero(c, sizeof(*c));
    c->packed = packed;
    c->sha = hash != NULL;
    if (hash) xx_rt_memcpy(c->hash, hash, 20);
    m->context = c;
    m->free_context = xx_mem_free;
    m->compression_method = zlib ? 8 : 0;
    m->read_all = ug_compressed_read;
    return true;
}
static bool ug_fallout(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t n = (uint64_t)pm_available(f), start, pos;
    uint8_t tail[8], *index = NULL;
    uint32_t tree, count, i;
    bool ok = false;
    if (n < 12 || n > UINT32_MAX || !pm_read(f, (int64_t)n - 8, tail, 8) || xx_data_get_u32(tail + 4, 4, 0, false) != n) {
        return false;
    }
    tree = xx_data_get_u32(tail, 4, 0, false);
    if (tree < 4 || tree > UG_INDEX_LIMIT || tree > n - 8) return false;
    start = n - 8 - tree;
    index = (uint8_t *)xx_mem_alloc(tree);
    if (!index || !pm_read(f, (int64_t)start, index, tree)) goto done;
    count = xx_data_get_u32(index, 4, 0, false);
    if (!count || count > UG_COUNT_LIMIT) goto done;
    pos = 4;
    for (i = 0; i < count; ++i) {
        uint32_t length, size, packed, at;
        uint8_t codec;
        char name[4097];
        if (ug_stop(pd) || !ug_span(pos, 4, tree)) goto done;
        length = xx_data_get_u32(index + pos, 4, 0, false);
        pos += 4;
        if (!length || length > 4096 || !ug_span(pos, (uint64_t)length + 13, tree) || xx_rt_memchr(index + pos, 0, length)) {
            goto done;
        }
        xx_rt_memcpy(name, index + pos, length);
        name[length] = 0;
        pos += length;
        codec = index[pos++];
        size = xx_data_get_u32(index + pos, 4, 0, false);
        packed = xx_data_get_u32(index + pos + 4, 4, 0, false);
        at = xx_data_get_u32(index + pos + 8, 4, 0, false);
        pos += 12;
        if (codec > 1 || (!codec && packed != size) || !ug_span(at, packed, start) || !ug_compressed_add(f, s, name, at, packed, size, codec != 0, NULL)) goto done;
    }
    if (pos != tree) {
        goto done;
    }
    ok = true;
done:
    xx_mem_free(index);
    return ok;
}
typedef struct ug_pak_entry {
    uint64_t offset, packed, size;
    uint32_t method, block_size, count;
    uint8_t hash[20], flags;
    uint64_t *blocks;
    size_t header_size;
} ug_pak_entry;
static void ug_pak_entry_free(void *p)
{
    ug_pak_entry *e = (ug_pak_entry *)p;
    if (e) {
        xx_mem_free(e->blocks);
        xx_mem_free(e);
    }
}
static bool ug_pak_entry_parse(const uint8_t *p, uint64_t n, uint64_t *at, uint32_t version, ug_pak_entry *e)
{
    uint64_t start = *at;
    unsigned i;
    size_t initial = version == 1 ? 56 : 48;
    xx_mem_zero(e, sizeof(*e));
    if (!ug_span(start, initial, n)) return false;
    e->offset = xx_data_get_u64(p + start, 8, 0, false);
    e->packed = xx_data_get_u64(p + start + 8, 8, 0, false);
    e->size = xx_data_get_u64(p + start + 16, 8, 0, false);
    e->method = xx_data_get_u32(p + start + 24, 4, 0, false);
    xx_rt_memcpy(e->hash, p + start + (version == 1 ? 36 : 28), 20);
    *at += initial;
    if (e->method > 1 || e->packed > INT64_MAX || e->size > INT64_MAX) return false;
    if (version >= 3) {
        if (e->method) {
            if (!ug_span(*at, 4, n)) return false;
            e->count = xx_data_get_u32(p + *at, 4, 0, false);
            *at += 4;
            if (!e->count || e->count > 65536 || !ug_span(*at, (uint64_t)e->count * 16, n)) return false;
            e->blocks = (uint64_t *)xx_mem_alloc((size_t)e->count * 16);
            if (!e->blocks) return false;
            for (i = 0; i < e->count * 2; ++i) e->blocks[i] = xx_data_get_u64(p + *at + (uint64_t)i * 8, 8, 0, false);
            *at += (uint64_t)e->count * 16;
        }
        if (!ug_span(*at, 5, n)) {
            return false;
        }
        e->flags = p[*at];
        e->block_size = xx_data_get_u32(p + *at + 1, 4, 0, false);
        *at += 5;
        if (e->flags) return false;
    }
    e->header_size = (size_t)(*at - start);
    return e->method != 0 || e->packed == e->size;
}
static bool ug_pak_string(const uint8_t *p, uint64_t n, uint64_t *at, char **out)
{
    int32_t length;
    uint64_t size;
    char *str;
    unsigned i;
    *out = NULL;
    if (!ug_span(*at, 4, n)) return false;
    length = (int32_t)xx_data_get_u32(p + *at, 4, 0, false);
    *at += 4;
    if (!length || length == INT32_MIN || length > 4097 || length < -4097) return false;
    size = length < 0 ? (uint64_t)(-(int64_t)length) * 2 : (uint64_t)length;
    if (!ug_span(*at, size, n)) return false;
    str = (char *)xx_mem_alloc((size_t)(length < 0 ? -(int64_t)length : length));
    if (!str) return false;
    if (length < 0) {
        for (i = 0; i < (unsigned)-length; ++i) {
            uint16_t c = xx_data_get_u16(p + *at + (uint64_t)i * 2, 2, 0, false);
            if (c > 127) {
                xx_mem_free(str);
                return false;
            }
            str[i] = (char)c;
        }
    } else xx_rt_memcpy(str, p + *at, (size_t)size);
    *at += size;
    if (str[(length < 0 ? -length : length) - 1] || xx_rt_strlen(str) != (size_t)(length < 0 ? -length : length) - 1) {
        xx_mem_free(str);
        return false;
    }
    *out = str;
    return true;
}
static bool ug_pak_read(Abstractformat *f, pm_member *m, xx_io_device *d, xx_pd_struct *pd)
{
    ug_pak_entry *e = (ug_pak_entry *)m->context;
    uint8_t *p = NULL, *out = NULL, hash[20];
    size_t total = 0, limit = ug_limit(f);
    unsigned i;
    bool ok = false;
    if (e->packed > limit || e->size > limit || ug_stop(pd)) {
        return false;
    }
    p = (uint8_t *)xx_mem_alloc(e->packed ? (size_t)e->packed : 1);
    if (!p || !pm_read(f, m->offset - f->base_address, p, (size_t)e->packed)) goto done;
    /* Engine releases have used both pre- and post-compression member hashes. */
    if (!e->method) {
        if (!xx_sha1_memory(p, (size_t)e->packed, hash) || xx_rt_memcmp(hash, e->hash, 20)) goto done;
        ok = ug_write(d, p, (size_t)e->size, pd);
        goto done;
    }
    out = (uint8_t *)xx_mem_alloc(e->size ? (size_t)e->size : 1);
    if (!out) goto done;
    for (i = 0; i < (e->count ? e->count : 1); ++i) {
        uint64_t begin = e->count ? e->blocks[2 * i] : 0, end = e->count ? e->blocks[2 * i + 1] : e->packed;
        uint8_t *chunk = NULL;
        size_t size = 0, expected = e->count && i + 1 < e->count ? e->block_size : (size_t)e->size - total;
        if (!ug_span(begin, end - begin, e->packed) || end < begin || !ug_zlib(p + (size_t)begin, (size_t)(end - begin), expected, &chunk, &size, pd) ||
            size != expected) {
            xx_mem_free(chunk);
            goto done;
        }
        if (size) {
            xx_rt_memcpy(out + total, chunk, size);
        }
        total += size;
        xx_mem_free(chunk);
    }
    if (total != (size_t)e->size) {
        goto done;
    }
    if (!xx_sha1_memory(p, (size_t)e->packed, hash)) goto done;
    if (xx_rt_memcmp(hash, e->hash, 20) && (!xx_sha1_memory(out, total, hash) || xx_rt_memcmp(hash, e->hash, 20))) goto done;
    ok = ug_write(d, out, total, pd);
done:
    xx_mem_free(p);
    xx_mem_free(out);
    return ok;
}
static bool ug_pak(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t n = (uint64_t)pm_available(f), offset = 0, size = 0, footer = 0, pos = 0;
    uint8_t tail[61], *index = NULL, hash[20];
    uint32_t version = 0, count = 0, i;
    unsigned candidate;
    char *mount = NULL, *name = NULL;
    bool ok = false;
    for (candidate = 44; candidate <= 61; candidate += candidate == 44 ? 1 : 16) {
        unsigned pre = candidate - 44;
        uint32_t v;
        if (n < candidate || !pm_read(f, (int64_t)n - candidate, tail, candidate) || xx_data_get_u32(tail + pre, 4, 0, false) != 0x5a6f12e1U) continue;
        v = xx_data_get_u32(tail + pre + 4, 4, 0, false);
        if (v < 1 || v > 7 || candidate != (v >= 7 ? 61U : v >= 4 ? 45U : 44U) || (pre && tail[pre - 1])) {
            continue;
        }
        version = v;
        footer = n - candidate;
        offset = xx_data_get_u64(tail + pre + 8, 8, 0, false);
        size = xx_data_get_u64(tail + pre + 16, 8, 0, false);
        xx_rt_memcpy(hash, tail + pre + 24, 20);
        break;
    }
    if (!version || size > UG_INDEX_LIMIT || size < 8 || !ug_span(offset, size, footer)) {
        return false;
    }
    index = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!index || !pm_read(f, (int64_t)offset, index, (size_t)size)) goto done;
    {
        uint8_t actual[20];
        if (!xx_sha1_memory(index, (size_t)size, actual) || xx_rt_memcmp(actual, hash, 20)) goto done;
    }
    if (!ug_pak_string(index, size, &pos, &mount) || !ug_span(pos, 4, size)) {
        goto done;
    }
    count = xx_data_get_u32(index + pos, 4, 0, false);
    pos += 4;
    if (!count || count > UG_COUNT_LIMIT) goto done;
    for (i = 0; i < count; ++i) {
        ug_pak_entry *e, *header = NULL;
        uint64_t header_at = 0, data;
        uint8_t *bytes = NULL;
        pm_member *m;
        unsigned j;
        if (ug_stop(pd) || !ug_pak_string(index, size, &pos, &name)) goto done;
        e = (ug_pak_entry *)xx_mem_alloc(sizeof(*e));
        if (!e) goto done;
        if (!ug_pak_entry_parse(index, size, &pos, version, e)) {
            ug_pak_entry_free(e);
            goto done;
        }
        if (!ug_span(e->offset, (uint64_t)e->header_size + e->packed, offset)) {
            ug_pak_entry_free(e);
            goto done;
        }
        data = e->offset + e->header_size;
        /* The data header must agree with the index; otherwise offsets alone
         * could make a damaged table appear to extract successfully. */
        bytes = (uint8_t *)xx_mem_alloc(e->header_size);
        header = (ug_pak_entry *)xx_mem_alloc(sizeof(*header));
        if (!bytes || !header) {
            xx_mem_free(bytes);
            xx_mem_free(header);
            ug_pak_entry_free(e);
            goto done;
        }
        xx_mem_zero(header, sizeof(*header));
        if (!pm_read(f, (int64_t)e->offset, bytes, e->header_size) || !ug_pak_entry_parse(bytes, e->header_size, &header_at, version, header) ||
            header_at != e->header_size || header->offset || header->packed != e->packed || header->size != e->size || header->method != e->method ||
            header->count != e->count || header->block_size != e->block_size || xx_rt_memcmp(header->hash, e->hash, 20) ||
            (e->count && xx_rt_memcmp(header->blocks, e->blocks, (size_t)e->count * 16))) {
            xx_mem_free(bytes);
            ug_pak_entry_free(header);
            ug_pak_entry_free(e);
            goto done;
        }
        xx_mem_free(bytes);
        ug_pak_entry_free(header);
        for (j = 0; j < e->count; ++j) {
            uint64_t begin = e->blocks[2 * j], end = e->blocks[2 * j + 1], base = version >= 5 ? e->offset : 0;
            if (begin > UINT64_MAX - base || end > UINT64_MAX - base || begin + base < data || end + base < data || end < begin ||
                !ug_span(begin + base - data, end - begin, e->packed) || (j && begin + base - data != e->blocks[2 * j - 1])) {
                ug_pak_entry_free(e);
                goto done;
            }
            e->blocks[2 * j] = begin + base - data;
            e->blocks[2 * j + 1] = end + base - data;
        }
        if (e->count && (e->blocks[0] || e->blocks[2 * e->count - 1] != e->packed || !e->block_size || (uint64_t)(e->count - 1) * e->block_size >= e->size ||
                         (uint64_t)e->count * e->block_size < e->size)) {
            ug_pak_entry_free(e);
            goto done;
        }
        if (!ug_add(f, s, name, data, e->packed, e->size)) {
            ug_pak_entry_free(e);
            goto done;
        }
        m = &s->items[s->count - 1];
        m->context = e;
        m->free_context = ug_pak_entry_free;
        m->read_all = ug_pak_read;
        m->compression_method = e->method ? 8 : 0;
        xx_str_free(name);
        name = NULL;
    }
    if (pos != size) {
        goto done;
    }
    ok = true;
done:
    xx_str_free(name);
    xx_str_free(mount);
    xx_mem_free(index);
    return ok;
}
/* Classic Gale103..107, all frames/layers. Export unshuffled raw/zlib
 * pixels as BGRA BMPs, preserving separate layers instead of flattening. */
static bool ug_gal(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t n = (uint64_t)pm_available(f), at, total = 0;
    uint8_t *p = NULL;
    uint32_t version, header, frames, compression, frame;
    size_t limit = ug_limit(f);
    bool ok = false;
    if (n < 51 || n > limit) {
        return false;
    }
    p = (uint8_t *)xx_mem_alloc((size_t)n);
    if (!p || !pm_read(f, 0, p, (size_t)n) || xx_rt_memcmp(p, "Gale", 4)) goto done;
    if (p[4] != '1' || p[5] != '0' || p[6] < '3' || p[6] > '7') {
        goto done;
    }
    version = 100U + (p[6] - '0');
    header = xx_data_get_u32(p + 7, 4, 0, false);
    if (header < 40 || header > 256 || !ug_span(11, header, n) || xx_data_get_u32(p + 11, 4, 0, false) != version || p[11 + 21] ||
        xx_data_get_u32(p + 11 + 28, 4, 0, false) || xx_data_get_u32(p + 11 + 32, 4, 0, false))
        goto done;
    frames = xx_data_get_u32(p + 11 + 16, 4, 0, false);
    compression = p[11 + 22];
    if (!frames || frames > 1024 || compression > 1) goto done;
    at = 11U + header;
    for (frame = 0; frame < frames; ++frame) {
        uint32_t name, layers, width, height, bpp, layer, palette_bytes = 0;
        uint64_t stride, alpha_stride, pixels;
        const uint8_t *palette = NULL;
        if (ug_stop(pd) || !ug_span(at, 4, n)) {
            goto done;
        }
        name = xx_data_get_u32(p + at, 4, 0, false);
        at += 4;
        if (name > 4096 || !ug_span(at, (uint64_t)name + 29, n)) goto done;
        at += name + 13;
        layers = xx_data_get_u32(p + at, 4, 0, false);
        at += 4;
        width = xx_data_get_u32(p + at, 4, 0, false);
        height = xx_data_get_u32(p + at + 4, 4, 0, false);
        bpp = xx_data_get_u32(p + at + 8, 4, 0, false);
        at += 12;
        if (!layers || layers > 1024 || !width || !height || width > 65535 || height > 65535 || (bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) goto done;
        stride = ((uint64_t)width * bpp + 7) / 8;
        if (bpp >= 8) stride = (stride + 3) & ~UINT64_C(3);
        alpha_stride = ((uint64_t)width + 3) & ~UINT64_C(3);
        pixels = (uint64_t)width * height * 4;
        if (pixels > limit || pixels + 54 > limit - total) goto done;
        if (bpp <= 8) {
            palette_bytes = (1U << bpp) * 4;
            if (!ug_span(at, palette_bytes, n)) goto done;
            palette = p + at;
            at += palette_bytes;
        }
        for (layer = 0; layer < layers; ++layer) {
            uint32_t packed, alpha_packed;
            uint8_t *color = NULL, *alpha = NULL, *bmp = NULL;
            size_t color_size = 0, alpha_size = 0;
            uint64_t payload_at, x, y;
            char name_out[64];
            bool emitted = false;
            if (ug_stop(pd) || !ug_span(at, 22, n)) {
                goto done;
            }
            name = xx_data_get_u32(p + at + 18, 4, 0, false);
            at += 22;
            if (name > 4096 || !ug_span(at, (uint64_t)name + (version >= 107 ? 1 : 0) + 4, n)) goto done;
            at += name + (version >= 107 ? 1 : 0);
            packed = xx_data_get_u32(p + at, 4, 0, false);
            at += 4;
            payload_at = at;
            if (!ug_span(at, (uint64_t)packed + 4, n) || stride * height > limit) goto done;
            if (compression == 0) {
                if (!ug_zlib(p + at, packed, (size_t)(stride * height), &color, &color_size, pd) || color_size != stride * height) goto layer_done;
            } else {
                if (packed != stride * height) goto layer_done;
                color = (uint8_t *)xx_mem_alloc(packed ? packed : 1);
                if (!color) goto layer_done;
                xx_rt_memcpy(color, p + at, packed);
                color_size = packed;
            }
            at += packed;
            alpha_packed = xx_data_get_u32(p + at, 4, 0, false);
            at += 4;
            if (!ug_span(at, alpha_packed, n) || alpha_stride * height > limit) goto layer_done;
            if (alpha_packed) {
                if (compression == 0) {
                    if (!ug_zlib(p + at, alpha_packed, (size_t)(alpha_stride * height), &alpha, &alpha_size, pd) || alpha_size != alpha_stride * height) goto layer_done;
                } else {
                    if (alpha_packed != alpha_stride * height) goto layer_done;
                    alpha = (uint8_t *)xx_mem_alloc(alpha_packed);
                    if (!alpha) goto layer_done;
                    xx_rt_memcpy(alpha, p + at, alpha_packed);
                }
            }
            at += alpha_packed;
            bmp = (uint8_t *)xx_mem_alloc((size_t)pixels + 54);
            if (!bmp) goto layer_done;
            xx_mem_zero(bmp, 54);
            bmp[0] = 'B';
            bmp[1] = 'M';
            xx_data_set_u32(bmp + 2, 4, 0, (uint32_t)pixels + 54, false);
            xx_data_set_u32(bmp + 10, 4, 0, 54, false);
            xx_data_set_u32(bmp + 14, 4, 0, 40, false);
            xx_data_set_u32(bmp + 18, 4, 0, width, false);
            xx_data_set_u32(bmp + 22, 4, 0, (uint32_t)(-(int32_t)height), false);
            bmp[26] = 1;
            bmp[28] = 32;
            xx_data_set_u32(bmp + 34, 4, 0, (uint32_t)pixels, false);
            for (y = 0; y < height; ++y) {
                if (ug_stop(pd)) goto layer_done;
                for (x = 0; x < width; ++x) {
                    uint8_t *out = bmp + 54 + (size_t)((y * width + x) * 4);
                    const uint8_t *row = color + (size_t)(y * stride);
                    if (bpp <= 8) {
                        unsigned v = bpp == 8 ? row[x] : (x & 1 ? row[x / 2] >> 4 : row[x / 2] & 15);
                        xx_rt_memcpy(out, palette + v * 4, 3);
                    } else if (bpp == 16) {
                        uint16_t v = xx_data_get_u16(row + x * 2, 2, 0, false);
                        out[0] = (uint8_t)((v & 31) * 255 / 31);
                        out[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
                        out[2] = (uint8_t)((v >> 11) * 255 / 31);
                    } else {
                        xx_rt_memcpy(out, row + x * (bpp / 8), 3);
                    }
                    out[3] = alpha ? alpha[(size_t)(y * alpha_stride + x)] : 255;
                }
            }
            xx_rt_snprintf(name_out, sizeof(name_out), "frame-%04u-layer-%04u.bmp", frame, layer);
            if (!ug_memory(f, s, name_out, bmp, (size_t)pixels + 54, payload_at, (uint64_t)packed + alpha_packed, false)) goto layer_done;
            bmp = NULL;
            total += pixels + 54;
            emitted = true;
            s->items[s->count - 1].compression_method = compression == 0 ? 8 : 0;
        layer_done:
            xx_mem_free(color);
            xx_mem_free(alpha);
            xx_mem_free(bmp);
            if (!emitted) goto done;
        }
    }
    if (at != n) {
        goto done;
    }
    ok = true;
done:
    xx_mem_free(p);
    return ok;
}
static const uint8_t ug_sgb_outer[] = {72, 9, 20, 154, 48, 169, 84, 225, 0, 8, 14, 9, 20, 60, 66, 70};
static const uint8_t ug_sgb_inner[] = {0, 14, 8, 30, 24, 55, 18, 0, 72, 135, 70, 11, 156, 104, 168, 75};
static bool ug_sgb_read_at(Abstractformat *f, uint64_t at, void *p, size_t n)
{
    size_t i;
    if (at > INT64_MAX || !pm_read(f, (int64_t)at, p, n)) return false;
    for (i = 0; i < n; ++i) {
        uint64_t pos = at + i;
        if (pos >= 8) ((uint8_t *)p)[i] = (uint8_t)(((uint8_t *)p)[i] - ug_sgb_outer[(pos - 8) % 16]);
    }
    return true;
}
typedef struct ug_sgb_member {
    uint32_t crc;
} ug_sgb_member;
static bool ug_sgb_read(Abstractformat *f, pm_member *m, xx_io_device *d, xx_pd_struct *pd)
{
    uint8_t *p = NULL, *plain = NULL;
    ug_buffer buffer;
    xx_io_device out;
    size_t used = 0, size, i, limit = ug_limit(f);
    bool ok = false;
    ug_sgb_member *c = (ug_sgb_member *)m->context;
    if ((uint64_t)m->packed_size > limit || (uint64_t)m->size > limit || ug_stop(pd)) {
        return false;
    }
    p = (uint8_t *)xx_mem_calloc((size_t)m->packed_size + 4, 1);
    if (!p || !ug_sgb_read_at(f, (uint64_t)(m->offset - f->base_address), p, (size_t)m->packed_size)) goto done;
    if (m->compression_method == 8) {
        xx_mem_zero(&buffer, sizeof(buffer));
        xx_mem_zero(&out, sizeof(out));
        buffer.maximum = (size_t)m->size;
        buffer.pd = pd;
        out.priv = &buffer;
        out.write = ug_buffer_write;
        ok =
            xx_deflate_unpack_memory_to_device_ex(p, (size_t)m->packed_size + 4, &out, &used, false, pd) && used == (size_t)m->packed_size && buffer.n == (size_t)m->size;
        plain = buffer.p;
        size = buffer.n;
        if (!ok) goto done;
    } else {
        plain = p;
        p = NULL;
        size = (size_t)m->size;
    }
    if (xx_crc32_calc(0, plain, size) != c->crc) {
        goto done;
    }
    for (i = 0; i < size; ++i) plain[i] = (uint8_t)(plain[i] + ug_sgb_inner[i % 16]);
    ok = ug_write(d, plain, size, pd);
done:
    xx_mem_free(p);
    xx_mem_free(plain);
    return ok;
}
static bool ug_sgb(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[46], tail[22];
    uint64_t n = (uint64_t)pm_available(f), central, pos, central_size;
    uint32_t count, i;
    if (n < 38 || !pm_read(f, 0, h, 8) || xx_rt_memcmp(h, "SGBDAT", 6) || !xx_data_get_u16(h + 6, 2, 0, true) || !ug_sgb_read_at(f, n - 22, tail, 22) ||
        xx_data_get_u32(tail, 4, 0, false) != 0x06054b50U || xx_data_get_u16(tail + 4, 2, 0, false) || xx_data_get_u16(tail + 6, 2, 0, false) ||
        xx_data_get_u16(tail + 8, 2, 0, false) != xx_data_get_u16(tail + 10, 2, 0, false) || xx_data_get_u16(tail + 20, 2, 0, false))
        return false;
    count = xx_data_get_u16(tail + 10, 2, 0, false);
    central_size = xx_data_get_u32(tail + 12, 4, 0, false);
    central = xx_data_get_u32(tail + 16, 4, 0, false);
    if (!count || count > UG_COUNT_LIMIT || central_size > UG_INDEX_LIMIT || !ug_span(central, central_size, n - 22) || central + central_size != n - 22) return false;
    pos = central;
    for (i = 0; i < count; ++i) {
        uint32_t local, packed, size, crc;
        uint16_t name_size, extra, comment, method, flags;
        char name[4097], local_name[4097];
        uint8_t l[30];
        uint64_t at;
        ug_sgb_member *c;
        pm_member *m;
        if (ug_stop(pd) || !ug_span(pos, 46, n - 22) || !ug_sgb_read_at(f, pos, h, 46) || xx_data_get_u32(h, 4, 0, false) != 0x02014b50U) {
            return false;
        }
        flags = xx_data_get_u16(h + 8, 2, 0, false);
        method = xx_data_get_u16(h + 10, 2, 0, false);
        crc = xx_data_get_u32(h + 16, 4, 0, false);
        packed = xx_data_get_u32(h + 20, 4, 0, false);
        size = xx_data_get_u32(h + 24, 4, 0, false);
        name_size = xx_data_get_u16(h + 28, 2, 0, false);
        extra = xx_data_get_u16(h + 30, 2, 0, false);
        comment = xx_data_get_u16(h + 32, 2, 0, false);
        local = xx_data_get_u32(h + 42, 4, 0, false);
        if (flags & ~0x0800U || (method != 0 && method != 8) || !name_size || name_size > 4096 || xx_data_get_u16(h + 34, 2, 0, false) ||
            !ug_span(pos + 46, (uint64_t)name_size + extra + comment, n - 22) || !ug_sgb_read_at(f, pos + 46, name, name_size)) {
            return false;
        }
        name[name_size] = 0;
        if (xx_rt_strlen(name) != name_size) return false;
        pos += 46U + name_size + extra + comment;
        if (!ug_span(local, 30, central) || !ug_sgb_read_at(f, local, l, 30)) {
            return false;
        }
        if (local == 0) {
            xx_rt_memcpy(l, "PK\3\4\x20\0\0\0", 8);
        }
        if (xx_data_get_u32(l, 4, 0, false) != 0x04034b50U || xx_data_get_u16(l + 6, 2, 0, false) != flags || xx_data_get_u16(l + 8, 2, 0, false) != method ||
            xx_data_get_u32(l + 14, 4, 0, false) != crc || xx_data_get_u32(l + 18, 4, 0, false) != packed || xx_data_get_u32(l + 22, 4, 0, false) != size ||
            xx_data_get_u16(l + 26, 2, 0, false) != name_size)
            return false;
        at = (uint64_t)local + 30 + name_size + xx_data_get_u16(l + 28, 2, 0, false);
        if (!ug_span(at, packed, central) || (!method && packed != size) || !ug_sgb_read_at(f, (uint64_t)local + 30, local_name, name_size) ||
            xx_rt_memcmp(name, local_name, name_size) || !ug_add(f, s, name, at, packed, size))
            return false;
        m = &s->items[s->count - 1];
        c = (ug_sgb_member *)xx_mem_alloc(sizeof(*c));
        if (!c) return false;
        c->crc = crc;
        m->context = c;
        m->free_context = xx_mem_free;
        m->read_all = ug_sgb_read;
        m->compression_method = method;
        m->source_encrypted = true;
    }
    return pos == central + central_size;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool ok = false;
    int64_t n = pm_available(f);
    if (n < 0) return false;
    switch ((unsigned)f->file_type) {
        case XX_UE2_GAME_BRUNS: ok = ug_bruns(f, s, pd); break;
        case XX_UE2_GAME_RPGMV: ok = ug_rpgmv(f, s, pd); break;
        case XX_UE2_GAME_UTAGE: ok = ug_utage(f, s, pd); break;
        case XX_UE2_GAME_YCG: ok = ug_ycg(f, s, pd); break;
        case XX_UE2_GAME_FALLOUT_DAT: ok = ug_fallout(f, s, pd); break;
        case XX_UE2_GAME_UNREAL_PAK: ok = ug_pak(f, s, pd); break;
        case XX_UE2_GAME_LIVEMAKER_GAL: ok = ug_gal(f, s, pd); break;
        case XX_UE2_GAME_SMILE_PACK: ok = ug_sgb(f, s, pd); break;
        default: return false;
    }
    if (ok) {
        s->size = n;
    }
    return ok && !ug_stop(pd) && ug_names_unique(s);
}
xx_ue2_games *xx_ue2_games_create(xx_io_device *d, int64_t b, xx_file_type_t type)
{
    const char *ext;
    xx_ue2_games *r;
    switch ((unsigned)type) {
        case XX_UE2_GAME_BRUNS: ext = "um3;png;bmp;brs"; break;
        case XX_UE2_GAME_RPGMV: ext = "rpgmvp;rpgmvo;rpgmvm;png_;ogg_;m4a_"; break;
        case XX_UE2_GAME_UTAGE: ext = "utage"; break;
        case XX_UE2_GAME_YCG: ext = "ycg"; break;
        case XX_UE2_GAME_UNREAL_PAK: ext = "pak"; break;
        case XX_UE2_GAME_FALLOUT_DAT: ext = "dat"; break;
        case XX_UE2_GAME_LIVEMAKER_GAL: ext = "gal"; break;
        case XX_UE2_GAME_SMILE_PACK: ext = "sgbpack"; break;
        default: return NULL;
    }
    r = (xx_ue2_games *)xx_mem_alloc(sizeof(*r));
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, type, ext);
    }
    return r;
}
void xx_ue2_games_free(xx_ue2_games *r)
{
    if (r) {
        xx_format_cleanup_extra_parameters(&r->format);
        xx_mem_free(r);
    }
}
xx_file_type_t xx_ue2_games_detect_device(xx_io_device *d, xx_pd_struct *pd)
{
    int64_t cursor = xx_io_tell(d), n = xx_io_size(d);
    uint8_t h[16];
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN, candidate = XX_FILE_TYPE_UNKNOWN;
    xx_ue2_games *r = NULL;
    if (n < 16 || ug_stop(pd) || xx_io_seek64(d, 0, SEEK_SET) != 0 || xx_io_read(d, h, 16) != 16) goto done;
    if (!xx_rt_memcmp(h, "EENC", 4) || !xx_rt_memcmp(h, "EENZ", 4) || xx_data_get_u32(h, 4, 0, false) == 0xac9898b0U) candidate = (xx_file_type_t)XX_UE2_GAME_BRUNS;
    else if (!xx_rt_memcmp(h, "RPGMV\0\0\0\0\3\1\0\0\0\0\0", 16)) {
        result = (xx_file_type_t)XX_UE2_GAME_RPGMV;
        goto done;
    } else if (xx_data_get_u32(h, 4, 0, false) == 0x00474359U) candidate = (xx_file_type_t)XX_UE2_GAME_YCG;
    else if (xx_data_get_u32(h, 4, 0, false) == 0x323e3ec0U) candidate = (xx_file_type_t)XX_UE2_GAME_UTAGE;
    else if (!xx_rt_memcmp(h, "Gale10", 6)) candidate = (xx_file_type_t)XX_UE2_GAME_LIVEMAKER_GAL;
    else if (!xx_rt_memcmp(h, "SGBDAT", 6)) candidate = (xx_file_type_t)XX_UE2_GAME_SMILE_PACK;
    if (candidate != XX_FILE_TYPE_UNKNOWN) {
        r = xx_ue2_games_create(d, 0, candidate);
        if (r && pm_valid(&r->format, pd)) result = candidate;
        goto done;
    }
    /* PAK and DAT2 indexes are at EOF, so probe only their cheap trailers. */
    if (n >= 44) {
        uint8_t tail[61];
        unsigned z;
        for (z = 44; z <= 61; z += z == 44 ? 1 : 16) {
            if (n >= z && xx_io_seek64(d, n - z, SEEK_SET) == 0 && xx_io_read(d, tail, z) == z && xx_data_get_u32(tail + z - 44, 4, 0, false) == 0x5a6f12e1U) {
                candidate = (xx_file_type_t)XX_UE2_GAME_UNREAL_PAK;
                break;
            }
        }
    }
    if (candidate == XX_FILE_TYPE_UNKNOWN && n >= 12 && n <= UINT32_MAX) {
        uint8_t tail[8];
        if (xx_io_seek64(d, n - 8, SEEK_SET) == 0 && xx_io_read(d, tail, 8) == 8 && xx_data_get_u32(tail + 4, 4, 0, false) == n &&
            xx_data_get_u32(tail, 4, 0, false) >= 4 && xx_data_get_u32(tail, 4, 0, false) <= UG_INDEX_LIMIT)
            candidate = (xx_file_type_t)XX_UE2_GAME_FALLOUT_DAT;
    }
    if (candidate != XX_FILE_TYPE_UNKNOWN) {
        r = xx_ue2_games_create(d, 0, candidate);
        if (r && pm_valid(&r->format, pd)) result = candidate;
    }
done:
    xx_ue2_games_free(r);
    if (cursor >= 0) (void)xx_io_seek64(d, cursor, SEEK_SET);
    return result;
}

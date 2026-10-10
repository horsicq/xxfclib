/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Neill Corlett ECM CD image stream. Grammar and sector reconstruction follow
 * XArchive/diskimages/xecmimage.cpp; P/Q parity follows ECMA-130 Annex A.
 * The unrelated legacy ECMPACKED reader retains its own identity. */
#include "xxfclib/formats/ecm/xx_ecm.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_payload_members.h"

#define ECM_MAX_OUTPUT UINT64_C(0x200000000)
#define ECM_MAX_RECORDS UINT64_C(1000000)
typedef struct ecm_input {
    Abstractformat *f;
    int64_t at, total, window_at;
    size_t window_size;
    uint8_t window[4096];
} ecm_input;
typedef struct ecm_tables {
    uint32_t edc[256];
    uint8_t exp[512], log[256], p[24], q[43], inv3;
} ecm_tables;

static const uint8_t *ecm_view(ecm_input *in, size_t n, xx_pd_struct *pd)
{
    if (xx_pd_is_stopped(pd) || in->at < 0 || in->at > in->total || n > (uint64_t)(in->total - in->at) || n > sizeof(in->window)) return NULL;
    if (in->at < in->window_at || (uint64_t)(in->at - in->window_at) + n > in->window_size) {
        in->window_at = in->at;
        in->window_size = (size_t)(in->total - in->at < (int64_t)sizeof(in->window) ? in->total - in->at : (int64_t)sizeof(in->window));
        if (!pm_read(in->f, in->at, in->window, in->window_size)) return NULL;
    }
    return in->window + (size_t)(in->at - in->window_at);
}
static bool ecm_header(ecm_input *in, unsigned *type, uint32_t *stored, xx_pd_struct *pd)
{
    const uint8_t *p = ecm_view(in, 1, pd);
    uint8_t b;
    unsigned bits = 5;
    uint64_t value;
    if (!p) return false;
    b = *p;
    ++in->at;
    *type = b & 3;
    value = (b & 0x7cU) >> 2;
    while (b & 0x80U) {
        if (bits > 33 || !(p = ecm_view(in, 1, pd))) return false;
        b = *p;
        ++in->at;
        value |= (uint64_t)(b & 0x7fU) << bits;
        bits += 7;
    }
    if (value > UINT32_MAX) return false;
    *stored = (uint32_t)value;
    return true;
}
static unsigned ecm_stored_size(unsigned type)
{
    static const unsigned sizes[] = {1, 2051, 2052, 2328};
    return sizes[type];
}
static unsigned ecm_output_size(unsigned type)
{
    return type == 0 ? 1 : type == 1 ? 2352 : 2336;
}
static bool ecm_begin(Abstractformat *f, ecm_input *in, xx_pd_struct *pd)
{
    const uint8_t *p;
    xx_mem_zero(in, sizeof(*in));
    in->f = f;
    in->total = pm_available(f);
    in->window_at = -1;
    if (in->total < 13 || !(p = ecm_view(in, 4, pd)) || xx_rt_memcmp(p, "ECM", 4)) return false;
    in->at = 4;
    return true;
}
static bool ecm_scan(Abstractformat *f, xx_ecm_info *info, xx_pd_struct *pd)
{
    ecm_input in;
    const uint8_t *p;
    xx_mem_zero(info, sizeof(*info));
    if (!ecm_begin(f, &in, pd)) return false;
    for (;;) {
        unsigned type;
        uint32_t stored;
        uint64_t count, input, output;
        if (!ecm_header(&in, &type, &stored, pd)) return false;
        if (stored == UINT32_MAX) break;
        if (stored >= UINT32_C(0x7fffffff)) return false;
        count = (uint64_t)stored + 1;
        input = count * ecm_stored_size(type);
        output = count * ecm_output_size(type);
        if (in.at > in.total - 4 || input > (uint64_t)(in.total - 4 - in.at) || output > ECM_MAX_OUTPUT - info->output_size || ++info->records > ECM_MAX_RECORDS)
            return false;
        in.at += (int64_t)input;
        info->output_size += output;
        if (!type) info->literal_bytes += count;
        else if (type == 1) info->mode1_sectors += count;
        else if (type == 2) info->mode2_form1_sectors += count;
        else info->mode2_form2_sectors += count;
    }
    if (!info->records || !info->output_size || in.at != in.total - 4 || !(p = ecm_view(&in, 4, pd))) return false;
    info->stored_check = xx_data_get_u32(p, 4, 0, false);
    return !xx_pd_is_stopped(pd);
}
static void ecm_tables_init(ecm_tables *t)
{
    unsigned i, j, power = 1;
    xx_mem_zero(t, sizeof(*t));
    for (i = 0; i < 256; ++i) {
        uint32_t v = i;
        for (j = 0; j < 8; ++j) v = (v >> 1) ^ ((v & 1) ? UINT32_C(0xd8018001) : 0);
        t->edc[i] = v;
    }
    for (i = 0; i < 255; ++i) {
        t->exp[i] = (uint8_t)power;
        t->log[power] = (uint8_t)i;
        power <<= 1;
        if (power & 256) power ^= 0x11d;
    }
    for (i = 255; i < 512; ++i) t->exp[i] = t->exp[i - 255];
    for (i = 0; i < 24; ++i) t->p[i] = t->exp[25 - i];
    for (i = 0; i < 43; ++i) t->q[i] = t->exp[44 - i];
    t->inv3 = t->exp[255 - t->log[3]];
}
static uint8_t ecm_mul(const ecm_tables *t, uint8_t a, uint8_t b)
{
    return a && b ? t->exp[t->log[a] + t->log[b]] : 0;
}
static uint32_t ecm_edc(const ecm_tables *t, uint32_t edc, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) edc = (edc >> 8) ^ t->edc[(edc ^ p[i]) & 255];
    return edc;
}
static void ecm_parity(const ecm_tables *t, uint8_t *sector, const unsigned *pos, const uint8_t *weights, unsigned n, unsigned a, unsigned b)
{
    uint8_t s0 = 0, s1 = 0, p0;
    unsigned i;
    for (i = 0; i < n; ++i) {
        s0 ^= sector[pos[i]];
        s1 ^= ecm_mul(t, sector[pos[i]], weights[i]);
    }
    p0 = ecm_mul(t, (uint8_t)(s0 ^ s1), t->inv3);
    sector[a] = p0;
    sector[b] = (uint8_t)(s0 ^ p0);
}
static void ecm_ecc(const ecm_tables *t, uint8_t *sector)
{
    unsigned pos[43], m, i;
    for (m = 0; m < 86; ++m) {
        for (i = 0; i < 24; ++i) pos[i] = 12 + m + 86 * i;
        ecm_parity(t, sector, pos, t->p, 24, 2076 + m, 2162 + m);
    }
    for (m = 0; m < 52; ++m) {
        for (i = 0; i < 43; ++i) pos[i] = 12 + (((m >> 1) * 86 + (m & 1) + 88 * i) % 2236);
        ecm_parity(t, sector, pos, t->q, 43, 2248 + m, 2300 + m);
    }
}
static bool ecm_emit(xx_io_device *out, const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    size_t at = 0;
    while (out && at < n) {
        ssize_t amount;
        if (xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(out, p + at, n - at);
        if (amount <= 0 || (size_t)amount > n - at) return false;
        at += (size_t)amount;
    }
    return !xx_pd_is_stopped(pd);
}
static bool ecm_decode(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    const xx_ecm_info *info = (const xx_ecm_info *)m->context;
    ecm_input in;
    ecm_tables t;
    uint32_t check = 0;
    uint64_t written = 0, records = 0;
    uint8_t sector[2352];
    const uint8_t *p;
    if (!info || !ecm_begin(f, &in, pd)) return false;
    ecm_tables_init(&t);
    for (;;) {
        unsigned type;
        uint32_t stored;
        uint64_t count;
        if (!ecm_header(&in, &type, &stored, pd)) return false;
        if (stored == UINT32_MAX) break;
        if (stored >= UINT32_C(0x7fffffff)) return false;
        if (++records > info->records) return false;
        count = (uint64_t)stored + 1;
        while (count) {
            size_t input = ecm_stored_size(type), output = ecm_output_size(type);
            const uint8_t *decoded;
            if (!type) input = output = count > sizeof(in.window) ? sizeof(in.window) : (size_t)count;
            if (output > info->output_size - written || !(p = ecm_view(&in, input, pd))) return false;
            decoded = p;
            if (type) {
                xx_mem_zero(sector, sizeof(sector));
                if (type == 1) {
                    xx_rt_memset(sector + 1, 255, 10);
                    xx_mem_copy(sector + 12, p, 3);
                    sector[15] = 1;
                    xx_mem_copy(sector + 16, p + 3, 2048);
                    xx_data_set_u32(sector + 2064, 4, 0, ecm_edc(&t, 0, sector, 2064), false);
                    ecm_ecc(&t, sector);
                    decoded = sector;
                } else {
                    xx_mem_copy(sector + 16, p, 4);
                    xx_mem_copy(sector + 20, p, 4);
                    xx_mem_copy(sector + 24, p + 4, type == 2 ? 2048 : 2324);
                    if (type == 2) {
                        xx_data_set_u32(sector + 2072, 4, 0, ecm_edc(&t, 0, sector + 16, 2056), false);
                        ecm_ecc(&t, sector);
                    } else xx_data_set_u32(sector + 2348, 4, 0, ecm_edc(&t, 0, sector + 16, 2332), false);
                    decoded = sector + 16;
                }
            }
            if (!ecm_emit(out, decoded, output, pd)) return false;
            check = ecm_edc(&t, check, decoded, output);
            written += output;
            in.at += (int64_t)input;
            count -= type ? 1 : input;
        }
    }
    return records == info->records && written == info->output_size && in.at == in.total - 4 && (p = ecm_view(&in, 4, pd)) &&
           xx_data_get_u32(p, 4, 0, false) == info->stored_check && check == info->stored_check && !xx_pd_is_stopped(pd);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_ecm_info info, *owned;
    pm_member *m;
    int64_t n = pm_available(f);
    if (!ecm_scan(f, &info, pd)) return false;
    owned = (xx_ecm_info *)xx_mem_alloc(sizeof(*owned));
    if (!owned) return false;
    *owned = info;
    if (!pm_add(f, s, "image.bin", 4, n - 8)) {
        xx_mem_free(owned);
        return false;
    }
    m = &s->items[0];
    m->size = (int64_t)info.output_size;
    m->compression_method = UINT16_MAX;
    m->context = owned;
    m->free_context = xx_mem_free;
    m->read_all = ecm_decode;
    s->size = n;
    return true;
}
void xx_ecm_init(xx_ecm *r, xx_io_device *d, int64_t base)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, base, XX_FILE_TYPE_ECM, "ecm");
    r->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&r->format, "application/x-ecm");
}
xx_ecm *xx_ecm_create(xx_io_device *d, int64_t base)
{
    xx_ecm *r = (xx_ecm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ecm_init(r, d, base);
    return r;
}
void xx_ecm_destroy(xx_ecm *r)
{
    if (r) {
        xx_format_cleanup_extra_parameters(&r->format);
        xx_format_invalidate_memory_map(&r->format);
    }
}
void xx_ecm_free(xx_ecm *r)
{
    if (r) {
        xx_ecm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ecm_get_info(xx_ecm *r, xx_ecm_info *info, xx_pd_struct *pd)
{
    int64_t saved;
    bool valid;
    if (!r || !info || !r->format.device || (saved = xx_io_tell(r->format.device)) < 0) return false;
    valid = ecm_scan(&r->format, info, pd);
    return xx_io_seek64(r->format.device, saved, SEEK_SET) == 0 && valid;
}
xx_file_type_t xx_ecm_detect(xx_io_device *d, int64_t base)
{
    uint8_t magic[4];
    xx_ecm r;
    bool valid;
    if (!d || base < 0 || !xx_io_read_at(d, base, magic, 4) || xx_rt_memcmp(magic, "ECM", 4)) return XX_FILE_TYPE_UNKNOWN;
    xx_ecm_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_ecm_destroy(&r);
    return valid ? XX_FILE_TYPE_ECM : XX_FILE_TYPE_UNKNOWN;
}

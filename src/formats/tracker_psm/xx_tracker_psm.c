/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_psm.cpp
 * New Epic MASI PSM FILE/MAINSONG containers with regular4-byte pattern IDs, up to256 patterns/samples/32 channels and one song. Validates chunk tiling, OPLH
 * playlist/settings opcode framing, packed row extents (including well-framed inactive rows, at most256 total), all used sample identities/loops/rates and order
 * references. Exports original chunks; sample bytes remain delta-coded, no decoding/playback. PSM16/Sinaria, unknown opcodes/chunks and extension modes rejected.256MiB
 * cap.
 */
#include "xxfclib/formats/tracker_psm/xx_tracker_psm.h"
#include "../common/xx_tracker_components.h"

static bool tracker_component_psmid(const uint8_t *b, uint32_t *id)
{
    unsigned i;
    bool digit = false;
    *id = 0;
    if (b[0] != 'P') return false;
    for (i = 1; i < 4; ++i) {
        if (b[i] >= '0' && b[i] <= '9') {
            if (digit && b[i - 1] == ' ') return false;
            digit = true;
            *id = *id * 10 + b[i] - '0';
        } else if (b[i] != ' ') return false;
    }
    return digit && *id < 256;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], b[96], q[8], patids[256] = {0}, smids[256] = {0}, samplerefs[256] = {0};
    uint32_t total, count = 0, patterns = 0, samples = 0, ch = 0, used = 0, orders[256], no = 0, i, j;
    binary_cursor c = {f, 0, (uint64_t)pm_available(f), pd, 0};
    char label[40];
    if (!binary_get(&c, h, 12) || xx_rt_memcmp(h, "PSM ", 4) || xx_rt_memcmp(h + 8, "FILE", 4) || (uint64_t)xx_data_get_u32(h + 4, 4, 0, false) + 12 > c.end ||
        (uint64_t)xx_data_get_u32(h + 4, 4, 0, false) + 12 > 268435456) {
        return false;
    }
    total = xx_data_get_u32(h + 4, 4, 0, false) + 12;
    c.end = total;
    if (!tracker_component_emit(f, s, "descriptor.bin", 0, 12, total)) return false;
    while (c.at < c.end) {
        uint64_t start = c.at;
        uint32_t n;
        binary_cursor d;
        if (++count > 1024 || !binary_get(&c, h, 8) || !binary_range(c.at, n = xx_data_get_u32(h + 4, 4, 0, false), c.end)) return false;
        d = c;
        d.end = c.at + n;
        if (!xx_rt_memcmp(h, "SDFT", 4)) {
            if (used & 1 || n != 8 || !binary_get(&d, b, 8) || xx_rt_memcmp(b, "MAINSONG", 8)) return false;
            used |= 1;
        } else if (!xx_rt_memcmp(h, "TITL", 4)) {
            if (used & 2 || n > 1024 || !binary_skip(&d, n)) return false;
            used |= 2;
        } else if (!xx_rt_memcmp(h, "PBOD", 4)) {
            uint32_t id, rows;
            if (!(used & 1) || !binary_get(&d, b, 10) || xx_data_get_u32(b, 4, 0, false) != n || !tracker_component_psmid(b + 4, &id) || patids[id] ||
                !(rows = xx_data_get_u16(b + 8, 2, 0, false)) || rows > 256)
                return false;
            patids[id] = 1;
            ++patterns;
            for (i = 0; i < rows || d.at < d.end; ++i) {
                uint32_t sz;
                uint64_t end;
                if (i >= 256 || !binary_get(&d, q, 2) || (sz = xx_data_get_u16(q, 2, 0, false)) < 2 || !binary_range(d.at, sz - 2, d.end)) return false;
                end = d.at + sz - 2;
                {
                    binary_cursor row = d;
                    row.end = end;
                    while (row.at < end) {
                        uint32_t mask;
                        if (!binary_get(&row, q, 2) || ((mask = q[0]) & 15U) || q[1] >= 32 || ((used & 4) && q[1] >= ch) || !mask) return false;
                        if (q[1] + 1U > ch) ch = q[1] + 1U;
                        if (mask & 128 && !binary_get(&row, q, 1)) return false;
                        if (mask & 64) {
                            if (!binary_get(&row, q, 1)) return false;
                            samplerefs[q[0]] = 1;
                        }
                        if (mask & 32 && (!binary_get(&row, q, 1) || q[0] > 127)) return false;
                        if (mask & 16) {
                            if (!binary_get(&row, q, 2)) return false;
                            if (q[0] == 0x29 && !binary_skip(&row, 2)) return false;
                            if (q[0] == 0x33 && !binary_skip(&row, 1)) return false;
                        }
                    }
                }
                d.at = end;
            }
        } else if (!xx_rt_memcmp(h, "DSMP", 4)) {
            uint32_t id, len, a, z;
            if (!(used & 1) || !binary_get(&d, b, 96) || ((b[0] & ~0x80U)) || (id = xx_data_get_u16(b + 52, 2, 0, false)) >= 256 || smids[id] ||
                (len = xx_data_get_u32(b + 54, 4, 0, false)) > 16777216 || n != 96U + (uint64_t)len || b[68] > 127 || !xx_data_get_u32(b + 73, 4, 0, false) ||
                xx_data_get_u32(b + 73, 4, 0, false) > 384000)
                return false;
            a = xx_data_get_u32(b + 58, 4, 0, false);
            z = xx_data_get_u32(b + 62, 4, 0, false);
            if (b[0] & 128 && (a >= len || (z != UINT32_MAX && (a > z || z > len)))) return false;
            smids[id] = 1;
            ++samples;
            if (!binary_skip(&d, len)) return false;
        } else if (!xx_rt_memcmp(h, "SONG", 4)) {
            uint32_t subcount = 0;
            bool playlist = false;
            if (!(used & 1) || used & 4 || !binary_get(&d, b, 11) || b[9] != 1 || !b[10] || b[10] > 32) return false;
            if (ch > b[10]) return false;
            ch = b[10];
            used |= 4;
            while (d.at < d.end) {
                uint32_t sn;
                binary_cursor sub;
                if (++subcount > 16 || !binary_get(&d, q, 8) || !binary_range(d.at, sn = xx_data_get_u32(q + 4, 4, 0, false), d.end)) return false;
                sub = d;
                sub.end = d.at + sn;
                if (!xx_rt_memcmp(q, "OPLH", 4)) {
                    uint32_t ops = 0, decl;
                    if (playlist || !binary_get(&sub, b, 2)) return false;
                    playlist = true;
                    decl = xx_data_get_u16(b, 2, 0, false);
                    while (sub.at < sub.end) {
                        uint32_t op;
                        if (!binary_get(&sub, b, 1)) return false;
                        if (!(op = b[0])) {
                            if (sub.at != sub.end) return false;
                            break;
                        }
                        ++ops;
                        if (op == 1) {
                            if (no >= 256 || !binary_get(&sub, b, 4) || !tracker_component_psmid(b, &orders[no++])) return false;
                        } else if (op == 2) {
                            if (!binary_skip(&sub, 4)) return false;
                        } else if (op == 3 || op == 4) {
                            if (!binary_skip(&sub, op == 3 ? 3 : 2)) return false;
                        } else if (op == 5 || op == 14) {
                            if (!binary_get(&sub, b, 2) || b[0] >= ch) return false;
                        } else if (op == 6) {
                            if (!binary_skip(&sub, 1)) return false;
                        } else if (op == 7 || op == 8) {
                            if (!binary_get(&sub, b, 1) || !b[0] || (op == 8 && b[0] < 32)) return false;
                        } else if (op == 12) {
                            if (!binary_get(&sub, b, 6) || xx_rt_memcmp(b, "\0\xff\0\0\1\0", 6)) return false;
                        } else if (op == 13) {
                            if (!binary_get(&sub, b, 3) || b[0] >= ch || (b[2] != 0 && b[2] != 2 && b[2] != 4)) return false;
                        } else return false;
                    }
                    if (!ops || ops > decl + 1U) return false;
                } else if (!xx_rt_memcmp(q, "DATE", 4)) {
                    if (sn != 6 || !binary_skip(&sub, sn)) return false;
                } else if (!xx_rt_memcmp(q, "PATT", 4) || !xx_rt_memcmp(q, "DSAM", 4)) {
                    if (sn > 4096 || !binary_skip(&sub, sn)) return false;
                } else return false;
                if (sub.at != sub.end) return false;
                d.at = sub.end;
            }
            if (!playlist || !no) return false;
        } else return false;
        if (d.at != d.end) return false;
        xx_rt_snprintf(label, sizeof(label), "chunk-%c%c%c%c.bin", h[0], h[1], h[2], h[3]);
        if (!tracker_component_emit(f, s, label, start, 8U + (uint64_t)n, c.end)) return false;
        c.at = d.end;
    }
    if ((used & 5) != 5 || !patterns || !samples) {
        return false;
    }
    for (j = 0; j < 256; ++j)
        if (samplerefs[j] && !smids[j]) return false;
    for (j = 0; j < no; ++j)
        if (!patids[orders[j]]) return false;
    s->size = total;
    return true;
}

void xx_tracker_psm_init(xx_tracker_psm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_PSM, "psm");
    }
}
xx_tracker_psm *xx_tracker_psm_create(xx_io_device *d, int64_t b)
{
    xx_tracker_psm *r = (xx_tracker_psm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_psm_init(r, d, b);
    return r;
}
void xx_tracker_psm_destroy(xx_tracker_psm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_psm_free(xx_tracker_psm *r)
{
    if (r) {
        xx_tracker_psm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_psm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_psm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

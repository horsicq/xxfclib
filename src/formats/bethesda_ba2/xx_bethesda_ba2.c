/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * GNRL layout reference: https://raw.githubusercontent.com/OpenMW/openmw/master/components/bsa/ba2gnrlfile.cpp
 * The v2/v3 headers and zlib/LZ4 payloads are checked against archives from
 * the independent dream_archive 1.0.0 producer. This reader borrows the
 * source device and uses numbered safe output names.
 */
#include "xxfclib/formats/bethesda_ba2/xx_bethesda_ba2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"

#define BA2_MAX_UNPACKED (64U * 1024U * 1024U)
#define BA2_MAX_DECODED_TOTAL (256U * 1024U * 1024U)
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[36], r[36], b[2];
    uint32_t count, i, version, codec = 0U, header_size;
    uint64_t names, at, end, floor, decoded_total = 0U;
    int64_t total = pm_available(f);
    if (!gm_read(f, total, 0, h, 24) || xx_rt_memcmp(h, "BTDX", 4) || xx_rt_memcmp(h + 8, "GNRL", 4)) return false;
    version = xx_data_get_u32(h + 4, 4, 0, false);
    if (version == 2U || version == 3U) header_size = version == 3U ? 36U : 32U;
    else if (version == 1U || version == 7U || version == 8U) header_size = 24U;
    else return false;
    if (!gm_read(f, total, 0, h, header_size)) return false;
    if (version == 3U) {
        codec = xx_data_get_u32(h + 32, 4, 0, false);
        if (codec != 0U && codec != 3U) return false;
    }
    count = xx_data_get_u32(h + 12, 4, 0, false);
    names = xx_data_get_u64(h + 16, 8, 0, false);
    floor = header_size + (uint64_t)count * 36U;
    if (count > 65536 || names < floor || !gm_range(total, 0, floor) || names > (uint64_t)total) {
        return false;
    }
    at = names;
    for (i = 0; i < count; ++i) {
        uint32_t n;
        if (gm_stopped(pd) || !gm_read(f, total, at, b, 2)) return false;
        at += 2;
        n = xx_data_get_u16(b, 2, 0, false);
        if (!n || !gm_range(total, at, n)) return false;
        at += n;
    }
    end = at;
    s->size = (int64_t)end;
    for (i = 0; i < count; ++i) {
        uint64_t off;
        uint32_t n;
        uint32_t packed, stored;
        pm_member *member;
        if (gm_stopped(pd) || !gm_read(f, total, header_size + (uint64_t)i * 36U, r, 36) || xx_data_get_u32(r + 32, 4, 0, false) != 0xbaadf00dU) return false;
        off = xx_data_get_u64(r + 16, 8, 0, false);
        packed = xx_data_get_u32(r + 24, 4, 0, false);
        n = xx_data_get_u32(r + 28, 4, 0, false);
        stored = packed ? packed : n;
        if ((off < end && off + stored > names) || !gm_add(f, s, "member.bin", off, stored, floor, total)) return false;
        member = &s->items[s->count - 1U];
        member->size = n;
        member->packed_size = stored;
        if (packed) {
            uint8_t *input;
            size_t written = 0U;
            bool okay;
            if (!n || n > BA2_MAX_UNPACKED || packed > BA2_MAX_UNPACKED || decoded_total + n > BA2_MAX_DECODED_TOTAL) return false;
            decoded_total += n;
            input = (uint8_t *)xx_mem_alloc(packed);
            member->memory = (uint8_t *)xx_mem_alloc(n);
            if (!input || !member->memory) {
                if (input) xx_mem_free(input);
                return false;
            }
            if (!gm_read(f, total, off, input, packed)) {
                xx_mem_free(input);
                return false;
            }
            if (codec == 3U) okay = xx_lz4_decompress_block(input, packed, member->memory, n, &written);
            else okay = xx_zlib_stream_decode_memory(input, packed, member->memory, n, &written) && xx_zlib_stream_trailer_matches(input, packed, member->memory, n);
            xx_mem_free(input);
            if (!okay || written != n) return false;
        }
    }
    return true;
}
void xx_bethesda_ba2_init(xx_bethesda_ba2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_BETHESDA_BA2, "ba2");
    }
}
xx_bethesda_ba2 *xx_bethesda_ba2_create(xx_io_device *d, int64_t b)
{
    xx_bethesda_ba2 *r = (xx_bethesda_ba2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_bethesda_ba2_init(r, d, b);
    return r;
}
void xx_bethesda_ba2_destroy(xx_bethesda_ba2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_bethesda_ba2_free(xx_bethesda_ba2 *r)
{
    if (r) {
        xx_bethesda_ba2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_bethesda_ba2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_bethesda_ba2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * FTSV DCX image: two self-contained context maps followed by bounded
 * counted bit streams.  The first stream is FILE.FDL; the second map is
 * shared by all FILE.SAV blocks.  Neither executable code nor VMS RMS APIs
 * are required to recover their exact byte streams.
 */
#include "xxfclib/formats/sfx_vms_dcx/xx_sfx_vms_dcx.h"
#include "../common/xx_carrier_helpers.h"

#define VD_SCAN_MAX (1024U * 1024U)
#define VD_INPUT_MAX (128U * 1024U * 1024U)
#define VD_OUTPUT_MAX (256U * 1024U * 1024U)
#define VD_TABLE_MAX (1024U * 1024U)
#define VD_SLOT_SIZE 0x440U
#define VD_NODE_OFFSET 0x40U
#define VD_MAP_OFFSET 0x240U
#define VD_MAX_FRAMES 65536U

typedef struct vd_table {
    uint8_t *slots;
    uint16_t contexts;
    int64_t end;
} vd_table;

static bool vd_table_open(Abstractformat *f, int64_t at, vd_table *table,
                          xx_pd_struct *pd) {
    uint8_t header[20], *data = NULL;
    uint32_t size;
    uint16_t count;
    size_t cursor, i;
    int64_t limit = pm_available(f);
    bool okay = false;
    xx_mem_zero(table, sizeof(*table));
    if (!carrier_range(limit, (uint64_t)at, sizeof(header)) ||
        !pm_read(f, at, header, sizeof(header))) return false;
    size = xx_data_get_u32(header, 4, 0, false);
    count = xx_data_get_u16(header + 16, 2, 0, false);
    if (size < 20U || size > VD_TABLE_MAX || !count || count > 256U ||
        xx_data_get_u32(header + 4, 4, 0, false) || xx_data_get_u32(header + 8, 4, 0, false) != 0x5bf5a3a7U ||
        xx_data_get_u32(header + 12, 4, 0, false) || xx_data_get_u16(header + 18, 2, 0, false) != 20U ||
        size < 20U + (uint32_t)count * 12U ||
        !carrier_range(limit, (uint64_t)at, size)) return false;
    data = (uint8_t *)xx_mem_alloc(size);
    table->slots = (uint8_t *)xx_mem_calloc((size_t)count, VD_SLOT_SIZE);
    if (!data || !table->slots || !pm_read(f, at, data, size)) goto done;
    cursor = 20U;
    for (i = 0U; i < count; ++i) {
        uint16_t block, node_at, map_at;
        unsigned first, last;
        size_t node_end, nodes, symbols;
        uint8_t *slot = table->slots + i * VD_SLOT_SIZE;
        if (carrier_stop(pd) || size - cursor < 12U) goto done;
        block = xx_data_get_u16(data + cursor, 2, 0, false);
        first = data[cursor + 2U]; last = data[cursor + 3U];
        node_at = xx_data_get_u16(data + cursor + 8U, 2, 0, false);
        map_at = xx_data_get_u16(data + cursor + 10U, 2, 0, false);
        if (first > last || data[cursor + 4U] || data[cursor + 5U] ||
            xx_data_get_u16(data + cursor + 6U, 2, 0, false) != 12U ||
            node_at <= 12U || node_at - 12U > VD_NODE_OFFSET ||
            block < node_at || block > size - cursor) goto done;
        node_end = map_at ? map_at : block;
        if (node_end <= node_at || node_end > block) goto done;
        nodes = node_end - node_at;
        if (nodes > VD_MAP_OFFSET - VD_NODE_OFFSET) goto done;
        xx_mem_copy(slot, data + cursor + 12U, node_at - 12U);
        xx_mem_copy(slot + VD_NODE_OFFSET, data + cursor + node_at, nodes);
        if (map_at) {
            symbols = last - first + 1U;
            if ((size_t)(block - map_at) != symbols * 2U) goto done;
            xx_mem_copy(slot + VD_MAP_OFFSET + first * 2U,
                        data + cursor + map_at, symbols * 2U);
        }
        cursor += block;
    }
    if (cursor != size) goto done;
    for (i = 0U; i < count; ++i) {
        uint8_t *slot = table->slots + i * VD_SLOT_SIZE;
        unsigned symbol;
        if (carrier_stop(pd)) goto done;
        for (symbol = 0U; symbol < 256U; ++symbol) {
            if (xx_data_get_u16(slot + VD_MAP_OFFSET + symbol * 2U, 2, 0, false) >= count)
                goto done;
        }
    }
    table->contexts = count;
    table->end = at + size;
    okay = true;
done:
    xx_mem_free(data);
    if (!okay) { xx_mem_free(table->slots); xx_mem_zero(table, sizeof(*table)); }
    return okay;
}

/* Consume every declared packed byte and the terminal tree edge.  Requiring
 * both exact lengths rejects a valid-looking prefix with unrelated data. */
static bool vd_decode(const vd_table *table, const uint8_t *packed,
                      size_t packed_size, uint8_t *plain, size_t raw_size,
                      xx_pd_struct *pd) {
    const uint8_t *context = table->slots;
    size_t in = 0U, out = 0U;
    unsigned node = 0U, bits = 0U, accumulator = 0U;
    while (true) {
        unsigned symbol, next;
        if ((in & 4095U) == 0U && carrier_stop(pd)) return false;
        if (!bits) {
            if (in >= packed_size) return false;
            accumulator = packed[in++];
            bits = 8U;
        }
        if (accumulator & 1U) ++node;
        accumulator >>= 1U;
        --bits;
        if (node >= 512U) return false;
        if (context[node >> 3] & (1U << (node & 7U))) {
            if (out >= raw_size) return false;
            symbol = context[VD_NODE_OFFSET + node];
            plain[out++] = (uint8_t)symbol;
            next = xx_data_get_u16(context + VD_MAP_OFFSET + symbol * 2U, 2, 0, false);
            if (next >= table->contexts) return false;
            context = table->slots + (size_t)next * VD_SLOT_SIZE;
            node = 0U;
        } else {
            unsigned child = context[VD_NODE_OFFSET + node];
            if (!child) return out == raw_size && in == packed_size;
            node = child * 2U;
        }
    }
}

static bool vd_candidate(Abstractformat *f, pm_stream *stream,
                          int64_t banner, xx_pd_struct *pd) {
    static const uint8_t group_head[4] = {0x33, 0x33, 0x44, 0x44};
    static const uint8_t next_head[8] = {
        0x55, 0x55, 0x66, 0x66, 0x11, 0x11, 0x22, 0x22
    };
    static const uint8_t group_tail[4] = {0x55, 0x55, 0x66, 0x66};
    uint8_t header[46], row[12], *fdl = NULL, *sav = NULL, *packed = NULL;
    vd_table first, second;
    int64_t limit = pm_available(f), first_data, first_end, sav_start, end;
    int64_t at, tail;
    uint32_t fdl_table_size, sav_table_size;
    uint16_t fdl_packed, fdl_raw;
    uint64_t sav_raw = 0U;
    unsigned frames = 0U;
    bool okay = false;
    xx_mem_zero(&first, sizeof(first));
    xx_mem_zero(&second, sizeof(second));
    if (!carrier_range(limit, (uint64_t)banner, sizeof(header)) ||
        !pm_read(f, banner, header, sizeof(header)) ||
        xx_rt_memcmp(header, "OpenVMS DCX FTSV Compressed File", 32) ||
        xx_rt_memcmp(header + 34, "\x11\x11\x22\x22", 4)) return false;
    fdl_raw = xx_data_get_u16(header + 32, 2, 0, false);
    fdl_table_size = xx_data_get_u32(header + 38, 4, 0, false);
    if (!fdl_raw || xx_data_get_u32(header + 42, 4, 0, false) != fdl_table_size ||
        !vd_table_open(f, banner + 42, &first, pd)) goto done;
    at = first.end;
    if (!carrier_range(limit, (uint64_t)at, 8U) ||
        !pm_read(f, at, row, 8U) ||
        xx_rt_memcmp(row, group_head, sizeof(group_head))) goto done;
    fdl_packed = xx_data_get_u16(row + 4, 2, 0, false);
    if (!fdl_packed || xx_data_get_u16(row + 6, 2, 0, false) != fdl_raw ||
        !carrier_range(limit, (uint64_t)at + 8U, fdl_packed)) goto done;
    first_data = at + 8;
    first_end = first_data + fdl_packed;
    if (!carrier_range(limit, (uint64_t)first_end, sizeof(next_head) + 8U) ||
        !pm_read(f, first_end, row, sizeof(row)) ||
        xx_rt_memcmp(row, next_head, sizeof(next_head))) goto done;
    sav_table_size = xx_data_get_u32(row + 8, 4, 0, false);
    if (!sav_table_size || !vd_table_open(f, first_end + 12, &second, pd) ||
        !pm_read(f, first_end + 12, row, 4U) ||
        xx_data_get_u32(row, 4, 0, false) != sav_table_size) goto done;
    at = second.end;
    if (!carrier_range(limit, (uint64_t)at, 2U) ||
        !pm_read(f, at, row, 2U) ||
        row[0] != 0x33U || row[1] != 0x33U) goto done;
    at += 2;
    sav_start = at + 6;
    while (at <= limit - 4) {
        uint16_t member_packed, member_raw;
        if (carrier_stop(pd)) goto done;
        if (at == limit - 4) break;
        if (!carrier_range(limit, (uint64_t)at, 6U) ||
            !pm_read(f, at, row, 6U) ||
            row[0] != 0x44U || row[1] != 0x44U) goto done;
        member_packed = xx_data_get_u16(row + 2, 2, 0, false);
        member_raw = xx_data_get_u16(row + 4, 2, 0, false);
        if (!member_packed || !member_raw ||
            ++frames > VD_MAX_FRAMES ||
            sav_raw > VD_OUTPUT_MAX - member_raw ||
            !carrier_range(limit - 4, (uint64_t)at + 6U, member_packed))
            goto done;
        sav_raw += member_raw;
        at += 6 + member_packed;
    }
    tail = at;
    if (!frames || !sav_raw || at != limit - 4 ||
        !pm_read(f, tail, row, 4U) ||
        xx_rt_memcmp(row, group_tail, sizeof(group_tail))) goto done;

    packed = (uint8_t *)xx_mem_alloc(65535U);
    fdl = (uint8_t *)xx_mem_alloc(fdl_raw);
    sav = (uint8_t *)xx_mem_alloc((size_t)sav_raw);
    if (!packed || !fdl || !sav ||
        !pm_read(f, first_data, packed, fdl_packed) ||
        !vd_decode(&first, packed, fdl_packed, fdl, fdl_raw, pd)) goto done;
    at = second.end + 2;
    end = 0;
    while (at < tail) {
        uint16_t member_packed, member_raw;
        if (carrier_stop(pd) || !pm_read(f, at, row, 6U)) goto done;
        member_packed = xx_data_get_u16(row + 2, 2, 0, false);
        member_raw = xx_data_get_u16(row + 4, 2, 0, false);
        if ((uint64_t)end + member_raw > sav_raw ||
            !pm_read(f, at + 6, packed, member_packed) ||
            !vd_decode(&second, packed, member_packed, sav + end,
                       member_raw, pd)) goto done;
        end += member_raw;
        at += 6 + member_packed;
    }
    if (end != (int64_t)sav_raw || !pm_add(f, stream, "FILE.FDL",
                                         first_data, fdl_packed)) goto done;
    xx_rt_snprintf(stream->items[stream->count - 1U].name,
                   sizeof(stream->items[stream->count - 1U].name),
                   "FILE.FDL");
    stream->items[stream->count - 1U].memory = fdl;
    stream->items[stream->count - 1U].size = fdl_raw;
    fdl = NULL;
    if (!pm_add(f, stream, "FILE.SAV", sav_start,
                tail - sav_start)) goto done;
    xx_rt_snprintf(stream->items[stream->count - 1U].name,
                   sizeof(stream->items[stream->count - 1U].name),
                   "FILE.SAV");
    stream->items[stream->count - 1U].memory = sav;
    stream->items[stream->count - 1U].size = (int64_t)sav_raw;
    stream->size = limit;
    sav = NULL;
    okay = true;
done:
    xx_mem_free(packed);
    xx_mem_free(fdl);
    xx_mem_free(sav);
    xx_mem_free(first.slots);
    xx_mem_free(second.slots);
    return okay;
}

static bool pm_parse(Abstractformat *f, pm_stream *stream,
                     xx_pd_struct *pd) {
    static const char banner[] = "OpenVMS DCX FTSV Compressed File";
    int64_t limit = pm_available(f);
    size_t size, i;
    uint8_t *scan;
    unsigned candidates = 0U;
    bool okay = false;
    if (limit < 128 || limit > VD_INPUT_MAX || carrier_stop(pd)) return false;
    size = (size_t)(limit > VD_SCAN_MAX ? VD_SCAN_MAX : limit);
    scan = (uint8_t *)xx_mem_alloc(size);
    if (!scan || !pm_read(f, 0, scan, size)) {
        xx_mem_free(scan);
        return false;
    }
    for (i = 0U; i + sizeof(banner) - 1U <= size; ++i) {
        if ((i & 4095U) == 0U && carrier_stop(pd)) break;
        if (xx_rt_memcmp(scan + i, banner, sizeof(banner) - 1U)) continue;
        if (++candidates > 8U) break;
        if (vd_candidate(f, stream, (int64_t)i, pd)) {
            okay = true;
            break;
        }
        if (stream->count) break;
    }
    xx_mem_free(scan);
    /* The member sizes are decoded lengths; compare their packed source
     * extents here.  The FDL output extends past the start of the SAV source
     * even though the two physical streams do not overlap. */
    return okay && stream->count == 2U &&
        stream->items[0].offset + stream->items[0].packed_size <=
        stream->items[1].offset;
}

void xx_sfx_vms_dcx_init(xx_sfx_vms_dcx *r, xx_io_device *d, int64_t b) {
    if (r) { xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_VMS_DCX, "exe"); }
}
xx_sfx_vms_dcx *xx_sfx_vms_dcx_create(xx_io_device *d, int64_t b) {
    xx_sfx_vms_dcx *r = (xx_sfx_vms_dcx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_vms_dcx_init(r, d, b);
    return r;
}
void xx_sfx_vms_dcx_destroy(xx_sfx_vms_dcx *r) {
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_vms_dcx_free(xx_sfx_vms_dcx *r) {
    if (r) { xx_sfx_vms_dcx_destroy(r); xx_mem_free(r); }
}
bool xx_sfx_vms_dcx_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    return pm_valid(f, pd);
}
bool xx_sfx_vms_dcx_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    return pm_handle(f, pd);
}

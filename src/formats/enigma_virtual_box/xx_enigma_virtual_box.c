/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * EVB layouts independently implemented from the interoperability research
 * documented in HNIdesu/Enigma-Virtual-Box-Unpacker (Apache-2.0).
 */
#include "xxfclib/formats/enigma_virtual_box/xx_enigma_virtual_box.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../ue2_indexed.h"
#include "xxfclib/data/xx_data.h"
#ifdef ENIGMA_VIRTUAL_BOX
#define UE2_ENIGMA_TYPE XX_FILE_TYPE_ENIGMA_VIRTUAL_BOX
#else
#define UE2_ENIGMA_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define EVB_MEMBER_LIMIT (256U * 1024U * 1024U)
#define EVB_CHUNK_LIMIT (64U * 1024U * 1024U)

typedef struct evb_parser {
    Abstractformat *format;
    ue2_index *index;
    int64_t cursor, payload, table_end, total;
    uint64_t nodes;
    bool legacy;
    xx_pd_struct *pd;
} evb_parser;

static bool evb_name(evb_parser *p, char *name, size_t capacity, uint8_t *type) {
    size_t done = 0, units = 0;
    uint8_t word[2];
    while (units++ < 2048) {
        uint32_t value;
        if (!ue2_read(p->format, p->cursor, word, 2)) return false;
        p->cursor += 2; value = xx_data_get_u16(word, 2, 0, false);
        if (!value) break;
        if (value >= 0xd800 && value <= 0xdbff) {
            uint32_t low;
            if (!ue2_read(p->format, p->cursor, word, 2)) return false;
            p->cursor += 2; low = xx_data_get_u16(word, 2, 0, false);
            if (low < 0xdc00 || low > 0xdfff) return false;
            value = 0x10000U + ((value - 0xd800U) << 10) + low - 0xdc00U;
        } else if (value >= 0xdc00 && value <= 0xdfff) return false;
        if (done + 4 >= capacity) return false;
        if (value < 0x80) name[done++] = (char)value;
        else if (value < 0x800) { name[done++] = (char)(0xc0 | value >> 6); name[done++] = (char)(0x80 | (value & 63)); }
        else if (value < 0x10000) {
            name[done++] = (char)(0xe0 | value >> 12); name[done++] = (char)(0x80 | ((value >> 6) & 63)); name[done++] = (char)(0x80 | (value & 63));
        } else {
            name[done++] = (char)(0xf0 | value >> 18); name[done++] = (char)(0x80 | ((value >> 12) & 63));
            name[done++] = (char)(0x80 | ((value >> 6) & 63)); name[done++] = (char)(0x80 | (value & 63));
        }
    }
    if (units > 2048 || !ue2_read(p->format, p->cursor, type, 1)) return false;
    ++p->cursor; name[done] = 0; return true;
}

static bool evb_nodes(evb_parser *p, uint32_t count, const char *prefix, unsigned depth) {
    uint32_t i;
    if (depth > 64 || count > UE2_INDEX_LIMIT) return false;
    for (i = 0; i < count; ++i) {
        uint8_t header[16], optional[53], type;
        uint32_t node_size, children, original, stored;
        char name[8192]; char *path = NULL;
        int64_t origin = p->cursor, data, end;
        bool result = false;
        if ((p->pd && xx_pd_is_stopped(p->pd)) || ++p->nodes > UE2_INDEX_LIMIT ||
            !ue2_read(p->format, origin, header, 16)) return false;
        node_size = xx_data_get_u32(header, 4, 0, false); children = xx_data_get_u32(header + 12, 4, 0, false); p->cursor += 16;
        if (!evb_name(p, name, sizeof(name), &type) || (type != 2 && type != 3) ||
            xx_rt_strchr(name, '/') || xx_rt_strchr(name, '\\') || xx_rt_strchr(name, ':')) return false;
        if (depth == 0 && type == 3 && !xx_rt_strcmp(name, "%DEFAULT FOLDER%")) name[0] = 0;
        if (!name[0] && !(depth == 0 && type == 3)) return false;
        path = prefix && prefix[0] ? xx_str_concat3(prefix, name[0] ? "/" : "", name) : xx_str_dup(name);
        if (!path || (path[0] && !ue2_safe_name(path))) goto done;
        if (type == 3) {
            if (p->legacy) {
                if (node_size < 16 || !ue2_range(p->total, origin, (int64_t)node_size + 4) || origin + node_size + 4 < p->cursor) goto done;
                p->cursor = origin + node_size + 4;
            } else {
                if (!ue2_range(p->table_end, p->cursor, 25)) goto done;
                p->cursor += 25;
            }
            if (path[0]) {
                if (!ue2_add(p->index, path, 0, 0, 0)) goto done;
                p->index->members[p->index->count - 1].is_folder = true;
            }
            result = evb_nodes(p, children, path, depth + 1);
        } else {
            if (children) goto done;
            if (p->legacy) {
                if (node_size < 45 || !ue2_range(p->total, origin, (int64_t)node_size + 4)) goto done;
                end = origin + node_size + 4;
                if (end - 49 < p->cursor || !ue2_read(p->format, end - 49, optional, 49)) goto done;
                original = xx_data_get_u32(optional + 2, 4, 0, false); stored = xx_data_get_u32(optional + 41, 4, 0, false); data = end;
                if (!ue2_range(p->total, data, stored)) goto done;
                p->cursor = data + stored;
            } else {
                if (!ue2_range(p->table_end, p->cursor, 53) || !ue2_read(p->format, p->cursor, optional, 53)) goto done;
                original = xx_data_get_u32(optional + 2, 4, 0, false); stored = xx_data_get_u32(optional + 49, 4, 0, false); p->cursor += 53;
                data = p->payload;
                if (!ue2_range(p->total, data, stored)) goto done;
                p->payload += stored;
            }
            if (!ue2_add(p->index, path, data, stored, original != stored)) goto done;
            p->index->members[p->index->count - 1].original_size = original;
            result = true;
        }
done: xx_str_free(path); if (!result) return false;
    }
    return true;
}
static ue2_index *evb_at(Abstractformat *f, int64_t start, bool legacy, xx_pd_struct *pd) {
    uint8_t header[80];
    evb_parser p;
    uint32_t size, roots;
    if (!ue2_read(f, start, header, sizeof(header)) || xx_rt_memcmp(header, "EVB\0", 4) || xx_data_get_u32(header + 4, 4, 0, false) != 64) return NULL;
    size = xx_data_get_u32(header + 64, 4, 0, false); roots = xx_data_get_u32(header + 76, 4, 0, false);
    if (!roots || roots > 65536U) return NULL;
    xx_mem_zero(&p, sizeof(p)); p.format = f; p.total = xx_io_total_size(f->device);
    p.pd = pd; p.legacy = legacy;
    p.index = (ue2_index *)xx_mem_calloc(1, sizeof(*p.index));
    if (!p.index) return NULL;
    if (legacy) {
        /* The root main node has an empty UTF-16 name and type0. */
        if (size < 15 || !ue2_range(p.total, start + 64, (int64_t)size + 4) || header[80 - 1] != 0) goto fail;
        p.cursor = start + 68 + size; p.table_end = p.total;
    } else {
        if (size < 16 || !ue2_range(p.total, start + 64, (int64_t)size + 4)) goto fail;
        p.cursor = start + 79; p.payload = start + 68 + size; p.table_end = p.payload;
    }
    if (!evb_nodes(&p, roots, "", 0) || !p.index->count || (!legacy && p.cursor > p.table_end)) goto fail;
    p.index->size = p.total - f->base_address; return p.index;
fail: ue2_index_free(p.index); return NULL;
}
static ue2_index *evb_parse(Abstractformat *f, uint32_t *legacy_out, int64_t *offset_out, xx_pd_struct *pd) {
    uint8_t *buffer, first[4];
    int64_t cursor, total;
    unsigned candidates = 0;
    ue2_index *index = NULL;
    if (!f || !f->device || f->base_address < 0 || !ue2_read(f, f->base_address, first, 4)) return NULL;
    total = xx_io_total_size(f->device);
    if (xx_rt_memcmp(first, "EVB\0", 4) && (first[0] != 'M' || first[1] != 'Z')) return NULL;
    buffer = (uint8_t *)xx_mem_alloc(65539U);
    if (!buffer) return NULL;
    for (cursor = f->base_address; cursor < total; cursor += 65536) {
        size_t take = total - cursor > 65539 ? 65539U : (size_t)(total - cursor), i;
        if ((pd && xx_pd_is_stopped(pd)) || !ue2_read(f, cursor, buffer, take)) break;
        for (i = 0; i + 4 <= take && i < 65536U; ++i) {
            int64_t offset;
            unsigned legacy;
            if (xx_rt_memcmp(buffer + i, "EVB\0", 4)) continue;
            if (++candidates > 64) goto done;
            offset = cursor + (int64_t)i;
            for (legacy = 0; legacy <= 1; ++legacy) {
                index = evb_at(f, offset, legacy != 0, pd);
                if (index) {
                    if (legacy_out) *legacy_out = legacy;
                    if (offset_out) *offset_out = offset;
                    goto done;
                }
            }
        }
    }
done: xx_mem_free(buffer); return index;
}

typedef struct evb_bits { const uint8_t *data; size_t size, cursor; unsigned tag, remaining; bool failed; } evb_bits;
static unsigned evb_byte(evb_bits *s) {
    if (s->cursor >= s->size) { s->failed = true; return 0; }
    return s->data[s->cursor++];
}
static unsigned evb_bit(evb_bits *s) {
    unsigned result;
    if (!s->remaining) { s->tag = evb_byte(s); s->remaining = 8; }
    result = (s->tag >> 7) & 1U; s->tag <<= 1; --s->remaining; return result;
}
static uint32_t evb_gamma(evb_bits *s) {
    uint32_t value = 1; unsigned n = 0;
    do {
        if (++n >= 31) { s->failed = true; return 0; }
        value = (value << 1) | evb_bit(s);
    } while (!s->failed && evb_bit(s));
    return value;
}
static bool evb_aplib(const uint8_t *data, size_t size, uint8_t *out, size_t capacity, size_t *written, xx_pd_struct *pd) {
    evb_bits s; size_t done = 0; uint32_t previous = 0; bool match = false;
    if (!size || !capacity) return false;
    xx_mem_zero(&s, sizeof(s)); s.data = data; s.size = size;
    out[done++] = (uint8_t)evb_byte(&s);
    while (!s.failed) {
        uint32_t offset = 0, length = 0;
        bool literal = false;
        if ((done & 4095U) == 0 && pd && xx_pd_is_stopped(pd)) return false;
        if (!evb_bit(&s)) { offset = evb_byte(&s); length = 1; literal = true; match = false; }
        else if (!evb_bit(&s)) {
            offset = evb_gamma(&s);
            if (!match && offset == 2) { offset = previous; length = evb_gamma(&s); }
            else {
                unsigned subtract = match ? 2 : 3;
                if (offset < subtract || offset - subtract > UINT32_MAX >> 8) return false;
                offset = ((offset - subtract) << 8) | evb_byte(&s); length = evb_gamma(&s);
                if (offset >= 32000) ++length;
                if (offset >= 1280) ++length;
                if (offset < 128) length += 2;
                previous = offset;
            }
            match = true;
        } else if (!evb_bit(&s)) {
            offset = evb_byte(&s); length = 2 + (offset & 1); offset >>= 1;
            if (!offset) { if (s.failed || s.cursor != s.size) return false; *written = done; return true; }
            previous = offset; match = true;
        } else {
            unsigned i;
            for (i = 0; i < 4; ++i) offset = (offset << 1) | evb_bit(&s);
            length = 1; match = false;
            if (!offset) { offset = 0; literal = true; }
        }
        if (s.failed || length > capacity - done || (!literal && (!offset || offset > done))) return false;
        while (length--) { out[done] = literal ? (uint8_t)offset : out[done - offset]; ++done; }
    }
    return false;
}
static bool evb_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    ue2_state *s;
    const ue2_member *m;
    uint8_t head[8], *sizes = NULL, *packed = NULL, *out = NULL;
    uint32_t block_size, count, i;
    size_t done = 0, packed_capacity = 0;
    int64_t cursor, remaining;
    bool result = false;
    xx_io_device *memory = NULL;
    if (!ue2_current(f, state) || !state->internal_state) return false;
    s = (ue2_state *)state->internal_state; m = &s->index->members[s->cursor];
    if (m->size == m->original_size || m->is_folder) return ue2_unpack(f, state, pd);
    if (m->original_size < 0 || m->original_size > EVB_MEMBER_LIMIT || m->size < 12 ||
        !ue2_read(f, m->offset, head, 8)) return false;
    block_size = xx_data_get_u32(head, 4, 0, false);
    if (block_size < 12 || block_size > 1024U * 1024U || block_size >= (uint64_t)m->size ||
        (block_size - 12) % 12) return false;
    count = (block_size - 12) / 12 + 1;
    sizes = (uint8_t *)xx_mem_alloc(block_size - 8);
    out = (uint8_t *)xx_mem_alloc((size_t)m->original_size);
    if (!sizes || !out || !ue2_read(f, m->offset + 8, sizes, block_size - 8)) goto cleanup;
    cursor = m->offset + block_size; remaining = m->size - block_size;
    for (i = 0; i < count; ++i) {
        uint32_t take = xx_data_get_u32(sizes + (size_t)i * 12, 4, 0, false); size_t produced = 0;
        const uint8_t *data; size_t data_size;
        if ((pd && xx_pd_is_stopped(pd)) || !take || take > EVB_CHUNK_LIMIT || take > remaining) goto cleanup;
        if (take > packed_capacity) {
            uint8_t *new_packed = (uint8_t *)xx_mem_realloc(packed, take);
            if (!new_packed) { goto cleanup; } packed = new_packed; packed_capacity = take;
        }
        if (!ue2_read(f, cursor, packed, take)) goto cleanup;
        data = packed; data_size = take;
        /* Optional AP32 wrapper includes two CRC32 checks and exact sizes. */
        if (take >= 24 && !xx_rt_memcmp(packed, "AP32", 4)) {
            uint32_t header_size = xx_data_get_u32(packed + 4, 4, 0, false), input_size = xx_data_get_u32(packed + 8, 4, 0, false);
            if (header_size < 24 || header_size > take || input_size != take - header_size ||
                xx_crc32(XX_CRC_TYPE_CRC32_ISO_HDLC, packed + header_size, input_size) != xx_data_get_u32(packed + 12, 4, 0, false)) goto cleanup;
            data = packed + header_size; data_size = input_size;
        }
        if (!evb_aplib(data, data_size, out + done, (size_t)m->original_size - done, &produced, pd)) goto cleanup;
        if (data != packed && (produced != xx_data_get_u32(packed + 16, 4, 0, false) ||
            xx_crc32(XX_CRC_TYPE_CRC32_ISO_HDLC, out + done, produced) != xx_data_get_u32(packed + 20, 4, 0, false))) goto cleanup;
        done += produced; cursor += take; remaining -= take;
    }
    if (remaining || done != (size_t)m->original_size) goto cleanup;
    memory = xx_io_mem_open_ro(out, done);
    if (memory) {
        Abstractformat output_format = *f;
        xx_archive_record_state output_state = *state;
        output_format.device = memory; output_state.format = &output_format;
        output_state.current_record.data_offset = 0; output_state.current_record.compressed_size = (int64_t)done;
        result = ue2_unpack(&output_format, &output_state, pd);
    }
cleanup:
    if (memory) xx_io_close(memory);
    xx_mem_free(sizes); xx_mem_free(packed); xx_mem_free(out); return result;
}
static bool evb_valid(Abstractformat *f, xx_pd_struct *pd) {
    ue2_index *index = evb_parse(f, NULL, NULL, pd);
    bool result = index != NULL; ue2_index_free(index); return result;
}
static bool evb_info(Abstractformat *f, xx_pd_struct *pd) {
    xx_enigma_virtual_box *a = (xx_enigma_virtual_box *)f;
    return ue2_accept(f, evb_parse(f, &a->legacy, &a->container_offset, pd));
}
void xx_enigma_virtual_box_init(xx_enigma_virtual_box *a, xx_io_device *device, int64_t base) {
    if (!a) { return; } xx_mem_zero(a, sizeof(*a));
    ue2_init_format(&a->format, device, base, UE2_ENIGMA_TYPE, "exe", "application/x-enigma-virtual-box");
    a->format.check_is_valid = evb_valid; a->format.handle_base_info = evb_info; a->format.unpack_current_archive_record = evb_unpack;
}
xx_enigma_virtual_box *xx_enigma_virtual_box_create(xx_io_device *device, int64_t base) {
    xx_enigma_virtual_box *a = (xx_enigma_virtual_box *)xx_mem_alloc(sizeof(*a));
    if (a) { xx_enigma_virtual_box_init(a, device, base); } return a;
}
void xx_enigma_virtual_box_destroy(xx_enigma_virtual_box *a) { if (a) ue2_destroy_format(&a->format); }
void xx_enigma_virtual_box_free(xx_enigma_virtual_box *a) { if (a) { xx_enigma_virtual_box_destroy(a); xx_mem_free(a); } }

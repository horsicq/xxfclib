/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Titan Quest ARC block archive with Adler-32 validation.
 */
#include "xxfclib/formats/titan_quest/xx_titan_quest.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"

typedef struct titan_quest_titan_block {
    uint32_t at, packed, size;
} titan_quest_titan_block;
typedef struct titan_quest_titan_member {
    uint32_t count, checksum;
    titan_quest_titan_block blocks[1];
} titan_quest_titan_member;
static bool titan_quest_titan_read(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    titan_quest_titan_member *ctx = (titan_quest_titan_member *)m->context;
    uint32_t i, checksum = XX_ADLER32_INIT;
    uint64_t total = 0;
    for (i = 0; i < ctx->count; ++i) {
        titan_quest_titan_block *b = &ctx->blocks[i];
        uint8_t *packed = NULL, *plain = NULL;
        bool ok = false;
        if (xgr_stop(pd)) return false;
        if (b->packed == b->size) {
            uint8_t buffer[65536];
            uint64_t offset = 0;
            while (offset < b->size) {
                size_t amount = b->size - offset < sizeof(buffer) ? (size_t)(b->size - offset) : sizeof(buffer);
                if (xgr_stop(pd) || !pm_read(f, (int64_t)(b->at + offset), buffer, amount) || !xgr_write(out, buffer, amount)) return false;
                checksum = xx_adler32_update(checksum, buffer, amount);
                offset += amount;
            }
            total += b->size;
            if (total > (uint64_t)m->size) return false;
            continue;
        }
        if (b->size > 16U * 1024U * 1024U || b->packed > 32U * 1024U * 1024U) return false;
        packed = (uint8_t *)xx_mem_alloc(b->packed ? b->packed : 1);
        if (!packed || !pm_read(f, b->at, packed, b->packed)) {
            xx_mem_free(packed);
            return false;
        }
        if (b->packed == b->size) {
            plain = packed;
            ok = true;
        } else if (b->packed >= 6 && xx_zlib_stream_header_is_valid(packed, b->packed)) {
            xx_io_device *memory;
            size_t consumed = 0;
            plain = (uint8_t *)xx_mem_alloc(b->size ? b->size : 1);
            memory = plain ? xx_io_mem_open(plain, b->size) : NULL;
            if (memory) {
                ok = xx_deflate_unpack_memory_to_device_ex(packed + 2, b->packed - 6, memory, &consumed, false, pd) && consumed == b->packed - 6 &&
                     xx_io_tell(memory) == b->size && xx_zlib_stream_trailer_matches(packed, b->packed, plain, b->size);
                xx_io_close(memory);
            }
        }
        if (ok) {
            checksum = xx_adler32_update(checksum, plain, b->size);
            total += b->size;
            ok = total <= (uint64_t)m->size && xgr_write(out, plain, b->size);
        }
        if (plain != packed) xx_mem_free(plain);
        xx_mem_free(packed);
        if (!ok) return false;
    }
    return total == (uint64_t)m->size && checksum == ctx->checksum && !xgr_stop(pd);
}
static bool titan_quest_titan(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[44];
    uint64_t n = (uint64_t)pm_available(f), parts, names, records, name_size;
    uint32_t count, blocks, i;
    if (n < 32 || !pm_read(f, 0, h, 32) || memcmp(h, "ARC\0", 4) || xgr_le32(h + 4) != 1) return false;
    count = xgr_le32(h + 8);
    blocks = xgr_le32(h + 12);
    parts = xgr_le32(h + 24);
    name_size = xgr_le32(h + 20);
    names = parts + xgr_le32(h + 16);
    records = names + name_size;
    if (!count || count > XGR_MAX_RECORDS || blocks > XGR_MAX_RECORDS || xgr_le32(h + 16) != 12ULL * blocks || parts < 32 || !xgr_range(parts, 12ULL * blocks, n) ||
        !xgr_range(names, name_size, n) || !xgr_range(records, 44ULL * count, n) || records + 44ULL * count != n)
        return false;
    for (i = 0; i < count; ++i) {
        uint32_t type, at, packed, size, used, first, len, k;
        uint64_t pos, sum_packed = 0, sum_size = 0;
        titan_quest_titan_member *ctx;
        char name[XGR_MAX_NAME + 1];
        size_t allocation;
        if (xgr_stop(pd) || !pm_read(f, (int64_t)(records + 44ULL * i), h, 44)) return false;
        type = xgr_le32(h);
        at = xgr_le32(h + 4);
        packed = xgr_le32(h + 8);
        size = xgr_le32(h + 12);
        used = xgr_le32(h + 28);
        first = xgr_le32(h + 32);
        len = xgr_le32(h + 36);
        pos = names + xgr_le32(h + 40);
        if ((type != 1 && type != 3) || !len || len > XGR_MAX_NAME || pos < names || !xgr_range(pos, len, name_size + names) || !pm_read(f, (int64_t)pos, name, len))
            return false;
        /* The recorded name length excludes the NUL terminator. */
        name[len] = 0;
        if (memchr(name, 0, len) || !xgr_name(name)) return false;
        {
            uint8_t nul;
            if (pos + len >= records || !pm_read(f, (int64_t)(pos + len), &nul, 1) || nul) return false;
        }
        if (type == 1) {
            if (packed != size || !xgr_range(at, size, parts)) return false;
            used = 1;
        } else if (!used || used > blocks || first > blocks - used) return false;
        allocation = sizeof(*ctx) + (size_t)(used - 1) * sizeof(ctx->blocks[0]);
        ctx = (titan_quest_titan_member *)xx_mem_alloc(allocation);
        if (!ctx) return false;
        ctx->count = used;
        ctx->checksum = xgr_le32(h + 16);
        for (k = 0; k < used; ++k) {
            titan_quest_titan_block *b = &ctx->blocks[k];
            uint8_t bh[12];
            if (type == 1) {
                b->at = at;
                b->packed = packed;
                b->size = size;
            } else {
                if (!pm_read(f, (int64_t)(parts + 12ULL * (first + k)), bh, 12)) {
                    xx_mem_free(ctx);
                    return false;
                }
                b->at = xgr_le32(bh);
                b->packed = xgr_le32(bh + 4);
                b->size = xgr_le32(bh + 8);
            }
            if (!xgr_range(b->at, b->packed, parts) || b->at < 32 || (b->packed != b->size && (b->size > 16U * 1024U * 1024U || b->packed > 32U * 1024U * 1024U))) {
                xx_mem_free(ctx);
                return false;
            }
            sum_packed += b->packed;
            sum_size += b->size;
        }
        if (sum_packed != packed || sum_size != size || !xgr_add(f, s, name, at, packed)) {
            xx_mem_free(ctx);
            return false;
        }
        {
            pm_member *m = &s->items[s->count - 1];
            m->size = size;
            m->context = ctx;
            m->free_context = xgr_free;
            m->read_all = titan_quest_titan_read;
            m->compression_method = type == 3 ? 8 : 0;
        }
    }
    s->size = (int64_t)n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return pm_available(f) >= 0 && !xgr_stop(pd) && titan_quest_titan(f, s, pd);
}
Abstractformat *xx_titan_quest_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (!f) return NULL;
    xx_mem_zero(f, sizeof(*f));
    pm_init(f, d, base, XX_FILE_TYPE_TITAN_QUEST, "arc");
    return f;
}
void xx_titan_quest_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}
xx_file_type_t xx_titan_quest_detect(xx_io_device *d, int64_t base)
{
    Abstractformat f;
    uint8_t h[64];
    int64_t size, old = xx_io_tell(d);
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f, sizeof(f));
    f.device = d;
    f.base_address = base;
    size = pm_available(&f);
    if (size < 8 || !pm_read(&f, 0, h, (size_t)(size < 64 ? size : 64))) goto done;
    {
        uint32_t magic = xgr_le32(h);
        if (magic == 0x00435241U && size >= 32 && xgr_le32(h + 4) == 1) type = XX_FILE_TYPE_TITAN_QUEST;
    }
done:
    if (old >= 0) xx_io_seek64(d, old, SEEK_SET);
    return type;
}
static Abstractformat *titan_quest_open(xx_io_device *d)
{
    return xx_titan_quest_create(d, 0);
}
static const xx_file_type_t titan_quest_types[] = {XX_FILE_TYPE_TITAN_QUEST};
static const xx_format_search_desc titan_quest_desc = {titan_quest_types, 1, NULL, 0, titan_quest_open, xx_titan_quest_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(titan_quest, titan_quest_desc)

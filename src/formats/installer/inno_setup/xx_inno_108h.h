/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private Inno 1.08/g/h wire layouts. */
static bool inno_parse_108h(Abstractformat *f, inno_ctx *ctx, int64_t size, xx_pd_struct *pd)
{
    const inno_table *t = &ctx->table;
    ibuf blk = {0};
    inno_rawlist names = {0};
    uint8_t **owned = NULL;
    uint32_t counts[7] = {0}, infos[3], i, k;
    bool early = ctx->ver.id_size == 6U, w16 = ctx->ver.win16;
    uint32_t width = w16 ? 2U : 4U;
    uint32_t ncounts = early ? 6U : 7U;
    uint64_t block_count = 1U, expected = 0U, data_size;
    int64_t cur = t->header_offset + (int64_t)ctx->ver.id_size, consumed = 0;
    bool ok = false;
    if (inno_stopped(pd) || t->kind != INNO_T_VX || t->data_offset <= cur || t->total_size > (uint64_t)size ||
        (uint64_t)t->data_offset >= t->total_size) return false;
    data_size = t->total_size - (uint64_t)t->data_offset;
    if (!inno_read_legacy_block(f, cur, t->data_offset, &blk, &consumed, pd) || blk.n != (w16 ? 317U : (early ? 333U : 337U))) goto done;
    cur += consumed;
    for (i = 0U; i < ncounts; ++i) {
        counts[i] = w16 ? xx_data_get_u16(blk.p + 288U + i * width, 2, 0, false) : xx_data_get_u32(blk.p + 288U + i * width, 4, 0, false);
        if (counts[i] > INNO_LEGACY_MAX_RECORDS) goto done;
        block_count += counts[i];
    }
    for (i = 0U; i < 3U; ++i) {
        infos[i] = w16 ? xx_data_get_u16(blk.p + 302U + i * width, 2, 0, false) : xx_data_get_u32(blk.p + (early ? 312U : 316U) + i * width, 4, 0, false);
        if (infos[i] > INNO_LEGACY_BLOCK_CAP) goto done;
        if (infos[i]) ++block_count;
    }
    if (!counts[1] || block_count > INNO_LEGACY_MAX_RECORDS) goto done;
    ibuf_free(&blk);
    for (i = 0U; i < 3U; ++i) {
        if (!infos[i]) continue;
        if (inno_stopped(pd) || !inno_read_legacy_block(f, cur, t->data_offset, &blk, &consumed, pd) || blk.n != infos[i]) goto done;
        cur += consumed;
        ibuf_free(&blk);
    }
    /* Directory actions are opaque48-byte metadata; only file records supply
     * output paths. A31-byte directory-name cap is not a wire constraint. */
    for (i = 0U; i < counts[0]; ++i) {
        if (inno_stopped(pd) || !inno_read_legacy_block(f, cur, t->data_offset, &blk, &consumed, pd) || blk.n != 48U) goto done;
        cur += consumed;
        ibuf_free(&blk);
    }
    ctx->loc_count = counts[1];
    ctx->locs = (inno_loc *)xx_mem_calloc(counts[1], sizeof(*ctx->locs));
    owned = (uint8_t **)xx_mem_calloc(counts[1], sizeof(*owned));
    if (!ctx->locs || !owned) goto done;
    for (i = 0U; i < counts[1]; ++i) {
        inno_loc *l = &ctx->locs[i];
        uint32_t off, raw, packed;
        uint8_t magic[4], length;
        if (inno_stopped(pd) || !inno_read_legacy_block(f, cur, t->data_offset, &blk, &consumed, pd) || (w16 ? blk.n != 94U : (blk.n != 98U && blk.n != 99U)) ||
            blk.p[4] > 63U) goto done;
        cur += consumed;
        off = xx_data_get_u32(blk.p, 4, 0, false);
        raw = xx_data_get_u32(blk.p + (w16 ? 80U : 84U), 4, 0, false);
        packed = xx_data_get_u32(blk.p + (w16 ? 84U : 88U), 4, 0, false);
        if ((uint64_t)off != expected || raw > INT32_MAX || !packed || packed > INT32_MAX || expected > data_size ||
            data_size - expected < 4U || packed > data_size - expected - 4U ||
            !inno_read_at(f->device, f->base_address + t->data_offset + (int64_t)expected, magic, 4U) || xx_rt_memcmp(magic, "zlb\x1a", 4U)) goto done;
        l->chunk_offset = off;
        l->size = raw;
        l->chunk_size = packed;
        l->hash_kind = INNO_H_ADLER32;
        l->compression = INNO_C_ZLIB;
        l->filetime = w16 ? xx_data_get_u32(blk.p + 68U, 4, 0, false) : xx_data_get_u64(blk.p + 68U, 8, 0, false);
        xx_rt_memcpy(l->checksum, blk.p + (w16 ? 88U : 92U), 4U);
        length = blk.p[4];
        if (length) {
            owned[i] = (uint8_t *)xx_mem_alloc(length);
            if (!owned[i]) goto done;
            xx_rt_memcpy(owned[i], blk.p + 5U, length);
            if (!inno_raw_add(&names, owned[i], length, false, i)) goto done;
        }
        expected += 4U + packed;
        ibuf_free(&blk);
    }
    for (k = 2U; k < ncounts; ++k) {
        for (i = 0U; i < counts[k]; ++i) {
            if (inno_stopped(pd) || !inno_read_legacy_block(f, cur, t->data_offset, &blk, &consumed, pd)) goto done;
            cur += consumed;
            ibuf_free(&blk);
        }
    }
    if (cur != t->data_offset || expected != data_size) goto done;
    ctx->data_base = f->base_address + t->data_offset;
    inno_group_locs(ctx);
    ok = inno_build_members(ctx, &names) && inno_sort_members(ctx);
    if (ok) inno_set_budget(ctx, xx_io_total_size(f->device));
done:
    ibuf_free(&blk);
    if (owned) {
        for (i = 0U; i < counts[1]; ++i) if (owned[i]) xx_mem_free(owned[i]);
        xx_mem_free(owned);
    }
    if (names.items) xx_mem_free(names.items);
    return ok;
}

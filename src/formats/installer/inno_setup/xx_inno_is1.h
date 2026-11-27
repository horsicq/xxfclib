/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private plain IS1 Win32 wire reader. */
static bool inno_parse_is1(Abstractformat *f, inno_ctx *ctx, int64_t size, xx_pd_struct *pd)
{
    const inno_table *t = &ctx->table;
    uint8_t root[300], entry[95];
    uint32_t dirs, files, icons, ini, registry, i;
    uint64_t meta_end, expected = 0U, data_size;
    int64_t cur = t->header_offset + 6;
    inno_rawlist names = {0};
    uint8_t **owned = NULL;
    bool ok = false;
    if (inno_stopped(pd) || t->kind != INNO_T_VX32 || t->total_size != (uint64_t)size || t->data_offset <= cur ||
        t->data_offset - cur < 300 || !inno_read_at(f->device, f->base_address + cur, root, sizeof(root))) return false;
    dirs = xx_data_get_u16(root + 290, 2, 0, false);
    files = xx_data_get_u16(root + 292, 2, 0, false);
    icons = xx_data_get_u16(root + 294, 2, 0, false);
    ini = xx_data_get_u16(root + 296, 2, 0, false);
    registry = xx_data_get_u16(root + 298, 2, 0, false);
    if (!files) return false;
    cur += 300;
    meta_end = (uint64_t)cur + (uint64_t)dirs * 48U + (uint64_t)files * 95U + (uint64_t)icons * 289U + (uint64_t)ini * 289U + (uint64_t)registry * 310U;
    if (meta_end != (uint64_t)t->data_offset) return false;
    data_size = t->total_size - (uint64_t)t->data_offset;
    cur += (int64_t)dirs * 48;
    ctx->loc_count = files;
    ctx->locs = (inno_loc *)xx_mem_calloc(files, sizeof(*ctx->locs));
    owned = (uint8_t **)xx_mem_calloc(files, sizeof(*owned));
    if (!ctx->locs || !owned) goto done;
    for (i = 0U; i < files; ++i) {
        inno_loc *l = &ctx->locs[i];
        uint32_t off, raw, packed;
        uint8_t length, magic[4];
        if (inno_stopped(pd) || !inno_read_at(f->device, f->base_address + cur, entry, sizeof(entry))) goto done;
        cur += 95;
        length = entry[6];
        off = xx_data_get_u32(entry + 2, 4, 0, false);
        raw = xx_data_get_u32(entry + 82, 4, 0, false);
        packed = xx_data_get_u32(entry + 86, 4, 0, false);
        if (length > 63U || (uint64_t)off != expected || raw > INT32_MAX || !packed || packed > INT32_MAX || expected > data_size ||
            data_size - expected < 4U || packed > data_size - expected - 4U ||
            !inno_read_at(f->device, f->base_address + t->data_offset + (int64_t)expected, magic, sizeof(magic)) || xx_rt_memcmp(magic, "zlb\x1a", 4U)) goto done;
        l->chunk_offset = off;
        l->size = raw;
        l->chunk_size = packed;
        l->hash_kind = INNO_H_ADLER32;
        l->compression = INNO_C_ZLIB;
        l->filetime = xx_data_get_u32(entry + 70, 4, 0, false);
        xx_rt_memcpy(l->checksum, entry + 90, 4U);
        if (length) {
            owned[i] = (uint8_t *)xx_mem_alloc(length);
            if (!owned[i]) goto done;
            xx_rt_memcpy(owned[i], entry + 7, length);
            if (!inno_raw_add(&names, owned[i], length, false, i)) goto done;
        }
        expected += 4U + packed;
    }
    if (expected != data_size) goto done;
    ctx->data_base = f->base_address + t->data_offset;
    inno_group_locs(ctx);
    ok = inno_build_members(ctx, &names) && inno_sort_members(ctx);
    if (ok) inno_set_budget(ctx, xx_io_total_size(f->device));
done:
    if (owned) {
        for (i = 0U; i < files; ++i) if (owned[i]) xx_mem_free(owned[i]);
        xx_mem_free(owned);
    }
    if (names.items) xx_mem_free(names.items);
    return ok;
}

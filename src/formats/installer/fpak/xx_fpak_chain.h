/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Numbered .PAK/.PA1..PA9 chains: extension order plus exact boundary identity,
 * never guessed permutations. The caller verifies every final payload CRC. */
static xx_io_device *fpak_chain_find(const char *directory, const xx_fpak_stream *current, unsigned desired, bool reverse, int64_t bytes, xx_pd_struct *pd)
{
    fpak_volume_scan scan;
    xx_mem_zero(&scan, sizeof(scan));
    scan.directory = directory;
    scan.selected_name = "";
    scan.selected = current;
    scan.pd = pd;
    scan.bytes = bytes;
    scan.chain = true;
    scan.desired = desired;
    scan.reverse = reverse;
    if (!xx_fs_platform_enumerate(directory, fpak_volume_candidate, &scan)) {
        if (scan.match) xx_io_close(scan.match);
        return NULL;
    }
    return scan.match;
}
static xx_fpak_stream *fpak_chain_table(xx_io_device *device, xx_pd_struct *pd)
{
    xx_fpak_stream *table = (xx_fpak_stream *)xx_mem_calloc(1U, sizeof(*table));
    Abstractformat logical;
    if (!table) return NULL;
    xx_mem_zero(&logical, sizeof(logical));
    logical.device = device;
    if (!xx_fpak_read_volume(&logical, table, false, pd)) {
        xx_fpak_stream_free(table);
        return NULL;
    }
    return table;
}
static xx_io_device *fpak_volume_chain_view(Abstractformat *self, const xx_fpak_stream *selected, xx_pd_struct *pd)
{
    xx_io_device *devices[10] = {0}, *out = NULL;
    const xx_fpak_stream *tables[10] = {0};
    const char *source, *cursor;
    char *directory;
    size_t prefix = 0;
    int number;
    unsigned i;
    int64_t bytes;
    bool ok = false;
    if (self->base_address || selected->is_truncated || !(source = xx_io_source_path(self->device)) || xx_str_len(source) > 4094U ||
        (number = fpak_volume_number(source)) < 0 || selected->is_lead != (number == 0))
        return NULL;
    for (cursor = source; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') prefix = (size_t)(cursor - source) + 1U;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return NULL;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    devices[number] = self->device;
    tables[number] = selected;
    bytes = selected->volume_size;
    /* Work backwards first so selecting any numbered continuation finds the
     * same lead. Every predecessor must be unique at its exact disk index. */
    for (i = (unsigned)number; i > 0; --i) {
        devices[i - 1U] = fpak_chain_find(directory, tables[i], i - 1U, true, bytes, pd);
        if (!devices[i - 1U] || !(tables[i - 1U] = fpak_chain_table(devices[i - 1U], pd))) goto done;
        if (tables[i - 1U]->volume_size > XX_FPAK_MAX_SIZE - bytes) goto done;
        bytes += tables[i - 1U]->volume_size;
    }
    out = xx_io_temp_open();
    if (!out || !fpak_volume_copy(out, devices[0], 0, pd)) goto done;
    for (i = 1; i < 10U; ++i) {
        xx_fpak_stream *probe;
        Abstractformat logical;
        size_t j;
        bool complete;
        if (!devices[i]) {
            devices[i] = fpak_chain_find(directory, tables[i - 1U], i, false, bytes, pd);
            if (!devices[i] || !(tables[i] = fpak_chain_table(devices[i], pd))) goto done;
            if (tables[i]->volume_size > XX_FPAK_MAX_SIZE - bytes) goto done;
            bytes += tables[i]->volume_size;
        }
        if (!fpak_volume_copy(out, devices[i], 4, pd)) goto done;
        logical = *self;
        logical.device = out;
        logical.base_address = 0;
        probe = xx_fpak_parse_single(&logical, pd);
        complete = probe && !probe->is_truncated && i >= (unsigned)number;
        if (probe)
            for (j = 0; j < probe->member_count; ++j)
                if (!probe->members[j].complete) complete = false;
        xx_fpak_stream_free(probe);
        if (complete) {
            ok = true;
            break;
        }
        if ((pd && xx_pd_is_stopped(pd)) || xx_io_seek64(out, 0, SEEK_END)) goto done;
    }
done:
    for (i = 0; i < 10U; ++i)
        if (i != (unsigned)number) {
            xx_fpak_stream_free((xx_fpak_stream *)tables[i]);
            if (devices[i]) xx_io_close(devices[i]);
        }
    xx_str_free(directory);
    if (!ok && out) {
        xx_io_close(out);
        out = NULL;
    }
    return out;
}

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://archive.gfjc.fiu.edu/workshops/resources/literature/ABIF_File_Format.pdf */
#include "xxfclib/formats/sequencing_abif/xx_sequencing_abif.h"
#include "../common/xx_scientific_tables_traces.h"

#define SEQUENCING_ABIF_NEED(x) \
    do {                        \
        if (!(x)) goto done;    \
    } while (0)

static bool sequencing_abif_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    uint64_t *keys = NULL;
    uint64_t directory, capacity, count, i, extent, budget = 1048576;
    bool ok = false;
    SEQUENCING_ABIF_NEED(b->n >= 34 && !xx_rt_memcmp(b->p, "ABIF", 4) && xx_data_get_u16(b->p + 4, 2, 0, true) >= 100 && xx_data_get_u16(b->p + 4, 2, 0, true) < 200 &&
                         !xx_rt_memcmp(b->p + 6, "tdir", 4) && xx_data_get_u16(b->p + 14, 2, 0, true) == 1023 && xx_data_get_u16(b->p + 16, 2, 0, true) == 28);
    count = xx_data_get_u32(b->p + 18, 4, 0, true);
    capacity = xx_data_get_u32(b->p + 22, 4, 0, true);
    directory = xx_data_get_u32(b->p + 26, 4, 0, true);
    SEQUENCING_ABIF_NEED(count && count <= 4094 && capacity >= count * 28 && capacity % 28 == 0 && capacity <= 4096 * 28 && directory >= 34 &&
                         blob_span(b, directory, capacity));
    extent = directory + capacity;
    keys = (uint64_t *)xx_mem_alloc(8192 * sizeof(*keys));
    SEQUENCING_ABIF_NEED(keys);
    xx_mem_zero(keys, 8192 * sizeof(*keys));
    SEQUENCING_ABIF_NEED(blob_add(f, s, b, "file-header", 0, 34) && blob_add(f, s, b, "tag-directory", directory, capacity));
    for (i = 0; i < count; ++i) {
        const uint8_t *p = b->p + (size_t)(directory + i * 28);
        uint64_t name = xx_data_get_u32(p, 4, 0, true), number = xx_data_get_u32(p + 4, 4, 0, true), key = (name << 32) | number, at,
                 size = xx_data_get_u32(p + 16, 4, 0, true), elements = xx_data_get_u32(p + 12, 4, 0, true), slot = (key ^ (key >> 33)) & 8191;
        unsigned type = xx_data_get_u16(p + 8, 2, 0, true), width = xx_data_get_u16(p + 10, 2, 0, true), j;
        char label[64];
        for (j = 0; j < 4; ++j) SEQUENCING_ABIF_NEED(p[j] >= 33 && p[j] <= 126);
        SEQUENCING_ABIF_NEED(width && elements <= 67108864 && (uint64_t)width * elements == size &&
                             ((type >= 1 && type <= 20 && type != 6 && type != 9 && type != 14 && type != 15 && type != 16 && type != 17 && type != 20) || type >= 1024));
        if (type == 1 || type == 2 || type == 13 || type == 18 || type == 19) SEQUENCING_ABIF_NEED(width == 1);
        if (type == 3 || type == 4) SEQUENCING_ABIF_NEED(width == 2);
        if (type == 5 || type == 7 || type == 10 || type == 11) SEQUENCING_ABIF_NEED(width == 4);
        if (type == 8) SEQUENCING_ABIF_NEED(width == 8);
        if (type == 12) SEQUENCING_ABIF_NEED(width == 10);
        while (keys[slot]) {
            SEQUENCING_ABIF_NEED(scientific_table_charge(b, &budget, 1) && keys[slot] != key);
            slot = (slot + 1) & 8191;
        }
        keys[slot] = key;
        at = size <= 4 ? directory + i * 28 + 20 : xx_data_get_u32(p + 20, 4, 0, true);
        SEQUENCING_ABIF_NEED(blob_span(b, at, size) && (size <= 4 || (at >= 34 && (at + size <= directory || at >= directory + capacity))));
        if (at + size > extent) extent = at + size;
        if (type == 7 || type == 8) SEQUENCING_ABIF_NEED(blob_floats(b, at, size, width, true));
        if (type == 18) SEQUENCING_ABIF_NEED(size && b->p[(size_t)at] == size - 1);
        if (type == 19) SEQUENCING_ABIF_NEED(size && b->p[(size_t)(at + size - 1)] == 0);
        xx_rt_snprintf(label, sizeof(label), "tag-%c%c%c%c-%u", p[0], p[1], p[2], p[3], (unsigned)number);
        SEQUENCING_ABIF_NEED(blob_add(f, s, b, label, at, size));
    }
    SEQUENCING_ABIF_NEED(extent == b->n);
    ok = true;
done:
    if (keys) xx_mem_free(keys);
    return ok;
}

#undef SEQUENCING_ABIF_NEED

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_SEQUENCING_ABIF && sequencing_abif_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_sequencing_abif_init(xx_sequencing_abif *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SEQUENCING_ABIF, "sequencing_abif");
    }
}
xx_sequencing_abif *xx_sequencing_abif_create(xx_io_device *d, int64_t b)
{
    xx_sequencing_abif *r = (xx_sequencing_abif *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sequencing_abif_init(r, d, b);
    return r;
}
void xx_sequencing_abif_destroy(xx_sequencing_abif *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sequencing_abif_free(xx_sequencing_abif *r)
{
    if (r) {
        xx_sequencing_abif_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sequencing_abif_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sequencing_abif_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

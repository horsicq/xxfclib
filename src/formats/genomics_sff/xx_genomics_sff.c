/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.SeqIO.SffIO.html */
#include "xxfclib/formats/genomics_sff/xx_genomics_sff.h"
#include "../common/xx_scientific_tables_traces.h"

#define GENOMICS_SFF_NEED(x) \
    do {                     \
        if (!(x)) goto done; \
    } while (0)

typedef struct genomics_sff_index_entry {
    scientific_text_token name;
    uint64_t offset;
    bool indexed;
} genomics_sff_index_entry;

static bool genomics_sff_sff_index(Abstractformat *f, pm_stream *s, memory_blob *b, uint64_t at, uint64_t size, genomics_sff_index_entry *reads, unsigned count)
{
    uint64_t table, end = at + size, manifest = 0, budget = 8388608;
    unsigned i;
    if (size < 12 || !blob_span(b, at, size) || xx_rt_memcmp(b->p + (size_t)at + 4, "1.00", 4)) return false;
    if (!xx_rt_memcmp(b->p + (size_t)at, ".mft", 4)) {
        if (size < 16) return false;
        manifest = xx_data_get_u32(b->p + (size_t)at + 8, 4, 0, true);
        if (manifest > 65536 || manifest + xx_data_get_u32(b->p + (size_t)at + 12, 4, 0, true) != size - 16 ||
            !blob_ascii(b->p + (size_t)at + 16, (size_t)manifest, false))
            return false;
        table = at + 16 + manifest;
        if (!blob_add(f, s, b, "index-header", at, 16) || !blob_add(f, s, b, "index-manifest", at + 16, manifest)) return false;
    } else if (!xx_rt_memcmp(b->p + (size_t)at, ".srt", 4)) {
        if (!blob_zero(b, at + 8, 4) || !blob_add(f, s, b, "index-header", at, 12)) return false;
        table = at + 12;
    } else return false;
    at = table;
    for (i = 0; i < count; ++i) {
        uint64_t start = at, offset = 0;
        unsigned j, k;
        bool found = false;
        while (at < end && b->p[(size_t)at]) {
            if (at - start >= 255 || b->p[(size_t)at] < 33 || b->p[(size_t)at] > 126) return false;
            ++at;
        }
        if (at == start || end - at < 6 || b->p[(size_t)at]) {
            return false;
        }
        ++at;
        for (k = 0; k < 4; ++k) {
            if (b->p[(size_t)at] == 255) return false;
            offset = offset * 255 + b->p[(size_t)at++];
        }
        if (b->p[(size_t)at++] != 255) return false;
        for (j = 0; j < count; ++j) {
            if (!scientific_table_charge(b, &budget, 1)) return false;
            if (reads[j].name.n == at - start - 6) {
                if (!scientific_table_charge(b, &budget, reads[j].name.n)) return false;
                if (!xx_rt_memcmp(b->p + (size_t)start, b->p + (size_t)reads[j].name.at, (size_t)reads[j].name.n)) {
                    if (reads[j].indexed || reads[j].offset != offset) return false;
                    reads[j].indexed = true;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
    }
    return at == end && blob_add(f, s, b, "index-table", table, end - table);
}

static bool genomics_sff_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    uint64_t index, index_size, at, header, flows, key, count, i, budget = 8388608;
    genomics_sff_index_entry *reads = NULL;
    bool skipped = false, ok = false;
    GENOMICS_SFF_NEED(b->n >= 31 && !xx_rt_memcmp(b->p, ".sff", 4) && xx_data_get_u32(b->p + 4, 4, 0, true) == 1);
    index = xx_data_get_u64(b->p + 8, 8, 0, true);
    index_size = xx_data_get_u32(b->p + 16, 4, 0, true);
    count = xx_data_get_u32(b->p + 20, 4, 0, true);
    header = xx_data_get_u16(b->p + 24, 2, 0, true);
    key = xx_data_get_u16(b->p + 26, 2, 0, true);
    flows = xx_data_get_u16(b->p + 28, 2, 0, true);
    GENOMICS_SFF_NEED(count && count <= 512 && flows && flows <= 4096 && key <= 256 && b->p[30] == 1 && header == ((31 + key + flows + 7) & ~UINT64_C(7)) &&
                      blob_span(b, 0, header) && (!index == !index_size) &&
                      (!index || (index >= header && index % 8 == 0 && blob_span(b, index, index_size) && blob_span(b, index, (index_size + 7) & ~UINT64_C(7)))));
    for (i = 0; i < flows + key; ++i) GENOMICS_SFF_NEED(xx_rt_strchr("ACGTN", b->p[(size_t)(31 + i)]) && b->p[(size_t)(31 + i)]);
    GENOMICS_SFF_NEED(blob_zero(b, 31 + key + flows, header - (31 + key + flows)) && blob_add(f, s, b, "flowgram-header", 0, header));
    reads = (genomics_sff_index_entry *)xx_mem_alloc((size_t)count * sizeof(*reads));
    GENOMICS_SFF_NEED(reads);
    xx_mem_zero(reads, (size_t)count * sizeof(*reads));
    at = header;
    for (i = 0; i < count; ++i) {
        uint64_t begin, name, n, data, padded, j, sum = 0;
        if (index && at == index) {
            GENOMICS_SFF_NEED(!skipped && blob_zero(b, index + index_size, ((index_size + 7) & ~UINT64_C(7)) - index_size));
            at += ((index_size + 7) & ~UINT64_C(7));
            skipped = true;
        }
        begin = at;
        GENOMICS_SFF_NEED(blob_span(b, at, 16));
        header = xx_data_get_u16(b->p + (size_t)at, 2, 0, true);
        name = xx_data_get_u16(b->p + (size_t)at + 2, 2, 0, true);
        n = xx_data_get_u32(b->p + (size_t)at + 4, 4, 0, true);
        GENOMICS_SFF_NEED(name && name <= 255 && n && n <= 1048576 && header == ((16 + name + 7) & ~UINT64_C(7)) && blob_span(b, at, header) &&
                          blob_zero(b, at + 16 + name, header - (16 + name)));
        reads[i].name.at = at + 16;
        reads[i].name.n = name;
        reads[i].offset = at;
        GENOMICS_SFF_NEED(scientific_text_ident(b, reads[i].name));
        for (j = 0; j < i; ++j) {
            GENOMICS_SFF_NEED(scientific_table_charge(b, &budget, 1));
            if (reads[i].name.n == reads[j].name.n) {
                GENOMICS_SFF_NEED(scientific_table_charge(b, &budget, name));
                GENOMICS_SFF_NEED(!structure_same(b, reads[i].name, reads[j].name));
            }
        }
        for (j = 8; j < 16; j += 2) GENOMICS_SFF_NEED(xx_data_get_u16(b->p + (size_t)at + j, 2, 0, true) <= n);
        at += header;
        data = flows * 2 + n * 3;
        padded = (data + 7) & ~UINT64_C(7);
        GENOMICS_SFF_NEED(blob_span(b, at, padded) && blob_zero(b, at + data, padded - data) && (!index || skipped || at + padded <= index));
        for (j = 0; j < n; ++j) {
            sum += b->p[(size_t)(at + flows * 2 + j)];
            GENOMICS_SFF_NEED(sum <= flows && b->p[(size_t)(at + flows * 2 + n + j)] && xx_rt_strchr("ACGTNacgtn", b->p[(size_t)(at + flows * 2 + n + j)]));
        }
        GENOMICS_SFF_NEED(blob_add(f, s, b, "read-header", begin, header) && blob_add(f, s, b, "flow-values", at, flows * 2) &&
                          blob_add(f, s, b, "flow-indices", at + flows * 2, n) && blob_add(f, s, b, "basecalls", at + flows * 2 + n, n) &&
                          blob_add(f, s, b, "qualities", at + flows * 2 + n * 2, n));
        at += padded;
    }
    if (index && at == index) {
        GENOMICS_SFF_NEED(!skipped && blob_zero(b, index + index_size, ((index_size + 7) & ~UINT64_C(7)) - index_size));
        at += ((index_size + 7) & ~UINT64_C(7));
        skipped = true;
    }
    GENOMICS_SFF_NEED(at == b->n && (!index || (skipped && genomics_sff_sff_index(f, s, b, index, index_size, reads, (unsigned)count))));
    ok = true;
done:
    if (reads) xx_mem_free(reads);
    return ok;
}

#undef GENOMICS_SFF_NEED

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GENOMICS_SFF && genomics_sff_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_sff_init(xx_genomics_sff *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_SFF, "genomics_sff");
    }
}
xx_genomics_sff *xx_genomics_sff_create(xx_io_device *d, int64_t b)
{
    xx_genomics_sff *r = (xx_genomics_sff *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_sff_init(r, d, b);
    return r;
}
void xx_genomics_sff_destroy(xx_genomics_sff *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_sff_free(xx_genomics_sff *r)
{
    if (r) {
        xx_genomics_sff_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_sff_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_sff_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://staden.sourceforge.net/manual/formats_unix_3.html */
#include "xxfclib/formats/sequencing_scf/xx_sequencing_scf.h"
#include "../common/xx_scientific_tables_traces.h"

static bool sequencing_scf_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    uint64_t samples, bases, offset[4], length[4], extent = 128, i, j;
    unsigned width;
    static const char *const channels[] = {"A", "C", "G", "T"};
    if (b->n < 128 || xx_rt_memcmp(b->p, ".scf", 4) || xx_rt_memcmp(b->p + 36, "3.00", 4))
        return false;
    samples = xx_data_get_u32(b->p + 4, 4, 0, true);
    bases = xx_data_get_u32(b->p + 12, 4, 0, true);
    width = xx_data_get_u32(b->p + 40, 4, 0, true);
    if (!samples || samples > 1048576 || !bases || bases > 1048576 || (width != 1 && width != 2) ||
        xx_data_get_u32(b->p + 44, 4, 0, true) > 4 || xx_data_get_u32(b->p + 16, 4, 0, true) > bases ||
        xx_data_get_u32(b->p + 20, 4, 0, true) > bases)
        return false;
    offset[0] = xx_data_get_u32(b->p + 8, 4, 0, true);
    length[0] = samples * 4 * width;
    offset[1] = xx_data_get_u32(b->p + 24, 4, 0, true);
    length[1] = bases * 12;
    offset[2] = xx_data_get_u32(b->p + 32, 4, 0, true);
    length[2] = xx_data_get_u32(b->p + 28, 4, 0, true);
    offset[3] = xx_data_get_u32(b->p + 52, 4, 0, true);
    length[3] = xx_data_get_u32(b->p + 48, 4, 0, true);
    for (i = 0; i < 4; ++i) {
        if (!length[i]) {
            if (offset[i] > b->n)
                return false;
            continue;
        }
        if (offset[i] < 128 || !blob_span(b, offset[i], length[i]))
            return false;
        if (offset[i] + length[i] > extent)
            extent = offset[i] + length[i];
        for (j = 0; j < i; ++j)
            if (length[j] && offset[i] < offset[j] + length[j] && offset[j] < offset[i] + length[i])
                return false;
    }
    if (extent != b->n || !blob_add(f, s, b, "trace-header", 0, 128))
        return false;
    for (i = 0; i < bases; ++i) {
        uint64_t peak = xx_data_get_u32(b->p + (size_t)(offset[1] + i * 4), 4, 0, true);
        uint8_t ch = b->p[(size_t)(offset[1] + bases * 8 + i)];
        if (!(i & 1023) && binary_stop(b->pd))
            return false;
        if (peak >= samples || (i && peak < xx_data_get_u32(b->p + (size_t)(offset[1] + (i - 1) * 4), 4, 0, true)) ||
            !ch || !xx_rt_strchr("ACGTNRYKMSWBDHVX-acgtnrykmswbdhvx", ch))
            return false;
    }
    for (i = 0; i < 4; ++i) {
        char label[32];
        xx_rt_snprintf(label, sizeof(label), "encoded-trace-%s", channels[i]);
        if (!blob_add(f, s, b, label, offset[0] + i * samples * width, samples * width))
            return false;
    }
    if (!blob_add(f, s, b, "base-positions", offset[1], bases * 4))
        return false;
    for (i = 0; i < 4; ++i) {
        char label[32];
        xx_rt_snprintf(label, sizeof(label), "base-probability-%s", channels[i]);
        if (!blob_add(f, s, b, label, offset[1] + bases * 4 + i * bases, bases))
            return false;
    }
    if (!blob_add(f, s, b, "basecalls", offset[1] + bases * 8, bases) ||
        !blob_add(f, s, b, "base-reserved", offset[1] + bases * 9, bases * 3))
        return false;
    /* Maintainer files also use a length-delimited comment block without NUL. */
    if (length[2]) {
        uint64_t text_size = length[2] - (b->p[(size_t)(offset[2] + length[2] - 1)] == 0);
        for (i = 0; i < text_size;) {
            size_t n = (size_t)(text_size - i > 65536 ? 65536 : text_size - i);
            if (binary_stop(b->pd) || !blob_ascii(b->p + (size_t)(offset[2] + i), n, false))
                return false;
            i += n;
        }
        if (!blob_add(f, s, b, "comments", offset[2], length[2]))
            return false;
    }
    return !length[3] || blob_add(f, s, b, "private-data", offset[3], length[3]);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_SEQUENCING_SCF && sequencing_scf_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_sequencing_scf_init(xx_sequencing_scf *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SEQUENCING_SCF, "sequencing_scf");
    }
}
xx_sequencing_scf *xx_sequencing_scf_create(xx_io_device *d, int64_t b) {
    xx_sequencing_scf *r = (xx_sequencing_scf *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_sequencing_scf_init(r, d, b);
    return r;
}
void xx_sequencing_scf_destroy(xx_sequencing_scf *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sequencing_scf_free(xx_sequencing_scf *r) {
    if (r) {
        xx_sequencing_scf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sequencing_scf_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_sequencing_scf_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ncbi.nlm.nih.gov/assembly/agp/AGP_Specification/ */
#include "xxfclib/formats/genomics_agp/xx_genomics_agp.h"
#include "../common/xx_scientific_tables_traces.h"

#define GENOMICS_AGP_NEED(x)                                                                                           \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            goto done;                                                                                                 \
    } while (0)

static bool genomics_agp_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token line, t[9], *names = NULL, current = {0};
    unsigned nt, count = 0, objects = 0;
    uint64_t end = 0, part = 0, budget = 8388608;
    bool ok = false;
    c.b = b;
    GENOMICS_AGP_NEED(structure_ascii(b));
    names = (scientific_text_token *)xx_mem_alloc(1024 * sizeof(*names));
    GENOMICS_AGP_NEED(names);
    while (c.at < b->n) {
        uint64_t start = c.at, a, z, p, x, y;
        bool same;
        GENOMICS_AGP_NEED(scientific_text_line(&c, &line));
        if (!line.n || b->p[(size_t)line.at] == '#') {
            GENOMICS_AGP_NEED(blob_add(f, s, b, "metadata", start, c.at - start));
            continue;
        }
        GENOMICS_AGP_NEED(scientific_text_split(b, line, t, 9, &nt, true) && nt == 9 && ++count <= 4094 &&
                          scientific_text_ident(b, t[0]) && scientific_text_uint(b, t[1], &a) &&
                          scientific_text_uint(b, t[2], &z) && a && a <= z && z <= UINT32_MAX &&
                          scientific_text_uint(b, t[3], &p) && t[4].n == 1 &&
                          scientific_text_chars(b, t[4], "ADFGOPWNU", true));
        same = current.n && structure_same(b, current, t[0]);
        if (!same) {
            GENOMICS_AGP_NEED(objects < 1024 && scientific_table_unique(b, t[0], names, objects, &budget) && a == 1 &&
                              p == 1);
            names[objects++] = t[0];
            current = t[0];
        } else
            GENOMICS_AGP_NEED(a == end + 1 && p == part + 1);
        if (scientific_text_eq(b, t[4], "N") || scientific_text_eq(b, t[4], "U")) {
            GENOMICS_AGP_NEED(scientific_text_uint(b, t[5], &x) && x == z - a + 1 &&
                              (!scientific_text_eq(b, t[4], "U") || x == 100) &&
                              (scientific_text_eq(b, t[6], "scaffold") || scientific_text_eq(b, t[6], "contig") ||
                               scientific_text_eq(b, t[6], "centromere") || scientific_text_eq(b, t[6], "short_arm") ||
                               scientific_text_eq(b, t[6], "heterochromatin") ||
                               scientific_text_eq(b, t[6], "telomere") || scientific_text_eq(b, t[6], "repeat") ||
                               scientific_text_eq(b, t[6], "contamination")) &&
                              (scientific_text_eq(b, t[7], "yes") || scientific_text_eq(b, t[7], "no")));
            if (scientific_text_eq(b, t[7], "no"))
                GENOMICS_AGP_NEED(scientific_text_eq(b, t[8], "na"));
            else {
                scientific_text_token evidence[16];
                unsigned n, i;
                GENOMICS_AGP_NEED(scientific_text_sep(b, t[8], ';', evidence, 16, &n));
                for (i = 0; i < n; ++i)
                    GENOMICS_AGP_NEED(scientific_text_eq(b, evidence[i], "paired-ends") ||
                                      scientific_text_eq(b, evidence[i], "align_genus") ||
                                      scientific_text_eq(b, evidence[i], "align_xgenus") ||
                                      scientific_text_eq(b, evidence[i], "align_trnscpt") ||
                                      scientific_text_eq(b, evidence[i], "within_clone") ||
                                      scientific_text_eq(b, evidence[i], "clone_contig") ||
                                      scientific_text_eq(b, evidence[i], "map") ||
                                      scientific_text_eq(b, evidence[i], "strobe") ||
                                      scientific_text_eq(b, evidence[i], "pcr") ||
                                      scientific_text_eq(b, evidence[i], "proximity_ligation") ||
                                      scientific_text_eq(b, evidence[i], "unspecified"));
            }
        } else
            GENOMICS_AGP_NEED(scientific_text_ident(b, t[5]) && scientific_text_uint(b, t[6], &x) &&
                              scientific_text_uint(b, t[7], &y) && x && x <= y && y <= UINT32_MAX && y - x == z - a &&
                              (scientific_text_eq(b, t[8], "+") || scientific_text_eq(b, t[8], "-") ||
                               scientific_text_eq(b, t[8], "?") || scientific_text_eq(b, t[8], "0") ||
                               scientific_text_eq(b, t[8], "na")));
        end = z;
        part = p;
        GENOMICS_AGP_NEED(blob_add(f, s, b, "assembly-component", start, c.at - start));
    }
    ok = count != 0;
done:
    if (names)
        xx_mem_free(names);
    return ok;
}

#undef GENOMICS_AGP_NEED

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GENOMICS_AGP && genomics_agp_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_agp_init(xx_genomics_agp *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_AGP, "genomics_agp");
    }
}
xx_genomics_agp *xx_genomics_agp_create(xx_io_device *d, int64_t b) {
    xx_genomics_agp *r = (xx_genomics_agp *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_genomics_agp_init(r, d, b);
    return r;
}
void xx_genomics_agp_destroy(xx_genomics_agp *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_agp_free(xx_genomics_agp *r) {
    if (r) {
        xx_genomics_agp_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_agp_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_genomics_agp_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }

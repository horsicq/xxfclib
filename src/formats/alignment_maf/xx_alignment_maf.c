/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/FAQ/FAQformat.html#format5 */
#include "xxfclib/formats/alignment_maf/xx_alignment_maf.h"
#include "../common/xx_sequence_alignment.h"
static bool maf_header(memory_blob *b, scientific_text_token line)
{
    scientific_text_token t[16];
    unsigned n, i;
    if (!scientific_text_split(b, line, t, 16, &n, false) || n < 2 || !scientific_text_eq(b, t[0], "##maf") || !scientific_text_eq(b, t[1], "version=1")) return false;
    for (i = 2; i < n; ++i)
        if (!scientific_text_prefix(b, t[i], "scoring=") || t[i].n <= 8 || !scientific_text_ident(b, scientific_text_slice(t[i], 8, t[i].n - 8))) return false;
    return n <= 3;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[8];
    sequence_sequence *q = NULL;
    unsigned blocks = 0, n = 0, nt;
    uint64_t total = 0, budget = 10000000;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd) && structure_ascii(&b));
    c.b = &b;
    q = sequence_sequences(&total);
    BLOB_NEED(q);
    BLOB_NEED(scientific_text_line(&c, &line) && maf_header(&b, line) && blob_add(f, s, &b, "alignment-text", 0, b.n));
    while (c.at < b.n) {
        scientific_text_token trim;
        BLOB_NEED(scientific_text_line(&c, &line));
        trim = scientific_text_trim(&b, line);
        if (!trim.n || b.p[(size_t)trim.at] == '#') continue;
        BLOB_NEED(scientific_text_split(&b, trim, t, 8, &nt, false) && (nt == 1 || nt == 2) && scientific_text_eq(&b, t[0], "a"));
        if (nt == 2) BLOB_NEED(scientific_text_prefix(&b, t[1], "score=") && scientific_text_float(&b, scientific_text_slice(t[1], 6, t[1].n - 6)));
        n = 0;
        while (c.at < b.n) {
            unsigned index;
            uint64_t start, size, source;
            BLOB_NEED(scientific_text_line(&c, &line));
            trim = scientific_text_trim(&b, line);
            if (!trim.n) break;
            if (b.p[(size_t)trim.at] == '#') continue;
            BLOB_NEED(scientific_text_split(&b, trim, t, 8, &nt, false) && nt == 7 && scientific_text_eq(&b, t[0], "s") && n < 1024 &&
                      sequence_find(&b, t[1], q, n, &index, &budget) && index == n && scientific_text_uint(&b, t[2], &start) && scientific_text_uint(&b, t[3], &size) &&
                      size && (scientific_text_eq(&b, t[4], "+") || scientific_text_eq(&b, t[4], "-")) && scientific_text_uint(&b, t[5], &source) && source &&
                      start <= source && size <= source - start);
            q[n].id = t[1];
            BLOB_NEED(sequence_append(&b, &q[n], t[6], true, true, false) && q[n].real == size);
            ++n;
        }
        BLOB_NEED(blocks < 1024 && sequence_alignment_export(f, s, q, n, &total));
        ++blocks;
    }
    BLOB_NEED(blocks);
    s->size = (int64_t)b.n;
    ok = true;
done:
    sequence_free_sequences(q, 1024);
    xx_mem_free(b.p);
    return ok;
}

void xx_alignment_maf_init(xx_alignment_maf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ALIGNMENT_MAF, "alignment_maf");
    }
}
xx_alignment_maf *xx_alignment_maf_create(xx_io_device *d, int64_t b)
{
    xx_alignment_maf *r = (xx_alignment_maf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_alignment_maf_init(r, d, b);
    return r;
}
void xx_alignment_maf_destroy(xx_alignment_maf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_alignment_maf_free(xx_alignment_maf *r)
{
    if (r) {
        xx_alignment_maf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_alignment_maf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_alignment_maf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

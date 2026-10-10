/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.PhylipIO.html */
#include "xxfclib/formats/alignment_phylip/xx_alignment_phylip.h"
#include "../common/xx_sequence_alignment.h"
static bool phylip_fragment(memory_blob *b, sequence_sequence *q, scientific_text_token line, uint64_t *width)
{
    scientific_text_token t[128];
    unsigned n, i;
    uint64_t count = 0;
    if (!scientific_text_split(b, line, t, 128, &n, false) || !n) return false;
    for (i = 0; i < n; ++i) {
        if (!t[i].n || t[i].n > 10 || !sequence_append(b, q, t[i], true, false, false) ||
            !scientific_text_chars(b, t[i], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-", true))
            return false;
        count += t[i].n;
    }
    if (*width && *width != count) {
        return false;
    }
    *width = count;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[3];
    sequence_sequence *q = NULL;
    unsigned n = 0, nt, i;
    uint64_t rows, columns, total = 0, budget = 10000000, width = 0;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd) && structure_ascii(&b));
    c.b = &b;
    q = sequence_sequences(&total);
    BLOB_NEED(q);
    BLOB_NEED(scientific_text_line(&c, &line) && scientific_text_split(&b, line, t, 3, &nt, false) && nt == 2 && scientific_text_uint(&b, t[0], &rows) && rows >= 2 &&
              rows <= 1024 && scientific_text_uint(&b, t[1], &columns) && columns && columns <= SEQUENCE_MAX_SEQUENCE && columns <= SEQUENCE_MAX_DECODED / rows &&
              blob_add(f, s, &b, "alignment-text", 0, b.n));
    n = (unsigned)rows;
    for (i = 0; i < n; ++i) {
        unsigned index;
        scientific_text_token id;
        BLOB_NEED(sequence_nonblank(&c, &line) && line.n > 10);
        id = scientific_text_trim(&b, scientific_text_slice(line, 0, 10));
        BLOB_NEED(scientific_text_ident(&b, id) && sequence_find(&b, id, q, i, &index, &budget) && index == i);
        q[i].id = id;
        BLOB_NEED(phylip_fragment(&b, &q[i], scientific_text_slice(line, 10, line.n - 10), &width) && q[i].n <= columns);
    }
    while (q[0].n < columns) {
        width = 0;
        for (i = 0; i < n; ++i) BLOB_NEED(sequence_nonblank(&c, &line) && phylip_fragment(&b, &q[i], line, &width) && q[i].n <= columns);
    }
    BLOB_NEED(sequence_terminated(&c) && q[0].n == columns && sequence_alignment_export(f, s, q, n, &total));
    s->size = (int64_t)b.n;
    ok = true;
done:
    sequence_free_sequences(q, 1024);
    xx_mem_free(b.p);
    return ok;
}

void xx_alignment_phylip_init(xx_alignment_phylip *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ALIGNMENT_PHYLIP, "alignment_phylip");
    }
}
xx_alignment_phylip *xx_alignment_phylip_create(xx_io_device *d, int64_t b)
{
    xx_alignment_phylip *r = (xx_alignment_phylip *)xx_mem_alloc(sizeof(*r));
    if (r) xx_alignment_phylip_init(r, d, b);
    return r;
}
void xx_alignment_phylip_destroy(xx_alignment_phylip *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_alignment_phylip_free(xx_alignment_phylip *r)
{
    if (r) {
        xx_alignment_phylip_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_alignment_phylip_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_alignment_phylip_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

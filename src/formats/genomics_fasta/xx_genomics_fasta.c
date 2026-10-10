/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ncbi.nlm.nih.gov/genbank/fastaformat/ */
#include "xxfclib/formats/genomics_fasta/xx_genomics_fasta.h"
#include "../common/xx_scientific_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, *names = NULL;
    unsigned count = 0;
    uint64_t record = 0, bases = 0, budget = 8388608;
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd) && b.p[0] == '>');
    names = (scientific_text_token *)xx_mem_alloc(4096 * sizeof(*names));
    BLOB_NEED(names);
    c.b = &b;
    while (c.at < b.n) {
        uint64_t start = c.at;
        BLOB_NEED(scientific_text_line(&c, &line) && line.n);
        if (b.p[(size_t)line.at] == '>') {
            scientific_text_token id;
            uint64_t i = 1;
            BLOB_NEED(line.n <= 65536);
            while (i < line.n && b.p[(size_t)(line.at + i)] != ' ' && b.p[(size_t)(line.at + i)] != '\t') ++i;
            id = scientific_text_slice(line, 1, i - 1);
            BLOB_NEED(scientific_text_ident(&b, id) && scientific_text_unique(&b, id, names, count, &budget) && count < 4096);
            if (count) BLOB_NEED(bases && blob_add(f, s, &b, "sequence-record", record, start - record));
            names[count++] = id;
            record = start;
            bases = 0;
        } else {
            BLOB_NEED(count && scientific_text_chars(&b, line, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz*-.", true));
            bases += line.n;
            BLOB_NEED(bases <= 16777216);
        }
    }
    BLOB_NEED(count && bases && blob_add(f, s, &b, "sequence-record", record, b.n - record));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(names);
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_fasta_init(xx_genomics_fasta *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_FASTA, "genomics_fasta");
    }
}
xx_genomics_fasta *xx_genomics_fasta_create(xx_io_device *d, int64_t b)
{
    xx_genomics_fasta *r = (xx_genomics_fasta *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_fasta_init(r, d, b);
    return r;
}
void xx_genomics_fasta_destroy(xx_genomics_fasta *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_fasta_free(xx_genomics_fasta *r)
{
    if (r) {
        xx_genomics_fasta_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_fasta_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_fasta_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

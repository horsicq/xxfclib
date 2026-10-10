/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/biopython/biopython/blob/master/Bio/Phylo/NewickIO.py */
#include "xxfclib/formats/phylo_newick/xx_phylo_newick.h"
#include "../common/xx_phylogenetic_text.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    phylo_text t;
    bool ok = false;
    unsigned count = 0;
    xx_mem_zero(&t, sizeof(t));
    BLOB_NEED(blob_load(f, &b, pd) && b.n >= 6 && bounded_utf8(b.p, (size_t)b.n, pd));
    t.b = &b;
    while (t.at < b.n) {
        uint64_t at;
        BLOB_NEED(phylo_skip(&t));
        if (t.at == b.n) break;
        at = t.at;
        BLOB_NEED(++count <= 1024 && phylo_tree(&t) && blob_add(f, s, &b, "tree", at, t.at - at));
    }
    BLOB_NEED(count && t.at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_phylo_newick_init(xx_phylo_newick *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PHYLO_NEWICK, "phylo_newick");
    }
}
xx_phylo_newick *xx_phylo_newick_create(xx_io_device *d, int64_t b)
{
    xx_phylo_newick *r = (xx_phylo_newick *)xx_mem_alloc(sizeof(*r));
    if (r) xx_phylo_newick_init(r, d, b);
    return r;
}
void xx_phylo_newick_destroy(xx_phylo_newick *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_phylo_newick_free(xx_phylo_newick *r)
{
    if (r) {
        xx_phylo_newick_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_phylo_newick_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_phylo_newick_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}

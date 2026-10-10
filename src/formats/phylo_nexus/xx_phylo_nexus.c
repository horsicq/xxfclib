/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/biopython/biopython/blob/master/Bio/Phylo/NexusIO.py */
#include "xxfclib/formats/phylo_nexus/xx_phylo_nexus.h"
#include "../common/xx_phylogenetic_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};phylo_text t;bool ok=false;unsigned count=0;uint64_t tax_at[512],tax_len[512];uint8_t tax_used[512];xx_mem_zero(&t,sizeof(t));t.label_left=8388608;BLOB_NEED(blob_load(f,&b,pd) && b.n>=20 && bounded_utf8(b.p,(size_t)b.n,pd));t.b=&b;
    BLOB_NEED(phylo_char(&t,'#') && phylo_word(&t,"nexus") && phylo_word(&t,"begin"));
    if(phylo_word(&t,"taxa")) {uint64_t at,n;unsigned i,j;BLOB_NEED(phylo_char(&t,';') && phylo_word(&t,"dimensions") && phylo_word(&t,"ntax") && phylo_char(&t,'=') && phylo_skip(&t));at=t.at;while(t.at<b.n && b.p[(size_t)t.at]>='0' && b.p[(size_t)t.at]<='9') ++t.at;BLOB_NEED(phylo_decimal(b.p+(size_t)at,(size_t)(t.at-at),&n) && n>=2 && n<=512 && phylo_char(&t,';') && phylo_word(&t,"taxlabels"));
        for(i=0;i<(unsigned)n;++i) {BLOB_NEED(phylo_skip(&t));tax_at[i]=t.at;BLOB_NEED(phylo_label(&t,true));tax_len[i]=t.at-tax_at[i];for(j=0;j<i;++j) {bool equal=phylo_label_eq(&b,tax_at[j],tax_len[j],tax_at[i],tax_len[i],&t.label_left);BLOB_NEED(t.label_left && !equal);}}
        BLOB_NEED(phylo_char(&t,';') && (phylo_word(&t,"end") || phylo_word(&t,"endblock")) && phylo_char(&t,';') && phylo_word(&t,"begin"));t.taxa=(unsigned)n;t.tax_at=tax_at;t.tax_len=tax_len;t.tax_used=tax_used;
    }
    BLOB_NEED(phylo_word(&t,"trees") && phylo_char(&t,';') && blob_add(f,s,&b,"header",0,t.at));
    while(true) {uint64_t at;BLOB_NEED(phylo_skip(&t));at=t.at;if(phylo_word(&t,"end") || phylo_word(&t,"endblock")) {BLOB_NEED(phylo_char(&t,';') && blob_add(f,s,&b,"end",at,t.at-at) && phylo_skip(&t) && t.at==b.n);break;}
        BLOB_NEED(++count<=1024 && (phylo_word(&t,"tree") || phylo_word(&t,"utree")) && phylo_skip(&t));if(t.at<b.n && b.p[(size_t)t.at]=='*') ++t.at;
        BLOB_NEED(phylo_label(&t,true) && phylo_char(&t,'=') && phylo_tree(&t) && blob_add(f,s,&b,"tree",at,t.at-at));
    }BLOB_NEED(count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_phylo_nexus_init(xx_phylo_nexus *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PHYLO_NEXUS,"phylo_nexus"); } }
xx_phylo_nexus *xx_phylo_nexus_create(xx_io_device *d,int64_t b) { xx_phylo_nexus *r=(xx_phylo_nexus *)xx_mem_alloc(sizeof(*r)); if(r) xx_phylo_nexus_init(r,d,b); return r; }
void xx_phylo_nexus_destroy(xx_phylo_nexus *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_phylo_nexus_free(xx_phylo_nexus *r) { if(r) { xx_phylo_nexus_destroy(r); xx_mem_free(r); } }
bool xx_phylo_nexus_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_phylo_nexus_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

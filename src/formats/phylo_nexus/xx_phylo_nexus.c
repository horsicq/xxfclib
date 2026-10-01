/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/biopython/biopython/blob/master/Bio/Phylo/NexusIO.py */
#include "xxfclib/formats/phylo_nexus/xx_phylo_nexus.h"
#include "../xx_tenth_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};tn_text t;bool ok=false;unsigned count=0;uint64_t tax_at[512],tax_len[512];uint8_t tax_used[512];xx_mem_zero(&t,sizeof(t));t.label_left=8388608;NH_NEED(nh_load(f,&b,pd) && b.n>=20 && fourth_utf8(b.p,(size_t)b.n,pd));t.b=&b;
    NH_NEED(tn_char(&t,'#') && tn_word(&t,"nexus") && tn_word(&t,"begin"));
    if(tn_word(&t,"taxa")) {uint64_t at,n;unsigned i,j;NH_NEED(tn_char(&t,';') && tn_word(&t,"dimensions") && tn_word(&t,"ntax") && tn_char(&t,'=') && tn_skip(&t));at=t.at;while(t.at<b.n && b.p[(size_t)t.at]>='0' && b.p[(size_t)t.at]<='9') ++t.at;NH_NEED(tn_decimal(b.p+(size_t)at,(size_t)(t.at-at),&n) && n>=2 && n<=512 && tn_char(&t,';') && tn_word(&t,"taxlabels"));
        for(i=0;i<(unsigned)n;++i) {NH_NEED(tn_skip(&t));tax_at[i]=t.at;NH_NEED(tn_label(&t,true));tax_len[i]=t.at-tax_at[i];for(j=0;j<i;++j) {bool equal=tn_label_eq(&b,tax_at[j],tax_len[j],tax_at[i],tax_len[i],&t.label_left);NH_NEED(t.label_left && !equal);}}
        NH_NEED(tn_char(&t,';') && (tn_word(&t,"end") || tn_word(&t,"endblock")) && tn_char(&t,';') && tn_word(&t,"begin"));t.taxa=(unsigned)n;t.tax_at=tax_at;t.tax_len=tax_len;t.tax_used=tax_used;
    }
    NH_NEED(tn_word(&t,"trees") && tn_char(&t,';') && nh_add(f,s,&b,"header",0,t.at));
    while(true) {uint64_t at;NH_NEED(tn_skip(&t));at=t.at;if(tn_word(&t,"end") || tn_word(&t,"endblock")) {NH_NEED(tn_char(&t,';') && nh_add(f,s,&b,"end",at,t.at-at) && tn_skip(&t) && t.at==b.n);break;}
        NH_NEED(++count<=1024 && (tn_word(&t,"tree") || tn_word(&t,"utree")) && tn_skip(&t));if(t.at<b.n && b.p[(size_t)t.at]=='*') ++t.at;
        NH_NEED(tn_label(&t,true) && tn_char(&t,'=') && tn_tree(&t) && nh_add(f,s,&b,"tree",at,t.at-at));
    }NH_NEED(count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_phylo_nexus_init(xx_phylo_nexus *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PHYLO_NEXUS,"phylo_nexus"); } }
xx_phylo_nexus *xx_phylo_nexus_create(xx_io_device *d,int64_t b) { xx_phylo_nexus *r=(xx_phylo_nexus *)xx_mem_alloc(sizeof(*r)); if(r) xx_phylo_nexus_init(r,d,b); return r; }
void xx_phylo_nexus_destroy(xx_phylo_nexus *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_phylo_nexus_free(xx_phylo_nexus *r) { if(r) { xx_phylo_nexus_destroy(r); xx_mem_free(r); } }
bool xx_phylo_nexus_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_phylo_nexus_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

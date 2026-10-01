/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.PhylipIO.html */
#include "xxfclib/formats/alignment_phylip/xx_alignment_phylip.h"
#include "../xx_fourteenth_root.h"
static bool phylip_fragment(nh_blob *b,f14_sequence *q,el_token line,uint64_t *width) {
    el_token t[128];unsigned n,i;uint64_t count=0;if(!el_split(b,line,t,128,&n,false) || !n) return false;
    for(i=0;i<n;++i) {if(!t[i].n || t[i].n>10 || !f14_append(b,q,t[i],true,false,false) || !el_chars(b,t[i],"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-",true)) return false;count+=t[i].n;}
    if(*width && *width!=count) return false;*width=count;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[3];f14_sequence *q=NULL;unsigned n=0,nt,i;uint64_t rows,columns,total=0,budget=10000000,width=0;bool ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);NH_NEED(q);
    NH_NEED(el_line(&c,&line) && el_split(&b,line,t,3,&nt,false) && nt==2 && el_uint(&b,t[0],&rows) && rows>=2 && rows<=1024 && el_uint(&b,t[1],&columns) && columns && columns<=F14_MAX_SEQUENCE && columns<=F14_MAX_DECODED/rows && nh_add(f,s,&b,"alignment-text",0,b.n));n=(unsigned)rows;
    for(i=0;i<n;++i) {unsigned index;el_token id;NH_NEED(f14_nonblank(&c,&line) && line.n>10);id=el_trim(&b,el_slice(line,0,10));NH_NEED(el_ident(&b,id) && f14_find(&b,id,q,i,&index,&budget) && index==i);q[i].id=id;NH_NEED(phylip_fragment(&b,&q[i],el_slice(line,10,line.n-10),&width) && q[i].n<=columns);}
    while(q[0].n<columns) {width=0;for(i=0;i<n;++i) NH_NEED(f14_nonblank(&c,&line) && phylip_fragment(&b,&q[i],line,&width) && q[i].n<=columns);}
    NH_NEED(f14_terminated(&c) && q[0].n==columns && f14_alignment_export(f,s,q,n,&total));s->size=(int64_t)b.n;ok=true;
done:f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_phylip_init(xx_alignment_phylip *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_PHYLIP,"alignment_phylip"); } }
xx_alignment_phylip *xx_alignment_phylip_create(xx_io_device *d,int64_t b) { xx_alignment_phylip *r=(xx_alignment_phylip *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_phylip_init(r,d,b); return r; }
void xx_alignment_phylip_destroy(xx_alignment_phylip *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_phylip_free(xx_alignment_phylip *r) { if(r) { xx_alignment_phylip_destroy(r); xx_mem_free(r); } }
bool xx_alignment_phylip_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_phylip_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

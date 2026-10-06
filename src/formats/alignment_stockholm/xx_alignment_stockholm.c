/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.StockholmIO.html */
#include "xxfclib/formats/alignment_stockholm/xx_alignment_stockholm.h"
#include "../xx_fourteenth_root.h"
typedef struct stock_anno {el_token id,key;uint64_t width;bool gc;} stock_anno;
static bool stock_annotation(nh_blob *b,stock_anno *a,unsigned *n,el_token id,el_token key,el_token data,bool gc,uint64_t *budget) {
    unsigned i;uint64_t j;if(!el_ident(b,key) || (!gc && !el_ident(b,id)) || !data.n) return false;
    for(j=0;j<data.n;++j) {unsigned ch=b->p[(size_t)(data.at+j)];if(ch<=32 || ch>126) return false;}
    for(i=0;i<*n;++i) {uint64_t cost=a[i].key.n+id.n;if(cost>*budget) return false;*budget-=cost;if(a[i].gc==gc && th_same(b,a[i].key,key) && (gc || th_same(b,a[i].id,id))) break;}
    if(i==*n) {if(*n==1024) return false;a[i].id=id;a[i].key=key;a[i].width=0;a[i].gc=gc;++*n;}
    if(data.n>F14_MAX_SEQUENCE-a[i].width) { return false; } a[i].width+=data.n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[8],*references=NULL;f14_sequence *q=NULL;stock_anno *annotations=NULL;unsigned n=0,nt,an=0,rn=0,i;uint64_t total=0,budget=10000000;bool ended=false,ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);references=(el_token *)xx_mem_alloc(4096*sizeof(*references));annotations=(stock_anno *)xx_mem_alloc(1024*sizeof(*annotations));NH_NEED(q && references && annotations);
    NH_NEED(el_line(&c,&line) && el_eq(&b,line,"# STOCKHOLM 1.0") && nh_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {unsigned index;el_token trim;NH_NEED(el_line(&c,&line));trim=el_trim(&b,line);if(!trim.n) continue;if(el_eq(&b,trim,"//")) {ended=true;break;}
        if(el_prefix(&b,trim,"#=GF ")) {NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt>=3 && el_ident(&b,t[1]));continue;}
        if(el_prefix(&b,trim,"#=GS ")) {NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt>=4 && el_ident(&b,t[1]) && el_ident(&b,t[2]) && rn<4096);references[rn++]=t[1];continue;}
        if(el_prefix(&b,trim,"#=GR ")) {NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt==4 && stock_annotation(&b,annotations,&an,t[1],t[2],t[3],false,&budget));continue;}
        if(el_prefix(&b,trim,"#=GC ")) {el_token none={0,0};NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt==3 && stock_annotation(&b,annotations,&an,none,t[1],t[2],true,&budget));continue;}
        if(b.p[(size_t)trim.at]=='#') {NH_NEED(!el_prefix(&b,trim,"#="));continue;}
        NH_NEED(el_split(&b,trim,t,8,&nt,false) && nt==2 && f14_find(&b,t[0],q,n,&index,&budget));if(index==n) {NH_NEED(n<1024);q[n++].id=t[0];}
        {size_t j,begin=q[index].n;NH_NEED(f14_append(&b,&q[index],t[1],true,false,false));for(j=begin;j<q[index].n;++j) {if(!(j&4095U)) NH_NEED(!fd_stop(pd));if(q[index].p[j]=='.') q[index].p[j]='-';}}
    }
    NH_NEED(ended && n>=2 && f14_terminated(&c));for(i=0;i<rn;++i) {unsigned index;NH_NEED(f14_find(&b,references[i],q,n,&index,&budget) && index<n);}
    for(i=0;i<an;++i) {unsigned index=0;if(!annotations[i].gc) NH_NEED(f14_find(&b,annotations[i].id,q,n,&index,&budget) && index<n);NH_NEED(annotations[i].width==q[index].n);}
    NH_NEED(f14_alignment_export(f,s,q,n,&total));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(references);xx_mem_free(annotations);f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_stockholm_init(xx_alignment_stockholm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_STOCKHOLM,"alignment_stockholm"); } }
xx_alignment_stockholm *xx_alignment_stockholm_create(xx_io_device *d,int64_t b) { xx_alignment_stockholm *r=(xx_alignment_stockholm *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_stockholm_init(r,d,b); return r; }
void xx_alignment_stockholm_destroy(xx_alignment_stockholm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_stockholm_free(xx_alignment_stockholm *r) { if(r) { xx_alignment_stockholm_destroy(r); xx_mem_free(r); } }
bool xx_alignment_stockholm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_stockholm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

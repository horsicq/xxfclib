/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.StockholmIO.html */
#include "xxfclib/formats/alignment_stockholm/xx_alignment_stockholm.h"
#include "../common/xx_sequence_alignment.h"
typedef struct stock_anno {scientific_text_token id,key;uint64_t width;bool gc;} stock_anno;
static bool stock_annotation(memory_blob *b,stock_anno *a,unsigned *n,scientific_text_token id,scientific_text_token key,scientific_text_token data,bool gc,uint64_t *budget) {
    unsigned i;uint64_t j;if(!scientific_text_ident(b,key) || (!gc && !scientific_text_ident(b,id)) || !data.n) return false;
    for(j=0;j<data.n;++j) {unsigned ch=b->p[(size_t)(data.at+j)];if(ch<=32 || ch>126) return false;}
    for(i=0;i<*n;++i) {uint64_t cost=a[i].key.n+id.n;if(cost>*budget) return false;*budget-=cost;if(a[i].gc==gc && structure_same(b,a[i].key,key) && (gc || structure_same(b,a[i].id,id))) break;}
    if(i==*n) {if(*n==1024) return false;a[i].id=id;a[i].key=key;a[i].width=0;a[i].gc=gc;++*n;}
    if(data.n>SEQUENCE_MAX_SEQUENCE-a[i].width) { return false; } a[i].width+=data.n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[8],*references=NULL;sequence_sequence *q=NULL;stock_anno *annotations=NULL;unsigned n=0,nt,an=0,rn=0,i;uint64_t total=0,budget=10000000;bool ended=false,ok=false;
    BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;q=sequence_sequences(&total);references=(scientific_text_token *)xx_mem_alloc(4096*sizeof(*references));annotations=(stock_anno *)xx_mem_alloc(1024*sizeof(*annotations));BLOB_NEED(q && references && annotations);
    BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_eq(&b,line,"# STOCKHOLM 1.0") && blob_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {unsigned index;scientific_text_token trim;BLOB_NEED(scientific_text_line(&c,&line));trim=scientific_text_trim(&b,line);if(!trim.n) continue;if(scientific_text_eq(&b,trim,"//")) {ended=true;break;}
        if(scientific_text_prefix(&b,trim,"#=GF ")) {BLOB_NEED(scientific_text_split(&b,trim,t,8,&nt,false) && nt>=3 && scientific_text_ident(&b,t[1]));continue;}
        if(scientific_text_prefix(&b,trim,"#=GS ")) {BLOB_NEED(scientific_text_split(&b,trim,t,8,&nt,false) && nt>=4 && scientific_text_ident(&b,t[1]) && scientific_text_ident(&b,t[2]) && rn<4096);references[rn++]=t[1];continue;}
        if(scientific_text_prefix(&b,trim,"#=GR ")) {BLOB_NEED(scientific_text_split(&b,trim,t,8,&nt,false) && nt==4 && stock_annotation(&b,annotations,&an,t[1],t[2],t[3],false,&budget));continue;}
        if(scientific_text_prefix(&b,trim,"#=GC ")) {scientific_text_token none={0,0};BLOB_NEED(scientific_text_split(&b,trim,t,8,&nt,false) && nt==3 && stock_annotation(&b,annotations,&an,none,t[1],t[2],true,&budget));continue;}
        if(b.p[(size_t)trim.at]=='#') {BLOB_NEED(!scientific_text_prefix(&b,trim,"#="));continue;}
        BLOB_NEED(scientific_text_split(&b,trim,t,8,&nt,false) && nt==2 && sequence_find(&b,t[0],q,n,&index,&budget));if(index==n) {BLOB_NEED(n<1024);q[n++].id=t[0];}
        {size_t j,begin=q[index].n;BLOB_NEED(sequence_append(&b,&q[index],t[1],true,false,false));for(j=begin;j<q[index].n;++j) {if(!(j&4095U)) BLOB_NEED(!binary_stop(pd));if(q[index].p[j]=='.') q[index].p[j]='-';}}
    }
    BLOB_NEED(ended && n>=2 && sequence_terminated(&c));for(i=0;i<rn;++i) {unsigned index;BLOB_NEED(sequence_find(&b,references[i],q,n,&index,&budget) && index<n);}
    for(i=0;i<an;++i) {unsigned index=0;if(!annotations[i].gc) BLOB_NEED(sequence_find(&b,annotations[i].id,q,n,&index,&budget) && index<n);BLOB_NEED(annotations[i].width==q[index].n);}
    BLOB_NEED(sequence_alignment_export(f,s,q,n,&total));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(references);xx_mem_free(annotations);sequence_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_stockholm_init(xx_alignment_stockholm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_STOCKHOLM,"alignment_stockholm"); } }
xx_alignment_stockholm *xx_alignment_stockholm_create(xx_io_device *d,int64_t b) { xx_alignment_stockholm *r=(xx_alignment_stockholm *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_stockholm_init(r,d,b); return r; }
void xx_alignment_stockholm_destroy(xx_alignment_stockholm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_stockholm_free(xx_alignment_stockholm *r) { if(r) { xx_alignment_stockholm_destroy(r); xx_mem_free(r); } }
bool xx_alignment_stockholm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_stockholm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

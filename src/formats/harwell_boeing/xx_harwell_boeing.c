/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.scipy.org/doc/scipy/reference/generated/scipy.io.hb_write.html */
#include "xxfclib/formats/harwell_boeing/xx_harwell_boeing.h"
#include "../common/xx_scientific_structure.h"
typedef struct hb_desc {unsigned repeat,width,precision;} hb_desc;
static bool hb_format(memory_blob *b,scientific_text_token t,bool real,hb_desc *d) {
    uint64_t a=0,n=0;uint8_t code;t=scientific_text_trim(b,t);if(t.n<5 || b->p[(size_t)t.at]!='(' || b->p[(size_t)(t.at+t.n-1)]!=')') return false;++a;
    while(a<t.n && b->p[(size_t)(t.at+a)]>='0' && b->p[(size_t)(t.at+a)]<='9') {n=n*10+b->p[(size_t)(t.at+a++)]-'0';if(n>1000) return false;}if(!n) return false;d->repeat=(unsigned)n;code=b->p[(size_t)(t.at+a++)];if(real ? code!='E' && code!='e':code!='I' && code!='i') return false;
    n=0;while(a<t.n && b->p[(size_t)(t.at+a)]>='0' && b->p[(size_t)(t.at+a)]<='9') {n=n*10+b->p[(size_t)(t.at+a++)]-'0';if(n>96) return false;}if(!n) return false;d->width=(unsigned)n;d->precision=0;
    if(real) {if(a>=t.n || b->p[(size_t)(t.at+a++)]!='.') return false;n=0;while(a<t.n && b->p[(size_t)(t.at+a)]>='0' && b->p[(size_t)(t.at+a)]<='9') {n=n*10+b->p[(size_t)(t.at+a++)]-'0';if(n>64) return false;}if(!n || n+6>d->width) return false;d->precision=(unsigned)n;}
    return a+1==t.n && d->repeat*d->width<=1048576;
}
static bool hb_array(scientific_text_lines *c,uint64_t count,hb_desc d,uint64_t *values,bool real) {
    scientific_text_token line,t;uint64_t seen=0;bool first=true;while(seen<count) {unsigned i,n=(unsigned)((count-seen)<d.repeat ? count-seen:d.repeat);if(!scientific_text_line(c,&line)) return false;
        /* SciPy's retained primary writer declares E(width) but emits width-1. */
        if(first && real && d.width>1 && line.n==(uint64_t)n*(d.width-1)) { --d.width; } first=false;
        if(line.n!=(uint64_t)n*d.width) return false;
        for(i=0;i<n;++i) {t=scientific_text_trim(c->b,scientific_text_slice(line,(uint64_t)i*d.width,d.width));if(real ? !scientific_text_float(c->b,t):!scientific_text_uint(c->b,t,&values[(size_t)seen])) return false;++seen;}}
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[6];hb_desc pointer={0},index={0},value={0};uint64_t total,pn,in,vn,rhs,rows,cols,nnz,element,i,j,head,pointers,indices,*ptr=NULL,*row=NULL;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(scientific_text_line(&c,&line) && line.n==80 && scientific_text_trim(&b,scientific_text_slice(line,0,72)).n);
    BLOB_NEED(scientific_text_line(&c,&line) && (line.n==56 || line.n==70));for(i=0;i<4;++i) t[i]=scientific_text_slice(line,i*14,14);
    BLOB_NEED(scientific_text_uint(&b,t[0],&total) && scientific_text_uint(&b,t[1],&pn) && scientific_text_uint(&b,t[2],&in) && scientific_text_uint(&b,t[3],&vn));rhs=0;if(line.n==70) BLOB_NEED(scientific_text_uint(&b,scientific_text_slice(line,56,14),&rhs));BLOB_NEED(!rhs && total==pn+in+vn && total<=1000000);
    BLOB_NEED(scientific_text_line(&c,&line) && line.n==70 && scientific_text_eq(&b,scientific_text_slice(line,0,3),"RUA") && !scientific_text_trim(&b,scientific_text_slice(line,3,11)).n);
    BLOB_NEED(scientific_text_uint(&b,scientific_text_slice(line,14,14),&rows) && rows && rows<=1000000 && scientific_text_uint(&b,scientific_text_slice(line,28,14),&cols) && cols && cols<=1000000 && scientific_text_uint(&b,scientific_text_slice(line,42,14),&nnz) && nnz && nnz<=1000000 && scientific_text_uint(&b,scientific_text_slice(line,56,14),&element) && !element);
    BLOB_NEED(scientific_text_line(&c,&line) && (line.n==52 || line.n==72) && hb_format(&b,scientific_text_slice(line,0,16),false,&pointer) && hb_format(&b,scientific_text_slice(line,16,16),false,&index) && hb_format(&b,scientific_text_slice(line,32,20),true,&value));if(line.n==72) BLOB_NEED(!scientific_text_trim(&b,scientific_text_slice(line,52,20)).n);
    BLOB_NEED(pn==(cols+1+pointer.repeat-1)/pointer.repeat && in==(nnz+index.repeat-1)/index.repeat && vn==(nnz+value.repeat-1)/value.repeat);head=c.at;
    ptr=(uint64_t *)xx_mem_alloc((size_t)(cols+1)*sizeof(uint64_t));row=(uint64_t *)xx_mem_alloc((size_t)nnz*sizeof(uint64_t));BLOB_NEED(ptr && row);
    BLOB_NEED(hb_array(&c,cols+1,pointer,ptr,false) && ptr[0]==1 && ptr[(size_t)cols]==nnz+1);pointers=c.at;
    for(i=0;i<cols;++i) BLOB_NEED(ptr[(size_t)i]>=1 && ptr[(size_t)i]<=ptr[(size_t)i+1] && ptr[(size_t)i+1]<=nnz+1);
    BLOB_NEED(hb_array(&c,nnz,index,row,false));indices=c.at;
    for(i=0;i<cols;++i) for(j=ptr[(size_t)i]-1;j<ptr[(size_t)i+1]-1;++j) BLOB_NEED(row[(size_t)j] && row[(size_t)j]<=rows && (j==ptr[(size_t)i]-1 || row[(size_t)j]>row[(size_t)j-1]));
    BLOB_NEED(hb_array(&c,nnz,value,NULL,true) && molecular_trailing(&c));
    BLOB_NEED(blob_add(f,s,&b,"matrix-header",0,head) && blob_add(f,s,&b,"column-pointers",head,pointers-head) && blob_add(f,s,&b,"row-indices",pointers,indices-pointers) && blob_add(f,s,&b,"matrix-values",indices,b.n-indices));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(ptr);xx_mem_free(row);xx_mem_free(b.p);return ok;
}

void xx_harwell_boeing_init(xx_harwell_boeing *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HARWELL_BOEING,"harwell_boeing"); } }
xx_harwell_boeing *xx_harwell_boeing_create(xx_io_device *d,int64_t b) { xx_harwell_boeing *r=(xx_harwell_boeing *)xx_mem_alloc(sizeof(*r)); if(r) xx_harwell_boeing_init(r,d,b); return r; }
void xx_harwell_boeing_destroy(xx_harwell_boeing *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_harwell_boeing_free(xx_harwell_boeing *r) { if(r) { xx_harwell_boeing_destroy(r); xx_mem_free(r); } }
bool xx_harwell_boeing_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_harwell_boeing_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.ClustalIO.html */
#include "xxfclib/formats/alignment_clustal/xx_alignment_clustal.h"
#include "../xx_fourteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[4];f14_sequence *q=NULL;unsigned n=0,row=0,nt;uint64_t width=0,total=0,budget=10000000,column=0;bool first=true,consensus=false,ok=false;
    NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;q=f14_sequences(&total);NH_NEED(q);
    NH_NEED(el_line(&c,&line) && el_prefix(&b,line,"CLUSTAL") && line.n>=12 && nh_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {el_token trim;unsigned index;NH_NEED(el_line(&c,&line));trim=el_trim(&b,line);
        if(!trim.n) {if(row) {NH_NEED(first ? n==row:row==n);first=false;row=0;width=0;column=0;consensus=false;}continue;}
        if(b.p[(size_t)line.at]==' ' || b.p[(size_t)line.at]=='\t') {uint64_t j;NH_NEED(row && !consensus && (first ? row==n:row==n) && line.n>=column && line.n-column==width);for(j=0;j<column;++j) NH_NEED(b.p[(size_t)(line.at+j)]==' ');NH_NEED(el_chars(&b,el_slice(line,column,width)," *:.",false));consensus=true;continue;}
        NH_NEED(!consensus && el_split(&b,line,t,4,&nt,false) && (nt==2 || nt==3) && t[1].n && f14_find(&b,t[0],q,n,&index,&budget));
        if(first) {NH_NEED(index==n && n<1024 && row==n);q[n++].id=t[0];}else NH_NEED(row<n && index==row);
        if(!row) {width=t[1].n;column=t[1].at-line.at;}NH_NEED(t[1].n==width && t[1].at-line.at==column && f14_append(&b,&q[index],t[1],true,false,false));
        if(nt==3) {uint64_t position;NH_NEED(el_uint(&b,t[2],&position) && position==q[index].real);}++row;
    }
    NH_NEED(row ? row==n:n>0);NH_NEED(f14_alignment_export(f,s,q,n,&total));s->size=(int64_t)b.n;ok=true;
done:f14_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_clustal_init(xx_alignment_clustal *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_CLUSTAL,"alignment_clustal"); } }
xx_alignment_clustal *xx_alignment_clustal_create(xx_io_device *d,int64_t b) { xx_alignment_clustal *r=(xx_alignment_clustal *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_clustal_init(r,d,b); return r; }
void xx_alignment_clustal_destroy(xx_alignment_clustal *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_clustal_free(xx_alignment_clustal *r) { if(r) { xx_alignment_clustal_destroy(r); xx_mem_free(r); } }
bool xx_alignment_clustal_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_clustal_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

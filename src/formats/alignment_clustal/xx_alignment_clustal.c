/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.AlignIO.ClustalIO.html */
#include "xxfclib/formats/alignment_clustal/xx_alignment_clustal.h"
#include "../common/xx_sequence_alignment.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[4];sequence_sequence *q=NULL;unsigned n=0,row=0,nt;uint64_t width=0,total=0,budget=10000000,column=0;bool first=true,consensus=false,ok=false;
    BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;q=sequence_sequences(&total);BLOB_NEED(q);
    BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_prefix(&b,line,"CLUSTAL") && line.n>=12 && blob_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {scientific_text_token trim;unsigned index;BLOB_NEED(scientific_text_line(&c,&line));trim=scientific_text_trim(&b,line);
        if(!trim.n) {if(row) {BLOB_NEED(first ? n==row:row==n);first=false;row=0;width=0;column=0;consensus=false;}continue;}
        if(b.p[(size_t)line.at]==' ' || b.p[(size_t)line.at]=='\t') {uint64_t j;BLOB_NEED(row && !consensus && (first ? row==n:row==n) && line.n>=column && line.n-column==width);for(j=0;j<column;++j) BLOB_NEED(b.p[(size_t)(line.at+j)]==' ');BLOB_NEED(scientific_text_chars(&b,scientific_text_slice(line,column,width)," *:.",false));consensus=true;continue;}
        BLOB_NEED(!consensus && scientific_text_split(&b,line,t,4,&nt,false) && (nt==2 || nt==3) && t[1].n && sequence_find(&b,t[0],q,n,&index,&budget));
        if(first) {BLOB_NEED(index==n && n<1024 && row==n);q[n++].id=t[0];}else BLOB_NEED(row<n && index==row);
        if(!row) {width=t[1].n;column=t[1].at-line.at;}BLOB_NEED(t[1].n==width && t[1].at-line.at==column && sequence_append(&b,&q[index],t[1],true,false,false));
        if(nt==3) {uint64_t position;BLOB_NEED(scientific_text_uint(&b,t[2],&position) && position==q[index].real);}++row;
    }
    BLOB_NEED(row ? row==n:n>0);BLOB_NEED(sequence_alignment_export(f,s,q,n,&total));s->size=(int64_t)b.n;ok=true;
done:sequence_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_clustal_init(xx_alignment_clustal *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_CLUSTAL,"alignment_clustal"); } }
xx_alignment_clustal *xx_alignment_clustal_create(xx_io_device *d,int64_t b) { xx_alignment_clustal *r=(xx_alignment_clustal *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_clustal_init(r,d,b); return r; }
void xx_alignment_clustal_destroy(xx_alignment_clustal *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_clustal_free(xx_alignment_clustal *r) { if(r) { xx_alignment_clustal_destroy(r); xx_mem_free(r); } }
bool xx_alignment_clustal_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_clustal_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

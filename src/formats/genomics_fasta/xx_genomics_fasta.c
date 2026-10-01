/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ncbi.nlm.nih.gov/genbank/fastaformat/ */
#include "xxfclib/formats/genomics_fasta/xx_genomics_fasta.h"
#include "../xx_eleventh_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,*names=NULL;unsigned count=0;uint64_t record=0,bases=0,budget=8388608;bool ok=false;
    NH_NEED(nh_load(f,&b,pd) && b.p[0]=='>');names=(el_token *)xx_mem_alloc(4096*sizeof(*names));NH_NEED(names);c.b=&b;
    while(c.at<b.n) {
        uint64_t start=c.at;NH_NEED(el_line(&c,&line) && line.n);
        if(b.p[(size_t)line.at]=='>') {
            el_token id;uint64_t i=1;NH_NEED(line.n<=65536);
            while(i<line.n && b.p[(size_t)(line.at+i)]!=' ' && b.p[(size_t)(line.at+i)]!='\t') ++i;
            id=el_slice(line,1,i-1);NH_NEED(el_ident(&b,id) && el_unique(&b,id,names,count,&budget) && count<4096);
            if(count) NH_NEED(bases && nh_add(f,s,&b,"sequence-record",record,start-record));
            names[count++]=id;record=start;bases=0;
        } else {NH_NEED(count && el_chars(&b,line,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz*-.",true));bases+=line.n;NH_NEED(bases<=16777216);}
    }
    NH_NEED(count && bases && nh_add(f,s,&b,"sequence-record",record,b.n-record));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(b.p);return ok;
}

void xx_genomics_fasta_init(xx_genomics_fasta *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_FASTA,"genomics_fasta"); } }
xx_genomics_fasta *xx_genomics_fasta_create(xx_io_device *d,int64_t b) { xx_genomics_fasta *r=(xx_genomics_fasta *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_fasta_init(r,d,b); return r; }
void xx_genomics_fasta_destroy(xx_genomics_fasta *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_fasta_free(xx_genomics_fasta *r) { if(r) { xx_genomics_fasta_destroy(r); xx_mem_free(r); } }
bool xx_genomics_fasta_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_fasta_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ncbi.nlm.nih.gov/sra/docs/submitformats/ */
#include "xxfclib/formats/genomics_fastq/xx_genomics_fastq.h"
#include "../common/xx_scientific_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token h,seq,plus,q;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd) && b.p[0]=='@');c.b=&b;
    while(c.at<b.n) {
        uint64_t start=c.at,i,n;scientific_text_token id;
        BLOB_NEED(scientific_text_line(&c,&h) && h.n>1 && h.n<=65536 && b.p[(size_t)h.at]=='@');
        n=1;while(n<h.n && b.p[(size_t)(h.at+n)]!=' ' && b.p[(size_t)(h.at+n)]!='\t') ++n;id=scientific_text_slice(h,1,n-1);BLOB_NEED(scientific_text_ident(&b,id));
        BLOB_NEED(scientific_text_line(&c,&seq) && scientific_text_chars(&b,seq,"ACGTRYSWKMBDHVNacgtryswkmbdhvn",true));
        BLOB_NEED(scientific_text_line(&c,&plus) && plus.n && b.p[(size_t)plus.at]=='+');
        if(plus.n>1) BLOB_NEED((plus.n==h.n && !xx_rt_memcmp(b.p+(size_t)plus.at+1,b.p+(size_t)h.at+1,(size_t)plus.n-1)) || (plus.n==id.n+1 && !xx_rt_memcmp(b.p+(size_t)plus.at+1,b.p+(size_t)id.at,(size_t)id.n)));
        BLOB_NEED(scientific_text_line(&c,&q) && q.n==seq.n);for(i=0;i<q.n;++i) BLOB_NEED(b.p[(size_t)(q.at+i)]>=33 && b.p[(size_t)(q.at+i)]<=126);
        BLOB_NEED(blob_add(f,s,&b,"read-record",start,c.at-start));
    }
    BLOB_NEED(s->count);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_genomics_fastq_init(xx_genomics_fastq *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_FASTQ,"genomics_fastq"); } }
xx_genomics_fastq *xx_genomics_fastq_create(xx_io_device *d,int64_t b) { xx_genomics_fastq *r=(xx_genomics_fastq *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_fastq_init(r,d,b); return r; }
void xx_genomics_fastq_destroy(xx_genomics_fastq *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_fastq_free(xx_genomics_fastq *r) { if(r) { xx_genomics_fastq_destroy(r); xx_mem_free(r); } }
bool xx_genomics_fastq_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_fastq_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

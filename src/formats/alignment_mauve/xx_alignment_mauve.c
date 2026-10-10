/* SPDX-License-Identifier: MIT
 * Independently implemented from https://darlinglab.org/mauve/user-guide/files.html */
#include "xxfclib/formats/alignment_mauve/xx_alignment_mauve.h"
#include "../common/xx_sequence_alignment.h"
static bool mauve_coords(memory_blob *b,scientific_text_token t,scientific_text_token *id,uint64_t *start,uint64_t *end) {
    scientific_text_token fields[2],positions[2];unsigned n;uint64_t number;
    return scientific_text_sep(b,t,':',fields,2,&n) && n==2 && scientific_text_uint(b,fields[0],&number) && number && number<=1024 && (*id=fields[0],true) && scientific_text_sep(b,fields[1],'-',positions,2,&n) && n==2 && scientific_text_uint(b,positions[0],start) && scientific_text_uint(b,positions[1],end) && ((!*start && !*end) || (*start && *end>=*start && *end-*start<SEQUENCE_MAX_SEQUENCE));
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[16];sequence_sequence *q=NULL;uint64_t *expected=NULL,total=0,budget=10000000;unsigned n=0,blocks=0,nt;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;q=sequence_sequences(&total);expected=(uint64_t *)xx_mem_alloc(1024*sizeof(*expected));BLOB_NEED(q && expected);
    BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_eq(&b,line,"#FormatVersion Mauve1") && blob_add(f,s,&b,"alignment-text",0,b.n));
    while(c.at<b.n) {scientific_text_token trim;BLOB_NEED(scientific_text_line(&c,&line));trim=scientific_text_trim(&b,line);if(!trim.n) continue;
        if(b.p[(size_t)trim.at]=='#') continue;
        if(scientific_text_eq(&b,trim,"=")) {unsigned i;BLOB_NEED(n>=2 && blocks<1024);for(i=0;i<n;++i) BLOB_NEED(!expected[i] || expected[i]==q[i].real);BLOB_NEED(sequence_alignment_export(f,s,q,n,&total));n=0;++blocks;continue;}
        if(b.p[(size_t)trim.at]=='>') {scientific_text_token id;unsigned index;uint64_t start,end;
            BLOB_NEED(scientific_text_split(&b,trim,t,16,&nt,false) && nt>=4 && nt<=8 && scientific_text_eq(&b,t[0],">") && mauve_coords(&b,t[1],&id,&start,&end) && (scientific_text_eq(&b,t[2],"+") || scientific_text_eq(&b,t[2],"-")) && scientific_text_ident(&b,t[3]) && n<1024 && sequence_find(&b,id,q,n,&index,&budget) && index==n);
            if(nt>4) { BLOB_NEED(scientific_text_eq(&b,t[4],"#") && nt>=6); } if(n) BLOB_NEED(q[n-1].n && (!expected[n-1] || expected[n-1]==q[n-1].real));q[n].id=id;expected[n]=start ? end-start+1:0;++n;continue;
        }
        BLOB_NEED(n && sequence_append(&b,&q[n-1],trim,true,true,false));
    }
    BLOB_NEED(blocks && !n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(expected);sequence_free_sequences(q,1024);xx_mem_free(b.p);return ok;
}

void xx_alignment_mauve_init(xx_alignment_mauve *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ALIGNMENT_MAUVE,"alignment_mauve"); } }
xx_alignment_mauve *xx_alignment_mauve_create(xx_io_device *d,int64_t b) { xx_alignment_mauve *r=(xx_alignment_mauve *)xx_mem_alloc(sizeof(*r)); if(r) xx_alignment_mauve_init(r,d,b); return r; }
void xx_alignment_mauve_destroy(xx_alignment_mauve *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_alignment_mauve_free(xx_alignment_mauve *r) { if(r) { xx_alignment_mauve_destroy(r); xx_mem_free(r); } }
bool xx_alignment_mauve_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_alignment_mauve_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

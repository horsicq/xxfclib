/* SPDX-License-Identifier: MIT
 * Independently implemented from https://web.expasy.org/docs/userman.html */
#include "xxfclib/formats/genomics_swissprot/xx_genomics_swissprot.h"
#include "../common/xx_sequence_alignment.h"
static bool swiss_header(memory_blob *b,scientific_text_token line,bool *accession) {
    static const char *const keys[]={"AC","DT","DE","GN","OS","OG","OC","OX","OH","RN","RP","RC","RX","RG","RA","RT","RL","CC","DR","PE","KW","FT"};unsigned i;scientific_text_token key;
    if(line.n<5 || b->p[(size_t)(line.at+2)]!=' ' || b->p[(size_t)(line.at+3)]!=' ' || b->p[(size_t)(line.at+4)]!=' ') { return false; } key=scientific_text_slice(line,0,2);
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) { if(scientific_text_eq(b,key,keys[i])) {if(i==0) {if(line.n<=5) return false;*accession=true;}return true;} } return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[32],*names=NULL;sequence_sequence q={0};unsigned nt,count=0;uint64_t total=0,budget=10000000;bool ok=false;
    q.decoded=&total;BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;names=(scientific_text_token *)xx_mem_alloc(1024*sizeof(*names));BLOB_NEED(names);
    while(c.at<b.n) {uint64_t start=c.at,length,declared,mass,crc;bool accession=false,sq=false;unsigned i;
        BLOB_NEED(scientific_text_line(&c,&line));if(!line.n) {BLOB_NEED(count && sequence_terminated(&c));break;}BLOB_NEED(scientific_text_prefix(&b,line,"ID   ") && scientific_text_split(&b,line,t,32,&nt,false) && nt==5 && scientific_text_ident(&b,t[1]) && (scientific_text_eq(&b,t[2],"Reviewed;") || scientific_text_eq(&b,t[2],"Unreviewed;")) && scientific_text_uint(&b,t[3],&length) && length && length<=SEQUENCE_MAX_SEQUENCE && scientific_text_eq(&b,t[4],"AA.") && count<1024 && scientific_text_unique(&b,t[1],names,count,&budget));names[count++]=t[1];
        while(c.at<b.n) {BLOB_NEED(scientific_text_line(&c,&line));if(scientific_text_prefix(&b,line,"SQ   ")) {sq=true;break;}BLOB_NEED(swiss_header(&b,line,&accession));}
        BLOB_NEED(sq && accession && scientific_text_split(&b,line,t,32,&nt,false) && nt==8 && scientific_text_eq(&b,t[0],"SQ") && scientific_text_eq(&b,t[1],"SEQUENCE") && scientific_text_uint(&b,t[2],&declared) && declared==length && scientific_text_eq(&b,t[3],"AA;") && scientific_text_uint(&b,t[4],&mass) && mass && mass<=UINT32_MAX && scientific_text_eq(&b,t[5],"MW;") && sequence_hex(&b,t[6],&crc) && scientific_text_eq(&b,t[7],"CRC64;") && blob_add(f,s,&b,"swissprot-header",start,c.at-start));
        while(c.at<b.n) {BLOB_NEED(scientific_text_line(&c,&line));if(scientific_text_eq(&b,line,"//")) break;BLOB_NEED(scientific_text_split(&b,line,t,32,&nt,false) && nt && nt<=6);for(i=0;i<nt;++i) BLOB_NEED(t[i].n<=10 && (i==nt-1 || t[i].n==10) && sequence_append(&b,&q,t[i],false,false,true));BLOB_NEED(q.n<=length);}
        BLOB_NEED(scientific_text_eq(&b,line,"//") && q.n==length && sequence_crc64(q.p,q.n,pd)==crc && !binary_stop(pd) && sequence_export(f,s,&q,"amino-acids",&total));
    }BLOB_NEED(count && c.at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(q.p);xx_mem_free(b.p);return ok;
}

void xx_genomics_swissprot_init(xx_genomics_swissprot *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_SWISSPROT,"genomics_swissprot"); } }
xx_genomics_swissprot *xx_genomics_swissprot_create(xx_io_device *d,int64_t b) { xx_genomics_swissprot *r=(xx_genomics_swissprot *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_swissprot_init(r,d,b); return r; }
void xx_genomics_swissprot_destroy(xx_genomics_swissprot *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_swissprot_free(xx_genomics_swissprot *r) { if(r) { xx_genomics_swissprot_destroy(r); xx_mem_free(r); } }
bool xx_genomics_swissprot_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_swissprot_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

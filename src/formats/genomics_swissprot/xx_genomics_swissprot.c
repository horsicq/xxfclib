/* SPDX-License-Identifier: MIT
 * Independently implemented from https://web.expasy.org/docs/userman.html */
#include "xxfclib/formats/genomics_swissprot/xx_genomics_swissprot.h"
#include "../xx_fourteenth_root.h"
static bool swiss_header(nh_blob *b,el_token line,bool *accession) {
    static const char *const keys[]={"AC","DT","DE","GN","OS","OG","OC","OX","OH","RN","RP","RC","RX","RG","RA","RT","RL","CC","DR","PE","KW","FT"};unsigned i;el_token key;
    if(line.n<5 || b->p[(size_t)(line.at+2)]!=' ' || b->p[(size_t)(line.at+3)]!=' ' || b->p[(size_t)(line.at+4)]!=' ') return false;key=el_slice(line,0,2);
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) if(el_eq(b,key,keys[i])) {if(i==0) {if(line.n<=5) return false;*accession=true;}return true;}return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[32],*names=NULL;f14_sequence q={0};unsigned nt,count=0;uint64_t total=0,budget=10000000;bool ok=false;
    q.decoded=&total;NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;names=(el_token *)xx_mem_alloc(1024*sizeof(*names));NH_NEED(names);
    while(c.at<b.n) {uint64_t start=c.at,length,declared,mass,crc;bool accession=false,sq=false;unsigned i;
        NH_NEED(el_line(&c,&line));if(!line.n) {NH_NEED(count && f14_terminated(&c));break;}NH_NEED(el_prefix(&b,line,"ID   ") && el_split(&b,line,t,32,&nt,false) && nt==5 && el_ident(&b,t[1]) && (el_eq(&b,t[2],"Reviewed;") || el_eq(&b,t[2],"Unreviewed;")) && el_uint(&b,t[3],&length) && length && length<=F14_MAX_SEQUENCE && el_eq(&b,t[4],"AA.") && count<1024 && el_unique(&b,t[1],names,count,&budget));names[count++]=t[1];
        while(c.at<b.n) {NH_NEED(el_line(&c,&line));if(el_prefix(&b,line,"SQ   ")) {sq=true;break;}NH_NEED(swiss_header(&b,line,&accession));}
        NH_NEED(sq && accession && el_split(&b,line,t,32,&nt,false) && nt==8 && el_eq(&b,t[0],"SQ") && el_eq(&b,t[1],"SEQUENCE") && el_uint(&b,t[2],&declared) && declared==length && el_eq(&b,t[3],"AA;") && el_uint(&b,t[4],&mass) && mass && mass<=UINT32_MAX && el_eq(&b,t[5],"MW;") && f14_hex(&b,t[6],&crc) && el_eq(&b,t[7],"CRC64;") && nh_add(f,s,&b,"swissprot-header",start,c.at-start));
        while(c.at<b.n) {NH_NEED(el_line(&c,&line));if(el_eq(&b,line,"//")) break;NH_NEED(el_split(&b,line,t,32,&nt,false) && nt && nt<=6);for(i=0;i<nt;++i) NH_NEED(t[i].n<=10 && (i==nt-1 || t[i].n==10) && f14_append(&b,&q,t[i],false,false,true));NH_NEED(q.n<=length);}
        NH_NEED(el_eq(&b,line,"//") && q.n==length && f14_crc64(q.p,q.n,pd)==crc && !fd_stop(pd) && f14_export(f,s,&q,"amino-acids",&total));
    }NH_NEED(count && c.at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(q.p);xx_mem_free(b.p);return ok;
}

void xx_genomics_swissprot_init(xx_genomics_swissprot *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_SWISSPROT,"genomics_swissprot"); } }
xx_genomics_swissprot *xx_genomics_swissprot_create(xx_io_device *d,int64_t b) { xx_genomics_swissprot *r=(xx_genomics_swissprot *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_swissprot_init(r,d,b); return r; }
void xx_genomics_swissprot_destroy(xx_genomics_swissprot *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_swissprot_free(xx_genomics_swissprot *r) { if(r) { xx_genomics_swissprot_destroy(r); xx_mem_free(r); } }
bool xx_genomics_swissprot_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_swissprot_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

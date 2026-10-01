/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.SeqIO.InsdcIO.html */
#include "xxfclib/formats/genomics_embl/xx_genomics_embl.h"
#include "../xx_fourteenth_root.h"
static bool embl_header(nh_blob *b,el_token line,bool *accession) {
    static const char *const keys[]={"XX","AC","DE","DT","KW","OS","OC","OG","OX","RN","RP","RC","RX","RA","RT","RL","DR","CC","AH","AS","CO","FH","FT"};
    unsigned i;el_token key;if(line.n<2) return false;key=el_slice(line,0,2);if(line.n>2 && b->p[(size_t)(line.at+2)]!=' ') return false;
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) if(el_eq(b,key,keys[i])) {if(i==1) {if(line.n<=5) return false;*accession=true;}return true;}return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[64],*names=NULL;f14_sequence q={0};unsigned nt,count=0;uint64_t total=0,budget=10000000;bool ok=false;
    q.decoded=&total;NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;names=(el_token *)xx_mem_alloc(1024*sizeof(*names));NH_NEED(names);
    while(c.at<b.n) {uint64_t start=c.at,length,declared,counts[5],observed[5]={0};bool accession=false,sq=false;unsigned i;el_token id;
        NH_NEED(el_line(&c,&line));if(!line.n) {NH_NEED(count && f14_terminated(&c));break;}
        NH_NEED(el_prefix(&b,line,"ID   ") && el_split(&b,line,t,64,&nt,false) && nt>=4 && el_eq(&b,t[nt-1],"BP.") && el_uint(&b,t[nt-2],&length) && length && length<=F14_MAX_SEQUENCE && t[1].n>1 && b.p[(size_t)(t[1].at+t[1].n-1)]==';');id=el_slice(t[1],0,t[1].n-1);NH_NEED(el_ident(&b,id) && count<1024 && el_unique(&b,id,names,count,&budget));names[count++]=id;
        while(c.at<b.n) {NH_NEED(el_line(&c,&line));if(el_prefix(&b,line,"SQ   ")) {sq=true;break;}NH_NEED(embl_header(&b,line,&accession));}
        NH_NEED(sq && accession && el_split(&b,line,t,64,&nt,false) && nt==14 && el_eq(&b,t[0],"SQ") && el_eq(&b,t[1],"Sequence") && el_uint(&b,t[2],&declared) && declared==length && el_eq(&b,t[3],"BP;") && el_eq(&b,t[5],"A;") && el_eq(&b,t[7],"C;") && el_eq(&b,t[9],"G;") && el_eq(&b,t[11],"T;") && el_eq(&b,t[13],"other;"));
        for(i=0;i<5;++i) NH_NEED(el_uint(&b,t[4+2*i],&counts[i]) && counts[i]<=length);NH_NEED(nh_add(f,s,&b,"embl-header",start,c.at-start));
        while(c.at<b.n) {uint64_t position;NH_NEED(el_line(&c,&line));if(el_eq(&b,line,"//")) break;NH_NEED(el_split(&b,line,t,64,&nt,false) && nt>=2 && nt<=7 && el_uint(&b,t[nt-1],&position));
            for(i=0;i<nt-1;++i) {uint64_t j;NH_NEED(t[i].n<=10 && (i==nt-2 || t[i].n==10) && f14_append(&b,&q,t[i],false,true,true));for(j=0;j<t[i].n;++j) {unsigned ch=f14_upper(b.p[(size_t)(t[i].at+j)]),k=ch=='A' ? 0:ch=='C' ? 1:ch=='G' ? 2:ch=='T' ? 3:4;++observed[k];}}NH_NEED(q.n==position && q.n<=length);
        }
        NH_NEED(el_eq(&b,line,"//") && q.n==length);for(i=0;i<5;++i) NH_NEED(counts[i]==observed[i]);NH_NEED(f14_export(f,s,&q,"nucleotides",&total));
    }NH_NEED(count && c.at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(q.p);xx_mem_free(b.p);return ok;
}

void xx_genomics_embl_init(xx_genomics_embl *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_EMBL,"genomics_embl"); } }
xx_genomics_embl *xx_genomics_embl_create(xx_io_device *d,int64_t b) { xx_genomics_embl *r=(xx_genomics_embl *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_embl_init(r,d,b); return r; }
void xx_genomics_embl_destroy(xx_genomics_embl *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_embl_free(xx_genomics_embl *r) { if(r) { xx_genomics_embl_destroy(r); xx_mem_free(r); } }
bool xx_genomics_embl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_embl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

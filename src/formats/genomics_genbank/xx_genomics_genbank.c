/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.SeqIO.InsdcIO.html */
#include "xxfclib/formats/genomics_genbank/xx_genomics_genbank.h"
#include "../xx_fourteenth_root.h"
static bool genbank_header(nh_blob *b,el_token line,bool *features,bool *accession) {
    static const char *const keys[]={"DEFINITION","ACCESSION","VERSION","DBLINK","KEYWORDS","SEGMENT","SOURCE","REFERENCE","COMMENT","PRIMARY","FEATURES"};
    unsigned i;el_token key;if(line.n<12) return false;
    if(*features) {uint64_t j;el_token feature,value;if(line.n<=21) return false;for(j=0;j<5;++j) if(b->p[(size_t)(line.at+j)]!=' ') return false;feature=el_trim(b,el_slice(line,5,16));value=el_trim(b,el_slice(line,21,line.n-21));if(!value.n) return false;return !feature.n || el_chars(b,feature,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_",true);}
    key=el_trim(b,el_slice(line,0,12));
    if(!key.n) return line.n>12;
    if(b->p[(size_t)line.at]==' ') return !*features && (el_eq(b,key,"ORGANISM") || el_eq(b,key,"AUTHORS") || el_eq(b,key,"CONSRTM") || el_eq(b,key,"TITLE") || el_eq(b,key,"JOURNAL") || el_eq(b,key,"PUBMED") || el_eq(b,key,"REMARK"));
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) if(el_eq(b,key,keys[i])) {if(*features) return false;if(i==1) {el_token t[32];unsigned n;if(*accession || !el_split(b,el_slice(line,12,line.n-12),t,32,&n,false) || !n) return false;for(i=0;i<n;++i) if(!el_ident(b,t[i])) return false;*accession=true;return true;}if(i==10) *features=true;return true;}
    return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[64],*names=NULL;f14_sequence q={0};unsigned nt,count=0;uint64_t total=0,budget=10000000;bool ok=false;
    q.decoded=&total;NH_NEED(nh_load(f,&b,pd) && th_ascii(&b));c.b=&b;names=(el_token *)xx_mem_alloc(1024*sizeof(*names));NH_NEED(names);
    while(c.at<b.n) {uint64_t start=c.at,length;bool features=false,accession=false,origin=false;NH_NEED(el_line(&c,&line));if(!line.n) {NH_NEED(count && f14_terminated(&c));break;}
        NH_NEED(el_split(&b,line,t,64,&nt,false) && nt>=7 && nt<=9 && el_eq(&b,t[0],"LOCUS") && el_ident(&b,t[1]) && el_uint(&b,t[2],&length) && length && length<=F14_MAX_SEQUENCE && el_eq(&b,t[3],"bp") && count<1024 && el_unique(&b,t[1],names,count,&budget));names[count++]=t[1];
        NH_NEED(el_eq(&b,t[4],"DNA") || el_eq(&b,t[4],"RNA") || el_eq(&b,t[4],"ss-DNA") || el_eq(&b,t[4],"ds-DNA"));
        while(c.at<b.n) {NH_NEED(el_line(&c,&line));if(el_eq(&b,el_trim(&b,line),"ORIGIN")) {origin=true;break;}NH_NEED(genbank_header(&b,line,&features,&accession));}
        NH_NEED(origin && accession && features && nh_add(f,s,&b,"genbank-header",start,c.at-start));
        while(c.at<b.n) {uint64_t position;unsigned i;NH_NEED(el_line(&c,&line));if(el_eq(&b,line,"//")) break;NH_NEED(el_split(&b,line,t,64,&nt,false) && nt>=2 && nt<=7 && el_uint(&b,t[0],&position) && position==q.n+1);
            for(i=1;i<nt;++i) { NH_NEED(t[i].n<=10 && (i==nt-1 || t[i].n==10) && f14_append(&b,&q,t[i],false,true,true)); } NH_NEED(q.n<=length);
        }
        NH_NEED(el_eq(&b,line,"//") && q.n==length && f14_export(f,s,&q,"nucleotides",&total));
    }NH_NEED(count && c.at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(q.p);xx_mem_free(b.p);return ok;
}

void xx_genomics_genbank_init(xx_genomics_genbank *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_GENBANK,"genomics_genbank"); } }
xx_genomics_genbank *xx_genomics_genbank_create(xx_io_device *d,int64_t b) { xx_genomics_genbank *r=(xx_genomics_genbank *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_genbank_init(r,d,b); return r; }
void xx_genomics_genbank_destroy(xx_genomics_genbank *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_genbank_free(xx_genomics_genbank *r) { if(r) { xx_genomics_genbank_destroy(r); xx_mem_free(r); } }
bool xx_genomics_genbank_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_genbank_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

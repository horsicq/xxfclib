/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.SeqIO.InsdcIO.html */
#include "xxfclib/formats/genomics_genbank/xx_genomics_genbank.h"
#include "../common/xx_sequence_alignment.h"
static bool genbank_header(memory_blob *b,scientific_text_token line,bool *features,bool *accession) {
    static const char *const keys[]={"DEFINITION","ACCESSION","VERSION","DBLINK","KEYWORDS","SEGMENT","SOURCE","REFERENCE","COMMENT","PRIMARY","FEATURES"};
    unsigned i;scientific_text_token key;if(line.n<12) return false;
    if(*features) {uint64_t j;scientific_text_token feature,value;if(line.n<=21) return false;for(j=0;j<5;++j) if(b->p[(size_t)(line.at+j)]!=' ') return false;feature=scientific_text_trim(b,scientific_text_slice(line,5,16));value=scientific_text_trim(b,scientific_text_slice(line,21,line.n-21));if(!value.n) return false;return !feature.n || scientific_text_chars(b,feature,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_",true);}
    key=scientific_text_trim(b,scientific_text_slice(line,0,12));
    if(!key.n) return line.n>12;
    if(b->p[(size_t)line.at]==' ') return !*features && (scientific_text_eq(b,key,"ORGANISM") || scientific_text_eq(b,key,"AUTHORS") || scientific_text_eq(b,key,"CONSRTM") || scientific_text_eq(b,key,"TITLE") || scientific_text_eq(b,key,"JOURNAL") || scientific_text_eq(b,key,"PUBMED") || scientific_text_eq(b,key,"REMARK"));
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);++i) if(scientific_text_eq(b,key,keys[i])) {if(*features) return false;if(i==1) {scientific_text_token t[32];unsigned n;if(*accession || !scientific_text_split(b,scientific_text_slice(line,12,line.n-12),t,32,&n,false) || !n) return false;for(i=0;i<n;++i) if(!scientific_text_ident(b,t[i])) return false;*accession=true;return true;}if(i==10) *features=true;return true;}
    return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[64],*names=NULL;sequence_sequence q={0};unsigned nt,count=0;uint64_t total=0,budget=10000000;bool ok=false;
    q.decoded=&total;BLOB_NEED(blob_load(f,&b,pd) && structure_ascii(&b));c.b=&b;names=(scientific_text_token *)xx_mem_alloc(1024*sizeof(*names));BLOB_NEED(names);
    while(c.at<b.n) {uint64_t start=c.at,length;bool features=false,accession=false,origin=false;BLOB_NEED(scientific_text_line(&c,&line));if(!line.n) {BLOB_NEED(count && sequence_terminated(&c));break;}
        BLOB_NEED(scientific_text_split(&b,line,t,64,&nt,false) && nt>=7 && nt<=9 && scientific_text_eq(&b,t[0],"LOCUS") && scientific_text_ident(&b,t[1]) && scientific_text_uint(&b,t[2],&length) && length && length<=SEQUENCE_MAX_SEQUENCE && scientific_text_eq(&b,t[3],"bp") && count<1024 && scientific_text_unique(&b,t[1],names,count,&budget));names[count++]=t[1];
        BLOB_NEED(scientific_text_eq(&b,t[4],"DNA") || scientific_text_eq(&b,t[4],"RNA") || scientific_text_eq(&b,t[4],"ss-DNA") || scientific_text_eq(&b,t[4],"ds-DNA"));
        while(c.at<b.n) {BLOB_NEED(scientific_text_line(&c,&line));if(scientific_text_eq(&b,scientific_text_trim(&b,line),"ORIGIN")) {origin=true;break;}BLOB_NEED(genbank_header(&b,line,&features,&accession));}
        BLOB_NEED(origin && accession && features && blob_add(f,s,&b,"genbank-header",start,c.at-start));
        while(c.at<b.n) {uint64_t position;unsigned i;BLOB_NEED(scientific_text_line(&c,&line));if(scientific_text_eq(&b,line,"//")) break;BLOB_NEED(scientific_text_split(&b,line,t,64,&nt,false) && nt>=2 && nt<=7 && scientific_text_uint(&b,t[0],&position) && position==q.n+1);
            for(i=1;i<nt;++i) { BLOB_NEED(t[i].n<=10 && (i==nt-1 || t[i].n==10) && sequence_append(&b,&q,t[i],false,true,true)); } BLOB_NEED(q.n<=length);
        }
        BLOB_NEED(scientific_text_eq(&b,line,"//") && q.n==length && sequence_export(f,s,&q,"nucleotides",&total));
    }BLOB_NEED(count && c.at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(names);xx_mem_free(q.p);xx_mem_free(b.p);return ok;
}

void xx_genomics_genbank_init(xx_genomics_genbank *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_GENBANK,"genomics_genbank"); } }
xx_genomics_genbank *xx_genomics_genbank_create(xx_io_device *d,int64_t b) { xx_genomics_genbank *r=(xx_genomics_genbank *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_genbank_init(r,d,b); return r; }
void xx_genomics_genbank_destroy(xx_genomics_genbank *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_genbank_free(xx_genomics_genbank *r) { if(r) { xx_genomics_genbank_destroy(r); xx_mem_free(r); } }
bool xx_genomics_genbank_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_genbank_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

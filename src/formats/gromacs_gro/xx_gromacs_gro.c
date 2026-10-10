/* SPDX-License-Identifier: MIT
 * Independently implemented from https://manual.gromacs.org/current/reference-manual/file-formats.html#gro */
#include "xxfclib/formats/gromacs_gro/xx_gromacs_gro.h"
#include "../common/xx_scientific_text.h"
static bool gro_fixed(memory_blob *b,scientific_text_token v,unsigned decimals) {unsigned i;if(!scientific_text_float(b,v) || v.n!=8 || b->p[(size_t)v.at+7-decimals]!='.') return false;for(i=8-decimals;i<8;++i) if(b->p[(size_t)v.at+i]<'0' || b->p[(size_t)v.at+i]>'9') return false;return true;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[12];uint64_t count,head,atoms,box,n;unsigned i,j,nt;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(scientific_text_line(&c,&line) && line.n && line.n<=1024 && scientific_text_line(&c,&line) && scientific_text_uint(&b,line,&count) && count && count<=4090);head=c.at;atoms=head;
    for(i=0;i<count;++i) {
        BLOB_NEED(scientific_text_line(&c,&line) && (line.n==44 || line.n==68) && scientific_text_uint(&b,scientific_text_slice(line,0,5),&n) && n<=99999 && scientific_text_uint(&b,scientific_text_slice(line,15,5),&n) && n<=99999);
        BLOB_NEED(scientific_text_ident(&b,scientific_text_trim(&b,scientific_text_slice(line,5,5))) && scientific_text_ident(&b,scientific_text_trim(&b,scientific_text_slice(line,10,5))));
        for(j=0;j<(line.n==44 ? 3U:6U);++j) BLOB_NEED(gro_fixed(&b,scientific_text_slice(line,20+8*j,8),j<3 ? 3:4));
    }
    box=c.at;BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_split(&b,line,t,12,&nt,false) && (nt==3 || nt==9));for(i=0;i<nt;++i) BLOB_NEED(scientific_text_float(&b,t[i]));
    BLOB_NEED(c.at==b.n && blob_add(f,s,&b,"header",0,head) && blob_add(f,s,&b,"atoms",atoms,box-atoms) && blob_add(f,s,&b,"box",box,b.n-box));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_gromacs_gro_init(xx_gromacs_gro *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GROMACS_GRO,"gromacs_gro"); } }
xx_gromacs_gro *xx_gromacs_gro_create(xx_io_device *d,int64_t b) { xx_gromacs_gro *r=(xx_gromacs_gro *)xx_mem_alloc(sizeof(*r)); if(r) xx_gromacs_gro_init(r,d,b); return r; }
void xx_gromacs_gro_destroy(xx_gromacs_gro *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gromacs_gro_free(xx_gromacs_gro *r) { if(r) { xx_gromacs_gro_destroy(r); xx_mem_free(r); } }
bool xx_gromacs_gro_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gromacs_gro_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

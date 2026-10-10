/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.xcrysden.org/doc/XSF.html */
#include "xxfclib/formats/xcrysden_xsf/xx_xcrysden_xsf.h"
#include "../common/xx_molecular_text.h"
static bool molecular_xsf_line(scientific_text_lines *c,scientific_text_token *v) {do {if(!scientific_text_line(c,v)) return false;*v=scientific_text_trim(c->b,*v);}while(!v->n || c->b->p[(size_t)v->at]=='#');return true;}
static bool molecular_xsf_atom(memory_blob *b,scientific_text_token line) {scientific_text_token t[8];unsigned n;return scientific_text_split(b,line,t,8,&n,false) && (n==4 || n==7) && (molecular_z(b,t[0]) || molecular_element(b,t[0])) && molecular_floats(b,t,1,n);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[4];unsigned n;uint64_t atoms=0,i,at;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;BLOB_NEED(molecular_xsf_line(&c,&line));
    if(scientific_text_eq(&b,line,"CRYSTAL")) {
        BLOB_NEED(molecular_xsf_line(&c,&line) && scientific_text_eq(&b,line,"PRIMVEC"));
        for(i=0;i<3;++i) BLOB_NEED(molecular_words(&c,&line,t,4,&n) && n==3 && molecular_floats(&b,t,0,3) && !(scientific_text_zero(&b,t[0]) && scientific_text_zero(&b,t[1]) && scientific_text_zero(&b,t[2])));
        BLOB_NEED(molecular_xsf_line(&c,&line) && scientific_text_eq(&b,line,"PRIMCOORD") && molecular_words(&c,&line,t,4,&n) && n==2 && scientific_text_uint(&b,t[0],&atoms) && atoms && atoms<=100000 && scientific_text_eq(&b,t[1],"1"));
        BLOB_NEED(blob_add(f,s,&b,"cell-header",0,c.at));at=c.at;for(i=0;i<atoms;++i) BLOB_NEED(scientific_text_line(&c,&line) && molecular_xsf_atom(&b,line));
        BLOB_NEED(blob_add(f,s,&b,"atoms",at,c.at-at));
    } else {
        BLOB_NEED(scientific_text_eq(&b,line,"ATOMS") && blob_add(f,s,&b,"molecule-header",0,c.at));at=c.at;
        while(c.at<b.n) {BLOB_NEED(scientific_text_line(&c,&line));if(!scientific_text_trim(&b,line).n) break;BLOB_NEED(++atoms<=100000 && molecular_xsf_atom(&b,line));}
        BLOB_NEED(atoms && blob_add(f,s,&b,"atoms",at,c.at-at));
    }
    while(c.at<b.n) {BLOB_NEED(scientific_text_line(&c,&line));line=scientific_text_trim(&b,line);BLOB_NEED(!line.n || b.p[(size_t)line.at]=='#');}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_xcrysden_xsf_init(xx_xcrysden_xsf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XCRYSDEN_XSF,"xcrysden_xsf"); } }
xx_xcrysden_xsf *xx_xcrysden_xsf_create(xx_io_device *d,int64_t b) { xx_xcrysden_xsf *r=(xx_xcrysden_xsf *)xx_mem_alloc(sizeof(*r)); if(r) xx_xcrysden_xsf_init(r,d,b); return r; }
void xx_xcrysden_xsf_destroy(xx_xcrysden_xsf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xcrysden_xsf_free(xx_xcrysden_xsf *r) { if(r) { xx_xcrysden_xsf_destroy(r); xx_mem_free(r); } }
bool xx_xcrysden_xsf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xcrysden_xsf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

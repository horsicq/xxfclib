/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/openbabel/openbabel/blob/master/src/formats/mdlformat.cpp */
#include "xxfclib/formats/mdl_molfile/xx_mdl_molfile.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[64];unsigned n;uint64_t atoms,bonds,i,a,z,kind,at;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_line(&c,&line) && scientific_text_line(&c,&line) && scientific_text_line(&c,&line) && line.n>=39 && scientific_text_eq(&b,scientific_text_slice(line,34,5),"V2000"));
    BLOB_NEED(scientific_text_uint(&b,scientific_text_slice(line,0,3),&atoms) && atoms && atoms<=999 && scientific_text_uint(&b,scientific_text_slice(line,3,3),&bonds) && bonds<=999);
    for(i=6;i<33;i+=3) { BLOB_NEED(scientific_text_integer(&b,scientific_text_slice(line,i,3))); } BLOB_NEED(blob_add(f,s,&b,"molecule-header",0,c.at));at=c.at;
    for(i=0;i<atoms;++i) {uint64_t j;BLOB_NEED(scientific_text_line(&c,&line) && line.n>=34 && line.n<=80 && scientific_text_float(&b,scientific_text_slice(line,0,10)) && scientific_text_float(&b,scientific_text_slice(line,10,10)) && scientific_text_float(&b,scientific_text_slice(line,20,10)) && b.p[(size_t)line.at+30]==' ' && molecular_element(&b,scientific_text_trim(&b,scientific_text_slice(line,31,3))));
        if(line.n>34) {BLOB_NEED(line.n>=36 && scientific_text_range(&b,scientific_text_slice(line,34,2),9,9));for(j=36;j+3<=line.n;j+=3) BLOB_NEED(scientific_text_range(&b,scientific_text_slice(line,j,3),0,999));BLOB_NEED(j==line.n);}
    }
    BLOB_NEED(blob_add(f,s,&b,"atoms",at,c.at-at));at=c.at;
    for(i=0;i<bonds;++i) {uint64_t j;BLOB_NEED(scientific_text_line(&c,&line) && line.n>=9 && scientific_text_uint(&b,scientific_text_slice(line,0,3),&a) && a>=1 && a<=atoms && scientific_text_uint(&b,scientific_text_slice(line,3,3),&z) && z>=1 && z<=atoms && z!=a && scientific_text_uint(&b,scientific_text_slice(line,6,3),&kind) && kind>=1 && kind<=4);for(j=9;j+3<=line.n;j+=3) BLOB_NEED(scientific_text_range(&b,scientific_text_slice(line,j,3),0,999));BLOB_NEED(j==line.n);}
    if(bonds) { BLOB_NEED(blob_add(f,s,&b,"bonds",at,c.at-at)); } at=c.at;
    for(;;) {BLOB_NEED(molecular_words(&c,&line,t,64,&n));if(scientific_text_eq(&b,line,"M  END")) break;
        BLOB_NEED(n>=3 && scientific_text_eq(&b,t[0],"M") && (scientific_text_eq(&b,t[1],"CHG") || scientific_text_eq(&b,t[1],"ISO") || scientific_text_eq(&b,t[1],"RAD")) && scientific_text_uint(&b,t[2],&kind) && kind>=1 && kind<=8 && n==3+kind*2);
        for(i=0;i<kind;++i) {BLOB_NEED(scientific_text_uint(&b,t[3+i*2],&a) && a>=1 && a<=atoms);if(scientific_text_eq(&b,t[1],"CHG")) BLOB_NEED(scientific_text_range(&b,t[4+i*2],15,15));else BLOB_NEED(scientific_text_uint(&b,t[4+i*2],&z) && z>=1 && z<=(scientific_text_eq(&b,t[1],"RAD") ? 3U:999U));}
    }
    BLOB_NEED(blob_add(f,s,&b,"properties-and-end",at,c.at-at) && molecular_trailing(&c));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_mdl_molfile_init(xx_mdl_molfile *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MDL_MOLFILE,"mdl_molfile"); } }
xx_mdl_molfile *xx_mdl_molfile_create(xx_io_device *d,int64_t b) { xx_mdl_molfile *r=(xx_mdl_molfile *)xx_mem_alloc(sizeof(*r)); if(r) xx_mdl_molfile_init(r,d,b); return r; }
void xx_mdl_molfile_destroy(xx_mdl_molfile *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mdl_molfile_free(xx_mdl_molfile *r) { if(r) { xx_mdl_molfile_destroy(r); xx_mem_free(r); } }
bool xx_mdl_molfile_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mdl_molfile_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

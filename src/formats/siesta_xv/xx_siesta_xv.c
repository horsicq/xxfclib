/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/siesta.html */
#include "xxfclib/formats/siesta_xv/xx_siesta_xv.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[10],cell[3][3];unsigned nt,j;uint64_t atoms,i,species,z,head,species_z[119]={0};bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    for(j=0;j<3;++j) {BLOB_NEED(structure_words(&c,&line,t,10,&nt,"#") && nt==6 && structure_floats(&b,t,6));xx_rt_memcpy(cell[j],t,3*sizeof(scientific_text_token));}BLOB_NEED(structure_cell(&b,cell));
    BLOB_NEED(structure_words(&c,&line,t,10,&nt,"#") && nt==1 && scientific_text_uint(&b,t[0],&atoms) && atoms && atoms<=100000);head=c.at;
    for(i=0;i<atoms;++i) {BLOB_NEED(structure_words(&c,&line,t,10,&nt,"#") && nt==8 && scientific_text_uint(&b,t[0],&species) && species && species<=118 && scientific_text_uint(&b,t[1],&z) && z && z<=118 && structure_floats(&b,t+2,6));BLOB_NEED(!species_z[(size_t)species] || species_z[(size_t)species]==z);species_z[(size_t)species]=z;}
    BLOB_NEED(molecular_trailing(&c) && blob_add(f,s,&b,"cell-and-count",0,head) && blob_add(f,s,&b,"atom-states",head,b.n-head));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_siesta_xv_init(xx_siesta_xv *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SIESTA_XV,"siesta_xv"); } }
xx_siesta_xv *xx_siesta_xv_create(xx_io_device *d,int64_t b) { xx_siesta_xv *r=(xx_siesta_xv *)xx_mem_alloc(sizeof(*r)); if(r) xx_siesta_xv_init(r,d,b); return r; }
void xx_siesta_xv_destroy(xx_siesta_xv *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_siesta_xv_free(xx_siesta_xv *r) { if(r) { xx_siesta_xv_destroy(r); xx_mem_free(r); } }
bool xx_siesta_xv_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_siesta_xv_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

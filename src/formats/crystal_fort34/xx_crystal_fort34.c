/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/crystal.html */
#include "xxfclib/formats/crystal_fort34/xx_crystal_fort34.h"
#include "../common/xx_scientific_structure.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[16],cell[3][3];unsigned nt,j;uint64_t dimension,operators,atoms,i,z,head,sym,coords;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;
    BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && (nt==3 || nt==8) && scientific_text_uint(&b,t[0],&dimension) && dimension<=3 && scientific_text_eq(&b,t[1],"1") && scientific_text_eq(&b,t[2],"1"));
    if(nt==8) BLOB_NEED(scientific_text_eq(&b,t[3],"E") && scientific_text_float(&b,t[4]) && scientific_text_eq(&b,t[5],"DE") && t[6].n>1 && b.p[(size_t)(t[6].at+t[6].n-1)]=='(' && scientific_text_float(&b,scientific_text_slice(t[6],0,t[6].n-1)) && scientific_text_eq(&b,t[7],"1)"));
    for(j=0;j<3;++j) {BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && nt==3);xx_rt_memcpy(cell[j],t,3*sizeof(scientific_text_token));}BLOB_NEED(structure_cell(&b,cell));head=c.at;
    BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && nt==1 && scientific_text_uint(&b,t[0],&operators) && operators==1);
    for(j=0;j<4;++j) {unsigned k;BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && nt==3 && structure_floats(&b,t,3));for(k=0;k<3;++k) BLOB_NEED(molecular_value(&b,t[k])==(j==k ? 1.0:0.0));}sym=c.at;
    BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && nt==1 && scientific_text_uint(&b,t[0],&atoms) && atoms && atoms<=100000);coords=c.at;
    for(i=0;i<atoms;++i) BLOB_NEED(structure_words(&c,&line,t,16,&nt,"#") && nt==4 && scientific_text_uint(&b,t[0],&z) && z && z<=118 && structure_floats(&b,t+1,3));
    BLOB_NEED(molecular_trailing(&c) && blob_add(f,s,&b,"geometry-cell",0,head) && blob_add(f,s,&b,"symmetry",head,sym-head) && blob_add(f,s,&b,"atom-count",sym,coords-sym) && blob_add(f,s,&b,"atoms",coords,b.n-coords));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_crystal_fort34_init(xx_crystal_fort34 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CRYSTAL_FORT34,"crystal_fort34"); } }
xx_crystal_fort34 *xx_crystal_fort34_create(xx_io_device *d,int64_t b) { xx_crystal_fort34 *r=(xx_crystal_fort34 *)xx_mem_alloc(sizeof(*r)); if(r) xx_crystal_fort34_init(r,d,b); return r; }
void xx_crystal_fort34_destroy(xx_crystal_fort34 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_crystal_fort34_free(xx_crystal_fort34 *r) { if(r) { xx_crystal_fort34_destroy(r); xx_mem_free(r); } }
bool xx_crystal_fort34_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_crystal_fort34_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

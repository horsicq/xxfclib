/* SPDX-License-Identifier: MIT
 * Independently implemented from https://pyscf.org/_modules/pyscf/tools/cubegen.html */
#include "xxfclib/formats/gaussian_cube/xx_gaussian_cube.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,t[8];unsigned n;uint64_t atoms,grids=1,dim,i,at;bool ok=false;
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;BLOB_NEED(scientific_text_line(&c,&line) && line.n && scientific_text_line(&c,&line));
    BLOB_NEED(molecular_words(&c,&line,t,8,&n) && n==4 && scientific_text_uint(&b,t[0],&atoms) && atoms && atoms<=100000 && molecular_floats(&b,t,1,4));
    for(i=0;i<3;++i) {BLOB_NEED(molecular_words(&c,&line,t,8,&n) && n==4 && scientific_text_uint(&b,t[0],&dim) && dim && dim<=1000000 && grids<=1000000/dim && molecular_floats(&b,t,1,4));grids*=dim;BLOB_NEED(!(scientific_text_zero(&b,t[1]) && scientific_text_zero(&b,t[2]) && scientific_text_zero(&b,t[3])));}
    BLOB_NEED(blob_add(f,s,&b,"grid-header",0,c.at));at=c.at;
    for(i=0;i<atoms;++i) BLOB_NEED(molecular_words(&c,&line,t,8,&n) && n==5 && molecular_z(&b,t[0]) && molecular_floats(&b,t,1,5));
    BLOB_NEED(blob_add(f,s,&b,"atoms",at,c.at-at));at=c.at;BLOB_NEED(molecular_numbers(&c,grids) && blob_add(f,s,&b,"scalar-grid",at,c.at-at) && molecular_trailing(&c));
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_gaussian_cube_init(xx_gaussian_cube *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GAUSSIAN_CUBE,"gaussian_cube"); } }
xx_gaussian_cube *xx_gaussian_cube_create(xx_io_device *d,int64_t b) { xx_gaussian_cube *r=(xx_gaussian_cube *)xx_mem_alloc(sizeof(*r)); if(r) xx_gaussian_cube_init(r,d,b); return r; }
void xx_gaussian_cube_destroy(xx_gaussian_cube *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gaussian_cube_free(xx_gaussian_cube *r) { if(r) { xx_gaussian_cube_destroy(r); xx_mem_free(r); } }
bool xx_gaussian_cube_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gaussian_cube_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

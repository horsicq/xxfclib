/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/siesta.html */
#include "xxfclib/formats/siesta_xv/xx_siesta_xv.h"
#include "../xx_thirteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[10],cell[3][3];unsigned nt,j;uint64_t atoms,i,species,z,head,species_z[119]={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    for(j=0;j<3;++j) {NH_NEED(th_words(&c,&line,t,10,&nt,"#") && nt==6 && th_floats(&b,t,6));xx_rt_memcpy(cell[j],t,3*sizeof(el_token));}NH_NEED(th_cell(&b,cell));
    NH_NEED(th_words(&c,&line,t,10,&nt,"#") && nt==1 && el_uint(&b,t[0],&atoms) && atoms && atoms<=100000);head=c.at;
    for(i=0;i<atoms;++i) {NH_NEED(th_words(&c,&line,t,10,&nt,"#") && nt==8 && el_uint(&b,t[0],&species) && species && species<=118 && el_uint(&b,t[1],&z) && z && z<=118 && th_floats(&b,t+2,6));NH_NEED(!species_z[(size_t)species] || species_z[(size_t)species]==z);species_z[(size_t)species]=z;}
    NH_NEED(tw_trailing(&c) && nh_add(f,s,&b,"cell-and-count",0,head) && nh_add(f,s,&b,"atom-states",head,b.n-head));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_siesta_xv_init(xx_siesta_xv *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SIESTA_XV,"siesta_xv"); } }
xx_siesta_xv *xx_siesta_xv_create(xx_io_device *d,int64_t b) { xx_siesta_xv *r=(xx_siesta_xv *)xx_mem_alloc(sizeof(*r)); if(r) xx_siesta_xv_init(r,d,b); return r; }
void xx_siesta_xv_destroy(xx_siesta_xv *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_siesta_xv_free(xx_siesta_xv *r) { if(r) { xx_siesta_xv_destroy(r); xx_mem_free(r); } }
bool xx_siesta_xv_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_siesta_xv_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

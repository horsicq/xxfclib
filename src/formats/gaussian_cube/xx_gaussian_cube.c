/* SPDX-License-Identifier: MIT
 * Independently implemented from https://pyscf.org/_modules/pyscf/tools/cubegen.html */
#include "xxfclib/formats/gaussian_cube/xx_gaussian_cube.h"
#include "../xx_twelfth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[8];unsigned n;uint64_t atoms,grids=1,dim,i,at;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;NH_NEED(el_line(&c,&line) && line.n && el_line(&c,&line));
    NH_NEED(tw_words(&c,&line,t,8,&n) && n==4 && el_uint(&b,t[0],&atoms) && atoms && atoms<=100000 && tw_floats(&b,t,1,4));
    for(i=0;i<3;++i) {NH_NEED(tw_words(&c,&line,t,8,&n) && n==4 && el_uint(&b,t[0],&dim) && dim && dim<=1000000 && grids<=1000000/dim && tw_floats(&b,t,1,4));grids*=dim;NH_NEED(!(el_zero(&b,t[1]) && el_zero(&b,t[2]) && el_zero(&b,t[3])));}
    NH_NEED(nh_add(f,s,&b,"grid-header",0,c.at));at=c.at;
    for(i=0;i<atoms;++i) NH_NEED(tw_words(&c,&line,t,8,&n) && n==5 && tw_z(&b,t[0]) && tw_floats(&b,t,1,5));
    NH_NEED(nh_add(f,s,&b,"atoms",at,c.at-at));at=c.at;NH_NEED(tw_numbers(&c,grids) && nh_add(f,s,&b,"scalar-grid",at,c.at-at) && tw_trailing(&c));
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_gaussian_cube_init(xx_gaussian_cube *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GAUSSIAN_CUBE,"gaussian_cube"); } }
xx_gaussian_cube *xx_gaussian_cube_create(xx_io_device *d,int64_t b) { xx_gaussian_cube *r=(xx_gaussian_cube *)xx_mem_alloc(sizeof(*r)); if(r) xx_gaussian_cube_init(r,d,b); return r; }
void xx_gaussian_cube_destroy(xx_gaussian_cube *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gaussian_cube_free(xx_gaussian_cube *r) { if(r) { xx_gaussian_cube_destroy(r); xx_mem_free(r); } }
bool xx_gaussian_cube_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gaussian_cube_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

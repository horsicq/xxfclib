/* SPDX-License-Identifier: MIT
 * Independently implemented from https://ase-lib.org/_modules/ase/io/xyz.html */
#include "xxfclib/formats/molecule_xyz/xx_molecule_xyz.h"
#include "../xx_twelfth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[8];unsigned n,frames=0;uint64_t atoms,i,at;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    while(c.at<b.n) {at=c.at;NH_NEED(el_line(&c,&line) && el_uint(&b,line,&atoms) && atoms && atoms<=100000 && ++frames<=4096 && el_line(&c,&line));
        for(i=0;i<atoms;++i) NH_NEED(tw_words(&c,&line,t,8,&n) && n==4 && tw_element(&b,t[0]) && tw_floats(&b,t,1,4));
        NH_NEED(nh_add(f,s,&b,"molecular-frame",at,c.at-at));
    }
    NH_NEED(frames);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_molecule_xyz_init(xx_molecule_xyz *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MOLECULE_XYZ,"molecule_xyz"); } }
xx_molecule_xyz *xx_molecule_xyz_create(xx_io_device *d,int64_t b) { xx_molecule_xyz *r=(xx_molecule_xyz *)xx_mem_alloc(sizeof(*r)); if(r) xx_molecule_xyz_init(r,d,b); return r; }
void xx_molecule_xyz_destroy(xx_molecule_xyz *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_molecule_xyz_free(xx_molecule_xyz *r) { if(r) { xx_molecule_xyz_destroy(r); xx_mem_free(r); } }
bool xx_molecule_xyz_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_molecule_xyz_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

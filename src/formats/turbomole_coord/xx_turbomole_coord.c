/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/turbomole.html */
#include "xxfclib/formats/turbomole_coord/xx_turbomole_coord.h"
#include "../xx_thirteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[8];unsigned nt,atoms=0;uint64_t head,tail=0;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    NH_NEED(th_words(&c,&line,t,8,&nt,"#") && el_eq(&b,t[0],"$coord") && (nt==1 || (nt==2 && el_eq(&b,t[1],"angs"))));head=c.at;
    while(c.at<b.n) {uint64_t start=c.at;NH_NEED(th_words(&c,&line,t,8,&nt,"#"));if(el_eq(&b,t[0],"$end")) {NH_NEED(nt==1);tail=start;break;}
        NH_NEED(nt==4 && ++atoms<=100000 && th_floats(&b,t,3) && th_symbol(&b,t[3]));}
    NH_NEED(atoms && tail && tw_trailing(&c));
    NH_NEED(nh_add(f,s,&b,"coordinate-header",0,head) && nh_add(f,s,&b,"atoms",head,tail-head) && nh_add(f,s,&b,"coordinate-end",tail,b.n-tail));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_turbomole_coord_init(xx_turbomole_coord *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TURBOMOLE_COORD,"turbomole_coord"); } }
xx_turbomole_coord *xx_turbomole_coord_create(xx_io_device *d,int64_t b) { xx_turbomole_coord *r=(xx_turbomole_coord *)xx_mem_alloc(sizeof(*r)); if(r) xx_turbomole_coord_init(r,d,b); return r; }
void xx_turbomole_coord_destroy(xx_turbomole_coord *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_turbomole_coord_free(xx_turbomole_coord *r) { if(r) { xx_turbomole_coord_destroy(r); xx_mem_free(r); } }
bool xx_turbomole_coord_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_turbomole_coord_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

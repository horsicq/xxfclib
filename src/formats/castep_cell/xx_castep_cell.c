/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/castep.html */
#include "xxfclib/formats/castep_cell/xx_castep_cell.h"
#include "../xx_thirteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[12],kind,cell[3][3];unsigned nt,blocks=0,rows=0;uint64_t start,atoms=0;bool lattice=false,positions=false,ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    while(c.at<b.n) {start=c.at;if(!th_words(&c,&line,t,12,&nt,"#!")) {NH_NEED(c.at==b.n);break;}
        NH_NEED(nt==2 && th_eq(&b,t[0],"%BLOCK") && ++blocks<=2);kind=t[1];rows=0;
        if(th_eq(&b,kind,"LATTICE_CART")) {NH_NEED(!lattice);lattice=true;}
        else {NH_NEED(!positions && (th_eq(&b,kind,"POSITIONS_ABS") || th_eq(&b,kind,"POSITIONS_FRAC")));positions=true;}
        for(;;) {NH_NEED(th_words(&c,&line,t,12,&nt,"#!"));
            if(th_eq(&b,t[0],"%ENDBLOCK")) {NH_NEED(nt==2 && (th_eq(&b,kind,"LATTICE_CART") ? th_eq(&b,t[1],"LATTICE_CART"):th_eq(&b,kind,"POSITIONS_ABS") ? th_eq(&b,t[1],"POSITIONS_ABS"):th_eq(&b,t[1],"POSITIONS_FRAC")));break;}
            if(!rows && nt==1 && (th_eq(&b,t[0],"ang") || th_eq(&b,t[0],"bohr"))) {NH_NEED(!th_eq(&b,kind,"POSITIONS_FRAC"));NH_NEED(th_words(&c,&line,t,12,&nt,"#!"));}
            if(th_eq(&b,kind,"LATTICE_CART")) {NH_NEED(nt==3 && rows<3 && th_floats(&b,t,3));xx_rt_memcpy(cell[rows],t,3*sizeof(el_token));}
            else {NH_NEED(nt==4 && tw_element(&b,t[0]) && th_floats(&b,t+1,3) && ++atoms<=100000);}++rows;
        }
        NH_NEED(rows && (!th_eq(&b,kind,"LATTICE_CART") || (rows==3 && th_cell(&b,cell))));
        NH_NEED(nh_add(f,s,&b,th_eq(&b,kind,"LATTICE_CART") ? "lattice-block":"positions-block",start,c.at-start));
    }
    NH_NEED(lattice && positions && atoms && blocks==2);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_castep_cell_init(xx_castep_cell *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CASTEP_CELL,"castep_cell"); } }
xx_castep_cell *xx_castep_cell_create(xx_io_device *d,int64_t b) { xx_castep_cell *r=(xx_castep_cell *)xx_mem_alloc(sizeof(*r)); if(r) xx_castep_cell_init(r,d,b); return r; }
void xx_castep_cell_destroy(xx_castep_cell *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_castep_cell_free(xx_castep_cell *r) { if(r) { xx_castep_cell_destroy(r); xx_mem_free(r); } }
bool xx_castep_cell_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_castep_cell_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

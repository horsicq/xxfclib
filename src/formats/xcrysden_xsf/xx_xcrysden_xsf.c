/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.xcrysden.org/doc/XSF.html */
#include "xxfclib/formats/xcrysden_xsf/xx_xcrysden_xsf.h"
#include "../xx_twelfth_root.h"
static bool tw_xsf_line(el_lines *c,el_token *v) {do {if(!el_line(c,v)) return false;*v=el_trim(c->b,*v);}while(!v->n || c->b->p[(size_t)v->at]=='#');return true;}
static bool tw_xsf_atom(nh_blob *b,el_token line) {el_token t[8];unsigned n;return el_split(b,line,t,8,&n,false) && (n==4 || n==7) && (tw_z(b,t[0]) || tw_element(b,t[0])) && tw_floats(b,t,1,n);}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[4];unsigned n;uint64_t atoms=0,i,at;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;NH_NEED(tw_xsf_line(&c,&line));
    if(el_eq(&b,line,"CRYSTAL")) {
        NH_NEED(tw_xsf_line(&c,&line) && el_eq(&b,line,"PRIMVEC"));
        for(i=0;i<3;++i) NH_NEED(tw_words(&c,&line,t,4,&n) && n==3 && tw_floats(&b,t,0,3) && !(el_zero(&b,t[0]) && el_zero(&b,t[1]) && el_zero(&b,t[2])));
        NH_NEED(tw_xsf_line(&c,&line) && el_eq(&b,line,"PRIMCOORD") && tw_words(&c,&line,t,4,&n) && n==2 && el_uint(&b,t[0],&atoms) && atoms && atoms<=100000 && el_eq(&b,t[1],"1"));
        NH_NEED(nh_add(f,s,&b,"cell-header",0,c.at));at=c.at;for(i=0;i<atoms;++i) NH_NEED(el_line(&c,&line) && tw_xsf_atom(&b,line));
        NH_NEED(nh_add(f,s,&b,"atoms",at,c.at-at));
    } else {
        NH_NEED(el_eq(&b,line,"ATOMS") && nh_add(f,s,&b,"molecule-header",0,c.at));at=c.at;
        while(c.at<b.n) {NH_NEED(el_line(&c,&line));if(!el_trim(&b,line).n) break;NH_NEED(++atoms<=100000 && tw_xsf_atom(&b,line));}
        NH_NEED(atoms && nh_add(f,s,&b,"atoms",at,c.at-at));
    }
    while(c.at<b.n) {NH_NEED(el_line(&c,&line));line=el_trim(&b,line);NH_NEED(!line.n || b.p[(size_t)line.at]=='#');}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_xcrysden_xsf_init(xx_xcrysden_xsf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XCRYSDEN_XSF,"xcrysden_xsf"); } }
xx_xcrysden_xsf *xx_xcrysden_xsf_create(xx_io_device *d,int64_t b) { xx_xcrysden_xsf *r=(xx_xcrysden_xsf *)xx_mem_alloc(sizeof(*r)); if(r) xx_xcrysden_xsf_init(r,d,b); return r; }
void xx_xcrysden_xsf_destroy(xx_xcrysden_xsf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xcrysden_xsf_free(xx_xcrysden_xsf *r) { if(r) { xx_xcrysden_xsf_destroy(r); xx_mem_free(r); } }
bool xx_xcrysden_xsf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xcrysden_xsf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

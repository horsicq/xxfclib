/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.ase-lib.org/_modules/ase/io/res.html */
#include "xxfclib/formats/shelx_res/xx_shelx_res.h"
#include "../xx_thirteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[128],species[118],*labels=NULL;unsigned nt,nsp,i,atoms=0,nlabels=0;uint64_t head,tail=0,index,budget=10000000;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    NH_NEED(th_words(&c,&line,t,128,&nt,"!") && nt>=1 && el_eq(&b,t[0],"TITL"));
    NH_NEED(th_words(&c,&line,t,128,&nt,"!") && nt==8 && el_eq(&b,t[0],"CELL") && th_floats(&b,t+1,7));
    for(i=1;i<=4;++i) { NH_NEED(th_positive(&b,t[i])); } for(i=5;i<8;++i) NH_NEED(tw_value(&b,t[i])>0 && tw_value(&b,t[i])<180);
    NH_NEED(th_words(&c,&line,t,128,&nt,"!") && nt==2 && el_eq(&b,t[0],"LATT") && el_eq(&b,t[1],"-1"));
    NH_NEED(th_words(&c,&line,t,128,&nt,"!") && nt>=2 && nt<=119 && el_eq(&b,t[0],"SFAC"));nsp=nt-1;
    for(i=0;i<nsp;++i) {unsigned j;NH_NEED(tw_element(&b,t[i+1]));for(j=0;j<i;++j) NH_NEED(!th_same(&b,species[j],t[i+1]));species[i]=t[i+1];}head=c.at;
    while(c.at<b.n) {uint64_t start=c.at;NH_NEED(th_words(&c,&line,t,128,&nt,"!"));if(el_eq(&b,t[0],"END")) {NH_NEED(nt==1);tail=start;break;}
        NH_NEED(nt==6 && ++atoms<=100000 && t[0].n<=8 && el_chars(&b,t[0],"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_",true) && el_uint(&b,t[1],&index) && index && index<=nsp && th_floats(&b,t+2,4) && tw_value(&b,t[5])>0 && tw_value(&b,t[5])<=1);
        /* ASE/AIRSS labels plain element rows by element; named sites are unique. */
        if(!tw_element(&b,t[0])) {unsigned j;NH_NEED(nlabels<4096);if(!labels) {labels=(el_token *)xx_mem_alloc(4096*sizeof(el_token));NH_NEED(labels);}
            for(j=0;j<nlabels;++j) {uint64_t k;if(labels[j].n!=t[0].n) continue;for(k=0;k<t[0].n;++k) {uint8_t a=b.p[(size_t)(labels[j].at+k)],z=b.p[(size_t)(t[0].at+k)];NH_NEED(budget);--budget;if(a>='a' && a<='z') a-=32;if(z>='a' && z<='z') z-=32;if(a!=z) break;}NH_NEED(k!=t[0].n);}labels[nlabels++]=t[0];}
    }
    NH_NEED(atoms && tail && tw_trailing(&c));NH_NEED(nh_add(f,s,&b,"structure-header",0,head) && nh_add(f,s,&b,"fractional-atoms",head,tail-head) && nh_add(f,s,&b,"structure-end",tail,b.n-tail));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(labels);xx_mem_free(b.p);return ok;
}

void xx_shelx_res_init(xx_shelx_res *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SHELX_RES,"shelx_res"); } }
xx_shelx_res *xx_shelx_res_create(xx_io_device *d,int64_t b) { xx_shelx_res *r=(xx_shelx_res *)xx_mem_alloc(sizeof(*r)); if(r) xx_shelx_res_init(r,d,b); return r; }
void xx_shelx_res_destroy(xx_shelx_res *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_shelx_res_free(xx_shelx_res *r) { if(r) { xx_shelx_res_destroy(r); xx_mem_free(r); } }
bool xx_shelx_res_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_shelx_res_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

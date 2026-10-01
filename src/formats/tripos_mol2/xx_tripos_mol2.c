/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/openbabel/openbabel/blob/master/src/formats/mol2format.cpp */
#include "xxfclib/formats/tripos_mol2/xx_tripos_mol2.h"
#include "../xx_twelfth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[16];unsigned n;uint64_t atoms,bonds,subs=0,i,id,a,z,at;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;NH_NEED(el_line(&c,&line) && el_eq(&b,line,"@<TRIPOS>MOLECULE") && el_line(&c,&line) && line.n && tw_words(&c,&line,t,16,&n) && n>=2 && n<=5 && el_uint(&b,t[0],&atoms) && atoms && atoms<=100000 && el_uint(&b,t[1],&bonds) && bonds<=100000);
    if(n>=3) NH_NEED(el_uint(&b,t[2],&subs) && subs<=atoms);for(i=3;i<n;++i) NH_NEED(el_eq(&b,t[i],"0"));
    NH_NEED(el_line(&c,&line) && el_eq(&b,el_trim(&b,line),"SMALL") && el_line(&c,&line) && (el_eq(&b,el_trim(&b,line),"NO_CHARGES") || el_eq(&b,el_trim(&b,line),"GASTEIGER") || el_eq(&b,el_trim(&b,line),"USER_CHARGES") || el_eq(&b,el_trim(&b,line),"AMBER") || el_eq(&b,el_trim(&b,line),"AM1BCC") || el_eq(&b,el_trim(&b,line),"MMFF94_CHARGES")));
    NH_NEED(el_line(&c,&line));while(!el_trim(&b,line).n) NH_NEED(el_line(&c,&line));NH_NEED(el_eq(&b,line,"@<TRIPOS>ATOM") && nh_add(f,s,&b,"molecule-header",0,c.at));at=c.at;
    for(i=1;i<=atoms;++i) {el_token e;uint64_t j=0;NH_NEED(tw_words(&c,&line,t,16,&n) && (n==6 || n==8 || n==9) && el_uint(&b,t[0],&id) && id==i && tw_name(&b,t[1]) && tw_floats(&b,t,2,5));e=t[5];while(j<e.n && b.p[(size_t)(e.at+j)]!='.') ++j;NH_NEED(tw_element(&b,el_slice(e,0,j)) && tw_name(&b,e));
        if(n>=8) NH_NEED(el_uint(&b,t[6],&id) && (subs ? id>=1:id<=atoms) && id<=(subs ? subs:atoms) && tw_name(&b,t[7]));if(n==9) NH_NEED(el_float(&b,t[8]));
    }
    NH_NEED(nh_add(f,s,&b,"atoms",at,c.at-at) && el_line(&c,&line) && el_eq(&b,line,"@<TRIPOS>BOND"));at=line.at;
    for(i=1;i<=bonds;++i) NH_NEED(tw_words(&c,&line,t,16,&n) && n==4 && el_uint(&b,t[0],&id) && id==i && el_uint(&b,t[1],&a) && a>=1 && a<=atoms && el_uint(&b,t[2],&z) && z>=1 && z<=atoms && z!=a && (el_eq(&b,t[3],"1") || el_eq(&b,t[3],"2") || el_eq(&b,t[3],"3") || el_eq(&b,t[3],"ar") || el_eq(&b,t[3],"am") || el_eq(&b,t[3],"du") || el_eq(&b,t[3],"un") || el_eq(&b,t[3],"nc")));
    NH_NEED(nh_add(f,s,&b,"bonds",at,c.at-at));
    if(subs) {NH_NEED(el_line(&c,&line) && el_eq(&b,line,"@<TRIPOS>SUBSTRUCTURE"));at=line.at;
        for(i=1;i<=subs;++i) {NH_NEED(tw_words(&c,&line,t,16,&n) && (n==3 || (n>=6 && n<=9)) && el_uint(&b,t[0],&id) && id==i && tw_name(&b,t[1]) && el_uint(&b,t[2],&a) && a>=1 && a<=atoms);
            if(n>=6) NH_NEED((el_eq(&b,t[3],"GROUP") || el_eq(&b,t[3],"RESIDUE") || el_eq(&b,t[3],"UNKNOWN")) && el_uint(&b,t[4],&id) && id<=65535 && tw_name(&b,t[5]));
            if(n>=7) NH_NEED(tw_name(&b,t[6]));if(n>=8) NH_NEED(el_uint(&b,t[7],&id) && id<=bonds);if(n==9) NH_NEED(el_eq(&b,t[8],"ROOT") || el_eq(&b,t[8],"LEAF"));
        }
        NH_NEED(nh_add(f,s,&b,"substructures",at,c.at-at));
    }
    NH_NEED(tw_trailing(&c));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_tripos_mol2_init(xx_tripos_mol2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRIPOS_MOL2,"tripos_mol2"); } }
xx_tripos_mol2 *xx_tripos_mol2_create(xx_io_device *d,int64_t b) { xx_tripos_mol2 *r=(xx_tripos_mol2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_tripos_mol2_init(r,d,b); return r; }
void xx_tripos_mol2_destroy(xx_tripos_mol2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tripos_mol2_free(xx_tripos_mol2 *r) { if(r) { xx_tripos_mol2_destroy(r); xx_mem_free(r); } }
bool xx_tripos_mol2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tripos_mol2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

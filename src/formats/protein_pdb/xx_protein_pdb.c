/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.wwpdb.org/documentation/file-format-content/format33/sect9.html */
#include "xxfclib/formats/protein_pdb/xx_protein_pdb.h"
#include "../xx_eleventh_data.h"
static bool pdb_metadata(nh_blob *b,el_token kind) {
    static const char *known[]={"HEADER","OBSLTE","TITLE","SPLT","CAVEAT","COMPND","SOURCE","KEYWDS","EXPDTA","NUMMDL","MDLTYP","AUTHOR","REVDAT","SPRSDE","JRNL","REMARK","DBREF","DBREF1","DBREF2","SEQADV","SEQRES","MODRES","HET","HETNAM","HETSYN","FORMUL","HELIX","SHEET","SSBOND","LINK","CISPEP","SITE","CRYST1","ORIGX1","ORIGX2","ORIGX3","SCALE1","SCALE2","SCALE3","MTRIX1","MTRIX2","MTRIX3","MASTER"};
    unsigned i;for(i=0;i<sizeof(known)/sizeof(known[0]);++i) if(el_eq(b,kind,known[i])) return true;return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,kind;uint8_t *serials=NULL;uint64_t atoms=0,body_start=0,end_start=0,last=0;bool ok=false,body=false,ended=false;
    NH_NEED(nh_load(f,&b,pd));serials=(uint8_t *)xx_mem_alloc(100000);NH_NEED(serials);xx_mem_zero(serials,100000);c.b=&b;
    while(c.at<b.n) {
        uint64_t start=c.at,n;unsigned i;NH_NEED(el_line(&c,&line) && line.n>=3 && line.n<=80);kind=el_trim(&b,el_slice(line,0,line.n>=6 ? 6:line.n));
        if(el_eq(&b,kind,"ATOM") || el_eq(&b,kind,"HETATM")) {
            NH_NEED(!ended && line.n>=78 && el_uint(&b,el_slice(line,6,5),&n) && n && n<=99999 && !serials[n]);serials[n]=1;last=n;++atoms;NH_NEED(atoms<=100000);
            NH_NEED(el_ident(&b,el_trim(&b,el_slice(line,12,4))) && el_chars(&b,el_trim(&b,el_slice(line,17,3)),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",true) && el_range(&b,el_slice(line,22,4),999,9999));
            for(i=0;i<3;++i) NH_NEED(el_float(&b,el_slice(line,30+8*i,8)));
            NH_NEED(el_float(&b,el_slice(line,54,6)) && el_float(&b,el_slice(line,60,6)) && el_chars(&b,el_trim(&b,el_slice(line,76,2)),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",false));
            {char text[8];const char *e;double occupancy;el_token v=el_trim(&b,el_slice(line,54,6));xx_rt_memcpy(text,b.p+(size_t)v.at,(size_t)v.n);text[v.n]=0;occupancy=xx_rt_strtod(text,&e);NH_NEED(occupancy>=0 && occupancy<=1);}
            if(!body) {body_start=start;body=true;}
        }else if(el_eq(&b,kind,"ANISOU")) {
            NH_NEED(body && line.n>=70 && el_uint(&b,el_slice(line,6,5),&n) && n==last);for(i=0;i<6;++i) NH_NEED(el_range(&b,el_slice(line,28+7*i,7),999999,9999999));
        }else if(el_eq(&b,kind,"TER")) {
            NH_NEED(body && line.n>=27 && el_uint(&b,el_slice(line,6,5),&n) && n && n<=99999 && el_ident(&b,el_trim(&b,el_slice(line,17,3))) && el_range(&b,el_slice(line,22,4),999,9999));
        }else if(el_eq(&b,kind,"CONECT")) {
            NH_NEED(body && line.n>=11);for(i=6;i+5<=line.n;i+=5) {el_token v=el_trim(&b,el_slice(line,i,5));if(v.n) NH_NEED(el_uint(&b,v,&n) && n && n<=99999 && serials[n]);}
        }else if(el_eq(&b,kind,"END")) {
            NH_NEED(body && el_eq(&b,el_trim(&b,line),"END") && c.at==b.n);end_start=start;ended=true;
        }else {
            NH_NEED(!ended && pdb_metadata(&b,kind));if(el_eq(&b,kind,"NUMMDL")) NH_NEED(line.n>=14 && el_uint(&b,el_slice(line,10,4),&n) && n==1);
        }
    }
    NH_NEED(atoms && ended);if(body_start) NH_NEED(nh_add(f,s,&b,"metadata",0,body_start));
    NH_NEED(nh_add(f,s,&b,"coordinates",body_start,end_start-body_start) && nh_add(f,s,&b,"end",end_start,b.n-end_start));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(serials);xx_mem_free(b.p);return ok;
}

void xx_protein_pdb_init(xx_protein_pdb *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PROTEIN_PDB,"protein_pdb"); } }
xx_protein_pdb *xx_protein_pdb_create(xx_io_device *d,int64_t b) { xx_protein_pdb *r=(xx_protein_pdb *)xx_mem_alloc(sizeof(*r)); if(r) xx_protein_pdb_init(r,d,b); return r; }
void xx_protein_pdb_destroy(xx_protein_pdb *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_protein_pdb_free(xx_protein_pdb *r) { if(r) { xx_protein_pdb_destroy(r); xx_mem_free(r); } }
bool xx_protein_pdb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_protein_pdb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/ParmEd/ParmEd/blob/master/parmed/amber/_amberparm.py */
#include "xxfclib/formats/amber_prmtop/xx_amber_prmtop.h"
#include "../common/xx_molecular_text.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};scientific_text_lines c={0};scientific_text_token line,names[256];unsigned flags=0;uint64_t budget=8388608,pointers[32]={0},np=0,counts[8]={0};bool seen[8]={false},ok=false;
    static const char *const core[]={"ATOM_NAME","CHARGE","MASS","ATOM_TYPE_INDEX","NUMBER_EXCLUDED_ATOMS","RESIDUE_LABEL","RESIDUE_POINTER","ATOMIC_NUMBER"};
    BLOB_NEED(blob_load(f,&b,pd));c.b=&b;BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_prefix(&b,line,"%VERSION  VERSION_STAMP = V0001.000") && blob_add(f,s,&b,"topology-version",0,c.at));
    while(c.at<b.n) {uint64_t begin=c.at,items=0,last_res=0;scientific_text_token name,format;unsigned width,perline,coreid=8;bool real=false,chars=false,ptr;
        BLOB_NEED(scientific_text_line(&c,&line) && scientific_text_prefix(&b,line,"%FLAG ") && flags<256);name=scientific_text_trim(&b,scientific_text_slice(line,6,line.n-6));BLOB_NEED(molecular_name(&b,name) && scientific_text_unique(&b,name,names,flags,&budget));names[flags++]=name;ptr=scientific_text_eq(&b,name,"POINTERS");
        for(coreid=0;coreid<8;++coreid) if(scientific_text_eq(&b,name,core[coreid])) break;
        BLOB_NEED(scientific_text_line(&c,&format));format=scientific_text_trim(&b,format);if(scientific_text_eq(&b,format,"%FORMAT(20a4)") || scientific_text_eq(&b,format,"%FORMAT(20A4)")) {width=4;perline=20;chars=true;}else if(scientific_text_eq(&b,format,"%FORMAT(1a80)") || scientific_text_eq(&b,format,"%FORMAT(1A80)")) {width=80;perline=1;chars=true;}else if(scientific_text_eq(&b,format,"%FORMAT(10I8)") || scientific_text_eq(&b,format,"%FORMAT(1I8)")) {width=8;perline=scientific_text_eq(&b,format,"%FORMAT(1I8)") ? 1:10;}else {BLOB_NEED(scientific_text_eq(&b,format,"%FORMAT(5E16.8)"));width=16;perline=5;real=true;}
        BLOB_NEED(!ptr || (!chars && !real));if(coreid<8) BLOB_NEED((coreid==0 || coreid==5) ? chars:(coreid==1 || coreid==2) ? real:(!chars && !real));
        while(c.at<b.n && b.p[(size_t)c.at]!='%') {uint64_t j;unsigned words;BLOB_NEED(scientific_text_line(&c,&line));if(!line.n) continue;BLOB_NEED(line.n%width==0 && line.n<=width*perline);words=(unsigned)(line.n/width);
            for(j=0;j<words;++j) {scientific_text_token v=scientific_text_trim(&b,scientific_text_slice(line,j*width,width));uint64_t u=0;if(!v.n) {uint64_t rest;for(rest=j+1;rest<words;++rest) BLOB_NEED(!scientific_text_trim(&b,scientific_text_slice(line,rest*width,width)).n);break;}
                BLOB_NEED(++items<=1000000);if(chars) BLOB_NEED(blob_ascii(b.p+(size_t)v.at,(size_t)v.n,false));else if(real) {BLOB_NEED(scientific_text_float(&b,v));if(coreid==2) BLOB_NEED(molecular_value(&b,v)>=0);}else {BLOB_NEED(scientific_text_integer(&b,v));if(ptr || coreid==3 || coreid==4 || coreid==6 || coreid==7) BLOB_NEED(scientific_text_uint(&b,v,&u));}
                if(ptr) {BLOB_NEED(np<32 && u<=INT32_MAX);pointers[np++]=u;}
                if(coreid==3) { BLOB_NEED(np>=31 && u>=1 && u<=pointers[1]); } if(coreid==4) BLOB_NEED(np>=31 && u<=pointers[0]);
                if(coreid==6) {BLOB_NEED(np>=31 && u>=1 && u<=pointers[0] && u>last_res && (items!=1 || u==1));last_res=u;}
                if(coreid==7) BLOB_NEED(u>=1 && u<=118);
            }
        }
        if(coreid<8) {seen[coreid]=true;counts[coreid]=items;}BLOB_NEED(blob_add(f,s,&b,"topology-section",begin,c.at-begin));
    }
    BLOB_NEED(np>=31 && pointers[0]>=1 && pointers[0]<=100000 && pointers[1]>=1 && pointers[1]<=100000 && pointers[11]>=1 && pointers[11]<=pointers[0]);
    {unsigned i;for(i=0;i<7;++i) BLOB_NEED(seen[i] && counts[i]==((i==5 || i==6) ? pointers[11]:pointers[0]));if(seen[7]) BLOB_NEED(counts[7]==pointers[0]);}
    s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_amber_prmtop_init(xx_amber_prmtop *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AMBER_PRMTOP,"amber_prmtop"); } }
xx_amber_prmtop *xx_amber_prmtop_create(xx_io_device *d,int64_t b) { xx_amber_prmtop *r=(xx_amber_prmtop *)xx_mem_alloc(sizeof(*r)); if(r) xx_amber_prmtop_init(r,d,b); return r; }
void xx_amber_prmtop_destroy(xx_amber_prmtop *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_amber_prmtop_free(xx_amber_prmtop *r) { if(r) { xx_amber_prmtop_destroy(r); xx_mem_free(r); } }
bool xx_amber_prmtop_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_amber_prmtop_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

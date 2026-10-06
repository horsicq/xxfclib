/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.lammps.org/dump.html */
#include "xxfclib/formats/lammps_dump/xx_lammps_dump.h"
#include "../xx_thirteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};el_lines c={0};el_token line,t[80],columns[64];uint8_t *ids=NULL;unsigned nt,ncol,j,frames=0,bounds,idcol,typecol,elementcol,mode;uint64_t count,i,id,type,begin,head,box,values,timestep,budget;bool ok=false;
    NH_NEED(nh_load(f,&b,pd));c.b=&b;
    while(c.at<b.n) {begin=c.at;if(!th_words(&c,&line,t,80,&nt,"#")) {NH_NEED(c.at==b.n);break;}
        NH_NEED(++frames<=1000 && nt==2 && el_eq(&b,t[0],"ITEM:") && el_eq(&b,t[1],"TIMESTEP"));
        NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt==1 && el_uint(&b,t[0],&timestep));
        NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt==4 && el_eq(&b,t[0],"ITEM:") && el_eq(&b,t[1],"NUMBER") && el_eq(&b,t[2],"OF") && el_eq(&b,t[3],"ATOMS"));
        NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt==1 && el_uint(&b,t[0],&count) && count && count<=1000000);head=c.at;
        NH_NEED(th_words(&c,&line,t,80,&nt,"#") && (nt==6 || nt==9) && el_eq(&b,t[0],"ITEM:") && el_eq(&b,t[1],"BOX") && el_eq(&b,t[2],"BOUNDS"));bounds=nt==9 ? 3:2;
        if(bounds==3) NH_NEED(el_eq(&b,t[3],"xy") && el_eq(&b,t[4],"xz") && el_eq(&b,t[5],"yz"));
        for(j=nt-3;j<nt;++j) NH_NEED(t[j].n==2 && el_chars(&b,t[j],"pfsm",true) && ((b.p[(size_t)t[j].at]=='p')==(b.p[(size_t)t[j].at+1]=='p')));
        for(j=0;j<3;++j) { NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt==bounds && th_floats(&b,t,bounds) && tw_value(&b,t[0])<tw_value(&b,t[1])); } box=c.at;
        NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt>=7 && nt<=66 && el_eq(&b,t[0],"ITEM:") && el_eq(&b,t[1],"ATOMS"));ncol=nt-2;idcol=typecol=elementcol=64;mode=0;budget=1000000;
        for(j=0;j<ncol;++j) {NH_NEED(tw_name(&b,t[j+2]) && el_unique(&b,t[j+2],columns,j,&budget));columns[j]=t[j+2];if(el_eq(&b,columns[j],"id")) idcol=j;if(el_eq(&b,columns[j],"type")) typecol=j;if(el_eq(&b,columns[j],"element")) elementcol=j;
            if(el_eq(&b,columns[j],"x")) { mode|=1; } if(el_eq(&b,columns[j],"y")) mode|=2;if(el_eq(&b,columns[j],"z")) mode|=4;
            if(el_eq(&b,columns[j],"xs")) { mode|=8; } if(el_eq(&b,columns[j],"ys")) mode|=16;if(el_eq(&b,columns[j],"zs")) mode|=32;
            if(el_eq(&b,columns[j],"xu")) { mode|=64; } if(el_eq(&b,columns[j],"yu")) mode|=128;if(el_eq(&b,columns[j],"zu")) mode|=256;}
        NH_NEED(idcol<ncol && typecol<ncol && (mode==7 || mode==56 || mode==448));values=c.at;ids=(uint8_t *)xx_mem_alloc((size_t)count);NH_NEED(ids);xx_rt_memset(ids,0,(size_t)count);
        for(i=0;i<count;++i) {NH_NEED(th_words(&c,&line,t,80,&nt,"#") && nt==ncol && el_uint(&b,t[idcol],&id) && id && id<=count && !ids[(size_t)id-1] && el_uint(&b,t[typecol],&type) && type && type<=100000);ids[(size_t)id-1]=1;
            for(j=0;j<ncol;++j) NH_NEED(j==elementcol ? tw_element(&b,t[j]):el_float(&b,t[j]));}
        xx_mem_free(ids);ids=NULL;
        NH_NEED(nh_add(f,s,&b,"frame-count-time",begin,head-begin) && nh_add(f,s,&b,"frame-bounds",head,box-head) && nh_add(f,s,&b,"frame-columns",box,values-box) && nh_add(f,s,&b,"frame-atoms",values,c.at-values));
    }NH_NEED(frames);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(ids);xx_mem_free(b.p);return ok;
}

void xx_lammps_dump_init(xx_lammps_dump *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LAMMPS_DUMP,"lammps_dump"); } }
xx_lammps_dump *xx_lammps_dump_create(xx_io_device *d,int64_t b) { xx_lammps_dump *r=(xx_lammps_dump *)xx_mem_alloc(sizeof(*r)); if(r) xx_lammps_dump_init(r,d,b); return r; }
void xx_lammps_dump_destroy(xx_lammps_dump *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lammps_dump_free(xx_lammps_dump *r) { if(r) { xx_lammps_dump_destroy(r); xx_mem_free(r); } }
bool xx_lammps_dump_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lammps_dump_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

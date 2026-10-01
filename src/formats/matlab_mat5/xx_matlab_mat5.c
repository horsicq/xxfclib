/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf */
#include "xxfclib/formats/matlab_mat5/xx_matlab_mat5.h"
#include "../xx_fifth_data.h"

typedef struct mt_tag { uint32_t type,size; uint64_t data,next; } mt_tag;
static bool mt_read(Abstractformat *f,uint64_t at,uint64_t end,bool be,mt_tag *t) { uint8_t b[8]; uint32_t first;
    if(!fd_range(at,8,end) || !pm_read(f,(int64_t)at,b,8)) return false; first=fd_u32(b,be);
    if(first>>16) { t->type=first&65535; t->size=first>>16; if(t->size>4) return false; t->data=at+4; t->next=at+8; }
    else { uint64_t padded; t->type=first; t->size=fd_u32(b+4,be); padded=((uint64_t)t->size+7)&~7ULL; if(!fd_range(at+8,padded,end)) return false; t->data=at+8; t->next=at+8+padded; }
    return t->type>=1 && t->type<=18 && t->type!=8 && t->type!=10 && t->type!=11;
}
static bool mt_utf8(Abstractformat *f,mt_tag *t,uint64_t elements,xx_pd_struct *pd) {
    uint8_t *text; uint32_t i; uint64_t count=0; bool result=false;
    if(t->size>1048576) return false; if(!t->size) return elements==0;
    text=(uint8_t *)xx_mem_alloc(t->size); if(!text) return false;
    if(pm_read(f,(int64_t)t->data,text,t->size) && fourth_utf8(text,t->size,pd)) {
        for(i=0;i<t->size;++i) { if(!(i&4095) && fd_stop(pd)) goto done; if((text[i]&0xc0)!=0x80) ++count; }
        result=count==elements;
    }
done: xx_mem_free(text); return result;
}
static bool mt_matrix(Abstractformat *f,mt_tag *outer,bool be,xx_pd_struct *pd) {
    mt_tag t; uint64_t at=outer->data,end=at+outer->size,elems=1,nbytes; uint32_t flags,cl,wanted; uint8_t b[8]; unsigned dims=0;
    if(!mt_read(f,at,end,be,&t) || t.type!=6 || t.size!=8 || !pm_read(f,(int64_t)t.data,b,8)) return false;
    flags=fd_u32(b,be); cl=flags&255; if(cl<4 || cl>15 || cl==5 || (flags&~0x0effU) || fd_u32(b+4,be)) return false;
    at=t.next; if(!mt_read(f,at,end,be,&t) || t.type!=5 || t.size<8 || (t.size&3) || t.size>128) return false;
    for(dims=0;dims<t.size/4;++dims) { uint32_t n; if(fd_stop(pd) || !pm_read(f,(int64_t)t.data+dims*4,b,4) || (n=fd_u32(b,be))>INT32_MAX || !fd_mul(elems,n,&elems)) return false; }
    at=t.next; if(!mt_read(f,at,end,be,&t) || t.type!=1 || !t.size || t.size>255) return false;
    { uint8_t name[255]; unsigned i; if(!pm_read(f,(int64_t)t.data,name,t.size)) return false; for(i=0;i<t.size;++i) if(!name[i] || name[i]<32) return false; }
    at=t.next; if(!mt_read(f,at,end,be,&t)) return false;
    wanted=cl==6 ? 9:cl==7 ? 7:cl==8 ? 1:cl==9 ? 2:cl==10 ? 3:cl==11 ? 4:cl==12 ? 5:cl==13 ? 6:cl==14 ? 12:13;
    if(cl==4) { if(t.type!=4 && t.type!=16 && t.type!=17 && t.type!=18) return false; }
    else if(t.type!=wanted) return false;
    { uint32_t width=t.type==1 || t.type==2 || t.type==16 ? 1:t.type==3 || t.type==4 || t.type==17 ? 2:t.type==5 || t.type==6 || t.type==7 || t.type==18 ? 4:8;
      if(cl==4 && t.type==16) { if(!mt_utf8(f,&t,elems,pd)) return false; }
      else if(!fd_mul(elems,width,&nbytes) || nbytes!=t.size) return false; }
    at=t.next;
    if(flags&0x800U) { mt_tag imag; if(cl==4 || !mt_read(f,at,end,be,&imag) || imag.type!=t.type || imag.size!=t.size) return false; at=imag.next; }
    return at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[128]; uint64_t at=128,end=(uint64_t)pm_available(f); bool be; unsigned count=0;
    if(!pm_read(f,0,h,128) || (xx_rt_memcmp(h,"MATLAB 5.0 MAT-file",19)) || ((be=(!xx_rt_memcmp(h+126,"MI",2))) ? fd_u16(h+124,true)!=256:xx_rt_memcmp(h+126,"IM",2) || fd_u16(h+124,false)!=256)) return false;
    for(count=116;count<124;++count) if(h[count]) return false;
    count=0; while(at<end) { mt_tag t; char name[48]; if(fd_stop(pd) || ++count>1024 || !mt_read(f,at,end,be,&t) || t.type!=14 || !mt_matrix(f,&t,be,pd)) return false;
        xx_rt_snprintf(name,sizeof(name),"matrix-%u.mat-element",count-1); if(!pm_add(f,s,name,(int64_t)at,(int64_t)(t.next-at))) return false; at=t.next;
    } s->size=(int64_t)at; return count>0;
}

void xx_matlab_mat5_init(xx_matlab_mat5 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MATLAB_MAT5,"matlab_mat5"); } }
xx_matlab_mat5 *xx_matlab_mat5_create(xx_io_device *d,int64_t b) { xx_matlab_mat5 *r=(xx_matlab_mat5 *)xx_mem_alloc(sizeof(*r)); if(r) xx_matlab_mat5_init(r,d,b); return r; }
void xx_matlab_mat5_destroy(xx_matlab_mat5 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_matlab_mat5_free(xx_matlab_mat5 *r) { if(r) { xx_matlab_mat5_destroy(r); xx_mem_free(r); } }
bool xx_matlab_mat5_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_matlab_mat5_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

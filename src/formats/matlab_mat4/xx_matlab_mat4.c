/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf */
#include "xxfclib/formats/matlab_mat4/xx_matlab_mat4.h"
#include "../xx_seventh_data.h"

static unsigned width(unsigned p) {static const unsigned w[]={8,4,4,2,2,1};return p<6?w[p]:0;}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t at=0;int64_t available=pm_available(f);unsigned index=0;uint8_t h[20],name[256];
    if(fd_stop(pd) || available<22 || available>67108864) return false;
    while(at<(uint64_t)available) {uint32_t type,rows,cols,imag,names;uint64_t count,n;unsigned w,i;bool be;char label[64];
        if(fd_stop(pd) || ++index>1024 || !fd_range(at,20,(uint64_t)available) || !pm_read(f,(int64_t)at,h,20)) return false;
        type=pm_le32(h);be=type>1999;if(be) type=pm_be32(h);
        if(type>1999 || type/1000!=(be?1U:0U) || (type/100)%10 || type%10>1 || !(w=width((type/10)%10))) return false;
        rows=fd_u32(h+4,be);cols=fd_u32(h+8,be);imag=fd_u32(h+12,be);names=fd_u32(h+16,be);
        if(!rows || rows>1000000 || !cols || cols>1000000 || imag>1 || (type%10==1 && imag) || names<2 || names>sizeof(name) || !fd_range(at+20,names,(uint64_t)available) || !pm_read(f,(int64_t)(at+20),name,names) || name[names-1]) return false;
        for(i=0;i<names-1;++i) if(!((name[i]>='a' && name[i]<='z') || (name[i]>='A' && name[i]<='Z') || name[i]=='_' || (i && name[i]>='0' && name[i]<='9'))) return false;
        if(!fd_mul(rows,cols,&count) || count>1000000 || !fd_mul(count,w,&n) || !fd_range(at+20+names,n*(1+imag),(uint64_t)available)) return false;
        xx_rt_snprintf(label,sizeof(label),"matrix-%u-header.bin",index-1);if(!pm_add(f,s,label,(int64_t)at,20+names)) return false;at+=20+names;
        xx_rt_snprintf(label,sizeof(label),"matrix-%u-real.bin",index-1);if(!pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;at+=n;
        if(imag) {xx_rt_snprintf(label,sizeof(label),"matrix-%u-imag.bin",index-1);if(!pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;at+=n;}
    }if(!index || at!=(uint64_t)available) return false;s->size=available;return true;
}

void xx_matlab_mat4_init(xx_matlab_mat4 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MATLAB_MAT4,"matlab_mat4"); } }
xx_matlab_mat4 *xx_matlab_mat4_create(xx_io_device *d,int64_t b) { xx_matlab_mat4 *r=(xx_matlab_mat4 *)xx_mem_alloc(sizeof(*r)); if(r) xx_matlab_mat4_init(r,d,b); return r; }
void xx_matlab_mat4_destroy(xx_matlab_mat4 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_matlab_mat4_free(xx_matlab_mat4 *r) { if(r) { xx_matlab_mat4_destroy(r); xx_mem_free(r); } }
bool xx_matlab_mat4_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_matlab_mat4_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

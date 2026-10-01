/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/LibRaw/LibRaw/master/internal/x3f_tools.h */
#include "xxfclib/formats/sigma_x3f/xx_sigma_x3f.h"
#include "../xx_eighth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[264],p[28];uint64_t total=(uint64_t)pm_available(f),directory,offsets[32],sizes[32],header;uint32_t version,count,i,j;bool image=false;char label[64];
    if(total>67108864 || total<56 || !pm_read(f,0,h,40) || xx_rt_memcmp(h,"FOVb",4) || (version=pm_le32(h+4))<0x20000 || version>0x20003 || !pm_le32(h+28) || !pm_le32(h+32) || pm_le32(h+36)>270 || pm_le32(h+36)%90) return false;
    header=version>=0x20003 ? 264:version>=0x20001 ? 232:40;
    if(!pm_read(f,0,h,(size_t)header) || (header>40 && !eh_float_array(f,header-128,128,4,false,pd)) || !pm_read(f,(int64_t)total-4,p,4) || (directory=pm_le32(p))<header || (directory&3) || !eh_span(directory,12,total-4) || !pm_read(f,(int64_t)directory,p,12) || xx_rt_memcmp(p,"SECd",4) || pm_le32(p+4)!=0x20000 || !(count=pm_le32(p+8)) || count>32 || directory+12+(uint64_t)count*12+4!=total || !pm_add(f,s,"x3f-header.bin",0,(int64_t)header)) return false;
    for(i=0;i<count;++i) {uint32_t type;if(!pm_read(f,(int64_t)(directory+12+(uint64_t)i*12),p,12)) return false;offsets[i]=pm_le32(p);sizes[i]=pm_le32(p+4);type=pm_le32(p+8);
        if(offsets[i]<header || (offsets[i]&3) || sizes[i]<24 || !eh_span(offsets[i],sizes[i],directory)) return false;for(j=0;j<i;++j) if(!eh_no_overlap(offsets[i],sizes[i],offsets[j],sizes[j])) return false;
        if(!pm_read(f,(int64_t)offsets[i],p,24) || pm_le32(p+4)<0x20000 || pm_le32(p+4)>0x20003) return false;
        if((type==0x47414d49U || type==0x32414d49U) && !xx_rt_memcmp(p,"SECi",4)) {uint32_t kind,width,height,stride;
            if(sizes[i]<29 || !pm_read(f,(int64_t)offsets[i],p,28) || !(width=pm_le32(p+16)) || !(height=pm_le32(p+20)) || width>1048576 || height>1048576) return false;
            if(pm_le32(p+8)>65535 || pm_le32(p+12)>65535) return false;kind=(pm_le32(p+8)<<16)|pm_le32(p+12);stride=pm_le32(p+24);
            if(kind==0x20003U) {uint64_t bytes;if((stride&3) || stride<(uint64_t)width*3 || !fd_mul(stride,height,&bytes) || bytes!=sizes[i]-28) return false;}
            else if(kind==0x20012U) {if(!eh_jpeg(f,offsets[i]+28,offsets[i]+sizes[i],pd)) return false;}
            else if(kind!=0x2000bU && kind!=0x30005U && kind!=0x30006U && kind!=0x3001eU && kind!=0x1001eU) return false;
            image=true;
        }else if(type==0x464d4143U && !xx_rt_memcmp(p,"SECc",4)) {
            /* Older-camera CAMF type2 metadata remains encoded. The EOF
             * directory bounds this complete header and cipher payload. */
            if(sizes[i]<=28 || pm_le32(p+8)!=2 || !pm_read(f,(int64_t)offsets[i],p,28)) return false;
        }else if(type==0x504f5250U && !xx_rt_memcmp(p,"SECp",4)) {uint32_t props=pm_le32(p+8);uint64_t pool;uint8_t pair[8];
            if(!props || props>1024 || pm_le32(p+12)!=0 || pm_le32(p+16) || !eh_span(24,(uint64_t)props*8,sizes[i]) || ((sizes[i]-24-(uint64_t)props*8)&1) || pm_le32(p+20)!=(sizes[i]-24-(uint64_t)props*8)/2) return false;pool=offsets[i]+24+(uint64_t)props*8;
            for(j=0;j<props;++j) {unsigned k;if(!pm_read(f,(int64_t)(offsets[i]+24+(uint64_t)j*8),pair,8)) return false;for(k=0;k<2;++k) {uint64_t text=pool+(uint64_t)pm_le32(pair+k*4)*2;if(!eh_utf16_end(f,text,offsets[i]+sizes[i],pd)) return false;}}
        }else return false;
        xx_rt_snprintf(label,sizeof(label),"section-%u.bin",i);if(!pm_add(f,s,label,(int64_t)offsets[i],(int64_t)sizes[i])) return false;
    }if(!image || !pm_add(f,s,"x3f-directory.bin",(int64_t)directory,(int64_t)(total-directory))) return false;s->size=(int64_t)total;return true;
}

void xx_sigma_x3f_init(xx_sigma_x3f *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SIGMA_X3F,"sigma_x3f"); } }
xx_sigma_x3f *xx_sigma_x3f_create(xx_io_device *d,int64_t b) { xx_sigma_x3f *r=(xx_sigma_x3f *)xx_mem_alloc(sizeof(*r)); if(r) xx_sigma_x3f_init(r,d,b); return r; }
void xx_sigma_x3f_destroy(xx_sigma_x3f *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sigma_x3f_free(xx_sigma_x3f *r) { if(r) { xx_sigma_x3f_destroy(r); xx_mem_free(r); } }
bool xx_sigma_x3f_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sigma_x3f_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

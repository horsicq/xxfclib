/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/freedesktop-unofficial-mirror/xorg__lib__libXcursor/master/src/file.c
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/xcursor/xx_xcursor.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[36],toc[12]; uint32_t head,count,i; int64_t starts[1024],ends[1024],end; unsigned images=0;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"Xcur",4) || (head=pm_le32(h+4))<16 || pm_le32(h+8)!=0x10000U || (count=pm_le32(h+12))==0 || count>1024 || head>(uint64_t)pm_available(f) || (uint64_t)count*12>(uint64_t)(pm_available(f)-head)) return false;
    end=head+(int64_t)count*12;
    for(i=0;i<count;++i) { uint32_t type,subtype,off,size,j; uint64_t bytes; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,head+(int64_t)i*12,toc,12)) { return false; } type=pm_le32(toc); subtype=pm_le32(toc+4); off=pm_le32(toc+8);
        if(off<head+(uint64_t)count*12 || !pm_read(f,off,h,16) || (size=pm_le32(h))<16 || pm_le32(h+4)!=type || pm_le32(h+8)!=subtype || pm_le32(h+12)!=1) return false;
        if(type==0xFFFD0002U) { uint32_t w,height;
            if(size<36 || !subtype || !pm_read(f,off+16,h+16,20) || !(w=pm_le32(h+16)) || !(height=pm_le32(h+20)) || w>32767 || height>32767 || pm_le32(h+24)>=w || pm_le32(h+28)>=height) return false;
            bytes=(uint64_t)w*height*4; ++images;
            xx_rt_snprintf(name,sizeof(name),"image-%u-descriptor.bin",i); if(!pm_add(f,s,name,off+16,size-16)) return false;
            xx_rt_snprintf(name,sizeof(name),"image-%u-argb.bin",i);
        } else if(type==0xFFFE0001U) { if(size<20 || subtype<1 || subtype>3 || !pm_read(f,off+16,h+16,4)) return false; bytes=pm_le32(h+16); if(bytes>1024U*1024U) return false; xx_rt_snprintf(name,sizeof(name),"comment-%u.txt",i); }
        else return false;
        if(bytes>(uint64_t)INT64_MAX || !pm_add(f,s,name,(int64_t)off+size,(int64_t)bytes)) { return false; } starts[i]=off; ends[i]=(int64_t)off+size+(int64_t)bytes;
        for(j=0;j<i;++j) if(starts[i]<ends[j] && ends[i]>starts[j]) return false;
        if(ends[i]>end) end=ends[i];
    } s->size=end; return images>0;
}

void xx_xcursor_init(xx_xcursor *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_XCURSOR,"xcursor"); } }
xx_xcursor *xx_xcursor_create(xx_io_device *d,int64_t b) { xx_xcursor *r=(xx_xcursor *)xx_mem_alloc(sizeof(*r)); if(r) xx_xcursor_init(r,d,b); return r; }
void xx_xcursor_destroy(xx_xcursor *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_xcursor_free(xx_xcursor *r) { if(r) { xx_xcursor_destroy(r); xx_mem_free(r); } }
bool xx_xcursor_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_xcursor_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

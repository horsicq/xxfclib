/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/DjVuLibre/djvulibre/master/libdjvu/IFFByteStream.cpp, https://raw.githubusercontent.com/DjVuLibre/djvulibre/master/libdjvu/DjVuInfo.cpp
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/djvu/xx_djvu.h"
#include "../xx_fifth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16],b[10]; uint64_t at=16,end; unsigned count=0; bool info=false,image=false;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"AT&TFORM",8) || xx_rt_memcmp(h+12,"DJVU",4) || pm_be32(h+8)<4) return false;
    end=12+(uint64_t)pm_be32(h+8); if(end>(uint64_t)pm_available(f) || !pm_add(f,s,"djvu-header.bin",0,16)) return false;
    while(at<end) { uint32_t size; uint64_t payload,next; char label[48]; unsigned i;
        if(fd_stop(pd) || ++count>4096 || !fd_range(at,8,end) || !pm_read(f,(int64_t)at,h,8)) return false; size=pm_be32(h+4); payload=at+8;
        if(!fd_range(payload,(uint64_t)size+(size&1),end)) return false; next=payload+size+(size&1);
        for(i=0;i<4;++i) if(h[i]<32 || h[i]>126) return false;
        if(!xx_rt_memcmp(h,"FORM",4)) return false;
        if(!xx_rt_memcmp(h,"INFO",4)) { uint64_t pixels;
            if(info || count!=1 || size!=10 || !pm_read(f,(int64_t)payload,b,10) || !pm_be16(b) || !pm_be16(b+2) || !fd_mul(pm_be16(b),pm_be16(b+2),&pixels) || pixels>67108864 || pm_le16(b+4)<20 || pm_le16(b+4)>26 || pm_le16(b+6)<25 || pm_le16(b+6)>6000 || b[8]<3 || b[8]>50 || (b[9]!=1 && b[9]!=2 && b[9]!=5 && b[9]!=6)) return false; info=true;
        } else { if(!info) return false;
            if(!xx_rt_memcmp(h,"Sjbz",4) || !xx_rt_memcmp(h,"BGjp",4) || !xx_rt_memcmp(h,"BG44",4)) { if(!size) return false; image=true; }
            if(!xx_rt_memcmp(h,"INCL",4)) { uint8_t name[1024]; if(!size || size>1024 || !pm_read(f,(int64_t)payload,name,size) || !fourth_utf8(name,size,pd)) return false; for(i=0;i<size;++i) if(!name[i]) return false; }
        }
        xx_rt_snprintf(label,sizeof(label),"chunk-%u-%c%c%c%c.bin",count-1,h[0],h[1],h[2],h[3]); if(!pm_add(f,s,label,(int64_t)payload,size)) return false; at=next;
    } s->size=(int64_t)end; return info && image && at==end;
}

void xx_djvu_init(xx_djvu *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DJVU,"djvu"); } }
xx_djvu *xx_djvu_create(xx_io_device *d,int64_t b) { xx_djvu *r=(xx_djvu *)xx_mem_alloc(sizeof(*r)); if(r) xx_djvu_init(r,d,b); return r; }
void xx_djvu_destroy(xx_djvu *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_djvu_free(xx_djvu *r) { if(r) { xx_djvu_destroy(r); xx_mem_free(r); } }
bool xx_djvu_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_djvu_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

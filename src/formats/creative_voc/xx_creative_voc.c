/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/voc.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/creative_voc/xx_creative_voc.h"
#include "../xx_fifth_data.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !fd_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[26],b[12]; uint32_t offset,version,kind,n,count=0,depth=0,frame=0; bool audio=false; uint64_t measured=0,start;
    fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
    if(!fd_get(&c,h,26) || xx_rt_memcmp(h,"Creative Voice File\x1a",20)) return false;
    offset=xx_data_get_u16(h+20, 2, 0, false); version=xx_data_get_u16(h+22, 2, 0, false);
    if(offset<26 || offset>4096 || (version!=0x10a && version!=0x114) || xx_data_get_u16(h+24, 2, 0, false)!=(uint16_t)(~version+0x1234U) || !sm_emit(f,s,"voc-descriptor.bin",0,offset,&measured)) { return false; } c.at=offset;
    for(;;) { char label[48]; start=c.at; if(++count>4096 || !fd_get(&c,b,1)) return false; kind=b[0];
        if(!kind) { if(depth || !audio || !sm_emit(f,s,"voc-terminator.bin",start,1,&measured)) return false; break; }
        if(!fd_get(&c,b,3)) { return false; } n=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16); if(!fd_range(c.at,n,c.end)) return false;
        if(kind==1) { if(n<3 || !fd_get(&c,b,2) || b[1]) return false; frame=1; audio=true; if(!fd_skip(&c,n-2)) return false; }
        else if(kind==2) { if(!frame || !n || n%frame || !fd_skip(&c,n)) return false; audio=true; }
        else if(kind==3) { if(n!=3 || !fd_get(&c,b,3)) return false; }
        else if(kind==4) { if(n!=2 || !fd_get(&c,b,2)) return false; }
        else if(kind==5) { uint32_t i; if(!n || n>4096) return false; for(i=0;i<n;++i) { if(!fd_get(&c,b,1) || (i==n-1 ? b[0]!=0 : b[0]==0)) return false; } }
        else if(kind==6) { if(n!=2 || !fd_get(&c,b,2) || xx_data_get_u16(b, 2, 0, false)==65535 || ++depth>32) return false; }
        else if(kind==7) { if(n || !depth) return false; --depth; }
        else if(kind==9) { uint32_t rate,bits,channels,codec; if(n<13 || !fd_get(&c,b,12)) return false; rate=xx_data_get_u32(b, 4, 0, false); bits=b[4]; channels=b[5]; codec=xx_data_get_u16(b+6, 2, 0, false);
            if(!rate || rate>384000 || !channels || channels>2 || !sm_zero(b+8,4) || !((bits==8 && codec==0) || (bits==16 && codec==4))) return false;
            frame=channels*(bits/8); if((n-12)%frame || !fd_skip(&c,n-12)) return false; audio=true;
        } else return false;
        xx_rt_snprintf(label,sizeof(label),"voc-block-%u-type-%u.bin",count-1,kind); if(!sm_emit(f,s,label,start,c.at-start,&measured)) return false;
    } s->size=(int64_t)measured; return true;
}

void xx_creative_voc_init(xx_creative_voc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CREATIVE_VOC,"voc"); } }
xx_creative_voc *xx_creative_voc_create(xx_io_device *d,int64_t b) { xx_creative_voc *r=(xx_creative_voc *)xx_mem_alloc(sizeof(*r)); if(r) xx_creative_voc_init(r,d,b); return r; }
void xx_creative_voc_destroy(xx_creative_voc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_creative_voc_free(xx_creative_voc *r) { if(r) { xx_creative_voc_destroy(r); xx_mem_free(r); } }
bool xx_creative_voc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_creative_voc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

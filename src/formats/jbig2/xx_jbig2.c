/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.itu.int/rec/T-REC-T.88-201808-I/en, https://raw.githubusercontent.com/ArtifexSoftware/jbig2dec/master/jbig2_segment.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/jbig2/xx_jbig2.h"
#include "../xx_fifth_data.h"

static bool sm_jb_type(unsigned type) { return type==0 || type==4 || type==6 || type==7 || type==16 || type==20 || type==22 || type==23 || type==36 || type==38 || type==39 || type==40 || type==42 || type==43 || type==48 || type==49 || type==50 || type==51 || type==52 || type==53 || type==62; }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[13],b[32]; uint32_t ids[4096],pages=0,completed=0,active=0,w=0,height=0,last=0; unsigned count=0; uint64_t at,end=(uint64_t)pm_available(f); bool unknown;
    if(!pm_read(f,0,h,9) || xx_rt_memcmp(h,"\x97JB2\r\n\x1a\n",8) || (h[8]&~3U) || !(h[8]&1)) return false;
    unknown=(h[8]&2)!=0; at=unknown ? 9:13;
    if(!unknown && (!pm_read(f,9,h+9,4) || !(pages=pm_be32(h+9)) || pages>4096)) return false;
    if(!pm_add(f,s,"jbig2-header.bin",0,(int64_t)at)) return false;
    while(at<end) { uint64_t begin=at,data,stop; uint32_t id,refs,page,length; unsigned type,width,j; uint8_t flags; char label[64];
        if(fd_stop(pd) || count>=4096 || !fd_range(at,6,end) || !pm_read(f,(int64_t)at,b,6)) return false;
        id=pm_be32(b); flags=b[4]; type=flags&63; refs=b[5]>>5;
        if(id==UINT32_MAX || (count && id<=last) || refs>4 || !sm_jb_type(type) || (b[5]&((uint8_t)(0x1fU & ~((1U<<(refs+1))-1U))))) return false;
        width=id<=256 ? 1:id<=65536 ? 2:4; at+=6;
        for(j=0;j<refs;++j) { uint32_t target; unsigned lo=0,hi=count;
            if(!fd_range(at,width,end) || !pm_read(f,(int64_t)at,b,width)) return false; target=width==1 ? b[0]:width==2 ? pm_be16(b):pm_be32(b); at+=width;
            while(lo<hi) { unsigned mid=lo+(hi-lo)/2; if(ids[mid]<target) lo=mid+1; else hi=mid; } if(lo==count || ids[lo]!=target) return false;
        }
        width=(flags&64) ? 4:1; if(!fd_range(at,width+4,end) || !pm_read(f,(int64_t)at,b,width+4)) return false;
        page=width==1 ? b[0]:pm_be32(b); length=pm_be32(b+width); data=at+width+4;
        if(length==UINT32_MAX || !fd_range(data,length,end)) return false; stop=data+length;
        if(type==48) { uint64_t pixels;
            if(active || !page || page>4096 || (!unknown && page>pages) || page!=completed+1 || refs || length!=19 || !pm_read(f,(int64_t)data,b,19)) return false;
            w=pm_be32(b); height=pm_be32(b+4);
            if(!w || !height || height==UINT32_MAX || !fd_mul(w,height,&pixels) || pixels>67108864 || (b[16]&0x80) || pm_be16(b+17)) return false; active=page;
        } else if(type==49) { if(!active || page!=active || length || refs) return false; active=0; ++completed; }
        else if(type==51) { if(page || length || refs || active || !completed || (!unknown && completed!=pages)) return false; }
        else { if(type==50 || (page && page!=active)) return false;
            if(type==4 || type==6 || type==7 || type==20 || type==22 || type==23 || type==36 || type==38 || type==39 || type==40 || type==42 || type==43) {
                uint32_t rw,rh,x,y; if(!active || page!=active || length<18 || !pm_read(f,(int64_t)data,b,17)) return false;
                rw=pm_be32(b); rh=pm_be32(b+4); x=pm_be32(b+8); y=pm_be32(b+12);
                if(!rw || !rh || x>w || y>height || rw>w-x || rh>height-y || b[16]>4) return false;
            } else if(type==62) { if(length<4) return false; }
            else if(!length) return false;
        }
        ids[count]=id; last=id; xx_rt_snprintf(label,sizeof(label),"segment-%u-type-%u.bin",id,type);
        if(!pm_add(f,s,label,(int64_t)begin,(int64_t)(stop-begin))) return false; ++count; at=stop;
        if(type==51) { s->size=(int64_t)at; return true; }
    } return false;
}

void xx_jbig2_init(xx_jbig2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_JBIG2,"jb2"); } }
xx_jbig2 *xx_jbig2_create(xx_io_device *d,int64_t b) { xx_jbig2 *r=(xx_jbig2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_jbig2_init(r,d,b); return r; }
void xx_jbig2_destroy(xx_jbig2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_jbig2_free(xx_jbig2 *r) { if(r) { xx_jbig2_destroy(r); xx_mem_free(r); } }
bool xx_jbig2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_jbig2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

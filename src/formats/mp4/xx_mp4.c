/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/axiomatic-systems/Bento4/master/Source/C%2B%2B/Core/Ap4Atom.cpp, https://raw.githubusercontent.com/axiomatic-systems/Bento4/master/Source/C%2B%2B/Core/Ap4AtomFactory.cpp
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/mp4/xx_mp4.h"
#include "../xx_payload_members.h"

static uint64_t mp_be64(const uint8_t *p) { return (uint64_t)pm_be32(p)<<32|pm_be32(p+4); }
static bool mp_tables(Abstractformat *f,const uint8_t *type,int64_t at,int64_t size) {
    uint8_t h[16]; uint32_t count,width=0;
    if(!xx_rt_memcmp(type,"stco",4)) width=4; else if(!xx_rt_memcmp(type,"co64",4)) width=8;
    else if(!xx_rt_memcmp(type,"stts",4) || !xx_rt_memcmp(type,"ctts",4)) width=8;
    else if(!xx_rt_memcmp(type,"stsc",4)) width=12; else if(!xx_rt_memcmp(type,"stss",4)) width=4;
    if(width) { if(size<8 || !pm_read(f,at,h,8)) return false; count=pm_be32(h+4); return (uint64_t)count*width==(uint64_t)size-8; }
    if(!xx_rt_memcmp(type,"stsz",4)) { if(size<12 || !pm_read(f,at,h,12)) return false; return pm_be32(h+4) ? size==12 : (uint64_t)pm_be32(h+8)*4==(uint64_t)size-12; }
    if(!xx_rt_memcmp(type,"stsd",4) || !xx_rt_memcmp(type,"dref",4)) {
        uint32_t i; int64_t pos=at+8;
        if(size<8 || !pm_read(f,at,h,8)) return false; count=pm_be32(h+4);
        if(count>65536) return false;
        for(i=0;i<count;++i) { uint32_t n; if(pos>at+size-8 || !pm_read(f,pos,h,8) || (n=pm_be32(h))<8 || n>(uint64_t)(at+size-pos)) return false; pos+=n; }
        return pos==at+size;
    } return true;
}
static bool mp_boxes(Abstractformat *f,pm_stream *s,int64_t at,int64_t end,unsigned depth,uint32_t parent,unsigned *moov,unsigned *mdat,xx_pd_struct *pd) {
    unsigned boxes=0,have=0; if(depth>24) return false;
    while(at<end) { uint8_t h[32]; uint64_t size; int64_t head=8,body; bool container; char name[40]; unsigned i;
        if((pd && xx_pd_is_stopped(pd)) || end-at<8 || !pm_read(f,at,h,8) || ++boxes>65536) return false;
        size=pm_be32(h);
        if(size==1) { if(end-at<16 || !pm_read(f,at+8,h+8,8)) return false; size=mp_be64(h+8); head=16; }
        if(!size) size=(uint64_t)(end-at);
        if(!xx_rt_memcmp(h+4,"uuid",4)) head+=16;
        if(size<(uint64_t)head || size>(uint64_t)(end-at)) return false; body=at+head;
        if(!depth && !xx_rt_memcmp(h+4,"moov",4)) ++*moov;
        if(!depth && !xx_rt_memcmp(h+4,"mdat",4)) { if(size==(uint64_t)head) return false; ++*mdat; }
        container=!xx_rt_memcmp(h+4,"moov",4) || !xx_rt_memcmp(h+4,"trak",4) || !xx_rt_memcmp(h+4,"mdia",4) || !xx_rt_memcmp(h+4,"minf",4) || !xx_rt_memcmp(h+4,"stbl",4) || !xx_rt_memcmp(h+4,"edts",4) || !xx_rt_memcmp(h+4,"dinf",4) || !xx_rt_memcmp(h+4,"mvex",4) || !xx_rt_memcmp(h+4,"moof",4) || !xx_rt_memcmp(h+4,"traf",4) || !xx_rt_memcmp(h+4,"mfra",4) || !xx_rt_memcmp(h+4,"udta",4);
        if(!xx_rt_memcmp(h+4,"mvhd",4)) { uint8_t version; if(have&1 || size-head<1 || !pm_read(f,body,&version,1) || version>1 || size-head!=(version ? 112U : 100U)) return false; have|=1; }
        if(!xx_rt_memcmp(h+4,"trak",4)) have|=2;
        if(!xx_rt_memcmp(h+4,"tkhd",4)) { uint8_t version; if(have&4 || size-head<1 || !pm_read(f,body,&version,1) || version>1 || size-head!=(version ? 96U : 84U)) return false; have|=4; }
        if(!xx_rt_memcmp(h+4,"mdia",4)) { if(have&8) return false; have|=8; }
        if(!xx_rt_memcmp(h+4,"mdhd",4)) { uint8_t version; if(have&16 || size-head<1 || !pm_read(f,body,&version,1) || version>1 || size-head!=(version ? 36U : 24U)) return false; have|=16; }
        if(!xx_rt_memcmp(h+4,"hdlr",4)) { if(have&32 || size-head<24) return false; have|=32; }
        if(!xx_rt_memcmp(h+4,"minf",4)) { if(have&64) return false; have|=64; }
        if(!xx_rt_memcmp(h+4,"stbl",4)) { if(have&128) return false; have|=128; }
        if(!xx_rt_memcmp(h+4,"stsd",4)) { if(have&256) return false; have|=256; }
        if(container) { if(!mp_boxes(f,s,body,at+(int64_t)size,depth+1,pm_be32(h+4),moov,mdat,pd)) return false; }
        else { if(!mp_tables(f,h+4,body,(int64_t)size-head)) return false;
            for(i=0;i<4;++i) name[i]=h[4+i]>=32 && h[4+i]<=126 ? (char)h[4+i] : '_'; name[4]=0;
            xx_rt_memcpy(name+4,".bin",5); if(!pm_add(f,s,name,body,(int64_t)size-head)) return false;
        }
        at+=(int64_t)size;
    }
    if((parent==0x6D6F6F76U && (have&3)!=3) || (parent==0x7472616BU && (have&12)!=12) || (parent==0x6D646961U && (have&112)!=112) || (parent==0x6D696E66U && !(have&128)) || (parent==0x7374626CU && !(have&256))) return false;
    return at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16]; int64_t size=pm_available(f); uint32_t n; unsigned moov=0,mdat=0;
    if(size<24 || !pm_read(f,0,h,16) || xx_rt_memcmp(h+4,"ftyp",4) || (n=pm_be32(h))<16 || n>(uint64_t)size || ((n-16)&3)) return false;
    if(!mp_boxes(f,s,0,size,0,0,&moov,&mdat,pd) || moov!=1 || !mdat) return false;
    s->size=size; return true;
}

void xx_mp4_init(xx_mp4 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MP4,"mp4"); } }
xx_mp4 *xx_mp4_create(xx_io_device *d,int64_t b) { xx_mp4 *r=(xx_mp4 *)xx_mem_alloc(sizeof(*r)); if(r) xx_mp4_init(r,d,b); return r; }
void xx_mp4_destroy(xx_mp4 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mp4_free(xx_mp4 *r) { if(r) { xx_mp4_destroy(r); xx_mem_free(r); } }
bool xx_mp4_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mp4_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

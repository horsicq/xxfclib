/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.color.org/specification/ICC.1-2022-05.pdf
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/icc/xx_icc.h"
#include "../xx_payload_members.h"

static bool ic_type(Abstractformat *f,int64_t at,uint32_t size) {
    uint8_t h[28]; uint32_t count,i; if(size<8 || !pm_read(f,at,h,size<28 ? size : 28) || pm_be32(h+4)) return false;
    if(!xx_rt_memcmp(h,"XYZ ",4)) return size>=20 && (size-8)%12==0;
    if(!xx_rt_memcmp(h,"curv",4)) { if(size<12) return false; return (uint64_t)pm_be32(h+8)*2==size-12; }
    if(!xx_rt_memcmp(h,"text",4)) { uint8_t b; return size>8 && pm_read(f,at+size-1,&b,1) && !b; }
    if(!xx_rt_memcmp(h,"mluc",4)) { if(size<16 || (count=pm_be32(h+8))>1024 || pm_be32(h+12)!=12 || (uint64_t)count*12>size-16) return false;
        for(i=0;i<count;++i) { uint32_t bytes,off; if(!pm_read(f,at+16+(int64_t)i*12,h,12)) return false; bytes=pm_be32(h+4); off=pm_be32(h+8); if((bytes&1) || off<16+count*12U || off>size || bytes>size-off) return false; }
    } else if(!xx_rt_memcmp(h,"desc",4)) { uint32_t n; if(size<12 || !(n=pm_be32(h+8)) || n>size-12) return false; }
    return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[132],t[12]; uint32_t size,count,i,offsets[1024],sizes[1024],tags[1024],common=0; uint16_t date[6];
    if(!pm_read(f,0,h,132) || (size=pm_be32(h))<144 || size>(uint64_t)pm_available(f) || xx_rt_memcmp(h+36,"acsp",4) || (h[8]!=2 && h[8]!=4) || h[10] || h[11] || pm_be32(h+64)>3 || (count=pm_be32(h+128))==0 || count>1024 || 132+(uint64_t)count*12>size) return false;
    if(xx_rt_memcmp(h+12,"scnr",4) && xx_rt_memcmp(h+12,"mntr",4) && xx_rt_memcmp(h+12,"prtr",4) && xx_rt_memcmp(h+12,"link",4) && xx_rt_memcmp(h+12,"spac",4) && xx_rt_memcmp(h+12,"abst",4) && xx_rt_memcmp(h+12,"nmcl",4)) return false;
    for(i=0;i<6;++i) { date[i]=pm_be16(h+24+i*2); } if(!date[0] || date[1]<1 || date[1]>12 || date[2]<1 || date[2]>31 || date[3]>23 || date[4]>59 || date[5]>59) return false;
    for(i=100;i<128;++i) if(h[i]) return false;
    for(i=0;i<count;++i) { uint32_t j,tag,at,n; char name[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,132+(int64_t)i*12,t,12)) { return false; } tag=pm_be32(t); at=pm_be32(t+4); n=pm_be32(t+8);
        if((at&3) || at<132+count*12U || at>size || n>size-at || !ic_type(f,at,n)) return false;
        for(j=0;j<i;++j) if(tag==tags[j] || (at<offsets[j]+sizes[j] && at+n>offsets[j] && (at!=offsets[j] || n!=sizes[j]))) return false;
        tags[i]=tag; offsets[i]=at; sizes[i]=n;
        if(tag==0x64657363U) { common|=1; } if(tag==0x63707274U) common|=2;
        xx_rt_snprintf(name,sizeof(name),"tag-%08X.bin",tag); if(!pm_add(f,s,name,at,n)) return false;
    } s->size=size; return common==3;
}

void xx_icc_init(xx_icc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ICC,"icc"); } }
xx_icc *xx_icc_create(xx_io_device *d,int64_t b) { xx_icc *r=(xx_icc *)xx_mem_alloc(sizeof(*r)); if(r) xx_icc_init(r,d,b); return r; }
void xx_icc_destroy(xx_icc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_icc_free(xx_icc *r) { if(r) { xx_icc_destroy(r); xx_mem_free(r); } }
bool xx_icc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_icc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

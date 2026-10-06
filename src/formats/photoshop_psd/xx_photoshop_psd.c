/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/photoshop_psd/xx_photoshop_psd.h"
#include "../xx_payload_members.h"

static uint64_t ps_be64(const uint8_t *p) { return (uint64_t)pm_be32(p)<<32|pm_be32(p+4); }
static bool ps_length(Abstractformat *f,int64_t *at,int64_t end,bool wide,uint64_t *value) {
    uint8_t b[8]; unsigned n=wide ? 8U : 4U; if(n>(uint64_t)(end-*at) || !pm_read(f,*at,b,n)) return false; *value=wide ? ps_be64(b) : pm_be32(b); *at+=n; return *value<=(uint64_t)(end-*at);
}
static bool ps_layers(Abstractformat *f,int64_t start,int64_t end,bool large) {
    uint64_t info; int64_t at=start,info_end; uint8_t h[32]; uint16_t rawcount; unsigned count,i; uint64_t channels_size=0;
    if(start==end) return true;
    if(!ps_length(f,&at,end,large,&info)) { return false; } info_end=at+(int64_t)info;
    if(info) {
        if(info<2 || !pm_read(f,at,h,2)) { return false; } rawcount=pm_be16(h); count=rawcount&0x8000 ? 65536U-rawcount : rawcount; at+=2;
        if(count>8192) return false;
        for(i=0;i<count;++i) { unsigned n,j; uint64_t extra; int64_t extra_end;
            if(info_end-at<18 || !pm_read(f,at,h,18) || (int32_t)pm_be32(h)>(int32_t)pm_be32(h+8) || (int32_t)pm_be32(h+4)>(int32_t)pm_be32(h+12) || (n=pm_be16(h+16))>56) { return false; } at+=18;
            for(j=0;j<n;++j) { uint64_t bytes; unsigned width=large ? 10U : 6U; if(width>(uint64_t)(info_end-at) || !pm_read(f,at,h,width)) return false; bytes=large ? ps_be64(h+2) : pm_be32(h+2); if(bytes<2 || bytes>UINT64_MAX-channels_size) return false; channels_size+=bytes; at+=width; }
            if(info_end-at<16 || !pm_read(f,at,h,16) || xx_rt_memcmp(h,"8BIM",4) || h[11]) { return false; } extra=pm_be32(h+12); at+=16;
            if(extra>(uint64_t)(info_end-at)) { return false; } extra_end=at+(int64_t)extra;
            for(j=0;j<2;++j) { uint64_t nbytes; if(!ps_length(f,&at,extra_end,false,&nbytes)) return false; at+=(int64_t)nbytes; }
            if(at>=extra_end || !pm_read(f,at,h,1) || ((uint32_t)h[0]+4U)/4U*4U>(uint64_t)(extra_end-at)) return false;
            at=extra_end;
        }
        if(channels_size>(uint64_t)(info_end-at) || (uint64_t)(info_end-at)-channels_size>1) return false;
    }
    at=info_end;
    if(end-at<4) return false;
    { uint64_t mask; if(!ps_length(f,&at,end,false,&mask)) return false; at+=(int64_t)mask; }
    /* Additional information consists of keyed, length-delimited blocks. */
    while(at<end) { uint64_t n; bool wide; int64_t data;
        if(end-at<12 || !pm_read(f,at,h,8) || (xx_rt_memcmp(h,"8BIM",4) && xx_rt_memcmp(h,"8B64",4))) return false;
        wide=!xx_rt_memcmp(h,"8B64",4); data=at+8; if(!ps_length(f,&data,end,wide,&n)) return false;
        if(n+(n&1U)>(uint64_t)(end-data)) { return false; } at=data+(int64_t)n+(int64_t)(n&1U);
    } return at==end;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32]; bool large; unsigned channels,depth,mode,i; uint32_t height,width; int64_t limit=pm_available(f),at=26; uint64_t n,raw; uint16_t compression;
    if(!pm_read(f,0,h,26) || xx_rt_memcmp(h,"8BPS",4) || (pm_be16(h+4)!=1 && pm_be16(h+4)!=2)) { return false; } large=pm_be16(h+4)==2;
    for(i=6;i<12;++i) if(h[i]) return false;
    channels=pm_be16(h+12); height=pm_be32(h+14); width=pm_be32(h+18); depth=pm_be16(h+22); mode=pm_be16(h+24);
    if(!channels || channels>56 || !height || !width || height>(large ? 300000U : 30000U) || width>(large ? 300000U : 30000U) || (depth!=1 && depth!=8 && depth!=16 && depth!=32) || (mode>4 && mode!=7 && mode!=8 && mode!=9)) return false;
    if(!ps_length(f,&at,limit,false,&n) || (mode==2 && n!=768) || (mode!=2 && mode!=8 && n)) return false;
    if(n && !pm_add(f,s,"color-mode.bin",at,(int64_t)n)) { return false; } at+=(int64_t)n;
    if(!ps_length(f,&at,limit,false,&n)) return false;
    { int64_t end=at+(int64_t)n; while(at<end) { uint32_t bytes; int64_t data; uint8_t len; char name[40];
        if((pd && xx_pd_is_stopped(pd)) || end-at<7 || !pm_read(f,at,h,7) || xx_rt_memcmp(h,"8BIM",4)) return false;
        len=h[6]; data=at+6+((uint32_t)len+2U)/2U*2U;
        xx_rt_snprintf(name,sizeof(name),"resource-%u.bin",pm_be16(h+4));
        if(end-data<4 || !pm_read(f,data,h,4)) { return false; } bytes=pm_be32(h); data+=4;
        if((uint64_t)bytes+(bytes&1U)>(uint64_t)(end-data) || !pm_add(f,s,name,data,bytes)) { return false; } at=data+bytes+(bytes&1U);
    }}
    if(!ps_length(f,&at,limit,large,&n) || !ps_layers(f,at,at+(int64_t)n,large)) return false;
    if(n && !pm_add(f,s,"layers-and-masks.bin",at,(int64_t)n)) { return false; } at+=(int64_t)n;
    if(limit-at<2 || !pm_read(f,at,h,2) || (compression=pm_be16(h))>3) { return false; } at+=2;
    raw=((uint64_t)width*depth+7)/8*height*channels;
    if(!compression) { if(raw>(uint64_t)(limit-at) || !pm_add(f,s,"merged-raw.bin",at,(int64_t)raw)) return false; at+=(int64_t)raw; }
    else if(compression==1) { uint64_t rows=(uint64_t)height*channels,sum=0,j; unsigned size=large ? 4U : 2U; int64_t table=at;
        if(rows*size>(uint64_t)(limit-at)) { return false; } at+=(int64_t)(rows*size);
        for(j=0;j<rows;++j) { uint32_t bytes; if(!pm_read(f,table+(int64_t)(j*size),h,size)) return false; bytes=large ? pm_be32(h) : pm_be16(h); sum+=bytes; }
        if(sum>(uint64_t)(limit-at) || !pm_add(f,s,"merged-rle-row-lengths.bin",table,(int64_t)(rows*size)) || !pm_add(f,s,"merged-rle.bin",at,(int64_t)sum)) { return false; } at+=(int64_t)sum;
    } else { if(limit-at<2 || !pm_read(f,at,h,2) || (h[0]&15)!=8 || (h[0]>>4)>7 || (((unsigned)h[0]<<8)|h[1])%31 || (h[1]&32) || !pm_add(f,s,compression==2 ? "merged-zip.bin" : "merged-zip-predicted.bin",at,limit-at)) return false; at=limit; }
    s->size=at; return true;
}

void xx_photoshop_psd_init(xx_photoshop_psd *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PHOTOSHOP_PSD,"photoshop_psd"); } }
xx_photoshop_psd *xx_photoshop_psd_create(xx_io_device *d,int64_t b) { xx_photoshop_psd *r=(xx_photoshop_psd *)xx_mem_alloc(sizeof(*r)); if(r) xx_photoshop_psd_init(r,d,b); return r; }
void xx_photoshop_psd_destroy(xx_photoshop_psd *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_photoshop_psd_free(xx_photoshop_psd *r) { if(r) { xx_photoshop_psd_destroy(r); xx_mem_free(r); } }
bool xx_photoshop_psd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_photoshop_psd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

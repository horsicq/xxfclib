/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html
 * KTX1 endian-aware key/value data and encoded mip levels; no pixel decoding or byte-swapping. Array/volume mip data stays grouped.
 */
#include "xxfclib/formats/ktx/xx_ktx.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[64],b[4],c; bool be; uint32_t faces,layers,levels,meta,i; int64_t at,metaend;
    if(!pm_read(f,0,h,64) || xx_rt_memcmp(h,"\xabKTX 11\xbb\r\n\x1a\n",12)) return false;
    be=pm_be32(h+12)==0x04030201; if(!be && pm_le32(h+12)!=0x04030201) return false;
    faces=r32(h+52,be); layers=r32(h+48,be); levels=r32(h+56,be); meta=r32(h+60,be);
    if(!r32(h+36,be) || r32(h+36,be)>16384 || r32(h+40,be)>16384 || r32(h+44,be)>16384 ||
       (faces!=1 && faces!=6) || layers>4096 || levels>32 || (meta&3) || meta>16U*1024U*1024U ||
       (r32(h+20,be)!=1 && r32(h+20,be)!=2 && r32(h+20,be)!=4) || !r32(h+28,be) || !r32(h+32,be)) return false;
    if((r32(h+16,be)==0)!=(r32(h+24,be)==0) || (!r32(h+16,be) && r32(h+20,be)!=1)) return false;
    at=64; metaend=at+meta; if(metaend>pm_available(f)) return false;
    while(at<metaend) {
        uint32_t n,j; bool ended=false;
        if(!pm_read(f,at,b,4) || (n=r32(b,be))==0 || n>metaend-at-4) return false;
        for(j=0;j<n;++j) { if(!pm_read(f,at+4+j,&c,1)) return false; if(!c) { ended=true; break; } }
        if(!ended || at+4+((n+3ULL)&~3ULL)>(uint64_t)metaend) return false;
        if(!pm_add(f,s,"keyvalue.bin",at+4,n)) { return false; } at+=4+((n+3ULL)&~3ULL);
    }
    if(!levels) levels=1;
    for(i=0;i<levels;++i) {
        uint32_t n,j,parts=faces==6 && !layers ? 6 : 1;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,b,4) || (n=r32(b,be))==0) { return false; } at+=4;
        for(j=0;j<parts;++j) {
            char label[40]; xx_rt_snprintf(label,sizeof(label),"mip-%u-face-%u.bin",(unsigned)i,(unsigned)j);
            if(!pm_add(f,s,label,at,n)) { return false; } at+=(n+3ULL)&~3ULL;
        }
        if(at>pm_available(f)) return false;
    }
    s->size=at; return true;
}

void xx_ktx_init(xx_ktx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_KTX,"ktx"); } }
xx_ktx *xx_ktx_create(xx_io_device *d,int64_t b) { xx_ktx *r=(xx_ktx *)xx_mem_alloc(sizeof(*r)); if(r) xx_ktx_init(r,d,b); return r; }
void xx_ktx_destroy(xx_ktx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ktx_free(xx_ktx *r) { if(r) { xx_ktx_destroy(r); xx_mem_free(r); } }
bool xx_ktx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ktx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
 * KTX2 non-supercompressed descriptor, metadata and encoded mip levels. BasisLZ/Zstandard/zlib supercompression is rejected; no pixel decoding.
 */
#include "xxfclib/formats/ktx2/xx_ktx2.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[80],e[24],b[12]; uint32_t levels,i,dfd,dfdsize,kvd,kvdsize; uint64_t end,table;
    if(!pm_read(f,0,h,80) || xx_rt_memcmp(h,"\xabKTX 20\xbb\r\n\x1a\n",12) || pm_le32(h+44) ||
       (pm_le32(h+16)!=1 && pm_le32(h+16)!=2 && pm_le32(h+16)!=4 && pm_le32(h+16)!=8) ||
       !pm_le32(h+20) || pm_le32(h+20)>16384 || pm_le32(h+24)>16384 || pm_le32(h+28)>16384 || pm_le32(h+32)>4096 ||
       (pm_le32(h+36)!=1 && pm_le32(h+36)!=6) || r64(h+64,false) || r64(h+72,false)) return false;
    levels=pm_le32(h+40); if(!levels) levels=1; if(levels>32) return false; table=80+(uint64_t)levels*24;
    dfd=pm_le32(h+48); dfdsize=pm_le32(h+52); kvd=pm_le32(h+56); kvdsize=pm_le32(h+60);
    if(dfd<table || (dfd&3) || dfdsize<28 || dfd>(uint64_t)pm_available(f) || dfdsize>(uint64_t)pm_available(f)-dfd ||
       !pm_read(f,dfd,b,4) || pm_le32(b)!=dfdsize || !pm_add(f,s,"data-format-descriptor.bin",dfd,dfdsize)) return false;
    end=(uint64_t)dfd+dfdsize;
    {
        uint64_t at=(uint64_t)dfd+4;
        while(at<end) {
            uint32_t n;
            if(end-at<24 || !pm_read(f,(int64_t)at,b,8) || (n=pm_le16(b+6))<24 || (n&3) || n>end-at) return false;
            at+=n;
        }
        if(at!=end) return false;
    }
    if(kvdsize) {
        uint64_t at=kvd,kend=(uint64_t)kvd+kvdsize;
        if(kvd<end || (kvd&3) || (kvdsize&3) || !pm_add(f,s,"keyvalues.bin",kvd,kvdsize)) return false;
        while(at<kend) {
            uint32_t n,j; uint8_t c; bool ended=false;
            if(kend-at<4 || !pm_read(f,(int64_t)at,b,4) || !(n=pm_le32(b)) || n>kend-at-4 || 4+((n+3ULL)&~3ULL)>kend-at) return false;
            for(j=0;j<n;++j) { if(!pm_read(f,(int64_t)at+4+j,&c,1)) return false; if(!c) { ended=true; break; } }
            if(!ended) { return false; } at+=4+((n+3ULL)&~3ULL);
        }
        end=(uint64_t)kvd+kvdsize;
    } else if(kvd) return false;
    for(i=0;i<levels;++i) {
        uint64_t off,size; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,80+(int64_t)i*24,e,24)) return false;
        off=r64(e,false); size=r64(e+8,false);
        if(!size || size!=r64(e+16,false) || off<(uint64_t)dfd+dfdsize ||
           (kvdsize && off<(uint64_t)kvd+kvdsize) || off>(uint64_t)pm_available(f) || size>(uint64_t)pm_available(f)-off) return false;
        {
            uint32_t j;
            for(j=0;j<i;++j) {
                uint8_t previous[24]; uint64_t po,ps;
                if(!pm_read(f,80+(int64_t)j*24,previous,24)) return false;
                po=r64(previous,false); ps=r64(previous+8,false);
                if(off<po+ps && po<off+size) return false;
            }
        }
        xx_rt_snprintf(label,sizeof(label),"mip-%u.bin",(unsigned)i);
        if(!pm_add(f,s,label,(int64_t)off,(int64_t)size)) return false;
        if(off+size>end) end=off+size;
    }
    s->size=(int64_t)end; return true;
}

void xx_ktx2_init(xx_ktx2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_KTX2,"ktx2"); } }
xx_ktx2 *xx_ktx2_create(xx_io_device *d,int64_t b) { xx_ktx2 *r=(xx_ktx2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_ktx2_init(r,d,b); return r; }
void xx_ktx2_destroy(xx_ktx2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ktx2_free(xx_ktx2 *r) { if(r) { xx_ktx2_destroy(r); xx_mem_free(r); } }
bool xx_ktx2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ktx2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

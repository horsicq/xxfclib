/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/0x0L/rs-utils/blob/master/bin/psarc.py
 * PSARC 1.4 unencrypted TOC and stored blocks only, including manifest member. zlib-compressed blocks and encrypted TOCs are rejected; original manifest names are not reconstructed.
 */
#include "xxfclib/formats/sony_psarc/xx_sony_psarc.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32],e[30],b[2]; uint32_t toc,count,block,i,zcount; uint64_t end;
    if(!pm_read(f,0,h,32) || xx_rt_memcmp(h,"PSAR",4) || pm_be32(h+4)!=0x10004 || xx_rt_memcmp(h+8,"zlib",4) ||
       pm_be32(h+16)!=30 || pm_be32(h+24)!=65536 || (pm_be32(h+28)&~3U)) return false;
    toc=pm_be32(h+12); count=pm_be32(h+20); block=65536;
    if(!count || count>65536 || toc<32+(uint64_t)count*30 || toc>pm_available(f) || (toc-32-count*30)&1) return false;
    zcount=(toc-32-count*30)/2; end=toc;
    for(i=0;i<count;++i) {
        uint64_t size=0,off=0,j,left; uint32_t index; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,32+(int64_t)i*30,e,30)) return false;
        index=pm_be32(e+16);
        for(j=0;j<5;++j) { size=(size<<8)|e[20+j]; off=(off<<8)|e[25+j]; }
        if(off<toc || off>(uint64_t)pm_available(f) || size>(uint64_t)pm_available(f)-off ||
           index>zcount || (size+block-1)/block>zcount-index) return false;
        left=size;
        for(j=0;left;++j) {
            uint32_t actual,want=left>block ? block : (uint32_t)left;
            if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,32+(int64_t)count*30+((int64_t)index+j)*2,b,2)) return false;
            actual=pm_be16(b); if(!actual) actual=block;
            /* Stored PSARC blocks have their uncompressed size in the table.
             * A smaller block denotes compressed data and is rejected. */
            if(actual!=want) { return false; } left-=want;
        }
        xx_rt_snprintf(label,sizeof(label),i ? "file-%u.bin" : "manifest.txt",(unsigned)i);
        if(!pm_add(f,s,label,(int64_t)off,(int64_t)size)) return false;
        if(off+size>end) end=off+size;
    }
    s->size=(int64_t)end; return true;
}

void xx_sony_psarc_init(xx_sony_psarc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_PSARC,"psarc"); } }
xx_sony_psarc *xx_sony_psarc_create(xx_io_device *d,int64_t b) { xx_sony_psarc *r=(xx_sony_psarc *)xx_mem_alloc(sizeof(*r)); if(r) xx_sony_psarc_init(r,d,b); return r; }
void xx_sony_psarc_destroy(xx_sony_psarc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_psarc_free(xx_sony_psarc *r) { if(r) { xx_sony_psarc_destroy(r); xx_mem_free(r); } }
bool xx_sony_psarc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_psarc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

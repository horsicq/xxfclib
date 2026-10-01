/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.w3.org/TR/WOFF/
 * WOFF 1.0, stored and zlib tables plus metadata/private data; verifies zlib and table checksums. No font rendering.
 */
#include "xxfclib/formats/woff/xx_woff.h"
#include "../xx_payload_members.h"
#include "../sfnt/xx_font_table_impl.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[44],e[20]; uint32_t count,total,previous=0,off,size,original,i,j; uint64_t sfntsize; int64_t end;
    uint32_t offsets[4098],sizes[4098]; uint8_t used[4098]={0};
    if(!pm_read(f,0,h,44) || xx_rt_memcmp(h,"wOFF",4) || !font_flavor(pm_be32(h+4)) || pm_be16(h+14)) return false;
    total=pm_be32(h+8); count=pm_be16(h+12); if(!count || count>4095 || total>pm_available(f) || total<44+(uint64_t)count*20) return false;
    sfntsize=12+(uint64_t)count*16; end=44+(int64_t)count*20;
    for(i=0;i<count+2;++i) {
        char name[64]; uint32_t checksum=0; int mode=0;
        if(pd && xx_pd_is_stopped(pd)) return false;
        if(i<count) {
            uint32_t tag;
            if(!pm_read(f,44+(int64_t)i*20,e,20) || !font_tag(e)) return false;
            tag=pm_be32(e); if(i && tag<=previous) return false; previous=tag;
            off=pm_be32(e+4); size=pm_be32(e+8); original=pm_be32(e+12); checksum=pm_be32(e+16);
            if(size>original) return false; mode=size<original; sfntsize+=((uint64_t)original+3)&~UINT64_C(3);
            if(sfntsize>64U*1024U*1024U) return false;
            xx_rt_snprintf(name,sizeof(name),"table-%08x.bin",pm_be32(e));
        } else {
            off=pm_be32(h+(i==count ? 24 : 36)); size=pm_be32(h+(i==count ? 28 : 40));
            original=i==count ? pm_be32(h+32) : size; mode=i==count;
            xx_rt_snprintf(name,sizeof(name),"%s",i==count ? "metadata.xml" : "private.bin");
            if(!off && !size && !original) { offsets[i]=sizes[i]=0; continue; }
            if(!off || !size || !original || off<end) return false;
        }
        if(off%4 || off<44+(uint64_t)count*20 || off>total || size>total-off) return false;
        for(j=0;j<i;++j) if(size && sizes[j] && off<(uint64_t)offsets[j]+sizes[j] && offsets[j]<(uint64_t)off+size) return false;
        offsets[i]=off; sizes[i]=size;
        if(!font_range(f,s,off,size,original,name,mode,checksum,i<count,pd)) return false;
        if((int64_t)off+size>end) end=(int64_t)off+size;
    }
    if(sfntsize>64U*1024U*1024U || sfntsize!=pm_be32(h+16) || end>total) return false;
    {
        uint64_t cursor=44+(uint64_t)count*20; uint32_t n;
        for(n=0;n<count+2;++n) {
            uint32_t chosen=UINT32_MAX; uint64_t first=UINT64_MAX;
            for(j=0;j<count+2;++j) if(!used[j] && sizes[j] && offsets[j]<first) { first=offsets[j]; chosen=j; }
            if(chosen==UINT32_MAX) break;
            if(first!=((cursor+3)&~UINT64_C(3))) return false;
            while(cursor<first) { uint8_t pad; if(!pm_read(f,(int64_t)cursor++,&pad,1) || pad) return false; }
            cursor=first+sizes[chosen]; used[chosen]=1;
        }
        if(cursor>total || total-cursor>3) return false;
        while(cursor<total) { uint8_t pad; if(!pm_read(f,(int64_t)cursor++,&pad,1) || pad) return false; }
    }
    s->size=total; return true;
}

void xx_woff_init(xx_woff *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WOFF,"woff"); } }
xx_woff *xx_woff_create(xx_io_device *d,int64_t b) { xx_woff *r=(xx_woff *)xx_mem_alloc(sizeof(*r)); if(r) xx_woff_init(r,d,b); return r; }
void xx_woff_destroy(xx_woff *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_woff_free(xx_woff *r) { if(r) { xx_woff_destroy(r); xx_mem_free(r); } }
bool xx_woff_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_woff_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

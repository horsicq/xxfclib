/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://developer.apple.com/library/archive/documentation/MusicAudio/Reference/CAFSpec/CAF_spec/CAF_spec.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "xxfclib/formats/caf/xx_caf.h"
#include "../xx_payload_members.h"

static uint64_t cf_be64(const uint8_t *p) { return (uint64_t)pm_be32(p)<<32|pm_be32(p+4); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32]; int64_t at=8,limit=pm_available(f),data_size=-1; uint32_t bpp=0,fpp=0; bool desc=false,data=false,pakt=false; uint64_t packet_bytes=0,packet_frames=0,valid_frames=0,total_frames=0;
    if(!pm_read(f,0,h,8) || xx_rt_memcmp(h,"caff",4) || pm_be16(h+4)!=1) return false;
    while(at<limit) { uint64_t n; int64_t body; char name[16]; unsigned i;
        if((pd && xx_pd_is_stopped(pd)) || limit-at<12 || !pm_read(f,at,h,12)) return false; n=cf_be64(h+4); body=at+12;
        if(n==UINT64_MAX && !xx_rt_memcmp(h,"data",4)) n=(uint64_t)(limit-body);
        if(n>INT64_MAX || n>(uint64_t)(limit-body) || (!desc && xx_rt_memcmp(h,"desc",4))) return false;
        for(i=0;i<4;++i) name[i]=h[i]>=32 && h[i]<127 ? (char)h[i] : '_'; name[4]=0; xx_rt_memcpy(name+4,".bin",5);
        if(!xx_rt_memcmp(h,"desc",4)) { uint64_t rate;
            if(desc || n!=32 || !pm_read(f,body,h,32)) return false; rate=cf_be64(h);
            if((rate>>63) || !(rate&0x7FFFFFFFFFFFFFFFULL) || ((rate>>52)&2047)==2047 || !pm_be32(h+8) || !pm_be32(h+24)) return false;
            bpp=pm_be32(h+16); fpp=pm_be32(h+20); desc=true;
        } else if(!xx_rt_memcmp(h,"data",4)) { if(data || n<4) return false; data_size=(int64_t)n-4; data=true;
            if(!pm_add(f,s,"audio.encoded",body+4,data_size)) return false; at=body+(int64_t)n; continue;
        } else if(!xx_rt_memcmp(h,"pakt",4)) { uint64_t count,j; int64_t pos=body+24;
            if(pakt || n<24 || !pm_read(f,body,h,24) || (count=cf_be64(h))>65536 || (cf_be64(h+8)>>63) || (pm_be32(h+16)&0x80000000U) || (pm_be32(h+20)&0x80000000U)) return false;
            valid_frames=cf_be64(h+8); total_frames=valid_frames+pm_be32(h+16)+pm_be32(h+20);
            for(j=0;j<count;++j) { unsigned field; for(field=0;field<2;++field) { uint64_t value=field ? fpp : bpp; unsigned k=0; uint8_t b;
                if(!value) { value=0; do { if(pos>=body+(int64_t)n || !pm_read(f,pos++,&b,1) || ++k>9 || value>(UINT64_MAX>>7)) return false; value=(value<<7)|(b&127U); } while(b&128); }
                if(field) { if(value>UINT64_MAX-packet_frames) return false; packet_frames+=value; }
                else { if(value>UINT64_MAX-packet_bytes) return false; packet_bytes+=value; }
            }}
            if(pos!=body+(int64_t)n || packet_frames!=total_frames) return false; pakt=true;
        }
        if(!pm_add(f,s,name,body,(int64_t)n)) return false; at=body+(int64_t)n;
    }
    if(!desc || !data || ((!bpp || !fpp) && !pakt) || (pakt && packet_bytes!=(uint64_t)data_size) || (bpp && data_size%bpp)) return false;
    s->size=at; return true;
}

void xx_caf_init(xx_caf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_CAF,"caf"); } }
xx_caf *xx_caf_create(xx_io_device *d,int64_t b) { xx_caf *r=(xx_caf *)xx_mem_alloc(sizeof(*r)); if(r) xx_caf_init(r,d,b); return r; }
void xx_caf_destroy(xx_caf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_caf_free(xx_caf *r) { if(r) { xx_caf_destroy(r); xx_mem_free(r); } }
bool xx_caf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_caf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://webassembly.github.io/spec/core/binary/modules.html
 * Core version 1 sections 0-12; validates framing, order and uniqueness. Exports encoded sections; no instruction validation or execution.
 */
#include "xxfclib/formats/wasm/xx_wasm.h"
#include "../xx_payload_members.h"


static bool uleb(Abstractformat *f,int64_t *at,int64_t end,uint32_t *v) {
    unsigned i; uint8_t b; uint32_t n=0;
    for(i=0;i<5;++i) { if(*at>=end || !pm_read(f,(*at)++,&b,1) || (i==4 && (b&0xf0))) return false; n|=(uint32_t)(b&127)<<(i*7); if(!(b&128)) { *v=n; return true; } }
    return false;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const uint8_t ranks[]={0,1,2,3,4,5,6,7,8,9,11,12,10};
    uint8_t h[8],id,rank=0; int64_t at=8,end=pm_available(f); uint32_t seen=0,size;
    if(!pm_read(f,0,h,8) || xx_rt_memcmp(h,"\0asm\1\0\0\0",8)) return false;
    while(at<end) {
        char name[64]; int64_t payload;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at++,&id,1) || id>12 || !uleb(f,&at,end,&size) || size>(uint64_t)(end-at)) return false;
        payload=at;
        if(id) { if((seen&(1U<<id)) || ranks[id]<rank) return false; seen|=1U<<id; rank=ranks[id]; }
        else { uint32_t namesize; int64_t temp=at; if(!uleb(f,&temp,at+size,&namesize) || namesize>(uint64_t)(at+size-temp)) return false; }
        xx_rt_snprintf(name,sizeof(name),"section-%u.bin",id);
        if(!pm_add(f,s,name,payload,size)) return false;
        at+=size;
    }
    s->size=end; return true;
}

void xx_wasm_init(xx_wasm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WASM,"wasm"); } }
xx_wasm *xx_wasm_create(xx_io_device *d,int64_t b) { xx_wasm *r=(xx_wasm *)xx_mem_alloc(sizeof(*r)); if(r) xx_wasm_init(r,d,b); return r; }
void xx_wasm_destroy(xx_wasm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_wasm_free(xx_wasm *r) { if(r) { xx_wasm_destroy(r); xx_mem_free(r); } }
bool xx_wasm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_wasm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

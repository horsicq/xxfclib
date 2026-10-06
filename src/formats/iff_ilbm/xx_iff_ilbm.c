/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://1fish2.github.io/IFF/IFF%20docs%20with%20Commodore%20revisions/ILBM.pdf, https://1fish2.github.io/IFF/
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/iff_ilbm/xx_iff_ilbm.h"
#include "../xx_payload_members.h"
#include "xxfclib/global/xx_global.h"

typedef struct fm_bytes { Abstractformat *f; xx_pd_struct *pd; int64_t pos,end,begin; size_t count,capacity; uint8_t *buffer; } fm_bytes;
static bool fm_start(fm_bytes *r,Abstractformat *f,xx_pd_struct *pd,int64_t at,int64_t end) {
    xx_mem_zero(r,sizeof(*r)); r->f=f; r->pd=pd; r->pos=at; r->end=end; r->begin=-1;
    r->capacity=xx_get_file_buffer_size();
    if(r->capacity>(SIZE_MAX>>1)) r->capacity=SIZE_MAX>>1;
    if(end<at) return false;
    if((uint64_t)(end-at)<r->capacity) r->capacity=(size_t)(end-at);
    if(!r->capacity) r->capacity=1;
    r->buffer=(uint8_t *)xx_mem_alloc(r->capacity); return r->buffer!=NULL;
}
static bool fm_finish(fm_bytes *r,bool result) { xx_mem_free(r->buffer); return result; }
static bool fm_byte(fm_bytes *r,uint8_t *b) {
    if(r->pos>=r->end) return false;
    if(r->begin<0 || r->pos<r->begin || r->pos-r->begin>=(int64_t)r->count) {
        int64_t left=r->end-r->pos; r->count=(uint64_t)left>r->capacity ? r->capacity : (size_t)left;
        if((r->pd && xx_pd_is_stopped(r->pd)) || !pm_read(r->f,r->pos,r->buffer,r->count)) { return false; } r->begin=r->pos;
    }
    *b=r->buffer[(size_t)(r->pos-r->begin)]; ++r->pos; return true;
}
static bool fm_skip(fm_bytes *r,uint64_t n) {
    if(r->pos>r->end || n>(uint64_t)(r->end-r->pos) || (r->pd && xx_pd_is_stopped(r->pd))) return false;
    r->pos+=(int64_t)n; return true;
}

static bool il_body(Abstractformat *f,xx_pd_struct *pd,int64_t at,uint32_t size,unsigned row,unsigned rows,bool compressed) {
    fm_bytes r; unsigned i; if(!compressed) return (uint64_t)row*rows==size; if(!fm_start(&r,f,pd,at,at+size)) return false;
    for(i=0;i<rows;++i) { unsigned done=0;
        if(pd && xx_pd_is_stopped(pd)) return fm_finish(&r,false);
        while(done<row) { uint8_t op; unsigned n;
            if(!fm_byte(&r,&op)) return fm_finish(&r,false);
            if(op==128) continue;
            n=op<128 ? (unsigned)op+1U : 257U-op;
            if(n>row-done || !fm_skip(&r,op<128 ? n : 1U)) { return fm_finish(&r,false); } done+=n;
        }
    }
    return fm_finish(&r,r.pos==r.end);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[20],pad; int64_t pos=12,end; unsigned row=0,rows=0,planes=0,chunks=0; bool bmhd=false,body=false,compressed=false;
    if(!pm_read(f,0,h,12) || xx_rt_memcmp(h,"FORM",4) || xx_rt_memcmp(h+8,"ILBM",4) || pm_be32(h+4)<4) return false;
    end=8+(int64_t)pm_be32(h+4); if(end>pm_available(f) || (end&1)) return false;
    while(pos<end) { uint32_t n,tag; char label[40]; unsigned i;
        if(++chunks>4096 || (pd && xx_pd_is_stopped(pd)) || end-pos<8 || !pm_read(f,pos,h,8)) return false;
        for(i=0;i<4;++i) if(h[i]<32 || h[i]>126) return false;
        tag=pm_be32(h); n=pm_be32(h+4); pos+=8;
        if(n>(uint64_t)(end-pos) || (n&1 && (n==(uint64_t)(end-pos) || !pm_read(f,pos+n,&pad,1) || pad))) return false;
        if(tag==0x424D4844U) { unsigned w,height,mask;
            if(bmhd || body || n!=20 || !pm_read(f,pos,h,20)) return false;
            w=pm_be16(h); height=pm_be16(h+2); planes=h[8]; mask=h[9];
            if(!w || !height || !planes || planes>32 || mask>2 || h[10]>1 || h[11]) return false;
            row=((w+15U)/16U)*2U; rows=height*(planes+(mask==1 ? 1U : 0U));
            if((uint64_t)row*rows>134217728U) { return false; } compressed=h[10]!=0; bmhd=true;
        } else if(tag==0x424F4459U) {
            if(!bmhd || body || !n || !il_body(f,pd,pos,n,row,rows,compressed)) { return false; } body=true;
        } else if(tag==0x434D4150U) { if(body || !n || n%3 || n>768) return false; }
        else if(tag==0x43414D47U && (body || n!=4)) return false;
        xx_rt_snprintf(label,sizeof(label),"chunk-%08X.bin",(unsigned)tag);
        if(!pm_add(f,s,label,pos,n)) { return false; } pos+=n+(n&1U);
    }
    if(!bmhd || !body || pos!=end) { return false; } s->size=end; return true;
}

void xx_iff_ilbm_init(xx_iff_ilbm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IFF_ILBM,"ilbm"); } }
xx_iff_ilbm *xx_iff_ilbm_create(xx_io_device *d,int64_t b) { xx_iff_ilbm *r=(xx_iff_ilbm *)xx_mem_alloc(sizeof(*r)); if(r) xx_iff_ilbm_init(r,d,b); return r; }
void xx_iff_ilbm_destroy(xx_iff_ilbm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_iff_ilbm_free(xx_iff_ilbm *r) { if(r) { xx_iff_ilbm_destroy(r); xx_mem_free(r); } }
bool xx_iff_ilbm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_iff_ilbm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

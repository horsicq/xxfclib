/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://qoiformat.org/qoi-specification.pdf, https://raw.githubusercontent.com/phoboslab/qoi/master/qoi.h
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/qoi/xx_qoi.h"
#include "../xx_payload_members.h"
#include "xxfclib/global/xx_global.h"

typedef struct qo_bytes { Abstractformat *f; xx_pd_struct *pd; int64_t pos,end,begin; size_t n,capacity; uint8_t *buf; } qo_bytes;
static bool qo_byte(qo_bytes *r,uint8_t *b) {
    if(r->pos>=r->end) return false;
    if(r->pos<r->begin || r->pos-r->begin>=(int64_t)r->n) { int64_t left=r->end-r->pos; r->n=(uint64_t)left>r->capacity ? r->capacity : (size_t)left;
        if((r->pd && xx_pd_is_stopped(r->pd)) || !pm_read(r->f,r->pos,r->buf,r->n)) return false; r->begin=r->pos; }
    *b=r->buf[(size_t)(r->pos-r->begin)]; ++r->pos; return true;
}
static bool qo_finish(qo_bytes *r,bool result) { xx_mem_free(r->buf); return result; }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[14],index[64][4],pixel[4]={0,0,0,255},marker[8]; uint64_t pixels,done=0; qo_bytes r;
    if(!pm_read(f,0,h,14) || xx_rt_memcmp(h,"qoif",4) || !pm_be32(h+4) || !pm_be32(h+8) || (h[12]!=3 && h[12]!=4) || h[13]>1 || (pixels=(uint64_t)pm_be32(h+4)*pm_be32(h+8))>16777216) return false;
    xx_mem_zero(index,sizeof(index)); xx_mem_zero(&r,sizeof(r)); r.f=f; r.pd=pd; r.pos=14; r.end=pm_available(f); r.begin=-1;
    r.capacity=xx_get_file_buffer_size(); if(r.capacity>(SIZE_MAX>>1)) r.capacity=SIZE_MAX>>1;
    if(r.end<r.pos) return false;
    if((uint64_t)(r.end-r.pos)<r.capacity) r.capacity=(size_t)(r.end-r.pos);
    if(!r.capacity) r.capacity=1;
    r.buf=(uint8_t *)xx_mem_alloc(r.capacity); if(!r.buf) return false;
    while(done<pixels) { uint8_t op; unsigned run=1,hash;
        if(!qo_byte(&r,&op)) return qo_finish(&r,false);
        if(op==254 || op==255) { unsigned i,n=op==254 ? 3U : 4U; for(i=0;i<n;++i) if(!qo_byte(&r,&pixel[i])) return qo_finish(&r,false); }
        else if((op&192)==0) xx_rt_memcpy(pixel,index[op&63],4);
        else if((op&192)==64) { pixel[0]=(uint8_t)(pixel[0]+((op>>4)&3)-2); pixel[1]=(uint8_t)(pixel[1]+((op>>2)&3)-2); pixel[2]=(uint8_t)(pixel[2]+(op&3)-2); }
        else if((op&192)==128) { uint8_t b; int dg=(op&63)-32; if(!qo_byte(&r,&b)) return qo_finish(&r,false); pixel[0]=(uint8_t)(pixel[0]+dg+(b>>4)-8); pixel[1]=(uint8_t)(pixel[1]+dg); pixel[2]=(uint8_t)(pixel[2]+dg+(b&15)-8); }
        else run=(op&63)+1U;
        if(run>pixels-done) return qo_finish(&r,false); hash=(pixel[0]*3U+pixel[1]*5U+pixel[2]*7U+pixel[3]*11U)%64U; xx_rt_memcpy(index[hash],pixel,4); done+=run;
    }
    if(!pm_read(f,r.pos,marker,8) || xx_rt_memcmp(marker,"\0\0\0\0\0\0\0\1",8) || !pm_add(f,s,"descriptor.bin",4,10) || !pm_add(f,s,"opcodes.qoi",14,r.pos-14)) return qo_finish(&r,false);
    s->size=r.pos+8; return qo_finish(&r,true);
}

void xx_qoi_init(xx_qoi *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_QOI,"qoi"); } }
xx_qoi *xx_qoi_create(xx_io_device *d,int64_t b) { xx_qoi *r=(xx_qoi *)xx_mem_alloc(sizeof(*r)); if(r) xx_qoi_init(r,d,b); return r; }
void xx_qoi_destroy(xx_qoi *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_qoi_free(xx_qoi *r) { if(r) { xx_qoi_destroy(r); xx_mem_free(r); } }
bool xx_qoi_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_qoi_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

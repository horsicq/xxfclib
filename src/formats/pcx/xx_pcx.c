/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://files.mpoli.fi/unpacked/software/programm/general/gcgpe10.zip/pcx.txt
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/pcx/xx_pcx.h"
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

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[128],b; unsigned w,height,planes,bits,stride,y; uint64_t row,expanded; fm_bytes r; int64_t raster_end;
    if(!pm_read(f,0,h,sizeof(h)) || h[0]!=10 || !(h[1]==0 || (h[1]>=2 && h[1]<=5)) || h[2]!=1 || h[64]!=0) return false;
    bits=h[3]; planes=h[65]; stride=pm_le16(h+66);
    if((bits!=1 && bits!=2 && bits!=4 && bits!=8) || !planes || planes>4 || !stride || (stride&1) || pm_le16(h+8)<pm_le16(h+4) || pm_le16(h+10)<pm_le16(h+6)) return false;
    if(bits*planes>8 && !(bits==8 && (planes==3 || planes==4))) return false;
    w=(unsigned)pm_le16(h+8)-pm_le16(h+4)+1U; height=(unsigned)pm_le16(h+10)-pm_le16(h+6)+1U;
    row=(uint64_t)stride*planes; expanded=row*height;
    if((uint64_t)w*height>16777216U || (uint64_t)stride*8U<(uint64_t)w*bits || expanded>134217728U) return false;
    if(!fm_start(&r,f,pd,128,pm_available(f))) return false;
    for(y=0;y<height;++y) { uint64_t done=0;
        if(pd && xx_pd_is_stopped(pd)) return fm_finish(&r,false);
        while(done<row) { unsigned n=1;
            if(!fm_byte(&r,&b)) return fm_finish(&r,false);
            if((b&192)==192) { n=b&63; if(!n || !fm_byte(&r,&b)) return fm_finish(&r,false); }
            if(n>row-done) { return fm_finish(&r,false); } done+=n;
        }
    }
    raster_end=r.pos;
    if(!pm_add(f,s,"descriptor.bin",0,128) || !pm_add(f,s,"scanlines.pcx-rle",128,raster_end-128)) return fm_finish(&r,false);
    if(h[1]==5 && bits==8 && planes==1) {
        if(!fm_byte(&r,&b) || b!=12 || !fm_skip(&r,768) || !pm_add(f,s,"palette.rgb",raster_end+1,768)) return fm_finish(&r,false);
    }
    s->size=r.pos; return fm_finish(&r,true);
}

void xx_pcx_init(xx_pcx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PCX,"pcx"); } }
xx_pcx *xx_pcx_create(xx_io_device *d,int64_t b) { xx_pcx *r=(xx_pcx *)xx_mem_alloc(sizeof(*r)); if(r) xx_pcx_init(r,d,b); return r; }
void xx_pcx_destroy(xx_pcx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pcx_free(xx_pcx *r) { if(r) { xx_pcx_destroy(r); xx_mem_free(r); } }
bool xx_pcx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pcx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

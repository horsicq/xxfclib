/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/ncch.h
 * NCCH versions 0/1/2 with NoCrypto and 512-byte media units. Exports extended header, plain/logo/ExeFS/RomFS encoded sections. No RSA/hash trust verification, ExeFS decompression or RomFS traversal; encrypted/seed crypto rejected.
 */
#include "xxfclib/formats/nintendo_ncch/xx_nintendo_ncch.h"
#include "../xx_payload_members.h"

static uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[512]; uint64_t total,offsets[5],sizes[5]; unsigned i,j; const char *labels[]={"extended-header.bin","plain.bin","logo.bin","exefs.bin","romfs.bin"};
    if(!pm_read(f,0,h,512) || xx_rt_memcmp(h+256,"NCCH",4) || pm_le16(h+0x112)>2 || h[0x18e] || !(h[0x18f]&4) || (h[0x18f]&0x21)) return false;
    total=(uint64_t)pm_le32(h+0x104)*512;
    if(total<512 || total>(uint64_t)pm_available(f)) return false;
    offsets[0]=512; sizes[0]=pm_le32(h+0x180); if(sizes[0] && sizes[0]!=0x400) return false; if(sizes[0]) sizes[0]=0x800;
    offsets[1]=(uint64_t)pm_le32(h+0x190)*512; sizes[1]=(uint64_t)pm_le32(h+0x194)*512;
    offsets[2]=(uint64_t)pm_le32(h+0x198)*512; sizes[2]=(uint64_t)pm_le32(h+0x19c)*512;
    offsets[3]=(uint64_t)pm_le32(h+0x1a0)*512; sizes[3]=(uint64_t)pm_le32(h+0x1a4)*512;
    offsets[4]=(uint64_t)pm_le32(h+0x1b0)*512; sizes[4]=(uint64_t)pm_le32(h+0x1b4)*512;
    if(pm_le32(h+0x1a8)>pm_le32(h+0x1a4) || pm_le32(h+0x1b8)>pm_le32(h+0x1b4)) return false;
    for(i=0;i<5;++i) { if(pd && xx_pd_is_stopped(pd)) return false; if(!sizes[i]) { if(i && offsets[i]) return false; continue; }
        if(offsets[i]<512 || !span(offsets[i],sizes[i],total)) return false;
        for(j=0;j<i;++j) if(sizes[j] && offsets[i]<offsets[j]+sizes[j] && offsets[j]<offsets[i]+sizes[i]) return false;
        if(!emit(f,s,labels[i],offsets[i],sizes[i],total)) return false; }
    s->size=(int64_t)total; return s->count!=0;

}

void xx_nintendo_ncch_init(xx_nintendo_ncch *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_NCCH,"cxi"); } }
xx_nintendo_ncch *xx_nintendo_ncch_create(xx_io_device *d,int64_t b) { xx_nintendo_ncch *r=(xx_nintendo_ncch *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_ncch_init(r,d,b); return r; }
void xx_nintendo_ncch_destroy(xx_nintendo_ncch *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_ncch_free(xx_nintendo_ncch *r) { if(r) { xx_nintendo_ncch_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_ncch_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_ncch_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }

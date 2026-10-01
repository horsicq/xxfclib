/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/cia.h
 * CIA type/version 0 with standard RSA2048/SHA256 TMD and stored unencrypted content. Exports certificate chain, ticket, TMD, included content records and optional metadata; verifies content SHA256. No signature trust verification or title-key decryption.
 */
#include "xxfclib/formats/nintendo_cia/xx_nintendo_cia.h"
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

#include "xxfclib/algo/hash/xx_hash.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[0x2020],t[0xb04],e[48],seen[8192],digest[32]; uint64_t cert,ticket,tmd,content,meta,total,content_size,at; uint32_t ts,ms; unsigned count,i;
    if(!pm_read(f,0,h,sizeof(h)) || pm_le32(h)!=0x2020 || pm_le32(h+4) || pm_le32(h+8)!=0xa00 || pm_le32(h+12)!=0x350) return false;
    ts=pm_le32(h+16); ms=pm_le32(h+20); content_size=g64(h+24,false);
    if(ts<0xb04 || ts>0xb04+1024*48 || (ms && ms!=0x3ac0) || !content_size || content_size>INT64_MAX) return false;
    cert=0x2040; ticket=cert+0xa00; tmd=(ticket+0x350+63)&~UINT64_C(63); content=(tmd+ts+63)&~UINT64_C(63);
    if(!span(content,content_size,(uint64_t)pm_available(f))) return false;
    meta=(content+content_size+63)&~UINT64_C(63); total=ms ? meta+ms : content+content_size;
    if(total>(uint64_t)pm_available(f) || !pm_read(f,(int64_t)tmd,t,sizeof(t)) || pm_be32(t)!=0x10004) return false;
    count=pm_be16(t+0x1de); if(!count || count>1024 || ts!=0xb04+count*48) return false;
    xx_mem_zero(seen,sizeof(seen));
    if(!emit(f,s,"certificates.bin",cert,0xa00,total) || !emit(f,s,"ticket.bin",ticket,0x350,total) || !emit(f,s,"tmd.bin",tmd,ts,total)) return false;
    at=content;
    for(i=0;i<count;++i) { uint16_t index,type; uint64_t n; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)(tmd+0xb04+i*48),e,48)) return false;
        index=pm_be16(e+4); type=pm_be16(e+6); n=g64(e+8,true);
        if(seen[index/8]&(1U<<(7-index%8))) return false; seen[index/8]|=(uint8_t)(1U<<(7-index%8));
        if(!(h[32+index/8]&(1U<<(7-index%8)))) continue;
        if((type&1) || !n || !span(at,n,content+content_size) || !xx_hash_device(XX_HASH_SHA256,f->device,f->base_address+(int64_t)at,(int64_t)n,digest,32,pd) || !xx_hash_equal(digest,e+16,32)) return false;
        xx_rt_snprintf(label,sizeof(label),"content-%u.bin",index); if(!emit(f,s,label,at,n,total)) return false; at+=n; }
    for(i=0;i<8192;++i) if(h[32+i]&~seen[i]) return false;
    if(at!=content+content_size || (ms && !emit(f,s,"metadata.bin",meta,ms,total))) return false;
    s->size=(int64_t)total; return true;

}

void xx_nintendo_cia_init(xx_nintendo_cia *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_CIA,"cia"); } }
xx_nintendo_cia *xx_nintendo_cia_create(xx_io_device *d,int64_t b) { xx_nintendo_cia *r=(xx_nintendo_cia *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_cia_init(r,d,b); return r; }
void xx_nintendo_cia_destroy(xx_nintendo_cia *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_cia_free(xx_nintendo_cia *r) { if(r) { xx_nintendo_cia_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_cia_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_cia_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
